/** @file touch_service.c @brief 独立触摸采样任务实现。 */

#include "touch_service.h"

#include "queue.h"

#include <string.h>

#define TOUCH_SERVICE_TASK_STACK     512U
#define TOUCH_SERVICE_TASK_PRIO      3U
#define TOUCH_SERVICE_PERIOD         pdMS_TO_TICKS(5U)

static QueueHandle_t s_sample_queue;
static TaskHandle_t s_gui_task;
static bool s_ready;

static void touch_service_task(void *parameter);
static bool touch_service_sample_changed(const touch_service_sample_t *first,
                                         const touch_service_sample_t *second);

/**
 * @brief touch_service_init：触摸数据采样和发布。
 */
bool touch_service_init(void)
{
    touch_service_sample_t initial_sample = {0};

    s_sample_queue = xQueueCreate(1U, sizeof(touch_service_sample_t));
    if (s_sample_queue == NULL) {
        return false;
    }
    (void)xQueueOverwrite(s_sample_queue, &initial_sample);

    if (!touch_is_ready()) {
        s_ready = false;
        return true;
    }

    s_ready = xTaskCreate(touch_service_task,
                          "touch",
                          TOUCH_SERVICE_TASK_STACK,
                          NULL,
                          TOUCH_SERVICE_TASK_PRIO,
                          NULL) == pdPASS;
    return s_ready;
}

/**
 * @brief touch_service_bind_gui_task：触摸数据采样和发布。
 */
void touch_service_bind_gui_task(TaskHandle_t gui_task)
{
    s_gui_task = gui_task;
}

/**
 * @brief touch_service_get_latest：触摸数据采样和发布。
 */
bool touch_service_get_latest(touch_service_sample_t *sample)
{
    if ((sample == NULL) || (s_sample_queue == NULL)) {
        return false;
    }
    return xQueuePeek(s_sample_queue, sample, 0U) == pdPASS;
}

/**
 * @brief touch_service_is_ready：触摸数据采样和发布。
 */
bool touch_service_is_ready(void)
{
    return s_ready;
}

/**
 * @brief touch_service_task：触摸数据采样和发布。
 */
static void touch_service_task(void *parameter)
{
    touch_service_sample_t previous = {0};
    touch_service_sample_t current = {0};
    TickType_t last_wake_time = xTaskGetTickCount();

    (void)parameter;
    for (;;) {
        current.count = touch_read_points(current.points, TOUCH_MAX_POINTS);
        if (touch_service_sample_changed(&current, &previous)) {
            current.sequence++;
            (void)xQueueOverwrite(s_sample_queue, &current);
            previous = current;
            if (s_gui_task != NULL) {
                xTaskNotifyGive(s_gui_task);
            }
        }
        xTaskDelayUntil(&last_wake_time, TOUCH_SERVICE_PERIOD);
    }
}

/**
 * @brief touch_service_sample_changed：触摸数据采样和发布。
 */
static bool touch_service_sample_changed(const touch_service_sample_t *first,
                                         const touch_service_sample_t *second)
{
    if (first->count != second->count) {
        return true;
    }
    return memcmp(first->points, second->points,
                  (size_t)first->count * sizeof(first->points[0])) != 0;
}
