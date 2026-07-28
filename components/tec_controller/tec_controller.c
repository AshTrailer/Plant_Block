#include "tec_controller.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include "driver/mcpwm.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "pin_definitions.h"
#include "fan_control.h"
#include "sht30_sensor.h"
#include "ds18b20_sensor.h"
#include "ventilation_control.h"
#include "event_bus.h"
#include "data_processor.h"

/* ================================================================
 *  配置宏
 * ================================================================ */
#define TEC_PWM_FREQ_HZ           20000   /* MCPWM 载波频率 20 kHz */
#define TEC_PWM_UNIT              MCPWM_UNIT_0
#define TEC_PWM_TIMER             MCPWM_TIMER_0
#define TEC_TASK_INTERVAL_MS      1000    /* 控制任务周期 1 Hz */
#define TEC_PI_INTERVAL_SEC       5       /* PI 运算间隔 5 秒 */
#define TEC_IDENT_COARSE_STEP     5.0f    /* 粗标定步长 (%) */
#define TEC_IDENT_FINE_STEP       1.0f    /* 精标定步长 (%) */
#define TEC_SAFE_TEMP_LIMIT       58.0f   /* 散热器安全上限 (℃) */
#define TEC_ATTEN_START_TEMP      55.0f   /* 开始线性衰减的温度 */
#define TEC_ATTEN_END_TEMP        60.0f   /* 完全衰减到 0 的温度 */
#define TEC_STEADY_STDDEV_PCT     0.5f    /* 稳态判据：标准差 < 0.5% */
#define TEC_STEADY_MIN_SAMPLES    30      /* 至少 30 个样本 (30 秒) */
#define TEC_STEADY_MAX_WAIT_SEC   1800    /* 最大等待 30 分钟（超时放松阈值） */
#define TEC_COOLDOWN_MAX_WAIT_SEC 1200    /* 回温最大等待 20 分钟 */
#define TEC_TEMP_RATE_THRESHOLD   0.0083f /* 0.5°C/min ≈ 0.0083°C/s */
#define TEC_MA_FILTER_LEN         10      /* 移动平均窗口 */
#define TEC_SENSOR_OFFLINE_THRESHOLD  5    /* 连续 5 秒无有效数据 → 视为硬件故障 */

#define TAG "TEC_CTRL"

#define TEC_LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define TEC_LOGW(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define TEC_LOGE(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)

/* ================================================================
 *  移动平均滤波器
 * ================================================================ */
typedef struct {
   float buf[TEC_MA_FILTER_LEN];
   int   idx;
   int   count;
   float sum;
} ma_filter_t;

static void ma_init(ma_filter_t *f) {
   memset(f, 0, sizeof(*f));
}

static void ma_push(ma_filter_t *f, float v) {
   if (f->count < TEC_MA_FILTER_LEN) {
      f->buf[f->count++] = v;
      f->sum += v;
   } else {
      f->sum -= f->buf[f->idx];
      f->buf[f->idx] = v;
      f->sum += v;
      f->idx = (f->idx + 1) % TEC_MA_FILTER_LEN;
   }
}

static float ma_value(const ma_filter_t *f) {
   if (f->count == 0) return 0.0f;
   return f->sum / (float)f->count;
}

static bool ma_ready(const ma_filter_t *f) {
   return f->count >= TEC_MA_FILTER_LEN / 2;
}

/* ================================================================
 *  全局状态
 * ================================================================ */
static tec_state_t          s_state = TEC_STATE_OFF;
static float                s_duty = 50.0f;       /* 当前占空比 */
static float                s_target_temp = 25.0f;
static tec_ident_result_t   s_ident;
static TaskHandle_t         s_task_handle = NULL;
static SemaphoreHandle_t    s_mutex = NULL;

/* 传感器连续离线计数器（用于短时故障容忍） */
static int s_sht30_offline_cnt = 0;
static int s_ds18_offline_cnt[2] = {0, 0};

/* 传感器缓存（由控制任务每秒更新） */
static float s_sht30_temp = 0.0f;
static float s_ds18_temp[2] = {0.0f, 0.0f};
static bool  s_sht30_valid = false;
static bool  s_ds18_valid[2] = {false, false};

/* PI 控制器状态 */
static float s_pi_integral = 0.0f;
static int   s_pi_tick_counter = 0;
static bool  s_pi_enabled = false;

/* 辨识过程状态机 */
typedef enum {
   IDSUB_IDLE = 0,
   /* --- 阶段 1：散热器极限 --- */
   IDSUB_H1_STABILIZE,        /* u=0 等待稳定 */
   IDSUB_H2_COARSE_FWD,       /* 粗标正向 */
   IDSUB_H3_FINE_FWD,         /* 精标正向 → u_heat_max */
   IDSUB_H4_DETERMINE_SIDE,   /* 判定冷热端 */
   IDSUB_H5_COOLDOWN,         /* 回温到环境 */
   IDSUB_H6_COARSE_REV,       /* 粗标反向 */
   IDSUB_H7_FINE_REV,         /* 精标反向 → u_cool_max */
   /* --- 阶段 2：系统模型 --- */
   IDSUB_S1_STABILIZE_AMB,    /* u=0 记录 T_amb */
   IDSUB_S2_STEP_COOL,        /* 阶跃制冷 */
   IDSUB_S3_COOLDOWN2,        /* 回温 */
   IDSUB_S4_STEP_HEAT,        /* 阶跃制热 */
   IDSUB_S5_FIT_MODEL,        /* 拟合 FOPDT → PI 参数 */
   IDSUB_DONE,
} ident_substate_t;

static ident_substate_t s_idsub = IDSUB_IDLE;
static float            s_id_coarse_u = 0.0f;       /* 当前粗标定 u 值 */
static float            s_id_recorded_u = 0.0f;     /* 精细标定记录的极限 u */
static int              s_id_over_temp_index = -1; /* 过温传感器索引 */
static uint32_t         s_sub_wait_start = 0;   /* 当前子阶段等待起始秒数（文件作用域，供状态机和 API 共用） */
static int              s_temp_hist_len = 0;    /* 稳态检测历史长度（同上） */

