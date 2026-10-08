/**
 * @file main.c
 * @brief 唯一程序入口；按模块前置条件装配并启动调度器。
 */

#include "main.h"
#include "app_init.h"
#include "bsp_init.h"
#include "devices_init.h"
#include "FreeRTOS.h"
#include "platform_init.h"
#include "platform_runtime.h"
#include "services_init.h"
#include "task.h"

/**
 * @brief main：系统入口和模块初始化编排。
 */
int main(void)
{
    if (!Platform_Init() ||
        !BSP_Init() ||
        !Devices_Init() ||
        !Platform_Runtime_Init() ||
        !Services_Init() ||
        !App_Init()) {
        Error_Handler();
    }

    vTaskStartScheduler();
    Error_Handler();
}
