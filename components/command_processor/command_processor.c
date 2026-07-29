#include "command_processor.h"
#include "esp_log.h"
#include "time_manager.h"
#include "light_control.h"
#include "ventilation_control.h"
#include "irrigation_controller.h"
#include "moisture_sensor.h"
#include "fan_control.h"
#include "system_monitor.h"
#include "sht30_sensor.h"
#include "ds18b20_sensor.h"
#include "float_switch.h"
#include "tec_controller.h"
#include "event_bus.h"
#include "cloud_comm.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CMD_PROC";

// ==================== 工具函数 ====================

// 解析帧为 argc/argv（就地修改，用 '\0' 分隔）
#define CMD_MAX_ARGS 16
static int parse_frame_to_argv(char *frame, char *argv[], int max_args)
{
   int argc = 0;
   char *token = strtok(frame, " ");
   while (token != NULL && argc < max_args) {
      argv[argc++] = token;
      token = strtok(NULL, " ");
   }
   return argc;
}

// 统一日志输出（本地串口，方便 Vofa+ 旁路查看）
static void cmd_log(const char *fmt, ...)
{
   char buf[256];
   va_list args;
   va_start(args, fmt);
   vsnprintf(buf, sizeof(buf), fmt, args);
   va_end(args);
   ESP_LOGI(TAG, "%s", buf);
   cloud_comm_publish("msg", buf);
}

// ==================== 帮助命令 ====================
static void cmd_help(int argc, char *argv[])
{
   const cmd_entry_t *table = command_processor_get_table();
   int count = command_processor_get_table_size();

   if (argc >= 2 && argv[1] != NULL) {
      // 查找特定命令的帮助
      for (int i = 0; i < count; i++) {
         if (strcmp(table[i].name, argv[1]) == 0) {
            cmd_log("=== %s ===", table[i].name);
            cmd_log("用法: %s", table[i].usage);
            cmd_log("%s", table[i].help);
            return;
         }
      }
      cmd_log("未知命令: %s，输入 'help' 查看全部命令", argv[1]);
      return;
   }

   cmd_log("=== 可用命令 ===");
   for (int i = 0; i < count; i++) {
      if (table[i].name == NULL) break;
      cmd_log("  %-16s — %s", table[i].name, table[i].usage);
   }
   cmd_log("输入 'help <命令>' 查看详细帮助");
}

// ==================== 时间命令 ====================
static void cmd_time(int argc, char *argv[])
{
   if (argc >= 4 && strcmp(argv[1], "set") == 0) {
      // time set YYYY/MM/DD HH:MM:SS
      int year, month, day, hour, minute, second;
      char date_str[32], time_str[32];
      if (argc >= 4) {
         snprintf(date_str, sizeof(date_str), "%s", argv[2]);
         snprintf(time_str, sizeof(time_str), "%s", argv[3]);
         if (sscanf(date_str, "%d/%d/%d", &year, &month, &day) == 3 &&
             sscanf(time_str, "%d:%d:%d", &hour, &minute, &second) == 3) {
            irrigation_controller_notify_time_reset();
            if (time_manager_set_time(year, month, day, hour, minute, second)) {
               cmd_log("时间已设置: %s", time_manager_get_time_string());
            } else {
               cmd_log("时间设置失败，请检查参数");
            }
            return;
         }
      }
      cmd_log("格式: time set YYYY/MM/DD HH:MM:SS");
      return;
   }

   // time / time get
   cmd_log("当前时间: %s", time_manager_get_time_string());
}

