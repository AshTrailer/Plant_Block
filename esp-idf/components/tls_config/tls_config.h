#ifndef TLS_CONFIG_H
#define TLS_CONFIG_H

// MQTT Broker 地址
#define MQTT_BROKER_URI  "mqtts://aelecti.top:8883"

// 设备 ID
#define DEVICE_ID        "Plant_Block_dev03"

// 服务器 TLS 证书（定义在 tls_config.c）
extern const char *server_cert;

#endif