#ifndef PIN_DEFINITIONS_H
#define PIN_DEFINITIONS_H

// ============================================================
//  V2.0 引脚定义 — 植物生长控制器
//  所有 GPIO 分配集中于此文件，main.c 和各模块统一引用
// ============================================================

// ---------- 系统固定引脚（不可更改）----------
#define PIN_UART_TX         1    // UART0 TXD → CH340C RX（固定 strapping）
#define PIN_UART_RX         3    // UART0 RXD → CH340C TX（固定 strapping）
// GPIO0  → 自动下载电路 + BOOT 按键（strapping）
// GPIO2  → 板载 LED 或 strapping（视 PCB 而定）

// ---------- 传感器 ----------
#define PIN_SHT30_SCL           25   // I²C 时钟
#define PIN_SHT30_SDA           26   // I²C 数据
#define PIN_DS18B20_BUS         33   // One Wire 单总线（2 颗 DS18B20 共享）
#define PIN_DS18B20_MAX_COUNT   2    // 冷端 + 热端

#define PIN_MOISTURE_POWER      32   // 土壤湿度传感器 VCC 控制（高电平上电）
#define PIN_MOISTURE_ADC        35   // 土壤湿度模拟输入（ADC1_CH7，仅输入）

#define PIN_FLOAT_SWITCH        21   // 浮球开关数字输入（需外部上拉）

#define PIN_NTC_OVERTEMP        36   // NTC 过温断电电路状态监控（仅输入）
                                     // 比较器输出：高电平=过温，低电平=正常

// ---------- 执行器 - NMOS 数字开关 ----------
#define PIN_VENTILATION_FAN     5    // 通风风扇 SI2302 低端开关
#define PIN_TEC_COLD_FAN        17   // TEC 冷端散热风扇 SI2302 低端开关
#define PIN_IRRIGATION_PUMP     16   // 蠕动泵 SI2302 低端开关

// ---------- 执行器 - PWM ----------
#define PIN_COB_LED_PWM         14   // COB LED 升压恒流驱动 PWM（XL6005E1 EN/DIM）
#define PIN_COB_LED_POWER       12   // COB LED 升压恒流驱动 EN（高电平使能）
#define PIN_WATER_FAN_PWM       18   // 水冷风扇 4 线 PWM 调速
#define PIN_WATER_FAN_TACH      19   // 水冷风扇转速反馈 TACH（可选，脉冲计数）

// ---------- TEC H 桥（Phase 3 实现）----------
#define PIN_TEC_PWM_H           13   // IR2104 #1 高侧 / IR2104 #2 低侧（MCPWM）
#define PIN_TEC_PWM_L           27   // IR2104 #1 低侧 / IR2104 #2 高侧（MCPWM）

// ============================================================
//  注意事项：
//  1. GPIO34~39 为 ESP32 仅输入引脚，已正确分配为输入功能
//  2. GPIO35/36 不能用作 UART TX（无输出能力），UART 使用 GPIO1/3
//  3. GPIO12 为 MTDI strapping，V1.0 用作灌溉，V2.0 已改为 GPIO16
//  4. 所有 NMOS 低端开关需栅极下拉电阻（已由硬件保证）
// ============================================================

#endif // PIN_DEFINITIONS_H