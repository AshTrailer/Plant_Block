#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

// ---------- 初始化并启动监控任务 ----------
// interval_ms: 监控周期（毫秒），建议 10000（10 秒）
// stack_low_water_threshold: 任务堆栈低水位阈值（字），低于此值触发报警
void system_monitor_init(uint32_t interval_ms, uint32_t stack_low_water_threshold);

// ---------- 注册需要监控堆栈的任务 ----------
// 任务创建后调用此函数注册
void system_monitor_register_task(const char *name, void *task_handle);

// ---------- 手动喂看门狗（主循环中调用）----------
void system_monitor_feed_watchdog(void);

// ---------- 查询 ----------
uint32_t system_monitor_get_free_heap(void);
uint32_t system_monitor_get_min_free_heap(void);

#endif // SYSTEM_MONITOR_H