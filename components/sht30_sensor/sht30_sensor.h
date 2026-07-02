#ifndef SHT30_SENSOR_H
#define SHT30_SENSOR_H

#include <stdbool.h>

/**
 * @brief SHT30 数据就绪回调
 * @param temperature 温度（℃）
 * @param humidity    相对湿度（%RH）
 * @param user_ctx    用户上下文指针
 */
typedef void (*sht30_data_cb_t)(float temperature, float humidity, void *user_ctx);

/**
 * @brief 初始化 SHT30 传感器模块
 * 
 * 安装 I2C 总线，创建 SHT3x 传感器实例，
 * 设为周期测量模式（1 次/秒，中等重复性）。
 * 
 * @param sda_pin I2C SDA 引脚
 * @param scl_pin I2C SCL 引脚
 */
void sht30_sensor_init(int sda_pin, int scl_pin);

/**
 * @brief 启动 1Hz 连续读取（完全非阻塞）
 * 
 * 内部使用 FreeRTOS 软件定时器 + 独立 worker 任务，
 * 以 1Hz 频率从传感器取回温湿度并输出日志 / 触发回调。
 */
void sht30_sensor_start(void);

/**
 * @brief 停止连续读取
 */
void sht30_sensor_stop(void);

/**
 * @brief 获取最近一次读取的温湿度缓存值（非阻塞，立即返回）
 * 
 * @param temperature 输出温度（℃）
 * @param humidity    输出湿度（%RH）
 * @return true 数据有效，false 尚未有数据
 */
bool sht30_sensor_get_data(float *temperature, float *humidity);

/**
 * @brief 注册数据回调（每次读取完成后调用）
 * 
 * @param callback 回调函数，可为 NULL 取消
 * @param user_ctx 用户上下文，透传给回调
 */
void sht30_sensor_set_callback(sht30_data_cb_t callback, void *user_ctx);

#endif