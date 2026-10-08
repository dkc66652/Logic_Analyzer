/** @file app_state.c @brief 应用状态集中存储与变更通知。 */

#include "app_state.h"

#include "task.h"

static EventGroupHandle_t s_state_events;
static volatile app_mode_t s_mode;

/**
 * @brief app_state_init：应用状态读写。
 */
bool app_state_init(void)
{
    const EventBits_t default_features = APP_FEATURE_DISPLAY |
                                         APP_FEATURE_TOUCH |
                                         APP_FEATURE_CAPTURE |
                                         APP_FEATURE_LOG;

    s_state_events = xEventGroupCreate();
    if (s_state_events == NULL) {
        return false;
    }

    s_mode = APP_MODE_NORMAL;
    (void)xEventGroupSetBits(s_state_events, default_features);
    return true;
}

/**
 * @brief app_state_get_mode：应用状态读写。
 */
app_mode_t app_state_get_mode(void)
{
    return s_mode;
}

/**
 * @brief app_state_set_mode：应用状态读写。
 */
void app_state_set_mode(app_mode_t mode)
{
    if (mode > APP_MODE_OTA_UPDATE) {
        return;
    }

    taskENTER_CRITICAL();
    s_mode = mode;
    taskEXIT_CRITICAL();
    if (s_state_events != NULL) {
        (void)xEventGroupSetBits(s_state_events, APP_STATE_EVENT_MODE_CHANGED);
    }
}

/**
 * @brief app_state_feature_is_enabled：应用状态读写。
 */
bool app_state_feature_is_enabled(app_feature_t feature)
{
    if (s_state_events == NULL) {
        return false;
    }
    return (xEventGroupGetBits(s_state_events) & (EventBits_t)feature) != 0U;
}

/**
 * @brief app_state_set_feature_enabled：应用状态读写。
 */
void app_state_set_feature_enabled(app_feature_t feature, bool enabled)
{
    if (s_state_events == NULL) {
        return;
    }

    if (enabled) {
        (void)xEventGroupSetBits(s_state_events,
                                 (EventBits_t)feature |
                                 APP_STATE_EVENT_FEATURE_CHANGED);
    } else {
        (void)xEventGroupClearBits(s_state_events, (EventBits_t)feature);
        (void)xEventGroupSetBits(s_state_events,
                                 APP_STATE_EVENT_FEATURE_CHANGED);
    }
}

/**
 * @brief app_state_get_event_group：应用状态读写。
 */
EventGroupHandle_t app_state_get_event_group(void)
{
    return s_state_events;
}