/* 阶跃响应记录（用于 FOPDT 拟合） */
#define ID_MAX_SAMPLES 3600  /* 最多记录 1 小时 */
static float s_id_time[ID_MAX_SAMPLES];
static float s_id_temp[ID_MAX_SAMPLES];
static int   s_id_record_len = 0;
static float s_id_step_u = 0.0f;  /* 阶跃幅值 */
static float s_id_T_start = 0.0f; /* 阶跃前温度 */

/* 运行计时 */
static uint32_t s_run_seconds = 0;  /* 控制任务累计秒数 */

/* ================================================================
 *  内部声明
 * ================================================================ */
static void tec_set_raw_duty(float duty_pct);
static void tec_safety_check(void);
static void tec_pi_control(void);
static void tec_ident_state_machine(void);
static bool tec_read_sensors(void);
static bool tec_check_steady_state(float current_temp, float *history,
                                   int len, float *out_stddev_pct);
static bool tec_check_temp_stable_rate(const ma_filter_t *f, float threshold_c_per_s);
static void tec_fit_fopdt_and_tune(void);
static void tec_save_ident_to_nvs(void);

/* ================================================================
 *  MCPWM 初始化
 * ================================================================ */
static void tec_mcpwm_init(void)
{
   /* GPIO 配置 */
   mcpwm_gpio_init(TEC_PWM_UNIT, MCPWM0A, PIN_TEC_PWM_H);
   mcpwm_gpio_init(TEC_PWM_UNIT, MCPWM0B, PIN_TEC_PWM_L);

   mcpwm_config_t cfg = {
      .frequency = TEC_PWM_FREQ_HZ,
      .cmpr_a = 50.0f,
      .cmpr_b = 50.0f,
      .duty_mode = MCPWM_DUTY_MODE_0,
      .counter_mode = MCPWM_UP_COUNTER,
   };
   ESP_ERROR_CHECK(mcpwm_init(TEC_PWM_UNIT, TEC_PWM_TIMER, &cfg));

   /* 设置为互补输出模式（A 和 B 反相），内建死区交由 IR2104 处理 */
   mcpwm_set_duty_type(TEC_PWM_UNIT, TEC_PWM_TIMER, MCPWM_OPR_A, MCPWM_DUTY_MODE_0);
   mcpwm_set_duty_type(TEC_PWM_UNIT, TEC_PWM_TIMER, MCPWM_OPR_B, MCPWM_DUTY_MODE_1);

   TEC_LOGI("MCPWM initialized: freq=%d Hz, pins H=GPIO%d L=GPIO%d",
            TEC_PWM_FREQ_HZ, PIN_TEC_PWM_H, PIN_TEC_PWM_L);
}

/* ================================================================
 *  设置 PWM 占空比（原始值，不做限幅）
 * ================================================================ */
static void tec_set_raw_duty(float duty_pct)
{
   if (duty_pct < 0.0f) duty_pct = 0.0f;
   if (duty_pct > 100.0f) duty_pct = 100.0f;

   s_duty = duty_pct;
   mcpwm_set_duty(TEC_PWM_UNIT, TEC_PWM_TIMER, MCPWM_OPR_A, duty_pct);
   mcpwm_set_duty(TEC_PWM_UNIT, TEC_PWM_TIMER, MCPWM_OPR_B, duty_pct);
}

/* ================================================================
 *  检查是否有传感器达到硬件故障阈值（连续离线超限）
 *  返回 true 表示至少一个传感器已确认离线
 * ================================================================ */
static bool tec_is_any_sensor_fault(void)
{
   if (s_sht30_offline_cnt >= TEC_SENSOR_OFFLINE_THRESHOLD) {
      TEC_LOGE("SHT30 offline for %d seconds → FAULT", s_sht30_offline_cnt);
      return true;
   }
   for (int i = 0; i < 2; i++) {
      if (s_ds18_offline_cnt[i] >= TEC_SENSOR_OFFLINE_THRESHOLD) {
         TEC_LOGE("DS18B20[%d] offline for %d seconds → FAULT", i, s_ds18_offline_cnt[i]);
         return true;
      }
   }
   return false;
}

/* ================================================================
 *  读取所有传感器（由控制任务调用）
 * ================================================================ */
static bool tec_read_sensors(void)
{
   /* SHT30 — 读缓存 */
   float t, h;
   s_sht30_valid = sht30_sensor_get_data(&t, &h);
   if (s_sht30_valid) {
      s_sht30_temp = t;
      s_sht30_offline_cnt = 0;     /* ← 有效则清零 */
   } else {
      s_sht30_offline_cnt++;        /* ← 无效则累加 */
   }

   /* DS18B20 × 2 — 缓存接口 */
   for (int i = 0; i < 2; i++) {
      float temp;
      bool ok = ds18b20_sensor_get_temperature_cached(i, &temp);
      if (ok && temp > -55.0f && temp < 125.0f) {
         s_ds18_temp[i] = temp;
         s_ds18_valid[i] = true;
         s_ds18_offline_cnt[i] = 0;       /* ← 有效清零 */
      } else {
         s_ds18_valid[i] = false;
         s_ds18_offline_cnt[i]++;          /* ← 无效累加 */
      }
   }

   return s_sht30_valid && s_ds18_valid[0] && s_ds18_valid[1];
}

/* ================================================================
 *  安全检查 & 动态限幅
 * ================================================================ */
/* 安全检查：超过 58°C 强制回退 */
static void tec_safety_check(void)
{
   for (int i = 0; i < 2; i++) {
      if (!s_ds18_valid[i]) continue;
      if (s_ds18_temp[i] > TEC_SAFE_TEMP_LIMIT) {
         TEC_LOGW("DS18B20[%d] over-temp: %.2f °C → forcing towards safe", i, s_ds18_temp[i]);

         /* 判断当前方向，向安全方向回退 */
         float u = s_duty - 50.0f;
         if (u > 0) {
            /* 正向输出 → 减小正向幅值 */
            float new_u = u - 5.0f; /* 回退 5% */
            if (new_u < 0) new_u = 0;
            tec_set_raw_duty(50.0f + new_u);
         } else if (u < 0) {
            /* 反向输出 → 减小反向幅值 */
            float new_u = u + 5.0f;
            if (new_u > 0) new_u = 0;
            tec_set_raw_duty(50.0f + new_u);
         }
         return;
      }
   }
}

/* ================================================================
 *  稳态检测
 * ================================================================ */
