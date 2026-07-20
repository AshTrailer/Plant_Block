#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---------- 事件类型枚举 ----------
typedef enum {
   EVENT_SENSOR_SHT30 = 0,      // 数据: {float temp, float hum}
   EVENT_SENSOR_DS18B20,        // 数据: {int index, float temp}
   EVENT_SENSOR_MOISTURE,       // 数据: {float hum_pct, uint32_t volt_mv}
   EVENT_SENSOR_FLOAT_SW,       // 数据: {bool has_water}  true=有水
   EVENT_SENSOR_NTC,            // 数据: {bool overtemp}
   EVENT_LIGHT_STATE,           // 数据: {bool on, uint8_t duty_pct}
   EVENT_PUMP_STATE,            // 数据: {bool on, uint8_t speed_pct}
   EVENT_FAN_STATE,             // 数据: {int fan_id, bool on, uint8_t duty_pct}
   EVENT_VENT_STATE,            // 数据: {bool on}
   EVENT_ALARM,                 // 数据: {int code, const char *msg}
   EVENT_SYSTEM_HEALTH,         // 数据: {uint32_t free_heap, uint32_t min_stack}
   EVENT_COUNT                  // 事件类型总数
} event_type_t;

// ---------- 回调原型 ----------
typedef void (*event_callback_t)(event_type_t type, const void *data, size_t len, void *user_ctx);

// ---------- API ----------
void event_bus_init(void);
bool event_bus_subscribe(event_type_t type, event_callback_t cb, void *user_ctx);
bool event_bus_unsubscribe(event_type_t type, event_callback_t cb);
void event_bus_publish(event_type_t type, const void *data, size_t len);

#endif // EVENT_BUS_H