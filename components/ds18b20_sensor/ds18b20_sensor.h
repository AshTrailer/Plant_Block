#ifndef DS18B20_SENSOR_H
#define DS18B20_SENSOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief 温度读取完成回调
 * @param index       设备索引
 * @param temperature 温度值（℃）
 * @param user_ctx    用户上下文指针
 */
typedef void (*ds18b20_temp_cb_t)(int index, float temperature, void *user_ctx);

/**
 * @brief 初始化 DS18B20 传感器模块
 * 
 * 安装 1-Wire 总线（RMT 后端），搜索总线上的 DS18B20 设备，
 * 并为每个设备设置 11 位分辨率。
 * 
 * @param gpio_num    1-Wire 总线 GPIO 引脚编号（如 27）
 * @param max_devices 预期最大设备数量
 */
void ds18b20_sensor_init(int gpio_num, int max_devices);

/**
 * @brief 获取搜索到的 DS18B20 设备数量
 * @return 设备数量（0 表示未找到任何设备）
 */
int ds18b20_sensor_get_device_count(void);

/**
 * @brief 获取指定索引设备的温度（直接读取暂存器，不触发转换）
 * 
 * 注意：调用前需确保已触发转换并等待了足够的转换时间（11-bit: 375ms）。
 * 
 * @param index       设备索引（0 ~ device_count-1）
 * @param temperature 输出温度值（℃）
 * @return true 成功，false 失败
 */
bool ds18b20_sensor_get_temperature(int index, float *temperature);

/**
 * @brief 获取所有设备的温度（一次触发，等待转换完成后逐个读取）
 * 
 * 阻塞版本：内部使用 vTaskDelay 等待转换完成。
 * 仅在可接受阻塞的任务中调用（如初始化阶段测试），
 * 正常运行中请使用 ds18b20_sensor_start_reading 或异步版本。
 * 
 * @param temperatures 温度数组，长度至少为 device_count
 * @return true 全部读取成功，false 存在读取失败
 */
bool ds18b20_sensor_get_all_temperatures_blocking(float *temperatures);

/**
 * @brief 异步触发一次温度转换并读取（非阻塞）
 * 
 * 流程：
 *   1. 立即触发总线所有设备开始温度转换（耗时 < 1ms，立即返回）
 *   2. 启动 380ms 软件定时器
 *   3. 定时器到期后自动读取所有设备温度，通过回调通知
 * 
 * @param callback 温度就绪回调（每个设备调用一次），可为 NULL
 * @param user_ctx 用户上下文，透传给回调
 */
void ds18b20_sensor_read_once_async(ds18b20_temp_cb_t callback, void *user_ctx);

/**
 * @brief 手动触发一次温度转换（非阻塞，立即返回）
 * 
 * 通常在希望精确控制"转换→读取"时序时使用。
 * 调用后等待 380ms，然后调用 ds18b20_sensor_get_temperature 读取。
 */
void ds18b20_sensor_trigger_conversion(void);

/**
 * @brief 获取设备 ROM 地址字符串（16 字符十六进制）
 * @param index  设备索引
 * @param buffer 输出缓冲区
 * @param len    缓冲区长度（至少 17 字节）
 * @return true 成功
 */
bool ds18b20_sensor_get_device_address_str(int index, char *buffer, size_t len);

/**
 * @brief 启动连续温度采集（基于定时器，完全非阻塞）
 * 
 * 内部使用 FreeRTOS 软件定时器链：
 *   周期定时器(interval_ms) → 触发转换 → 单次定时器(380ms) → 读取并输出日志
 * 
 * 不会阻塞任何调用者任务，所有操作在定时器回调中完成。
 * 
 * @param interval_ms 采集间隔（毫秒），建议 >= 2000
 */
void ds18b20_sensor_start_reading(uint32_t interval_ms);

/**
 * @brief 停止连续温度采集
 */
void ds18b20_sensor_stop_reading(void);

/**
 * @brief 启动最快连续温度采集（worker 自循环，约 380ms/次）
 * 
 * 与 start_reading 不同，此函数不使用定时器，worker task 在每次
 * 读取完成后立即开始下一轮转换，达到硬件允许的最快采样率。
 * 
 * 调用 ds18b20_sensor_stop_reading() 停止。
 */
void ds18b20_sensor_start_continuous(void);

// 冷/热端语义映射（V2.0 专用）
// 约定：先搜索到的为冷端 [0]，后搜索到的为热端 [1]
// 如 ROM ID 顺序与预期不同，可调用以下函数重新指定
void ds18b20_set_role(int index, const char *role);  // "cold" / "hot"
const char *ds18b20_get_role(int index);

/**
 * @brief 获取缓存中的温度（线程安全，不操作总线）
 * 
 * 前提：连续采集（start_continuous 或 start_reading）已在运行。
 * worker 每次读取成功后更新内部缓存，此函数直接返回缓存值。
 * 
 * @param index       设备索引
 * @param temperature 输出温度值（℃）
 * @return true 缓存有效，false 尚未有数据或索引无效
 */
bool ds18b20_sensor_get_temperature_cached(int index, float *temperature);

#endif