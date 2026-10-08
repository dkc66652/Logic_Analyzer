/**
 * @file    diagnostics_cpu_monitor.c
 * @brief   使用 FreeRTOS 运行时统计数据输出各任务的区间 CPU 占用率。
 */

#include "diagnostics_cpu_monitor.h"

#include "FreeRTOS.h"
#include "task.h"
#include "log_service.h"

#include <string.h>

#define TASK_CPU_MONITOR_MAX_TASKS       12U
#define TASK_CPU_MONITOR_PERIOD_TICKS    pdMS_TO_TICKS(5000U)

typedef struct
{
    TaskHandle_t handle;
    configRUN_TIME_COUNTER_TYPE runtime;
} task_cpu_monitor_snapshot_t;

static TaskStatus_t s_current[TASK_CPU_MONITOR_MAX_TASKS];
static task_cpu_monitor_snapshot_t s_previous[TASK_CPU_MONITOR_MAX_TASKS];
static UBaseType_t s_previous_count;
static configRUN_TIME_COUNTER_TYPE s_previous_total;
static bool s_has_previous;

static void task_cpu_monitor_task(void *parameter);
static bool task_cpu_monitor_previous_runtime(TaskHandle_t handle,
                                              configRUN_TIME_COUNTER_TYPE *runtime);

/**
 * @brief task_cpu_monitor_create：CPU 占用率监测。
 */
bool task_cpu_monitor_create(void)
{
    return xTaskCreate(task_cpu_monitor_task,
                       "cpu_mon",
                       TASK_CPU_MONITOR_TASK_STACK,
                       NULL,
                       TASK_CPU_MONITOR_TASK_PRIO,
                       NULL) == pdPASS;
}

/**
 * @brief task_cpu_monitor_task：CPU 占用率监测。
 */
static void task_cpu_monitor_task(void *parameter)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    (void)parameter;
    for (;;) {
        UBaseType_t task_count;
        UBaseType_t index;
        configRUN_TIME_COUNTER_TYPE total_runtime;
        configRUN_TIME_COUNTER_TYPE total_delta;
        uint32_t idle_percent_x100 = 0U;
        bool idle_found = false;

        xTaskDelayUntil(&last_wake_time, TASK_CPU_MONITOR_PERIOD_TICKS);
        task_count = uxTaskGetNumberOfTasks();
        if (task_count > TASK_CPU_MONITOR_MAX_TASKS) {
            (void)log_printf("cpu monitor: task count=%lu exceeds limit=%u\r\n",
                             (unsigned long)task_count,
                             TASK_CPU_MONITOR_MAX_TASKS);
            continue;
        }

        task_count = uxTaskGetSystemState(s_current, task_count, &total_runtime);
        if (task_count == 0U) {
            continue;
        }

        if (!s_has_previous) {
            for (index = 0U; index < task_count; index++) {
                s_previous[index].handle = s_current[index].xHandle;
                s_previous[index].runtime = s_current[index].ulRunTimeCounter;
            }
            s_previous_count = task_count;
            s_previous_total = total_runtime;
            s_has_previous = true;
            continue;
        }

        total_delta = total_runtime - s_previous_total;
        if (total_delta == 0U) {
            continue;
        }

        (void)log_printf("cpu usage, last 5 s:\r\n");
        for (index = 0U; index < task_count; index++) {
            configRUN_TIME_COUNTER_TYPE task_delta;
            configRUN_TIME_COUNTER_TYPE previous_runtime;
            uint32_t percent_x100;

            if (task_cpu_monitor_previous_runtime(s_current[index].xHandle,
                                                  &previous_runtime)) {
                task_delta = s_current[index].ulRunTimeCounter - previous_runtime;
            } else {
                /* 本采样周期中新建的任务，下一个周期再开始计算其占用率。 */
                task_delta = 0U;
            }
            percent_x100 = (uint32_t)((task_delta * 10000ULL) / total_delta);
            (void)log_printf("  %-15s %3lu.%02lu%%\r\n",
                             s_current[index].pcTaskName,
                             (unsigned long)(percent_x100 / 100U),
                             (unsigned long)(percent_x100 % 100U));

            if (strcmp(s_current[index].pcTaskName, "IDLE") == 0) {
                idle_percent_x100 = percent_x100;
                idle_found = true;
            }
        }

        if (idle_found) {
            uint32_t busy_percent_x100 = (idle_percent_x100 < 10000U) ?
                                        (10000U - idle_percent_x100) : 0U;
            (void)log_printf("  total busy       %3lu.%02lu%%\r\n",
                             (unsigned long)(busy_percent_x100 / 100U),
                             (unsigned long)(busy_percent_x100 % 100U));
        }

        for (index = 0U; index < task_count; index++) {
            s_previous[index].handle = s_current[index].xHandle;
            s_previous[index].runtime = s_current[index].ulRunTimeCounter;
        }
        s_previous_count = task_count;
        s_previous_total = total_runtime;
    }
}

/**
 * @brief task_cpu_monitor_previous_runtime：CPU 占用率监测。
 */
static bool task_cpu_monitor_previous_runtime(TaskHandle_t handle,
                                              configRUN_TIME_COUNTER_TYPE *runtime)
{
    UBaseType_t index;

    for (index = 0U; index < s_previous_count; index++) {
        if (s_previous[index].handle == handle) {
            *runtime = s_previous[index].runtime;
            return true;
        }
    }

    return false;
}