// ==================== 补光灯命令 ====================
static void cmd_light(int argc, char *argv[])
{
   if (argc < 2) {
      cmd_log("缺少子命令: light <on|off|auto|status|set>");
      return;
   }

   if (strcmp(argv[1], "on") == 0) {
      light_control_manual_set(true);
      cmd_log("补光灯已手动开启");
   } else if (strcmp(argv[1], "off") == 0) {
      light_control_manual_set(false);
      cmd_log("补光灯已手动关闭");
   } else if (strcmp(argv[1], "auto") == 0) {
      light_control_set_auto_mode();
      cmd_log("补光灯已切换为自动模式");
   } else if (strcmp(argv[1], "status") == 0) {
      char buf[256];
      snprintf(buf, sizeof(buf),
               "start=%02d:%02d end=%02d:%02d dur=%.1fh state=%s duty=%d%% mode=%s",
               light_control_get_start_hour(), light_control_get_start_minute(),
               light_control_get_end_hour(), light_control_get_end_minute(),
               light_control_get_duration(),
               light_control_is_on() ? "on" : "off",
               light_control_get_pwm_duty(),
               light_control_is_manual_mode() ? "manual" : "auto");
      cmd_log("补光灯: %s", buf);
   } else if (strcmp(argv[1], "set") == 0 && argc >= 3) {
      if (strcmp(argv[2], "start") == 0 && argc >= 4) {
         int h, m;
         if (sscanf(argv[3], "%d:%d", &h, &m) == 2) {
            light_control_set_start_time(h, m);
            cmd_log("开启时间: %02d:%02d", h, m);
         }
      } else if (strcmp(argv[2], "end") == 0 && argc >= 4) {
         int h, m;
         if (sscanf(argv[3], "%d:%d", &h, &m) == 2) {
            light_control_set_end_time(h, m);
            cmd_log("关闭时间: %02d:%02d", h, m);
         }
      } else if (strcmp(argv[2], "duration") == 0 && argc >= 4) {
         float dur;
         if (sscanf(argv[3], "%f", &dur) == 1) {
            light_control_set_duration(dur);
            cmd_log("照明时长: %.1fh", dur);
         }
      } else {
         cmd_log("用法: light set <start HH:MM|end HH:MM|duration H.H>");
      }
   } else {
      cmd_log("用法: light <on|off|auto|status|set ...>");
   }
}

// ==================== 风扇命令 ====================
static void cmd_fan(int argc, char *argv[])
{
   if (argc < 2) {
      cmd_log("用法: fan <list|status|set <name> <on|off> [duty]>");
      return;
   }

   if (strcmp(argv[1], "list") == 0) {
      cmd_log("风扇列表: ventilation, tec_cold, water_cool");
      return;
   }

   if (strcmp(argv[1], "status") == 0) {
      cmd_log("ventilation: %s", fan_control_is_on(FAN_VENTILATION) ? "ON" : "OFF");
      cmd_log("tec_cold:    %s", fan_control_is_on(FAN_TEC_COLD) ? "ON" : "OFF");
      cmd_log("water_cool:  %s (duty=%d%%)",
              fan_control_is_on(FAN_WATER_COOLING) ? "ON" : "OFF",
              fan_control_get_duty(FAN_WATER_COOLING));
      return;
   }

   if (strcmp(argv[1], "set") == 0 && argc >= 4) {
      fan_id_t fid;
      if (strcmp(argv[2], "ventilation") == 0) fid = FAN_VENTILATION;
      else if (strcmp(argv[2], "tec_cold") == 0) fid = FAN_TEC_COLD;
      else if (strcmp(argv[2], "water_cool") == 0) fid = FAN_WATER_COOLING;
      else { cmd_log("未知风扇: %s", argv[2]); return; }

      bool on = (strcmp(argv[3], "on") == 0);
      uint8_t duty = 95;
      if (argc >= 5) duty = (uint8_t)atoi(argv[4]);

      fan_control_set(fid, on, duty);
      cmd_log("风扇 '%s': %s, duty=%d%%", argv[2], on ? "ON" : "OFF", duty);
      return;
   }

   cmd_log("用法: fan <list|status|set <name> <on|off> [duty]>");
}

// ==================== 灌溉命令 ====================
static void cmd_irrigation(int argc, char *argv[])
{
   if (argc < 2) {
      cmd_log("用法: irrigation <status|set ...|reset>");
      return;
   }
   if (strcmp(argv[1], "status") == 0) {
      cmd_log("阈值=%d%% 水量=%dml 周min=%d 周max=%d 本周=%d/%d",
              irrigation_controller_get_threshold(),
              irrigation_controller_get_volume(),
              irrigation_controller_get_week_min(),
              irrigation_controller_get_week_max(),
              irrigation_controller_get_week_count(),
              irrigation_controller_get_week_max());
      return;
   }
   if (strcmp(argv[1], "set") == 0 && argc >= 4) {
      if (strcmp(argv[2], "threshold") == 0) {
         irrigation_controller_set_threshold(atoi(argv[3]));
      } else if (strcmp(argv[2], "volume") == 0) {
         irrigation_controller_set_volume(atoi(argv[3]));
      } else if (strcmp(argv[2], "week_min") == 0) {
         irrigation_controller_set_week_min(atoi(argv[3]));
      } else if (strcmp(argv[2], "week_max") == 0) {
         irrigation_controller_set_week_max(atoi(argv[3]));
      } else {
         cmd_log("用法: irrigation set <threshold|volume|week_min|week_max> <value>");
         return;
      }
      cmd_log("已设置");
      return;
   }
   if (strcmp(argv[1], "reset") == 0) {
      irrigation_controller_reset_week();
      cmd_log("本周浇水次数已重置");
      return;
   }
   cmd_log("用法: irrigation <status|set ...|reset>");
}