static bool tec_check_steady_state(float current_temp, float *history,
                                   int len, float *out_stddev_pct)
{
   if (len < TEC_STEADY_MIN_SAMPLES) return false;

   /* 转换为 int（data_processor 接口是 int） */
   int *idata = (int *)malloc(len * sizeof(int));
   if (!idata) return false;
   for (int i = 0; i < len; i++) {
      idata[i] = (int)(history[i] * 100.0f); /* 保留 2 位小数 */
   }

   int *out_data = (int *)malloc(len * sizeof(int));
   int out_count = 0;
   bool stable = data_processor_check_stability(idata, len, TEC_STEADY_STDDEV_PCT, 0,
                                                 out_data, &out_count);
   if (stable && out_stddev_pct) {
      float mean = data_processor_mean(out_data, out_count);
      float stddev = data_processor_stddev(out_data, out_count);
      if (mean != 0) *out_stddev_pct = (stddev / mean) * 100.0f;
   }

   free(idata);
   free(out_data);
   return stable;
}

static bool tec_check_temp_stable_rate(const ma_filter_t *f, float threshold_c_per_s)
{
   if (f->count < TEC_MA_FILTER_LEN) return false;
   /* 简单线性拟合最近 5 个点的斜率 */
   int n = 5;
   float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
   for (int i = 0; i < n; i++) {
      int idx = (f->idx - 1 - i + TEC_MA_FILTER_LEN) % TEC_MA_FILTER_LEN;
      float x = (float)(-i);  /* 时间（秒），0 是最近 */
      float y = f->buf[idx];
      sum_x += x;
      sum_y += y;
      sum_xy += x * y;
      sum_x2 += x * x;
   }
   float denom = n * sum_x2 - sum_x * sum_x;
   if (fabsf(denom) < 1e-6f) return true;
   float slope = (n * sum_xy - sum_x * sum_y) / denom;
   return fabsf(slope) < threshold_c_per_s;
}

/* ================================================================
 *  PI 控制器（离散化：后向欧拉）
 *  u(k) = Kc * [ e(k) + (Ts/Ti) * Σ e(j) ]
 *  Ts = 5 秒
 * ================================================================ */
static void tec_pi_control(void)
{
   /* ---------- 0. 基础有效性 ---------- */
   if (!s_ident.valid || !s_pi_enabled) return;

   /* ---------- 1. SHT30 离线：跳过本次 PI ---------- */
   if (!s_sht30_valid) {
      TEC_LOGW("SHT30 invalid → PI skipped");
      return;
   }

   /* ---------- 2. DS18B20 离线：失去散热器温度监控，主动降功率 ---------- */
   if (!s_ds18_valid[s_ident.ds18_hot_idx] || !s_ds18_valid[s_ident.ds18_cold_idx]) {
      TEC_LOGW("DS18B20 offline → reducing output to safe level");
      float u = s_duty - 50.0f;
      if (u > 0) {
         tec_set_raw_duty(50.0f + u * 0.5f);   // 正向减半
      } else if (u < 0) {
         tec_set_raw_duty(50.0f + u * 0.5f);   // 负向减半（值更接近50%）
      }
      return;
   }

   /* ---------- 以下是你原来的 PI 逻辑（不变） ---------- */
   float Ts = (float)TEC_PI_INTERVAL_SEC;
   float e = s_target_temp - s_sht30_temp;

   // 动态限幅
   float u_lim_pos = 50.0f;
   float u_lim_neg = -50.0f;
   if (s_ident.valid) {
      u_lim_pos = s_ident.u_heat_max;
      u_lim_neg = s_ident.u_cool_max;
   }

   // 散热器温度衰减
   int hot_idx = s_ident.ds18_hot_idx;
   if (hot_idx >= 0 && hot_idx < 2 && s_ds18_valid[hot_idx]) {
      float t = s_ds18_temp[hot_idx];
      if (t > TEC_ATTEN_START_TEMP) {
         float r = (TEC_ATTEN_END_TEMP - t) / (TEC_ATTEN_END_TEMP - TEC_ATTEN_START_TEMP);
         if (r < 0) r = 0;
         if (r > 1) r = 1;
         u_lim_pos *= r;
      }
   }
   int cold_idx = s_ident.ds18_cold_idx;
   if (cold_idx >= 0 && cold_idx < 2 && s_ds18_valid[cold_idx]) {
      float t = s_ds18_temp[cold_idx];
      if (t > TEC_ATTEN_START_TEMP) {
         float r = (TEC_ATTEN_END_TEMP - t) / (TEC_ATTEN_END_TEMP - TEC_ATTEN_START_TEMP);
         if (r < 0) r = 0;
         if (r > 1) r = 1;
         u_lim_neg = u_lim_neg * r;
      }
   }

   // PI 计算与抗饱和
   float P = s_ident.Kc * e;
   float I = s_ident.Kc * (Ts / s_ident.Ti) * s_pi_integral;
   float u_raw = P + I;

   bool saturated = false;
   if (u_raw > u_lim_pos && e > 0) saturated = true;
   if (u_raw < u_lim_neg && e < 0) saturated = true;

   if (!saturated) {
      s_pi_integral += e;
   }

   if (u_raw > u_lim_pos) u_raw = u_lim_pos;
   if (u_raw < u_lim_neg) u_raw = u_lim_neg;

   float duty = 50.0f + u_raw;
   tec_set_raw_duty(duty);

   TEC_LOGI("PI: T=%.2f target=%.2f e=%.2f u=%.1f%% duty=%.1f%% int=%.2f%s",
            s_sht30_temp, s_target_temp, e, u_raw, duty, s_pi_integral,
            saturated ? " [SAT]" : "");
}

/* ================================================================
 *  FOPDT 拟合 & Lambda PI 整定
 * ================================================================ */
