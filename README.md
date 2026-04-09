2026年4月10日更新
Plant Block
Plant Block：基于 ESP32 实现环境监测与自动控制（通风、补光、浇水），支持本地串口调试、云端 MQTT 远程监控及串口屏本地交互。项目持续升级中。

Plant Block: Plant module based on ESP32 for environmental monitoring and automatic control (ventilation, supplemental lighting, watering). It features local serial debugging, cloud MQTT remote monitoring, and serial touch screen interaction. Continuously under active development.

功能模块 / Modules
通风控制 – 定时启停风扇

补光灯控制 – PWM 智能调光（自动渐亮/渐暗）

智能浇水 – 基于土壤湿度阈值、4 小时防重复、周计划管理

时间管理 – 软件计时、Unix 时间戳、ISO 周数、星期计算

命令解析器 – 非阻塞串口命令，完整调试接口

云端通信 – WiFi 连接 + MQTT over TLS（设备状态上报、远程命令下发、遗嘱消息）

串口屏交互 – 淘晶驰串口屏数值上报与本地控制

数据处理 – 去极值、均值、标准差、稳定性判断

Ventilation – timed fan control

Supplemental Light – PWM dimming with auto ramp-up/down

Smart Watering – soil moisture threshold, 4‑hour cooldown, weekly plan

Time Management – software RTC, Unix timestamp, ISO week number, weekday

Command Parser – non‑blocking serial commands, full debugging interface

Cloud Communication – WiFi + MQTT over TLS (status reporting, remote commands, last will)

HMI Interface – DWIN serial touch screen data reporting and local control

Data Processing – outlier removal, mean, standard deviation, stability check

当前进展 / Current Status
硬件：Cadence 原理图、封装、初版 PCB 已完成并打样，等待测试

软件：核心功能已完成，即将升级为 4 通道版本（支持独立控制 4 个植物模块）

Hardware: Cadence schematic, footprint, and first PCB prototype have been fabricated – awaiting testing

Software: Core functions are complete; next step is upgrading to a 4‑channel version (supporting independent control of 4 plant modules)

<img width="1103" height="1051" alt="92994e99e9851de65859d0994ca0367" src="https://github.com/user-attachments/assets/e26dbbd2-c406-47df-9ab2-72ccef3995f8" />
<img width="1098" height="1063" alt="f88b9a7ae5009c51f814155c0ea91d6" src="https://github.com/user-attachments/assets/67e4706a-3f06-4ab7-864e-ef8576fc715b" />

2025年11月更新
Plant Block：基于 ESP32 实现环境监测与自动控制（通风、补光、浇水），支持本地串口调试、云端 MQTT 远程监控及串口屏本地交互。项目持续升级中。

Plant Block：30×30×60 mm 简易 3D 打印植物模块，结合 Arduino 自动监测环境温湿度和土壤湿度进行自助浇水，项目持续升级中。
Plant Block: Plant module based on ESP32 for environmental monitoring and automatic control (ventilation, supplemental lighting, watering). It features local serial debugging, cloud MQTT remote monitoring, and serial touch screen interaction. Continuously under active development.

Plant Block: A 30×30×60 mm simple 3D-printed plant module with Arduino-based monitoring of air and soil humidity and automatic self-watering, currently under active development.
功能模块 / Modules
通风控制 – 定时启停风扇

![Overall Block Struct](https://github.com/user-attachments/assets/33ce66a8-7f14-4820-b882-5774e183f363)
![Overall Electrical Components](https://github.com/user-attachments/assets/7ace10a7-a562-42b0-9e68-cb6ab06a2fb1)
补光灯控制 – PWM 智能调光（自动渐亮/渐暗）

智能浇水 – 基于土壤湿度阈值、4 小时防重复、周计划管理

时间管理 – 软件计时、Unix 时间戳、ISO 周数、星期计算

命令解析器 – 非阻塞串口命令，完整调试接口

云端通信 – WiFi 连接 + MQTT over TLS（设备状态上报、远程命令下发、遗嘱消息）

串口屏交互 – 淘晶驰串口屏数值上报与本地控制

数据处理 – 去极值、均值、标准差、稳定性判断

Ventilation – timed fan control

Supplemental Light – PWM dimming with auto ramp-up/down

Smart Watering – soil moisture threshold, 4‑hour cooldown, weekly plan

Time Management – software RTC, Unix timestamp, ISO week number, weekday

Command Parser – non‑blocking serial commands, full debugging interface

Cloud Communication – WiFi + MQTT over TLS (status reporting, remote commands, last will)

HMI Interface – DWIN serial touch screen data reporting and local control

Data Processing – outlier removal, mean, standard deviation, stability check

当前进展 / Current Status
硬件：Cadence 原理图、封装、初版 PCB 已完成并打样，等待测试

软件：核心功能已完成，即将升级为 4 通道版本（支持独立控制 4 个植物模块）

Hardware: Cadence schematic, footprint, and first PCB prototype have been fabricated – awaiting testing

Software: Core functions are complete; next step is upgrading to a 4‑channel version (supporting independent control of 4 plant modules)