// ==================== 传感器命令 ====================
static void cmd_moisture(int argc, char *argv[])
{
   if (argc < 2) {
      cmd_log("用法: moisture <on|off|read|stop|cal dry|cal wet|status>");
      return;
   }
   if (strcmp(argv[1], "on") == 0) moisture_sensor_power_on();
   else if (strcmp(argv[1], "off") == 0) moisture_sensor_power_off();
   else if (strcmp(argv[1], "read") == 0) moisture_sensor_start_reading();
   else if (strcmp(argv[1], "stop") == 0) moisture_sensor_stop_reading();
   else if (strcmp(argv[1], "cal") == 0 && argc >= 3) {
      if (strcmp(argv[2], "dry") == 0) moisture_sensor_cal_dry();
      else if (strcmp(argv[2], "wet") == 0) moisture_sensor_cal_wet();
   }
   else if (strcmp(argv[1], "status") == 0) {
      cmd_log("电源=%s 湿度=%.1f%% 电压=%lumV",
              moisture_sensor_is_powered() ? "on" : "off",
              moisture_sensor_get_humidity_percent(),
              (unsigned long)moisture_sensor_get_calibrated_voltage());
   }
   else cmd_log("用法: moisture <on|off|read|stop|cal dry|cal wet|status>");
}

static void cmd_ds18b20(int argc, char *argv[])
{
   (void)argc; (void)argv;
   int count = ds18b20_sensor_get_device_count();
   cmd_log("DS18B20: %d device(s)", count);
   float temps[2] = {0};
   for (int i = 0; i < count && i < 2; i++) {
      ds18b20_sensor_get_temperature_cached(i, &temps[i]);  // ← 改用 cached
   }
   if (count >= 1) cmd_log("  [0] cold_side: %.2f °C", temps[0]);
   if (count >= 2) cmd_log("  [1] hot_side:  %.2f °C", temps[1]);
}

static void cmd_sht30(int argc, char *argv[])
{
   (void)argc; (void)argv;
   float t, h;
   if (sht30_sensor_get_data(&t, &h)) {
      cmd_log("SHT30: T=%.2f°C H=%.2f%%RH", t, h);
   } else {
      cmd_log("SHT30: 数据无效");
   }
}

// ==================== 通风命令 ====================
static void cmd_ventilation(int argc, char *argv[])
{
   if (argc >= 2 && strcmp(argv[1], "status") == 0) {
      cmd_log("通风: %s | 周期 ON=%ds OFF=%ds",
              ventilation_control_get_state() ? "开" : "关",
              ventilation_control_get_on_seconds(),
              ventilation_control_get_off_seconds());
      return;
   }
   cmd_log("用法: ventilation <status>");
}

// ==================== 系统命令 ====================
static void cmd_system(int argc, char *argv[])
{
   if (argc < 2) {
      cmd_log("用法: system <status|health>");
      return;
   }
   if (strcmp(argv[1], "status") == 0) {
      cmd_log("=== 系统状态 ===");
      cmd_log("时间: %s", time_manager_get_time_string());
      cmd_log("补光灯: %s (duty=%d%%)",
              light_control_is_on() ? "ON" : "OFF", light_control_get_pwm_duty());
      cmd_log("通风: %s", ventilation_control_get_state() ? "ON" : "OFF");
      cmd_log("浇水: 本周%d次", irrigation_controller_get_week_count());
   } else if (strcmp(argv[1], "health") == 0) {
      cmd_log("Free heap: %lu bytes", (unsigned long)system_monitor_get_free_heap());
      cmd_log("Min free heap: %lu bytes", (unsigned long)system_monitor_get_min_free_heap());
   } else {
      cmd_log("用法: system <status|health>");
   }
}

