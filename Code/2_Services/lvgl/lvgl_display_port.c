/**
 ******************************************************************************
 * @file    lvgl_display_port.c
 * @brief   LVGL显示端口，将LVGL的RGB565局部绘制结果复制到LTDC帧缓冲。
 *
 * @note    SDRAM、LTDC和LCD设备由main()在LVGL启动前完成初始化。本文件只负责
 *          创建LVGL display、提供绘制缓冲区并注册刷新回调，不直接初始化硬件。
 ******************************************************************************
 */

/*Copy this file as "lvgl_display_port.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lvgl_display_port.h"
#include "lcd.h"
#include "layer1_frame_test.h"
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/
/** LVGL局部绘制缓冲区可同时容纳的屏幕行数；数值越大，占用的片内RAM越多。 */
#define LVGL_DRAW_BUF_LINES    40U

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
/** @brief 执行显示端口所需的底层准备；当前硬件已由main()初始化，因此为空。 */
static void disp_init(void);

/**
 * @brief  将LVGL已经绘制完成的一块RGB565像素数据写入LCD帧缓冲。
 * @param  disp LVGL调用该回调时传入的显示对象。
 * @param  area 本次需要刷新的矩形区域，右下角坐标包含在区域内。
 * @param  px_map LVGL绘制缓冲区首地址，像素按RGB565逐行连续排列。
 * @note   该函数由LVGL刷新流程调用，应用代码不应直接调用。
 */
static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map);

/**********************
 *  STATIC VARIABLES
 **********************/
LV_ATTRIBUTE_MEM_ALIGN 
static uint16_t s_draw_buf[LCD_FRAMEBUFFER_MAX_WIDTH * LVGL_DRAW_BUF_LINES]
    __attribute__((aligned( 4 )));

/** 为true时允许把LVGL绘制结果写入LTDC帧缓冲；为false时只完成LVGL握手。 */
static volatile bool s_flush_enabled = true;

/**********************
 *      MACROS
 **********************/

/*==============================================================================
 * 公共函数：供其他模块调用
 *============================================================================*/

/**
 * @brief  创建并配置LVGL显示对象，将LVGL输出连接到LCD设备层。
 *
 * @details 本函数依次完成：
 *          1. 确认LCD设备已经初始化；
 *          2. 从LCD设备层取得实际分辨率；
 *          3. 创建LVGL display并指定RGB565颜色格式；
 *          4. 注册disp_flush()刷新回调；
 *          5. 注册40行单缓冲并使用PARTIAL局部渲染模式。
 *
 * @note   由LVGL任务在lv_init()之后调用一次。函数返回后，LVGL内部刷新定时器
 *         会在lv_timer_handler()执行期间按需调用disp_flush()。
 */
void lv_port_disp_init(void)
{
    uint16_t hor_res;
    uint16_t ver_res;
    uint32_t draw_buf_size;

    /* SDRAM、LTDC和LCD设备已由main()在LVGL任务启动前初始化。 */
    disp_init();

    if(!lcd_is_ready()) {
        return;
    }

    hor_res = lcd_get_width();
    ver_res = lcd_get_height();
    if(hor_res == 0U || ver_res == 0U || hor_res > LCD_FRAMEBUFFER_MAX_WIDTH) {
        return;
    }

    lv_display_t * disp = lv_display_create(hor_res, ver_res);
    if(disp == NULL) {
        return;
    }

    /* 在创建应用控件前明确活动屏幕尺寸，子对象会以该尺寸作为布局依据。 */
    lv_obj_set_size(lv_display_get_screen_active(disp), hor_res, ver_res);

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, disp_flush);

    /* 单缓冲局部渲染：缩到40行，为波形DMA乒乓缓冲释放片内SRAM。 */
    draw_buf_size = (uint32_t)hor_res * LVGL_DRAW_BUF_LINES * sizeof(uint16_t);
    lv_display_set_buffers(disp, s_draw_buf, NULL, draw_buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

}

/**
 * @brief  允许后续disp_flush()把绘制结果写入LCD帧缓冲。
 * @note   该接口可用于暂停显示更新；它不会停止LVGL的布局、动画或绘制计算。
 */
void disp_enable_update(void)
{
    s_flush_enabled = true;
}

/**
 * @brief  禁止后续disp_flush()把绘制结果写入LCD帧缓冲。
 * @note   禁止期间仍会调用lv_display_flush_ready()，否则LVGL会一直等待刷新完成。
 */
void disp_disable_update(void)
{
    s_flush_enabled = false;
}

/*==============================================================================
 * 私有函数：仅供本文件内部调用
 *============================================================================*/

/**
 * @brief  准备显示端口依赖的底层硬件。
 * @note   当前main()已先后初始化SDRAM和LCD，因此本函数无需重复操作。保留该函数
 *         是为了维持清晰的Port层结构；以后改变启动顺序时可在这里补充适配操作。
 */
static void disp_init(void)
{
    /* Nothing to do here: main() has already initialized SDRAM and LTDC. */
}

/**
 * @brief  把LVGL局部绘制缓冲区复制到LTDC正在扫描的SDRAM帧缓冲。
 * @param  disp_drv 发起刷新的LVGL显示对象，用于完成刷新握手。
 * @param  area LVGL要求更新的矩形区域，x2和y2均为包含式坐标。
 * @param  px_map 与area对应的RGB565像素数组首地址。
 *
 * @details 当前lcd_write_area()使用CPU同步复制。因此函数返回前写入已经完成，可以
 *          立即调用lv_display_flush_ready()。将来改成DMA2D异步复制后，不能在这里
 *          立即通知完成，而应在DMA2D传输完成中断中调用该函数。
 */
static void disp_flush(lv_display_t * disp_drv, const lv_area_t * area, uint8_t * px_map)
{
    if(s_flush_enabled && area->x1 >= 0 && area->y1 >= 0) {
        uint32_t pixels = (uint32_t)(area->x2 - area->x1 + 1) *
                          (uint32_t)(area->y2 - area->y1 + 1);

        lcd_write_area((uint16_t)area->x1, (uint16_t)area->y1,
                       (uint16_t)area->x2, (uint16_t)area->y2,
                       (const uint16_t *)px_map);
        frame_test_record_flush(pixels);

    }

    /* 告诉LVGL当前缓冲区已经可以再次用于绘制；每次回调都必须完成该握手。 */
    lv_display_flush_ready(disp_drv);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