static void tec_fit_fopdt_and_tune(void)
{
   if (s_id_record_len < 30) {
      TEC_LOGE("Too few samples for FOPDT fit (%d)", s_id_record_len);
      return;
   }

   /* 找到温度开始明显变化的时刻（变化 > 5% 总变化量） */
   float dT_total = s_id_temp[s_id_record_len - 1] - s_id_T_start;
   if (fabsf(dT_total) < 0.1f) {
      TEC_LOGE("Temperature change too small (%.2f °C)", dT_total);
      return;
   }

   float threshold = fabsf(dT_total) * 0.05f;
   int start_idx = 0;
   for (int i = 0; i < s_id_record_len; i++) {
      if (fabsf(s_id_temp[i] - s_id_T_start) > threshold) {
         start_idx = i;
         break;
      }
   }

   /* θ = 开始变化的时间 */
   float theta = s_id_time[start_idx];

   /* 找到达到 63.2% 变化量的时间 */
   float target_temp = s_id_T_start + dT_total * 0.632f;
   int tau_idx = start_idx;
   for (int i = start_idx; i < s_id_record_len; i++) {
      if ((dT_total > 0 && s_id_temp[i] >= target_temp) ||
          (dT_total < 0 && s_id_temp[i] <= target_temp)) {
         tau_idx = i;
         break;
      }
   }
   float tau = s_id_time[tau_idx] - theta;
   if (tau < 1.0f) tau = 1.0f;

   /* K = ΔT / Δu */
   float K = fabsf(dT_total / s_id_step_u);
   if (K < 1e-6f) K = 1e-6f;

   /* Lambda 整定 */
   float lambda = tau;
   if (lambda < 3.0f * theta) lambda = 3.0f * theta;
   if (lambda < 1.0f) lambda = 1.0f;

   float Kc = tau / (K * (lambda + theta));
   float Ti = tau;

   /* 写入结果 */
   s_ident.K = K;
   s_ident.tau = tau;
   s_ident.theta = theta;
   s_ident.Kc = Kc;
   s_ident.Ti = Ti;

   TEC_LOGI("FOPDT: K=%.4f tau=%.1fs theta=%.1fs", K, tau, theta);
   TEC_LOGI("Lambda: λ=%.1fs → Kc=%.4f Ti=%.1fs", lambda, Kc, Ti);
}

/* ================================================================
 *  辨识状态机
 * ================================================================ */
