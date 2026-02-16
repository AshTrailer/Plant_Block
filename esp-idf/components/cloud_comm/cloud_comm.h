#ifndef CLOUD_COMM_H
#define CLOUD_COMM_H

#include <stdbool.h>

/**
 * @brief 初始化云端通信模块（仅初始化内部状态，不启动连接）
 */
void cloud_comm_init(void);

/**
 * @brief 启动云端通信（连接 WiFi、同步时间、连接 MQTT）
 */
void cloud_comm_start(void);

/**
 * @brief 检查 MQTT 是否已连接
 * @return true 已连接，false 未连接
 */
bool cloud_comm_is_mqtt_connected(void);

/**
 * @brief 发布消息到指定子主题
 * @param sub_topic 子主题，例如 "data" 将发布到 device/{device_id}/data
 * @param data      要发送的字符串数据
 */
void cloud_comm_publish(const char *sub_topic, const char *data);

/**
 * @brief 注册消息接收回调（当收到订阅的指令时调用）
 * @param callback 回调函数，参数为 (topic, topic_len, data, data_len)
 */
typedef void (*cloud_comm_msg_cb_t)(const char *topic, int topic_len, const char *data, int data_len);
void cloud_comm_register_msg_cb(cloud_comm_msg_cb_t callback);

#endif // CLOUD_COMM_H