/** @file app_state.h @brief 应用系统模式和功能使能状态管理。 */
#ifndef APP_STATE_H
#define APP_STATE_H

#include "FreeRTOS.h"
#include "event_groups.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    APP_MODE_NORMAL = 0,
    APP_MODE_LOW_POWER,
    APP_MODE_SLEEP,
    APP_MODE_OTA_UPDATE
} app_mode_t;

typedef enum
{
    APP_FEATURE_DISPLAY = (1UL << 0),
    APP_FEATURE_TOUCH   = (1UL << 1),
    APP_FEATURE_CAPTURE = (1UL << 2),
    APP_FEATURE_LOG     = (1UL << 3)
} app_feature_t;

#define APP_STATE_EVENT_MODE_CHANGED     (1UL << 16)
#define APP_STATE_EVENT_FEATURE_CHANGED  (1UL << 17)

bool app_state_init(void);
app_mode_t app_state_get_mode(void);
void app_state_set_mode(app_mode_t mode);
bool app_state_feature_is_enabled(app_feature_t feature);
void app_state_set_feature_enabled(app_feature_t feature, bool enabled);
EventGroupHandle_t app_state_get_event_group(void);

#endif /* APP_STATE_H */