static void tec_ident_state_machine(void)
{
   /* 局部稳态检测历史 */
   static float s_temp_history[600];  /* 最多 10 分钟 */
   static ma_filter_t s_ma_sht30;
   static ma_filter_t s_ma_ds18[2];
   static float s_id_start_temp = 0.0f;

   /* 读取传感器 */
   tec_read_sensors();

   /* 任一传感器连续离线超过阈值 → 立即终止辨识 */
   if (s_idsub != IDSUB_IDLE && tec_is_any_sensor_fault()) {
      TEC_LOGE("Sensor hardware fault → abort identification");
      s_idsub = IDSUB_IDLE;
      s_state = TEC_STATE_SAFE;
      tec_set_raw_duty(50.0f);
      return;
   }

   /* 更新移动平均 */
   if (s_sht30_valid) ma_push(&s_ma_sht30, s_sht30_temp);
   for (int i = 0; i < 2; i++) {
      if (s_ds18_valid[i]) ma_push(&s_ma_ds18[i], s_ds18_temp[i]);
   }

   switch (s_idsub) {

   /* ================================================================
    *  H1：u=0 等待初始稳定
    * ================================================================ */
   case IDSUB_H1_STABILIZE: {
      tec_set_raw_duty(50.0f);
      uint32_t elapsed = s_run_seconds - s_sub_wait_start;

      /* 收集 SHT30 历史 */
      if (s_sht30_valid && s_temp_hist_len < 600) {
         s_temp_history[s_temp_hist_len++] = ma_value(&s_ma_sht30);
      }

      float stddev_pct;
      bool stable = tec_check_steady_state(ma_value(&s_ma_sht30), s_temp_history,
                                            s_temp_hist_len, &stddev_pct);
      if (stable) {
         TEC_LOGI("H1: stable at %.2f °C (std=%.3f%%)", ma_value(&s_ma_sht30), stddev_pct);
         s_temp_hist_len = 0;
         s_idsub = IDSUB_H2_COARSE_FWD;
         s_id_coarse_u = 0.0f;
         s_sub_wait_start = s_run_seconds;
      } else if (elapsed > TEC_STEADY_MAX_WAIT_SEC) {
         TEC_LOGW("H1: timeout, forcing continue");
         s_temp_hist_len = 0;
         s_idsub = IDSUB_H2_COARSE_FWD;
         s_id_coarse_u = 0.0f;
         s_sub_wait_start = s_run_seconds;
      }
      break;
   }

   /* ================================================================
    *  H2：粗标正向（5% 步长）
    * ================================================================ */
   case IDSUB_H2_COARSE_FWD: {
      /* 检查任一 DS18B20 是否 ≥ 58°C */
      for (int i = 0; i < 2; i++) {
         if (s_ds18_valid[i] && ma_value(&s_ma_ds18[i]) >= TEC_SAFE_TEMP_LIMIT) {
            s_id_over_temp_index = i;
            s_id_recorded_u = s_id_coarse_u;
            TEC_LOGI("H2: DS18B20[%d] reached %.2f °C at u=%.1f%%",
                     i, ma_value(&s_ma_ds18[i]), s_id_coarse_u);
            s_idsub = IDSUB_H3_FINE_FWD;
            s_sub_wait_start = s_run_seconds;
            return;
         }
      }

      /* 检查温度变化率是否已稳定或超时 */
      uint32_t elapsed = s_run_seconds - s_sub_wait_start;
      bool rate_stable = true;
      for (int i = 0; i < 2; i++) {
         if (s_ds18_valid[i] && ma_ready(&s_ma_ds18[i])) {
            if (!tec_check_temp_stable_rate(&s_ma_ds18[i], TEC_TEMP_RATE_THRESHOLD)) {
               rate_stable = false;
            }
         }
      }

      bool timeout = (elapsed > 300);  /* 5 分钟超时 */

      if (rate_stable || timeout) {
         if (s_id_coarse_u >= 50.0f) {
            /* 已到 100% 占空比仍未超温 */
            s_id_recorded_u = 50.0f;
            TEC_LOGI("H2: max duty reached (u=50%%) without over-temp");
            s_idsub = IDSUB_H4_DETERMINE_SIDE;
            return;
         }
         /* 步进 5% */
         s_id_coarse_u += TEC_IDENT_COARSE_STEP;
         if (s_id_coarse_u > 50.0f) s_id_coarse_u = 50.0f;
         tec_set_raw_duty(50.0f + s_id_coarse_u);
         s_sub_wait_start = s_run_seconds;
         /* 重置 MA 滤波器以重新跟踪新阶段的温度变化率 */
         ma_init(&s_ma_ds18[0]);
         ma_init(&s_ma_ds18[1]);
         TEC_LOGI("H2: step to u=+%.1f%% (duty=%.1f%%)", s_id_coarse_u, 50.0f + s_id_coarse_u);
      }
      break;
   }

   /* ================================================================
    *  H3：精标正向（1% 步长，目标 58-60°C）
    * ================================================================ */
   case IDSUB_H3_FINE_FWD: {
      if (s_id_over_temp_index < 0) {
         s_idsub = IDSUB_H4_DETERMINE_SIDE;
         return;
      }

      uint32_t elapsed = s_run_seconds - s_sub_wait_start;
      float t_heat = ma_value(&s_ma_ds18[s_id_over_temp_index]);

      bool rate_stable = tec_check_temp_stable_rate(&s_ma_ds18[s_id_over_temp_index], 0.004f); /* ~0.25°C/min */
      bool timeout = (elapsed > 120);

      if (rate_stable || timeout) {
         if (t_heat >= 58.0f && t_heat <= 60.0f) {
            /* 已在窗口内 */
            s_id_recorded_u = s_id_coarse_u;
            TEC_LOGI("H3: fine-tuned u_heat_max=%.1f%% (T=%.2f°C)", s_id_coarse_u, t_heat);
            s_idsub = IDSUB_H4_DETERMINE_SIDE;
         } else if (t_heat > 60.0f) {
            s_id_coarse_u -= TEC_IDENT_FINE_STEP;
            if (s_id_coarse_u < 0) s_id_coarse_u = 0;
            tec_set_raw_duty(50.0f + s_id_coarse_u);
            s_sub_wait_start = s_run_seconds;
            ma_init(&s_ma_ds18[s_id_over_temp_index]);
         } else if (t_heat < 58.0f) {
            if (s_id_coarse_u >= 50.0f) {
               s_id_recorded_u = 50.0f;
               s_idsub = IDSUB_H4_DETERMINE_SIDE;
            } else {
               s_id_coarse_u += TEC_IDENT_FINE_STEP;
               tec_set_raw_duty(50.0f + s_id_coarse_u);
               s_sub_wait_start = s_run_seconds;
               ma_init(&s_ma_ds18[s_id_over_temp_index]);
            }
         }
      }
      break;
   }

   /* ================================================================
    *  H4：判定冷/热端（过温的是热端 → 水冷侧）
    * ================================================================ */
   case IDSUB_H4_DETERMINE_SIDE: {
      if (s_id_over_temp_index >= 0) {
         /* 正向时，制热的是水冷侧 → DS18B20 过温的那个是热端（水冷侧） */
         s_ident.ds18_hot_idx = s_id_over_temp_index;
         s_ident.ds18_cold_idx = 1 - s_id_over_temp_index;
      } else {
         /* 两路都没超温，默认 [0]=hot, [1]=cold */
         s_ident.ds18_hot_idx = 0;
         s_ident.ds18_cold_idx = 1;
      }
      s_ident.u_heat_max = s_id_recorded_u;
      ds18b20_set_role(s_ident.ds18_cold_idx, "cold");
      ds18b20_set_role(s_ident.ds18_hot_idx, "hot");
      TEC_LOGI("H4: hot=DS[%d] cold=DS[%d] u_heat_max=%.1f%%",
               s_ident.ds18_hot_idx, s_ident.ds18_cold_idx, s_ident.u_heat_max);

      /* 进入回温阶段 */
      tec_set_raw_duty(50.0f);
      fan_control_on(FAN_VENTILATION);  /* 开启通风加速回温 */
      s_sub_wait_start = s_run_seconds;
      s_temp_hist_len = 0;
      s_idsub = IDSUB_H5_COOLDOWN;
      break;
   }

   /* ================================================================
    *  H5：回温到接近环境
    * ================================================================ */
   case IDSUB_H5_COOLDOWN: {
      if (s_sht30_valid && s_temp_hist_len < 600) {
         s_temp_history[s_temp_hist_len++] = ma_value(&s_ma_sht30);
      }

      uint32_t elapsed = s_run_seconds - s_sub_wait_start;
      float stddev_pct;
      bool stable = tec_check_steady_state(ma_value(&s_ma_sht30), s_temp_history,
                                            s_temp_hist_len, &stddev_pct);
      if (stable || elapsed > TEC_COOLDOWN_MAX_WAIT_SEC) {
         fan_control_off(FAN_VENTILATION);
         s_id_start_temp = ma_value(&s_ma_sht30);
         TEC_LOGI("H5: cooled down to %.2f °C", s_id_start_temp);
         s_temp_hist_len = 0;
         s_idsub = IDSUB_H6_COARSE_REV;
         s_id_coarse_u = 0.0f;
         s_sub_wait_start = s_run_seconds;
         ma_init(&s_ma_ds18[0]);
         ma_init(&s_ma_ds18[1]);
      }
      break;
   }

   /* ================================================================
    *  H6：粗标反向（-5% 步长）
    * ================================================================ */
   case IDSUB_H6_COARSE_REV: {
      for (int i = 0; i < 2; i++) {
         if (s_ds18_valid[i] && ma_value(&s_ma_ds18[i]) >= TEC_SAFE_TEMP_LIMIT) {
            s_id_over_temp_index = i;
            s_id_recorded_u = s_id_coarse_u;  /* 负值 */
            TEC_LOGI("H6: DS18B20[%d] reached %.2f °C at u=%.1f%%",
                     i, ma_value(&s_ma_ds18[i]), s_id_coarse_u);
            s_idsub = IDSUB_H7_FINE_REV;
            s_sub_wait_start = s_run_seconds;
            return;
         }
      }

      uint32_t elapsed = s_run_seconds - s_sub_wait_start;
      bool rate_stable = true;
      for (int i = 0; i < 2; i++) {
         if (s_ds18_valid[i] && ma_ready(&s_ma_ds18[i])) {
            if (!tec_check_temp_stable_rate(&s_ma_ds18[i], TEC_TEMP_RATE_THRESHOLD)) {
               rate_stable = false;
            }
         }
      }
      bool timeout = (elapsed > 300);

      if (rate_stable || timeout) {
         if (s_id_coarse_u <= -50.0f) {
            s_id_recorded_u = -50.0f;
            TEC_LOGI("H6: min duty reached (u=-50%%) without over-temp");
            /* 完成阶段 1，进入阶段 2 */
            tec_set_raw_duty(50.0f);
            s_ident.u_cool_max = s_id_recorded_u;
            s_ident.u_heat_max = s_ident.u_heat_max;  /* 已在 H4 记录 */
            fan_control_on(FAN_VENTILATION);
            s_sub_wait_start = s_run_seconds;
            s_temp_hist_len = 0;
            s_idsub = IDSUB_S1_STABILIZE_AMB;
            return;
         }
         s_id_coarse_u -= TEC_IDENT_COARSE_STEP;
         if (s_id_coarse_u < -50.0f) s_id_coarse_u = -50.0f;
         tec_set_raw_duty(50.0f + s_id_coarse_u);
         s_sub_wait_start = s_run_seconds;
         ma_init(&s_ma_ds18[0]);
         ma_init(&s_ma_ds18[1]);
         TEC_LOGI("H6: step to u=%.1f%% (duty=%.1f%%)", s_id_coarse_u, 50.0f + s_id_coarse_u);
      }
      break;
   }

   /* ================================================================
    *  H7：精标反向
    * ================================================================ */
   case IDSUB_H7_FINE_REV: {
      if (s_id_over_temp_index < 0) {
         s_ident.u_cool_max = s_id_recorded_u;
         tec_set_raw_duty(50.0f);
         fan_control_on(FAN_VENTILATION);
         s_sub_wait_start = s_run_seconds;
         s_temp_hist_len = 0;
         s_idsub = IDSUB_S1_STABILIZE_AMB;
         return;
      }

      uint32_t elapsed = s_run_seconds - s_sub_wait_start;
      float t_heat = ma_value(&s_ma_ds18[s_id_over_temp_index]);
      bool rate_stable = tec_check_temp_stable_rate(&s_ma_ds18[s_id_over_temp_index], 0.004f);
      bool timeout = (elapsed > 120);

      if (rate_stable || timeout) {
         if (t_heat >= 58.0f && t_heat <= 60.0f) {
            s_id_recorded_u = s_id_coarse_u;
            TEC_LOGI("H7: fine-tuned u_cool_max=%.1f%% (T=%.2f°C)", s_id_coarse_u, t_heat);
            s_ident.u_cool_max = s_id_recorded_u;
            tec_set_raw_duty(50.0f);
            fan_control_on(FAN_VENTILATION);
            s_sub_wait_start = s_run_seconds;
            s_temp_hist_len = 0;
            s_idsub = IDSUB_S1_STABILIZE_AMB;
         } else if (t_heat > 60.0f) {
            s_id_coarse_u += TEC_IDENT_FINE_STEP;  /* 向 0 靠近 */
            if (s_id_coarse_u > 0) s_id_coarse_u = 0;
            tec_set_raw_duty(50.0f + s_id_coarse_u);
            s_sub_wait_start = s_run_seconds;
            ma_init(&s_ma_ds18[s_id_over_temp_index]);
         } else if (t_heat < 58.0f) {
            if (s_id_coarse_u <= -50.0f) {
               s_id_recorded_u = -50.0f;
               s_ident.u_cool_max = s_id_recorded_u;
               tec_set_raw_duty(50.0f);
               fan_control_on(FAN_VENTILATION);
               s_sub_wait_start = s_run_seconds;
               s_temp_hist_len = 0;
               s_idsub = IDSUB_S1_STABILIZE_AMB;
            } else {
               s_id_coarse_u -= TEC_IDENT_FINE_STEP;
               tec_set_raw_duty(50.0f + s_id_coarse_u);
               s_sub_wait_start = s_run_seconds;
               ma_init(&s_ma_ds18[s_id_over_temp_index]);
            }
         }
      }
      break;
   }

   /* ================================================================
    *  S1：u=0 稳定记录 T_amb
    * ================================================================ */
   case IDSUB_S1_STABILIZE_AMB: {
      tec_set_raw_duty(50.0f);
      if (s_sht30_valid && s_temp_hist_len < 600) {
         s_temp_history[s_temp_hist_len++] = ma_value(&s_ma_sht30);
      }

      uint32_t elapsed = s_run_seconds - s_sub_wait_start;
      float stddev_pct;
      bool stable = tec_check_steady_state(ma_value(&s_ma_sht30), s_temp_history,
                                            s_temp_hist_len, &stddev_pct);
      if (stable || elapsed > TEC_STEADY_MAX_WAIT_SEC) {
         fan_control_off(FAN_VENTILATION);
         s_ident.T_amb = ma_value(&s_ma_sht30);
         TEC_LOGI("S1: T_amb = %.2f °C", s_ident.T_amb);
         s_temp_hist_len = 0;
         s_id_record_len = 0;
         s_idsub = IDSUB_S2_STEP_COOL;
         s_sub_wait_start = s_run_seconds;
         s_id_step_u = s_ident.u_cool_max;
         s_id_T_start = s_ident.T_amb;
         tec_set_raw_duty(50.0f + s_ident.u_cool_max);
         TEC_LOGI("S2: step to u=%.1f%% (cooling)", s_ident.u_cool_max);
      }
      break;
   }

   /* ================================================================
    *  S2：阶跃制冷，记录温度时间序列
    * ================================================================ */
   case IDSUB_S2_STEP_COOL: {
      if (s_sht30_valid && s_id_record_len < ID_MAX_SAMPLES) {
         s_id_time[s_id_record_len] = (float)(s_run_seconds - s_sub_wait_start);
         s_id_temp[s_id_record_len] = ma_value(&s_ma_sht30);
         s_id_record_len++;
      }

      /* 检测稳态 */
      if (s_temp_hist_len < 600 && s_sht30_valid) {
         s_temp_history[s_temp_hist_len++] = ma_value(&s_ma_sht30);
      }
      uint32_t elapsed = s_run_seconds - s_sub_wait_start;
      float stddev_pct;
      bool stable = tec_check_steady_state(ma_value(&s_ma_sht30), s_temp_history,
                                            s_temp_hist_len, &stddev_pct);
      if (stable || elapsed > TEC_STEADY_MAX_WAIT_SEC) {
         s_ident.T_min = ma_value(&s_ma_sht30);
         TEC_LOGI("S2: T_min = %.2f °C (samples=%d)", s_ident.T_min, s_id_record_len);
         s_ident.K = 0; /* 先用正向数据拟合，这里先记录 */

         /* 回温 */
         tec_set_raw_duty(50.0f);
         fan_control_on(FAN_VENTILATION);
         s_sub_wait_start = s_run_seconds;
         s_temp_hist_len = 0;
         s_idsub = IDSUB_S3_COOLDOWN2;
      }
      break;
   }

   /* ================================================================
    *  S3：回温到 T_amb
    * ================================================================ */
   case IDSUB_S3_COOLDOWN2: {
      if (s_sht30_valid && s_temp_hist_len < 600) {
         s_temp_history[s_temp_hist_len++] = ma_value(&s_ma_sht30);
      }
      uint32_t elapsed = s_run_seconds - s_sub_wait_start;
      float stddev_pct;
      bool stable = tec_check_steady_state(ma_value(&s_ma_sht30), s_temp_history,
                                            s_temp_hist_len, &stddev_pct);
      /* 还要检查是否接近 T_amb */
      float diff = fabsf(ma_value(&s_ma_sht30) - s_ident.T_amb);
      if ((stable && diff < 1.0f) || elapsed > TEC_COOLDOWN_MAX_WAIT_SEC) {
         fan_control_off(FAN_VENTILATION);
         TEC_LOGI("S3: returned to %.2f °C (T_amb=%.2f)", ma_value(&s_ma_sht30), s_ident.T_amb);
         s_temp_hist_len = 0;
         s_id_record_len = 0;
         s_idsub = IDSUB_S4_STEP_HEAT;
         s_sub_wait_start = s_run_seconds;
         s_id_step_u = s_ident.u_heat_max;
         s_id_T_start = ma_value(&s_ma_sht30);
         tec_set_raw_duty(50.0f + s_ident.u_heat_max);
         TEC_LOGI("S4: step to u=+%.1f%% (heating)", s_ident.u_heat_max);
      }
      break;
   }

   /* ================================================================
    *  S4：阶跃制热，记录温度时间序列
    * ================================================================ */
   case IDSUB_S4_STEP_HEAT: {
      if (s_sht30_valid && s_id_record_len < ID_MAX_SAMPLES) {
         s_id_time[s_id_record_len] = (float)(s_run_seconds - s_sub_wait_start);
         s_id_temp[s_id_record_len] = ma_value(&s_ma_sht30);
         s_id_record_len++;
      }

      if (s_temp_hist_len < 600 && s_sht30_valid) {
         s_temp_history[s_temp_hist_len++] = ma_value(&s_ma_sht30);
      }
      uint32_t elapsed = s_run_seconds - s_sub_wait_start;
      float stddev_pct;
      bool stable = tec_check_steady_state(ma_value(&s_ma_sht30), s_temp_history,
                                            s_temp_hist_len, &stddev_pct);
      if (stable || elapsed > TEC_STEADY_MAX_WAIT_SEC) {
         s_ident.T_max = ma_value(&s_ma_sht30);
         TEC_LOGI("S4: T_max = %.2f °C (samples=%d)", s_ident.T_max, s_id_record_len);

         /* 拟合 FOPDT 并整定 PI */
         tec_fit_fopdt_and_tune();

         s_ident.valid = true;
         tec_set_raw_duty(50.0f);
         fan_control_off(FAN_VENTILATION);
         tec_save_ident_to_nvs();

         s_idsub = IDSUB_DONE;
         s_state = TEC_STATE_TUNED;
         TEC_LOGI("Identification complete! Range: [%.1f, %.1f] °C", s_ident.T_min, s_ident.T_max);
      }
      break;
   }

   case IDSUB_DONE:
   case IDSUB_IDLE:
   default:
      break;
   }
}

