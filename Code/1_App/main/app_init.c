/** @file app_init.c @brief 应用状态初始化与 GUI 总体流程任务。 */

#include "app_init.h"

/* 字库数据仅在本翻译单元实例化一次。 */
#define APP_FONT_IMPLEMENTATION
#include "resource_font.h"

#include "app_state.h"
#include "layer1_frame_test.h"
#include "FreeRTOS.h"
#include "log_service.h"
#include "main.h"
#include "bsp_init.h"
#include "lvgl_display_port.h"
#include "lvgl_input_port.h"
#include "lvgl.h"
#include "platform_dwt.h"
#include "layer0_right_panel.h"
#include "task.h"
#include "layer0_wave_gesture.h"
#include "touch_service.h"
#include "layer1_wave.h"

#define APP_GUI_TASK_STACK_DEPTH    2048U
#define APP_GUI_TASK_PRIORITY       2U
#define APP_GUI_MAX_WAIT_MS         10U

static void App_GUI_Task(void *parameter);

/**
 * @brief App_Init：应用状态初始化和 GUI 任务装配。
 */
bool App_Init(void)
{
    TaskHandle_t gui_task = NULL;

    if (!app_state_init()) {
        return false;
    }
    if (xTaskCreate(App_GUI_Task,
                    "lvgl",
                    APP_GUI_TASK_STACK_DEPTH,
                    NULL,
                    APP_GUI_TASK_PRIORITY,
                    &gui_task) != pdPASS) {
        return false;
    }

    touch_service_bind_gui_task(gui_task);
    return true;
}

/**
 * @brief App_GUI_Task：应用状态初始化和 GUI 任务装配。
 */
static void App_GUI_Task(void *parameter)
{
    uint32_t delay_ms;

    (void)parameter;
    lv_init();
    lv_port_disp_init();
    if (lv_display_get_default() == NULL) {
        Error_Handler();
    }

    lv_port_indev_init();
    if (!BSP_Lvgl_Tick_Start()) {
        Error_Handler();
    }

    if (!wave_init(lv_screen_active())) {
        Error_Handler();
    }
    side_panel_create(lv_screen_active());
    if (!wave_apply_configuration()) {
        Error_Handler();
    }
    wave_display_task_start();

    lv_port_indev_set_gesture_callback(touch_gesture_handle_wave, NULL);
    (void)log_printf("lvgl task started, heap free=%lu bytes\r\n",
                     (unsigned long)xPortGetFreeHeapSize());

    for (;;) {
        uint32_t cycle_start;
        uint32_t flush_count_before;

        wave_process_gui_updates();
        (void)side_panel_capture_process();

        cycle_start = platform_dwt_cycles();
        flush_count_before = frame_test_get_flush_count();
        delay_ms = lv_timer_handler();
        frame_test_record_lvgl_handler(platform_dwt_elapsed(cycle_start),
                                       frame_test_get_flush_count() != flush_count_before);

        if (delay_ms < 1U) {
            delay_ms = 1U;
        } else if (delay_ms > APP_GUI_MAX_WAIT_MS) {
            delay_ms = APP_GUI_MAX_WAIT_MS;
        }

        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
    }
}
