#ifndef TEC_CONTROLLER_H
#define TEC_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

/* ================================================================
 *  TEC 温度控制器 — 带自动辨识 + Lambda PI 整定 + 动态限幅
 *
 *  占空比语义：duty = 50% + u,  u ∈ [-50, 50]
 *    u =  0 → 50% 占空比 → TEC 无输出
 *    u >  0 → 正向（水冷侧制热 / 风冷侧制冷）
 *    u <  0 → 反向（水冷侧制冷 / 风冷侧制热）
 * ================================================================ */

/* ---- 运行状态 ---- */
typedef enum {
   TEC_STATE_OFF = 0,           /* 未初始化 / 已关闭 (duty=50%) */
   TEC_STATE_IDLE,              /* 已初始化，等待指令 */
   TEC_STATE_ID_HEATSINK,      /* 阶段 1：正在辨识散热器极限 */
   TEC_STATE_ID_SYSTEM,        /* 阶段 2：正在辨识系统模型 */
   TEC_STATE_TUNED,            /* PI 参数已整定，待设定目标温度 */
   TEC_STATE_RUNNING,          /* 正常运行中（PI 闭环） */
   TEC_STATE_SAFE,             /* 安全停机（过温 / 传感器故障） */
   TEC_STATE_MANUAL,           /* 手动模式 */
} tec_state_t;

/* ---- 辨识结果（可存入 NVS）---- */
typedef struct {
   float  u_cool_max;          /* 制冷方向极限（负值，如 -42.0 表示 duty=8%） */
   float  u_heat_max;          /* 制热方向极限（正值，如 38.0 表示 duty=88%） */
   float  T_amb;               /* 环境温度 (℃) */
   float  T_min;               /* 极限低温 (℃) */
   float  T_max;               /* 极限高温 (℃) */
   float  K;                   /* 过程增益 (ΔT/Δu)，取绝对值 */
   float  tau;                 /* 时间常数 (秒) */
   float  theta;               /* 纯滞后时间 (秒) */
   float  Kc;                  /* PI 比例增益 */
   float  Ti;                  /* PI 积分时间 (秒) */
   int    ds18_cold_idx;       /* 冷端（风冷侧）DS18B20 索引 */
   int    ds18_hot_idx;        /* 热端（水冷侧）DS18B20 索引 */
   bool   valid;               /* 数据是否有效 */
} tec_ident_result_t;

/* ---- 公开 API ---- */

/* 初始化 TEC 控制器（MCPWM + 控制任务） */
void tec_controller_init(void);

/* 启动完整辨识流程（异步，在内部控制任务中执行） */
void tec_controller_start_identification(void);

/* 设定目标温度（℃），辨识完成后调用 */
void tec_controller_set_target(float temperature);

/* 获取当前运行状态 */
tec_state_t tec_controller_get_state(void);

/* 获取状态字符串 */
const char *tec_controller_get_state_str(void);

/* 获取辨识结果（只读） */
const tec_ident_result_t *tec_controller_get_ident_result(void);

/* 紧急停止 → u=0，关闭风扇，进入 SAFE 状态 */
void tec_controller_emergency_stop(void);

/* 手动设置占空比（调试用，自动进入 MANUAL 模式） */
void tec_controller_set_manual_duty(float duty_percent);

/* 获取当前实际占空比 (%) */
float tec_controller_get_duty(void);

/* 获取控制变量 u (= duty - 50) */
float tec_controller_get_u(void);

/* 恢复自动模式（从 MANUAL/SAFE 回到 IDLE 或 RUNNING） */
void tec_controller_resume_auto(void);

/* 加载 NVS 中保存的辨识结果（开机快速恢复） */
bool tec_controller_load_ident_from_nvs(void);

#endif /* TEC_CONTROLLER_H */