/* ================================================================
 *  NVS 存取辨识结果
 * ================================================================ */
static const char *NVS_NAMESPACE = "tec_ctrl";
static const char *NVS_KEY = "ident";

static void tec_save_ident_to_nvs(void)
{
   nvs_handle_t handle;
   if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
   nvs_set_blob(handle, NVS_KEY, &s_ident, sizeof(s_ident));
   nvs_commit(handle);
   nvs_close(handle);
   TEC_LOGI("Identification result saved to NVS");
}

bool tec_controller_load_ident_from_nvs(void)
{
   nvs_handle_t handle;
   if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;

   tec_ident_result_t buf;
   size_t len = sizeof(buf);
   esp_err_t err = nvs_get_blob(handle, NVS_KEY, &buf, &len);
   nvs_close(handle);

   if (err == ESP_OK && len == sizeof(buf) && buf.valid) {
      memcpy(&s_ident, &buf, sizeof(s_ident));
      ds18b20_set_role(s_ident.ds18_cold_idx, "cold");
      ds18b20_set_role(s_ident.ds18_hot_idx, "hot");
      s_state = TEC_STATE_TUNED;
      TEC_LOGI("Identification loaded from NVS: range [%.1f, %.1f] °C",
               s_ident.T_min, s_ident.T_max);
      return true;
   }
   return false;
}

