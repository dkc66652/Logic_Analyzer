/**
 * @file lv_port_disp_template.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp_template.h"
#include "ltdc.h"
#include "frame_test.h"
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/
#define LVGL_DRAW_BUF_LINES    120U

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map);

/**********************
 *  STATIC VARIABLES
 **********************/
LV_ATTRIBUTE_MEM_ALIGN
static uint16_t s_draw_buf[LTDC_FRAMEBUFFER_MAX_WIDTH * LVGL_DRAW_BUF_LINES];

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    uint16_t hor_res;
    uint16_t ver_res;
    uint32_t draw_buf_size;

    /* SDRAM and LTDC are initialized by the BSP before LVGL starts. */
    disp_init();

    if(!ltdc_is_ready()) {
        return;
    }

    hor_res = ltdc_get_width();
    ver_res = ltdc_get_height();
    if(hor_res == 0U || ver_res == 0U || hor_res > LTDC_FRAMEBUFFER_MAX_WIDTH) {
        return;
    }

    lv_display_t * disp = lv_display_create(hor_res, ver_res);
    if(disp == NULL) {
        return;
    }

    /*
     * Keep the active screen geometry explicit before any application widget
     * is created.  The side panel inherits its height from this screen.
     */
    lv_obj_set_size(lv_display_get_screen_active(disp), hor_res, ver_res);

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, disp_flush);

    /* Follow section 2.3 of the guide: one partial-render buffer for 10 lines. */
    draw_buf_size = (uint32_t)hor_res * LVGL_DRAW_BUF_LINES * sizeof(uint16_t);
    lv_display_set_buffers(disp, s_draw_buf, NULL, draw_buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /* Nothing to do here: main() has already initialized SDRAM and LTDC. */
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/*Flush the content of the internal buffer the specific area on the display.
 *`px_map` contains the rendered image as raw pixel map and it should be copied to `area` on the display.
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_display_flush_ready()' has to be called when it's finished.*/
static void disp_flush(lv_display_t * disp_drv, const lv_area_t * area, uint8_t * px_map)
{
    if(disp_flush_enabled && area->x1 >= 0 && area->y1 >= 0) {
        ltdc_color_fill((uint16_t)area->x1, (uint16_t)area->y1,
                        (uint16_t)area->x2, (uint16_t)area->y2,
                        (const uint16_t *)px_map);

        /* 统计真正送往 LTDC 帧缓冲的像素量，而不是仅统计失效请求数。 */
        frame_test_record_flush((uint32_t)(area->x2 - area->x1 + 1) *
                                (uint32_t)(area->y2 - area->y1 + 1));
    }

    /*IMPORTANT!!!
     *Inform the graphics library that you are ready with the flushing*/
    lv_display_flush_ready(disp_drv);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
