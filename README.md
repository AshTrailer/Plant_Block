# Plant Block - 智能植物养护系统 / Intelligent Plant Care System

[![Project Status](https://img.shields.io/badge/Status-Active%20Development-brightgreen)]()
[![Hardware](https://img.shields.io/badge/Hardware-Cadence-blue)]()
[![MCU](https://img.shields.io/badge/MCU-ESP32--WROOM--32E--N8-orange)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey)]()

基于 ESP32 的环境监测与自动控制系统，支持通风、补光、TEC 温控、浇水等核心功能，具备本地串口调试、云端 MQTT 远程监控能力。

*An ESP32-based environmental monitoring and automatic control system, supporting ventilation, supplemental lighting, TEC temperature control, watering, and other core functions, with local serial debugging and cloud MQTT remote monitoring capabilities.*

---

## 项目升级日志 / Project Update Log

### 🔧 最新更新 — 第二版原理图设计完成 / Latest: v2.0 Schematic Design Completed

第二版原理图绘制、焊盘及封装绘制已全部完成。电源架构与功能模块方案已最终确定，详见下方第二版硬件设计章节。PCB 布局布线为下一阶段工作。

*The v2.0 schematic, footprint, and PCB library have been completed. The power architecture and functional module design have been finalized — see the v2.0 hardware design section below. PCB layout is the next phase.*

### 2026年4月15日更新 — 第二版开发预告 / Apr 15, 2026 — v2.0 Development Preview

第二版硬件规划启动，将进行多项重大升级，提升系统性能和扩展性。

*v2.0 hardware planning initiated, featuring major upgrades to improve system performance and scalability.*

### 2026年3月更新 — 第一版完成 / Mar 2026 — v1.0 Completed

第一版硬件已完成设计、打样，软件核心功能开发完毕，升级为 4 通道版本。

*v1.0 hardware design, fabrication, and core software development completed; upgraded to a 4-channel version.*

---

## 第一版硬件设计 (v1.0) / v1.0 Hardware Design

> **设计思路 / Design Philosophy**：第一版以"多通道独立控制"为核心，目标是同时管理 4 个独立植物模块，每个模块配备独立的 LED 补光与传感器接口。12V 供电、单级降压的简洁架构便于快速验证核心功能。

> *v1.0 centered on "multi-channel independent control," aiming to manage 4 separate plant modules simultaneously, each with independent LED lighting and sensor interfaces. The simple 12V single-stage buck architecture enabled rapid core functionality validation.*

### 硬件架构 / Hardware Architecture

#### 电源系统 / Power System输入 / Input：12V DC
├── TPS54302DDCR：12V → 5V 降压转换 / Buck Converter\
├── PT4115：LED 恒流驱动 ×4 / Constant Current LED Driver ×4\
└── 各模块供电分配 / Peripheral Power Distribution

#### 控制通道配置 / Control Channel Configuration

| 模块类型 / Module Type | 设计预留 / Designed | 实际可用 / Available | 原因说明 / Note |
|---|---|---|---|
| **光照控制 / Lighting** | 4 通道 / ch | 4 通道 / ch | 每通道 3W，总计 12W / 3W per ch, 12W total |
| **水泵控制 / Water Pump** | 4 个 | 1 个 | PCB GPIO 端口分配错误 / GPIO assignment error |
| **传感器接口 / Sensor** | 4 个 | 1 个 | PCB GPIO 端口分配错误 / GPIO assignment error |

#### 核心功能模块 / Core Functional Modules

| # | 模块 / Module | 说明 / Description |
|---|---|---|
| 1 | **通风控制 / Ventilation** | 定时启停风扇 / Timed fan on/off |
| 2 | **补光灯控制 / Lighting** | PWM 智能调光（自动渐亮/渐暗）/ PWM dimming with auto fade |
| 3 | **LED 散热控制 / LED Cooling** | 独立散热管理 / Independent thermal management |
| 4 | **智能浇水 / Watering** | 土壤湿度阈值、4 小时防重复、周计划管理 / Soil moisture threshold, 4h anti-repeat, weekly schedule |
| 5 | **时间管理 / Time** | 软件计时、Unix 时间戳、ISO 周数、星期计算 / Software timer, Unix timestamp, ISO week, weekday calc |
| 6 | **命令解析器 / CLI** | 非阻塞串口命令，完整调试接口 / Non-blocking serial command parser |
| 7 | **云端通信 / Cloud** | WiFi + MQTT over TLS（状态上报、远程命令、遗嘱消息）/ Status report, remote command, LWT |
| 8 | **数据处理 / Data** | 去极值、均值、标准差、稳定性判断 / Outlier removal, mean, stddev, stability check |

> **注 / Note**：串口屏交互功能暂未开发，将在后续版本中实现。
> *The serial touchscreen UI is not yet developed and will be implemented in a future version.*

#### 硬件图片 / Hardware Images

<div align="center">
  <img src="https://github.com/user-attachments/assets/ba24fef7-72bb-4d3b-9d40-662109841446" width="400" alt="v1.0 PCB Front / 第一版电路板正面">
  <br/><em>第一版电路板正面 / v1.0 PCB Front View</em>
</div>

<div align="center">
  <img src="https://github.com/user-attachments/assets/e26dbbd2-c406-47df-9ab2-72ccef3995f8" width="400" alt="System Block Diagram / 系统框图">
  <img src="https://github.com/user-attachments/assets/67e4706a-3f06-4ab7-864e-ef8576fc715b" width="400" alt="Electrical Components / 电气组件图">
  <br/><em>左：系统框图 / Left: System Block Diagram &nbsp;|&nbsp; 右：电气组件图 / Right: Electrical Components</em>
</div>

#### 第一版小结 / v1.0 Summary

| 维度 / Aspect | 状态 / Status |
|---|---|
| **硬件 / Hardware** | Cadence 原理图、封装、初版 PCB 已完成并打样，等待测试 / Schematic, footprint, PCB designed & fabricated, awaiting test |
| **软件 / Software** | 核心功能已完成，升级为 4 通道版本 / Core functions complete, 4-channel support |
| **已知问题 / Known Issues** | GPIO 端口分配错误导致部分通道不可用；12V 供电限制了 TEC 等大功率模块的引入 / GPIO assignment errors; 12V limits high-power modules like TEC |

---

## 第二版硬件设计 (v2.0) / v2.0 Hardware Design

> **设计思路 / Design Philosophy**：第二版从"多通道简单控制"转向"单系统精细化控制"。核心变化包括：① 提升输入电压至 24V 以支持更大功率负载；② 引入 TEC 半导体制冷实现主动温控；③ 使用 50W COB 灯珠替代 4 颗分立 LED，简化光路并提升光效；④ 增加多种传感器实现全方位环境感知；⑤ 增加硬件级过温保护，提升系统安全性。

> *v2.0 shifts from "multi-channel simple control" to "single-system precision control." Key changes: ① 24V input to support higher power loads; ② TEC-based active cooling; ③ 50W COB LED replaces 4 discrete LEDs for simplified optics and higher efficiency; ④ multi-sensor fusion for comprehensive environmental awareness; ⑤ hardware-level over-temperature protection for improved safety.*

---

### 2.1 电源架构 / Power Architecture

#### 电源树 / Power Tree
24V DC 输入 / Input\
│\
├── [保护电路 / Protection]\
│   ├── 防反接 / Reverse Polarity Protection\
│   ├── 防静电 (ESD) / ESD Protection\
│   └── 防浪涌 / Surge Protection\
│\
├── TPS56A37 ────────────► 12V @ 3A+ ────┬── 蠕动泵 / Peristaltic Pump (SI2302)\
│   (24V → 12V Buck)                      ├── TEC 冷端风扇 / TEC Cold-side Fan (SI2302)\
│                                         ├── 通风风扇 / Ventilation Fan (SI2302)\
│                                         └── 水冷风扇 / Water-cooling Fan (SI2302)\
│\
├── TPS54302DDCR ────────► 5V @ 2A+ ────┬── ESP32-WROOM-32E-N8(24V → 5V Buck)\
│                                       ├── CH340C (USB-UART)\
│                                       ├── DS18B20 ×2 (One Wire)\
│                                       ├── SHT30 (I²C)\
│                                       ├── 浮球开关 / Float Switch\
│                                       ├── IR2104STRPBF ×2 (TEC 半桥驱动)\
│                                       └── LM393DT ×2 (PT100 比较器)\
│\
└── AMS1117-3V3 ─────────► 3.3V ────────► ESP32 内核及 GPIO / ESP32 Core & GPIO
(5V → 3.3V LDO)

#### 电源芯片选型对比 / Power IC Selection

| 功能 / Function | 芯片 / IC | 输入范围 / Input | 输出 / Output | 说明 / Note |
|---|---|---|---|---|
| 24V → 12V Buck | **TPS56A37** | 4.5V – 28V | 12V / 3A+ | 同步降压，高效率 / Synchronous buck, high efficiency |
| 24V → 5V Buck | **TPS54302DDCR** | 4.5V – 28V | 5V / 2A+ | 与第一版同型号，输入范围满足 24V / Same IC as v1.0, input range covers 24V |
| 5V → 3.3V LDO | **AMS1117-3V3** | up to 15V | 3.3V / 1A | 经典低压差线性稳压器 / Classic low-dropout regulator |

#### 保护电路详情 / Protection Circuit Details

| 保护类型 / Protection | 实现方式 / Implementation | 防护对象 / Target |
|---|---|---|
| **防反接 / Reverse Polarity** | PMOS / 肖特基二极管方案 | 全部后端电路 / All downstream circuits |
| **防静电 / ESD** | TVS 二极管阵列（24V 输入端 + USB 信号线） | 电源入口及敏感信号引脚 |
| **防浪涌 / Surge** | TVS 管 + 共模电感 + π 型滤波 | 电源入口，抑制瞬态过压 |

---

### 2.2 功能模块详述 / Functional Modules

#### 2.2.1 蠕动泵控制 / Peristaltic Pump Control

| 参数 / Parameter | 规格 / Specification |
|---|---|
| **供电电压 / Voltage** | 12V DC |
| **驱动方式 / Drive** | **SI2302** N-MOSFET 低端开关 / Low-side switch |
| **控制接口 / Control** | ESP32 GPIO → SI2302 Gate（带栅极下拉电阻 / with gate pull-down） |
| **PWM 调速** | 支持 / Supported（软件可调流量 / software-adjustable flow rate） |
| **续流保护 / Flyback** | 肖特基二极管并联泵电机 / Schottky diode across pump motor |

#### 2.2.2 风扇控制（3 路）/ Fan Control (3 Channels)

三路风扇均采用统一驱动方案，仅控制 GPIO 不同：

*All 3 fan channels use the same driver topology with different control GPIOs:*

| 风扇 / Fan | 用途 / Purpose | 供电 / Voltage | 驱动 MOSFET |
|---|---|---|---|
| **TEC 冷端风扇** / Cold-side Fan | 为 TEC 冷端散热器强制通风 / Force air over TEC cold-side heatsink | 12V | SI2302 |
| **通风风扇** / Ventilation Fan | 培养空间空气交换 / Grow space air exchange | 12V | SI2302 |
| **水冷风扇** / Water-cooling Fan | TEC 热端水冷排散热 / Radiator cooling for TEC hot-side | 12V | SI2302 |

#### 2.2.3 浮球开关 / Float Switch (液位检测 / Water Level Detection)

| 参数 / Parameter | 规格 / Specification |
|---|---|
| **类型 / Type** | 机械浮球开关 / Mechanical float switch（常开/常闭可选 / NO/NC selectable） |
| **接口 / Interface** | GPIO 数字输入 + 上拉电阻 / GPIO digital input with pull-up |
| **功能 / Function** | 水箱低液位检测，防止泵空转 / Low water level detection to prevent pump dry-running |
| **软件处理 / Software** | 低液位触发中断 → 停止蠕动泵 + MQTT 报警 / Low level interrupt → stop pump + MQTT alert |

#### 2.2.4 DS18B20 温度传感器 ×2 / Dual DS18B20 Temperature Sensors

| 参数 / Parameter | 规格 / Specification |
|---|---|
| **总线类型 / Bus** | **One Wire（单总线）**，两颗传感器共享同一 GPIO / Shared single GPIO |
| **传感器 1** | TEC 冷端温度 / TEC cold-side temperature |
| **传感器 2** | TEC 热端温度 / TEC hot-side temperature |
| **分辨率 / Resolution** | 9–12 bit 可配置 / configurable（默认 12 bit / default 12 bit） |
| **供电方式 / Power** | 寄生供电模式 / Parasite power mode（VDD 接 GND）或外部供电 |
| **识别方式 / ID** | 通过唯一 64-bit ROM ID 区分两颗传感器 / Distinguished by unique 64-bit ROM ID |

> **设计说明 / Design Note**：两颗 DS18B20 共享单个 GPIO 可节省 ESP32 引脚资源。软件通过 ROM ID 分别读取冷端和热端温度，用于 TEC PID 控制算法的反馈输入。
> *Sharing a single GPIO conserves ESP32 pins. Firmware distinguishes sensors by ROM ID for cold/hot side temperature feedback into the TEC PID control loop.*

#### 2.2.5 SHT30 温湿度传感器 / SHT30 Temperature & Humidity Sensor

| 参数 / Parameter | 规格 / Specification |
|---|---|
| **芯片 / IC** | SHT30-DIS（I²C 接口） |
| **测量范围 / Range** | 温度 / Temp：-40°C ~ +125°C；湿度 / Humidity：0% ~ 100% RH |
| **精度 / Accuracy** | 温度 / Temp：±0.3°C；湿度 / Humidity：±2% RH（典型值 / typical） |
| **接口 / Interface** | I²C（SCL + SDA），3.3V 供电 |
| **用途 / Purpose** | 培养空间环境温湿度监测 / Ambient temp & humidity monitoring in grow space |

#### 2.2.6 50W COB 灯珠驱动 / 50W COB LED Driver

| 参数 / Parameter | 规格 / Specification |
|---|---|
| **LED 类型 / Type** | 50W COB（Chip-on-Board）灯珠，全光谱 / Full-spectrum |
| **驱动芯片 / Driver IC** | **XL6005E1** — Boost 升压恒流驱动 / Boost constant-current driver |
| **调光方式 / Dimming** | **PWM 调光**，ESP32 GPIO 输出 PWM 至 XL6005E1 EN/DIM 引脚 |
| **调光频率 / PWM Freq** | 建议 1kHz – 20kHz（避开人耳敏感频段 / avoid audible range） |
| **散热策略 / Cooling** | COB 灯珠独立散热片 + 通风风扇辅助 / Dedicated heatsink + ventilation fan |

> **设计说明 / Design Note**：相比第一版的 4 路 PT4115 + 3W 分立 LED，第二版采用单颗 50W COB + XL6005E1 Boost 方案。优势：① 光效更高；② 光谱均匀性更好；③ 简化 PCB 布线（单通道 vs 四通道）；④ 减少元器件数量。
> *Compared to v1.0's 4× PT4115 + 3W discrete LEDs, v2.0 uses a single 50W COB + XL6005E1 boost topology. Advantages: higher efficacy, better spectral uniformity, simplified PCB routing (1ch vs 4ch), and reduced BOM count.*

#### 2.2.7 TEC H 桥控制电路 / TEC H-Bridge Control

这是第二版最核心的新增模块，用于驱动 TEC 半导体制冷片实现双向温控（加热/制冷）。

*This is the most critical new module in v2.0, driving the TEC (Peltier) module for bidirectional temperature control (heating/cooling).*

| 组件 / Component | 型号 / Part | 数量 / Qty | 功能 / Function |
|---|---|---|---|
| **TEC 模块 / TEC Module** | TEC1-12706 | 1 | 12V / 6A max，实际 ≤3A |
| **半桥驱动 / Half-bridge Driver** | **IR2104STRPBF** | 2 | 自举高侧驱动 / Bootstrap high-side driver |
| **H 桥 MOSFET** | **IRFZ44NS** | 4 | N-MOSFET，55V/49A，低 Rds(on) |
| **自举电容 / Bootstrap Cap** | 10µF–22µF 瓷片 | 2 | 为高侧 MOSFET 提供栅极驱动电压 |
| **自举二极管 / Bootstrap Diode** | 快恢复二极管 / Fast recovery | 2 | 自举电容充电通路 |

| 设计要点 / Design Point | 详情 / Detail |
|---|---|
| **死区时间 / Dead Time** | 软件控制，防止上下管直通 / Software-controlled to prevent shoot-through |
| **开关频率 / Switching Freq** | 建议 20kHz–50kHz PWM / Recommended 20kHz–50kHz PWM |
| **仿真验证 / Simulation** | 已通过 **LTspice** 仿真验证 H 桥开关波形及自举电路工作正常 |
| **电流采样 / Current Sense** | 可选：H 桥低端串联采样电阻 + 差分放大器 → ESP32 ADC |

> **LTspice 仿真已通过** — H 桥开关时序、自举电容充放电、死区保护逻辑均已仿真验证。
> *LTspice simulation passed — H-bridge switching timing, bootstrap capacitor charging, and dead-time protection have been verified.*

#### 2.2.8 PT100 过温断电电路 / PT100 Over-Temperature Shutdown

独立于 MCU 的硬件级过温保护，当 TEC 或 COB LED 区域温度异常升高时自动切断电源。

*Hardware-level over-temperature protection independent of the MCU — automatically cuts power if TEC or COB LED area temperature rises abnormally.*

| 组件 / Component | 型号 / Part | 数量 / Qty | 功能 / Function |
|---|---|---|---|
| **温度传感器 / Temp Sensor** | PT100 铂电阻 / Platinum RTD | 1 | 温度 → 电阻变化 / Temperature → resistance |
| **比较器 / Comparator** | **LM393DT** | 2 | 双路比较器，提供窗口比较 / Dual comparator for window comparison |
| **基准电压 / Reference** | 电阻分压或 TL431 | 1 | 设定过温阈值 / Sets over-temp threshold |

Q1/Q3 由 IR2104 #1 驱动 / driven by IR2104 #1
Q2/Q4 由 IR2104 #2 驱动 / driven by IR2104 #2

方向控制 / Direction Control:

Q1+Q4 导通 → TEC 正向电流 → 制冷模式 / Cooling
Q2+Q3 导通 → TEC 反向电流 → 加热模式 / Heating
PWM 占空比调节 → 功率控制 / Power modulation
PT100 过温断电电路原理 / Over-Temp Shutdown Principle:

PT100 ──► 惠斯通电桥 / Wheatstone Bridge ──► LM393DT #1 (上限比较 / Upper threshold)
──► LM393DT #2 (下限比较 / Lower threshold)
│
比较器输出 / Comparator Out ──► MOSFET 切断主电源
Cut main power via MOSFET


| 设计要点 / Design Point | 详情 / Detail |
|---|---|
| **阈值设定 / Threshold** | 通过分压电阻设定，如 80°C 触发断电 / Set via resistor divider, e.g., 80°C trip point |
| **滞回 / Hysteresis** | 正反馈电阻实现滞回，防止临界温度反复开关 / Positive feedback resistor prevents oscillation near threshold |
| **独立性 / Independence** | 纯硬件电路，ESP32 死机/复位不影响保护功能 / Pure hardware — protection works even if ESP32 crashes |
| **输出 / Output** | 比较器输出驱动 MOSFET 或继电器切断 24V 主干路 |

#### 2.2.9 MCU 及编程接口 / MCU & Programming Interface

| 参数 / Parameter | 规格 / Specification |
|---|---|
| **主控 / MCU** | **ESP32-WROOM-32E-N8**（8MB Flash，WiFi + BLE） |
| **USB 转串口 / USB-UART** | **CH340C** |
| **USB 接口 / USB Connector** | **USB-C**（支持烧录与串口通信 / programming & serial） |
| **自动下载电路 / Auto-Download** | EN + IO0 自动时序控制（DTR/RTS → 三极管/MOSFET） |
| **备用烧录 / Alt Programming** | 排母/排针引出 TX/RX/EN/IO0/GND，可外接 USB-UART 模块 |
| **按键 / Buttons** | RESET + BOOT（IO0）按键，支持手动进入下载模式 |
自动下载电路原理 / Auto-Download Circuit:

CH340C DTR ──► 电容耦合 ──► ESP32 EN
CH340C RTS ──► 电容耦合 ──► ESP32 IO0

当 esptool/platformio 发起下载时，DTR/RTS 自动产生复位 + 进入下载模式的时序。
When esptool/platformio initiates flashing, DTR/RTS automatically generate
the reset + bootloader entry timing sequence.

---

### 2.3 原理图 / Schematics

#### 原理图一：电源架构及外设模块 / Schematic 1: Power Architecture& Peripherals

> **涵盖内容 / Coverage**：24V DC 输入保护（防反接/ESD/浪涌）→ TPS56A37 (24V→12V) → TPS54302DDCR (24V→5V) → AMS1117-3V3 (5V→3.3V)，各电压轨去耦电容及测试点。DS18B20 ×2 (One Wire) + SHT30 (I²C) + 浮球开关 + SI2302 ×4 (蠕动泵/3路风扇) + XL6005E1 COB LED 驱动

> *24V DC input protection (reverse polarity / ESD / surge) → TPS56A37 (24V→12V) → TPS54302DDCR (24V→5V) → AMS1117-3V3 (5V→3.3V), decoupling capacitors and test points for each rail.DS18B20 ×2 (One Wire) + SHT30 (I²C) + float switch + SI2302 ×4 (pump / 3× fans) + XL6005E1 COB LED driver.*

<div align="center">

<img width="1416" height="1007" alt="image" src="https://github.com/user-attachments/assets/f83ef4be-57d3-412a-bd55-918e9a3b749a" />

*原理图一：电源架构及外设模块 / Schematic 1: Power Architecture& Peripherals*

</div>

---
<img width="1422" height="1010" alt="image" src="https://github.com/user-attachments/assets/aee2608c-d3d0-456d-920a-a9cf33104f50" />

#### 原理图二：主控及TEC H 桥及过温保护 / Schematic 2: MCU & TEC H-Bridge & Over-Temp Protection

> **涵盖内容 / Coverage**：ESP32-WROOM-32E-N8 + CH340C + USB-C + 自动下载电路 + 按键电路 + IR2104STRPBF ×2 半桥驱动 + IRFZ44NS ×4 H 桥 MOSFET + TEC 接口 + 自举电路 + PT100 惠斯通电桥 + LM393DT ×2 窗口比较器 + 过温断电输出。

> *ESP32-WROOM-32E-N8 + CH340C + USB-C + auto-download circuit + buttons + IR2104STRPBF ×2 half-bridge drivers + IRFZ44NS ×4 H-bridge MOSFETs + TEC connector + bootstrap circuits + PT100 Wheatstone bridge + LM393DT ×2 window comparator + over-temp shutdown output.*

<div align="center">



*原理图二：主控及TEC H 桥及过温保护 / Schematic 2: MCU & TEC H-Bridge & Over-Temp Protection*

</div>

---

#### 原理图三：接口 / Schematic 3: Connector

> **涵盖内容 / Coverage**：各级接口。

> *Various of Connectors*

<div align="center">

<img width="964" height="351" alt="image" src="https://github.com/user-attachments/assets/aeef42c2-2674-4f79-a45e-9256a0216248" />


*原理图三：接口 / Schematic 3: Connector*

</div>

---

### 2.4 第一版 → 第二版 设计演进对比 / v1.0 → v2.0 Design Evolution

以下对比表清晰展示了两个版本之间的设计思路变化：

*The following comparison clearly illustrates the design evolution between the two versions:*

| 维度 / Aspect | 第一版 v1.0 | 第二版 v2.0 | 变化说明 / Note |
|---|---|---|---|
| **输入电源 / Input** | 12V DC / 5A | **24V DC / 5A** | 提升电压减少电流损耗，适配 TEC 等大功率负载 |
| **电压轨 / Rails** | 12V → 5V（单级） | 24V → 12V → 5V → 3.3V（三级） | 分级供电，各电压轨独立稳压 |
| **24V→12V Buck** | — | **TPS56A37** | 新增，为 12V 设备供电 |
| **24V→5V Buck** | TPS54302DDCR (12V→5V) | **TPS54302DDCR** (24V→5V) | 同芯片，输入范围满足 24V |
| **5V→3.3V LDO** | — | **AMS1117-3V3** | 新增，ESP32 独立 3.3V 供电 |
| **输入保护 / Protection** | 基本 / Basic | **防反接 + ESD + 浪涌** | 全面提升可靠性 |
| **LED 照明 / Lighting** | 4× 3W 分立 LED + PT4115 | **1× 50W COB + XL6005E1** | 集成化、高光效、PWM 调光 |
| **TEC 温控 / TEC** | 无 / None | **TEC1-12706 + H 桥** | 核心新增功能，主动制冷/加热 |
| **H 桥驱动 / H-Bridge** | — | **IR2104STRPBF ×2 + IRFZ44NS ×4** | LTspice 仿真验证 |
| **过温保护 / OT Protection** | 无 / None | **PT100 + LM393DT ×2** | 硬件级独立保护 |
| **温度传感器 / Temp Sensor** | — | **DS18B20 ×2** (One Wire 共享 GPIO) | TEC 冷端 & 热端双点监测 |
| **温湿度传感器 / RH Sensor** | — | **SHT30** (I²C) | 环境温湿度监测 |
| **液位检测 / Water Level** | — | **浮球开关** | 水箱液位，防泵空转 |
| **水泵 / Pump** | 普通水泵（1 路可用） | **蠕动泵** (SI2302 驱动) | 更精确的流量控制 |
| **风扇控制 / Fan** | 通风风扇 | **3 路** (TEC冷端+通风+水冷) | 全部 12V / SI2302 NMOS |
| **MCU / 主控** | ESP32 | **ESP32-WROOM-32E-N8** | 8MB Flash，性能更稳定 |
| **USB 接口 / USB** | Micro-USB | **USB-C + CH340C** | 正反插，更可靠 |
| **自动下载 / Auto-DL** | 基础 / Basic | **DTR/RTS 自动下载电路** | 一键烧录 |
| **烧录方式 / Programming** | USB | **USB-C + 排母/排针** | 双通道烧录 |
| **PCB 层数 / PCB Layers** | 2 层 | **规划 4 层** | 改善信号完整性和 EMC |
| **软件适配 / Software** | 4 通道架构 | 需适配新硬件驱动 | TEC PID、多传感器融合等 |

---

### 2.5 软件功能规划 / Software Features Plan

第二版软件将在第一版基础上进行以下升级（具体实现待 PCB 完成后启动）：

*The v2.0 firmware will upgrade upon v1.0 with the following features (implementation begins after PCB completion):*

| 模块 / Module | 第一版 v1.0 | 第二版 v2.0 规划 / Plan |
|---|---|---|
| **TEC 控制 / TEC Control** | — | 双闭环 PID（温度环 + 电流环），冷/热端温差监控 |
| **COB 调光 / COB Dimming** | 4ch PWM 渐亮/渐暗 | 单通道高精度 PWM，模拟日出日落 |
| **传感器融合 / Sensor Fusion** | 土壤湿度 | DS18B20×2 + SHT30 + PT100 + 浮球开关 |
| **风扇策略 / Fan Strategy** | 定时 5min/55min | 温控自适应调速（TEC 冷端联动 + 环境通风联动） |
| **蠕动泵 / Pump** | 定时浇水 | 基于浮球开关 + 周计划精确浇水 |
| **云端通信 / Cloud** | WiFi + MQTT over TLS | 保持不变，扩展上报数据点 |
| **安全保护 / Safety** | — | PT100 过温断电 + 浮球低液位 + 看门狗 |
| **串口屏 / Touch UI** | 未开发 | 暂不开发，先定硬件再实现 |

---

### 2.6 当前进展 / Current Status

| 阶段 / Phase | 状态 / Status | 说明 / Note |
|---|---|---|
| **原理图绘制 / Schematic** |  已完成 | 三张原理图，涵盖电源/主控外设/TEC驱动 |
| **焊盘及封装 / Footprint & Library** | 已完成 | Cadence PCB 封装库建立完毕 |
| **LTspice 仿真 / Simulation** |  已通过 | TEC H 桥开关时序及自举电路仿真验证 |
| **PCB 布局布线 / PCB Layout** |  待进行 | 规划 4 层板设计 |
| **PCB 打样 / Fabrication** |  待进行 | — |
| **硬件调试 / Hardware Debug** |  待进行 | — |
| **软件适配 / Firmware** |  待进行 | 基于第一版代码重构，适配新硬件 |

---

## 项目持续开发中 / Project Under Active Development

Plant Block 项目将持续迭代升级，致力于打造更智能、更可靠的植物自动养护系统。欢迎关注项目进展并提出宝贵建议！

*The Plant Block project will continue to iterate and upgrade, striving to build a smarter and more reliable automatic plant care system. Follow the project and share your suggestions!*

> **项目状态 / Project Status**：
> - **v1.0**：硬件打样完成，核心软件功能开发完毕，待测试 / Hardware fabricated, core firmware complete, awaiting testing
> - **v2.0**：原理图及封装设计完成，待 PCB 布局布线 / Schematic & footprint design complete, PCB layout pending

---

## 开发环境 / Development Environment

| 类型 / Type | 工具 / Tool |
|---|---|
| **MCU 固件 / Firmware** | ESP-IDF / PlatformIO |
| **硬件设计 / Hardware** | Cadence (OrCAD / Allegro) |
| **电路仿真 / Simulation** | LTspice |
| **版本管理 / VCS** | Git + GitHub |
| **调试工具 / Debug** | 串口终端 (PuTTY / minicom) + MQTT 客户端 |

---

## 许可证 / License

MIT License

---

*README last updated: 2026年 / 保持更新*
