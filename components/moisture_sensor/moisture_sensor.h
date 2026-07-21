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
 * 启动后延迟2秒，然后以1秒间隔连续读取电压并打印（校准后电压和湿度百分比）。
 * 若任务已运行，重复调用不会创建新任务。
 */
void moisture_sensor_start_reading(void);

/**
 * @brief 手动停止连续采集任务（电源关闭时自动停止）
 */
void moisture_sensor_stop_reading(void);

/**
 * @brief 执行干燥校准（直接执行，无交互确认）
 * @return true 成功，false 失败
 */
bool moisture_sensor_cal_dry(void);

/**
 * @brief 执行湿润校准（直接执行，无交互确认）
 * @return true 成功，false 失败
 */
bool moisture_sensor_cal_wet(void);

/**
 * @brief 获取干燥校准状态
 */
bool moisture_sensor_is_dry_calibrated(void);

/**
 * @brief 获取湿润校准状态
 */
bool moisture_sensor_is_wet_calibrated(void);

/**
 * @brief 获取线性拟合系数
 * @param k 输出斜率
 * @param b 输出截距
 * @return true 如果双校准已完成
 */
bool moisture_sensor_get_calibration(float *k, float *b);

/**
 * @brief 获取当前校准后的电压（仅用于连续采集内部调用）
 * @return 校准后电压（mV）
 */
uint32_t moisture_sensor_get_calibrated_voltage(void);

/**
 * @brief 获取当前湿度百分比（0~100%），基于干燥/湿润校准目标值
 * @return 湿度百分比（干燥=100%，湿润=0%）
 */
float moisture_sensor_get_humidity_percent(void);

/**
 * @brief 单次稳定湿度读取（自动上电→稳定→采样→断电）
 *
 * 流程：上电 → 等待 5s → 每秒采 1 个样本共 30 个 → 去极值 →
 *       移动平均 → 标准差校验 → 返回结果
 *
 * @param humidity_percent 输出湿度百分比（0-100%）
 * @return true 数据稳定有效，false 采样失败
 */
bool moisture_sensor_read_stable(float *humidity_percent);

#endif