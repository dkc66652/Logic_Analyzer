/** @file services_init.c @brief LA采集、日志、触摸与波形显示服务初始化。 */

#include "services_init.h"

#include "log_service.h"
#include "touch_service.h"
#include "layer1_wave.h"
#include "layer1_frame_test.h"
#include "platform_config.h"
#if PLATFORM_LA_CAPTURE_ENABLE
#include "fmc/fmc_sdram.h"
#include "la/la_service.h"
#endif

/**
 * @brief Services_Init：服务模块初始化编排。
 */
bool Services_Init(void)
{
#if PLATFORM_LA_CAPTURE_ENABLE
    if (BSP_FMC_SDRAM_Init() != BSP_FMC_SDRAM_OK) {
        return false;
    }
    if (LA_Service_Init() != 0U) {
        return false;
    }
#endif
    if (!log_task_creat()) {
        return false;
    }
    if (!touch_service_init()) {
        return false;
    }
    if (!wave_display_task_create()) {
        return false;
    }
#if FRAME_TEST_DIAGNOSTIC_ENABLE
    if (!frame_test_task_create()) {
        return false;
    }
#endif

    return true;
}
