/** @file touch_service.h @brief 触摸采样任务与 GUI 输入快照。 */
#ifndef TOUCH_SERVICE_H
#define TOUCH_SERVICE_H

#include "FreeRTOS.h"
#include "task.h"
#include "touch.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    touch_point_t points[TOUCH_MAX_POINTS];
    uint8_t count;
    uint32_t sequence;
} touch_service_sample_t;

bool touch_service_init(void);
void touch_service_bind_gui_task(TaskHandle_t gui_task);
bool touch_service_get_latest(touch_service_sample_t *sample);
bool touch_service_is_ready(void);

#endif /* TOUCH_SERVICE_H */
