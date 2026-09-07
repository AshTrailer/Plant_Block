// ===== system_monitor.h =====
#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H
#include <stdint.h>
#include <stdbool.h>

void system_monitor_init(uint32_t interval_ms, uint32_t stack_low_water_threshold);
void system_monitor_register_task(const char *name, void *task_handle);
void system_monitor_feed_watchdog(void);
uint32_t system_monitor_get_free_heap(void);
uint32_t system_monitor_get_min_free_heap(void);
#endif // SYSTEM_MONITOR_H