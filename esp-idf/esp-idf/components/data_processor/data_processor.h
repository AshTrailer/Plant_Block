#ifndef DATA_PROCESSOR_H
#define DATA_PROCESSOR_H

#include <stdbool.h>

/**
 * @brief 计算整数数组的平均值
 */
float data_processor_mean(const int *data, int count);

/**
 * @brief 计算整数数组的样本标准差
 */
float data_processor_stddev(const int *data, int count);

/**
 * @brief 舍弃最大最小值，返回有效数据
 * @param input 输入数组
 * @param count 输入个数
 * @param output 输出数组（需预先分配至少 count 空间）
 * @param min_samples 最小有效样本数，若舍弃后少于该值，则返回0
 * @return 有效数据个数，若为0表示失败
 */
int data_processor_remove_outliers(const int *input, int count, int *output, int min_samples);

/**
 * @brief 检查数据稳定性
 * @param data 输入数据
 * @param count 数据个数
 * @param max_stddev_percent 最大允许标准差百分比（如0.5表示0.5%）
 * @param max_retry 最大重试次数
 * @param final_data 输出最终有效数据（需预先分配空间）
 * @param final_count 输出最终数据个数
 * @return true 表示稳定，false 表示不稳定
 */
bool data_processor_check_stability(const int *data, int count, float max_stddev_percent, int max_retry, int *final_data, int *final_count);

/**
 * @brief 线性拟合（两点确定直线）
 * @param x1, y1 第一个点
 * @param x2, y2 第二个点
 * @param k 输出斜率
 * @param b 输出截距
 */
void data_processor_linear_fit(float x1, float y1, float x2, float y2, float *k, float *b);

/**
 * @brief 应用线性校准
 * @param raw 原始值
 * @param k 斜率
 * @param b 截距
 * @return 校准后值
 */
float data_processor_apply_linear(int raw, float k, float b);

#endif