/* ================================================================
 *  控制任务（1 Hz）
 * ================================================================ */
static void tec_control_task(void *arg)
{
   esp_task_wdt_add(NULL);
   TEC_LOGI("Control task started");

   /* 等待传感器就绪 */
   vTaskDelay(pdMS_TO_TICKS(3000));

   /* 首次尝试从 NVS 加载 */
   if (!tec_controller_load_ident_from_nvs()) {
      TEC_LOGI("No saved identification found → IDLE");
      s_state = TEC_STATE_IDLE;
   }

      while (1) {
      s_run_seconds++;

      /* 读取传感器 */
      tec_read_sensors();

      /* 任意传感器硬件故障（连续离线超限）→ 紧急停机 */
      if (tec_is_any_sensor_fault()) {
         if (s_state != TEC_STATE_SAFE) {
            TEC_LOGE("Sensor fault in state %s → emergency stop", tec_controller_get_state_str());
            s_state = TEC_STATE_SAFE;
            tec_set_raw_duty(50.0f);
            s_pi_enabled = false;
            s_pi_integral = 0.0f;
         }
      }

      /* 运行时安全检查（SAFE 状态也继续检查，但不再做 safety_check） */
      if (s_state == TEC_STATE_RUNNING || s_state == TEC_STATE_ID_HEATSINK ||
          s_state == TEC_STATE_ID_SYSTEM) {
         tec_safety_check();
      }

      /* 状态机 */
      switch (s_state) {
      case TEC_STATE_ID_HEATSINK:
      case TEC_STATE_ID_SYSTEM:
         tec_ident_state_machine();
         break;

      case TEC_STATE_RUNNING:
         s_pi_tick_counter++;
         if (s_pi_tick_counter >= TEC_PI_INTERVAL_SEC) {
            s_pi_tick_counter = 0;
            tec_pi_control();
         }
         break;

      case TEC_STATE_TUNED:
         break;

      default:
         break;
      }
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(TEC_TASK_INTERVAL_MS));
   }
}

