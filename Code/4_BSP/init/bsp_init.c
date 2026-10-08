/** @file bsp_init.c @brief 片上外设和板级调试接口初始化。 */

#include "bsp_init.h"

#include "bsp_dma2d.h"
#include "platform_config.h"
#if PLATFORM_LA_CAPTURE_ENABLE
#include "dma.h"
#include "la/la_ic.h"
#endif
#include "gpio.h"
#include "tim.h"
#include "uart.h"

/**
 * @brief BSP_Init：bsp init。
 */
bool BSP_Init(void)
{
    MX_GPIO_Init();
#if PLATFORM_LA_CAPTURE_ENABLE
    MX_DMA_Init();
#endif
    MX_TIM7_Init();
#if PLATFORM_LA_CAPTURE_ENABLE
    BSP_LA_IC_Init();
#endif
    if(!bsp_dma2d_init()) {
        return false;
    }
    return uart_init();
}

/**
 * @brief 启动供 LVGL 使用的 TIM7 周期中断。
 * @return true 表示启动成功；false 表示 HAL 报错。
 */
bool BSP_Lvgl_Tick_Start(void)
{
    return HAL_TIM_Base_Start_IT(&htim7) == HAL_OK;
}
