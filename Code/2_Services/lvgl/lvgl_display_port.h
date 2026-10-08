/** @file lvgl_display_port.h @brief LVGL显示端口初始化和刷新开关接口。 */

/*Copy this file as "lvgl_display_port.h" and set this value to "1" to enable content*/
#if 1

#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/*==============================================================================
 * 公共接口
 *============================================================================*/
/**
 * @brief  创建LVGL显示对象，注册RGB565绘制缓冲区和LCD刷新回调。
 * @note   调用位置：Core/Src/main.c的lvgl_task()。
 * @note   调用顺序：SDRAM、LTDC、LCD和lv_init()完成后调用一次。
 * @usage  lv_init(); lv_port_disp_init();
 */
void lv_port_disp_init(void);

/**
 * @brief  允许LVGL刷新回调把像素写入LTDC帧缓冲。
 * @note   当前工程没有调用者；需要解除画面冻结时，可在LVGL任务中调用。
 * @usage  disp_enable_update();
 */
void disp_enable_update(void);

/**
 * @brief  暂停LVGL刷新回调向LTDC帧缓冲写入像素，但不停止LVGL内部绘制。
 * @note   当前工程没有调用者；需要临时冻结当前LCD画面时，可在LVGL任务中调用。
 * @usage  disp_disable_update();
 */
void disp_disable_update(void);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* LV_PORT_DISP_H */

#endif /*Disable/Enable content*/
