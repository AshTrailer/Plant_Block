#ifndef MOISTURE_SENSOR_H
#define MOISTURE_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 初始化土壤湿度传感器模块
 * @param power_pin  传感器电源控制引脚（GPIO15）
 * @param adc_pin    模拟信号输入引脚（GPIO18）
 */
void moisture_sensor_init(int power_pin, int adc_pin);

/**
 * @brief 打开传感器电源（GPIO输出高电平）
 */
void moisture_sensor_power_on(void);

/**
 * @brief 关闭传感器电源（GPIO输出低电平），并停止任何正在进行的采集任务
 */
void moisture_sensor_power_off(void);

/**
 * @brief 获取传感器电源状态
 * @return true - 已上电，false - 未上电
 */
bool moisture_sensor_is_powered(void);

/**
 * @brief 启动连续采集任务
 * 
 * 传感器必须已上电，否则不会启动。
 * 启动后延迟2秒，然后以1秒间隔连续读取ADC值并打印电压。
 * 若任务已运行，重复调用不会创建新任务。
 */
void moisture_sensor_start_reading(void);

/**
 * @brief 手动停止连续采集任务（电源关闭时自动停止）
 */
void moisture_sensor_stop_reading(void);

#endif