/* ================================================================
 *  公开 API
 * ================================================================ */

void tec_controller_init(void)
{
   s_mutex = xSemaphoreCreateMutex();

   /* 初始化 MCPWM */
   tec_mcpwm_init();

   /* 关闭通风（以防 ventilation_control 正在运行） */
   ventilation_control_stop();

   /* 开启 TEC 冷端风扇（持续运行） */
   fan_control_on(FAN_TEC_COLD);

   /* 开启水冷风扇（持续运行，固定转速） */
   fan_control_on(FAN_WATER_COOLING);

   tec_set_raw_duty(50.0f);
   s_state = TEC_STATE_IDLE;

   /* 创建控制任务 */
   xTaskCreate(tec_control_task, "tec_ctrl", 8192, NULL, 5, &s_task_handle);
   if (s_task_handle) {
      TEC_LOGI("TEC controller initialized, task created");
   }
}

void tec_controller_start_identification(void)
{
   if (s_state == TEC_STATE_ID_HEATSINK || s_state == TEC_STATE_ID_SYSTEM) {
      TEC_LOGW("Identification already running");
      return;
   }
   /* 暂停通风自动循环 */
   ventilation_control_stop();
   fan_control_off(FAN_VENTILATION);
   /* ← 不再停止 DS18B20 连续采集！worker 继续运行，我们通过缓存读 */
   /* 开启辅助风扇 */
   fan_control_on(FAN_TEC_COLD);
   fan_control_on(FAN_WATER_COOLING);
   tec_set_raw_duty(50.0f);
   s_idsub = IDSUB_H1_STABILIZE;
   s_state = TEC_STATE_ID_HEATSINK;
   s_sub_wait_start = s_run_seconds;
   s_temp_hist_len = 0;
   s_id_over_temp_index = -1;
   s_id_coarse_u = 0.0f;
   memset(&s_ident, 0, sizeof(s_ident));
   s_ident.ds18_cold_idx = -1;
   s_ident.ds18_hot_idx = -1;
   /* 重置离线计数器 */
   s_sht30_offline_cnt = 0;
   s_ds18_offline_cnt[0] = 0;
   s_ds18_offline_cnt[1] = 0;
   TEC_LOGI("Identification started");
}

void tec_controller_set_target(float temperature)
{
   if (s_state != TEC_STATE_TUNED && s_state != TEC_STATE_RUNNING) {
      TEC_LOGW("Cannot set target: not tuned (state=%d)", s_state);
      return;
   }
   if (s_ident.valid) {
      if (temperature < s_ident.T_min) temperature = s_ident.T_min;
      if (temperature > s_ident.T_max) temperature = s_ident.T_max;
   }
   s_target_temp = temperature;
   s_pi_integral = 0.0f;
   s_pi_tick_counter = 0;
   s_pi_enabled = true;
   s_state = TEC_STATE_RUNNING;
   /* 恢复通风自动循环 */
   ventilation_control_start();
   /* ← 不再调用 ds18b20_sensor_start_continuous()，它应该一直在运行 */
   TEC_LOGI("Target set to %.2f °C, PI enabled", temperature);
}

tec_state_t tec_controller_get_state(void)
{
   return s_state;
}

const char *tec_controller_get_state_str(void)
{
   switch (s_state) {
   case TEC_STATE_OFF:       return "OFF";
   case TEC_STATE_IDLE:      return "IDLE";
   case TEC_STATE_ID_HEATSINK: return "ID_HEATSINK";
   case TEC_STATE_ID_SYSTEM:  return "ID_SYSTEM";
   case TEC_STATE_TUNED:     return "TUNED";
   case TEC_STATE_RUNNING:   return "RUNNING";
   case TEC_STATE_SAFE:      return "SAFE";
   case TEC_STATE_MANUAL:    return "MANUAL";
   default:                  return "UNKNOWN";
   }
}

const tec_ident_result_t *tec_controller_get_ident_result(void)
{
   return s_ident.valid ? &s_ident : NULL;
}

void tec_controller_emergency_stop(void)
{
   tec_set_raw_duty(50.0f);
   s_pi_enabled = false;
   s_pi_integral = 0.0f;
   s_state = TEC_STATE_SAFE;
   fan_control_off(FAN_TEC_COLD);
   fan_control_off(FAN_WATER_COOLING);
   TEC_LOGW("EMERGENCY STOP → SAFE state");
}

void tec_controller_set_manual_duty(float duty_pct)
{
   s_pi_enabled = false;
   s_state = TEC_STATE_MANUAL;
   tec_set_raw_duty(duty_pct);
   TEC_LOGI("Manual duty: %.1f%%", duty_pct);
}

float tec_controller_get_duty(void)
{
   return s_duty;
}

float tec_controller_get_u(void)
{
   return s_duty - 50.0f;
}

void tec_controller_resume_auto(void)
{
   if (s_state == TEC_STATE_MANUAL || s_state == TEC_STATE_SAFE) {
      if (s_ident.valid) {
         s_state = TEC_STATE_TUNED;
         tec_set_raw_duty(50.0f);
         TEC_LOGI("Resumed auto mode (TUNED)");
      } else {
         s_state = TEC_STATE_IDLE;
         tec_set_raw_duty(50.0f);
         TEC_LOGI("Resumed auto mode (IDLE, no ident)");
      }
   }
}