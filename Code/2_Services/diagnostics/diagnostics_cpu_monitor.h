/** @file diagnostics_cpu_monitor.h @brief FreeRTOS 任务 CPU 占用率采样与日志输出。 */
#ifndef TASK_CPU_MONITOR_H
#define TASK_CPU_MONITOR_H

#include <stdbool.h>

#define TASK_CPU_MONITOR_TASK_STACK    512U
#define TASK_CPU_MONITOR_TASK_PRIO     1U

/** @brief 创建低优先级任务 CPU 采样任务。 */
bool task_cpu_monitor_create(void);

#endif /* TASK_CPU_MONITOR_H */
