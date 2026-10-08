/**
 * @file platform_runtime.c
 * @brief 通过 FreeRTOS 软件定时器维护平台 DWT 64 位时间。
 */

#include "platform_runtime.h"
#include "platform_dwt.h"
#include "FreeRTOS.h"
#include "timers.h"

#define PLATFORM_DWT_MAINTAIN_PERIOD_MS 1000U

static TimerHandle_t s_dwt_maintain_timer;
static bool s_dwt_maintain_started;

/**
 * @brief 在 Timer Service 任务中更新 64 位 DWT 累计值。
 * @param timer 当前触发的软件定时器句柄；回调不需要读取它。
 */
static void platform_dwt_maintain_callback(TimerHandle_t timer)
{
    (void)timer;
    platform_dwt_maintain();
}

/**
 * @brief 装配平台 DWT 周期维护定时器。
 * @return true 表示定时器启动命令已入队；false 表示时基或 FreeRTOS 对象不可用。
 * @note 定时器以 1 s 为周期，短于 480 MHz 下 DWT 32 位计数器约 8.95 s 的回绕周期。
 */
bool Platform_Runtime_Init(void)
{
    TickType_t period = pdMS_TO_TICKS(PLATFORM_DWT_MAINTAIN_PERIOD_MS);

    if (s_dwt_maintain_started) {
        return true;
    }
    if (!platform_dwt_is_enabled() || period == 0U ||
        platform_dwt_max_maintain_interval_us() <=
            (PLATFORM_DWT_MAINTAIN_PERIOD_MS * 1000U)) {
        return false;
    }

    if (s_dwt_maintain_timer == NULL) {
        s_dwt_maintain_timer = xTimerCreate("PlatformTick",
                                            period,
                                            pdTRUE,
                                            NULL,
                                            platform_dwt_maintain_callback);
        if (s_dwt_maintain_timer == NULL) {
            return false;
        }
    }

    if (xTimerStart(s_dwt_maintain_timer, 0U) != pdPASS) {
        return false;
    }

    s_dwt_maintain_started = true;
    return true;
}