// ==================== TEC 命令 ====================
static void cmd_tec(int argc, char *argv[])
{
   if (argc < 2) {
      cmd_log("用法: tec <identify|set <temp>|status|stop|manual <duty>|auto>");
      return;
   }

   if (strcmp(argv[1], "identify") == 0) {
      tec_controller_start_identification();
      cmd_log("TEC 辨识已启动（异步）");
   } else if (strcmp(argv[1], "set") == 0 && argc >= 3) {
      float t = atof(argv[2]);
      tec_controller_set_target(t);
      cmd_log("TEC 目标温度: %.2f °C", t);
   } else if (strcmp(argv[1], "status") == 0) {
      cmd_log("状态: %s", tec_controller_get_state_str());
      cmd_log("占空比: %.1f%% (u=%.1f%%)", tec_controller_get_duty(), tec_controller_get_u());
      const tec_ident_result_t *id = tec_controller_get_ident_result();
      if (id) {
         cmd_log("辨识: u_cool=%.1f u_heat=%.1f T_range=[%.1f,%.1f]",
                 id->u_cool_max, id->u_heat_max, id->T_min, id->T_max);
         cmd_log("PI: Kc=%.4f Ti=%.1fs K=%.4f τ=%.1fs θ=%.1fs",
                 id->Kc, id->Ti, id->K, id->tau, id->theta);
      } else {
         cmd_log("辨识: 未完成");
      }
   } else if (strcmp(argv[1], "stop") == 0) {
      tec_controller_emergency_stop();
      cmd_log("TEC 紧急停止");
   } else if (strcmp(argv[1], "manual") == 0 && argc >= 3) {
      float duty = atof(argv[2]);
      tec_controller_set_manual_duty(duty);
      cmd_log("TEC 手动占空比: %.1f%%", duty);
   } else if (strcmp(argv[1], "auto") == 0) {
      tec_controller_resume_auto();
      cmd_log("TEC 恢复自动模式");
   } else {
      cmd_log("用法: tec <identify|set <temp>|status|stop|manual <duty>|auto>");
   }
}

// ==================== 命令表 ====================
#define CMD_ENTRY(name, usage, help_text) { #name, cmd_##name, usage, help_text }

static const cmd_entry_t s_command_table[] = {
   CMD_ENTRY(help,        "help [command]",              "显示帮助信息"),
   CMD_ENTRY(time,        "time [set Y/M/D H:M:S]",      "系统时间管理"),
   CMD_ENTRY(light,       "light <on|off|auto|status|set ...>", "补光灯控制"),
   CMD_ENTRY(fan,         "fan <list|status|set ...>",   "风扇控制"),
   CMD_ENTRY(irrigation,  "irrigation <status|set ...>", "浇水控制"),
   CMD_ENTRY(moisture,    "moisture <on|off|read|...>",  "土壤湿度传感器"),
   CMD_ENTRY(ds18b20,     "ds18b20",                     "DS18B20 温度查询"),
   CMD_ENTRY(sht30,       "sht30",                       "SHT30 温湿度查询"),
   CMD_ENTRY(ventilation, "ventilation <status>",        "通风控制状态"),
   CMD_ENTRY(tec,         "tec <identify|set|status|stop|manual|auto>", "TEC温度控制"),
   CMD_ENTRY(system,      "system <status|health>",      "系统状态与健康检查"),
   { NULL, NULL, NULL, NULL }  // 哨兵
};

const cmd_entry_t *command_processor_get_table(void) { return s_command_table; }
int command_processor_get_table_size(void)
{
   int count = 0;
   while (s_command_table[count].name != NULL) count++;
   return count;
}

// ==================== 初始化 ====================
void command_processor_init(void)
{
   ESP_LOGI(TAG, "Command processor initialized (table mode)");
   // 模拟一个 help 调用输出
   char *fake_argv[] = {"help", NULL};
   cmd_help(1, fake_argv);
}

// ==================== 帧处理核心 ====================
void command_processor_process_frame(const char *frame)
{
   if (frame == NULL || frame[0] == '\0') return;

   // 拷贝到可变缓冲区
   char buf[256];
   strncpy(buf, frame, sizeof(buf) - 1);
   buf[sizeof(buf) - 1] = '\0';

   // 解析为 argc/argv
   char *argv[CMD_MAX_ARGS];
   int argc = parse_frame_to_argv(buf, argv, CMD_MAX_ARGS);

   if (argc == 0) return;

   ESP_LOGI(TAG, "CMD: %s", frame);

   // 查表
   for (int i = 0; s_command_table[i].name != NULL; i++) {
      if (strcmp(s_command_table[i].name, argv[0]) == 0) {
         s_command_table[i].handler(argc, argv);
         return;
      }
   }

   cmd_log("未知命令: '%s'，输入 'help' 查看可用命令", argv[0]);
}