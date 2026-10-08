/**
 ******************************************************************************
 * @file    lcd.h
 * @brief   RGB LCD面板识别及RGB565显示功能接口
 ******************************************************************************
 */
#ifndef __LCD_H
#define __LCD_H

#include "bsp_ltdc_lcd.h"
#include <stdbool.h>
#include <stdint.h>

#define LCD_FRAMEBUFFER_MAX_WIDTH BSP_LTDC_FRAMEBUFFER_MAX_WIDTH /**< 支持的最大水平分辨率。 */

/*
 * LTDC 场频压力测试：保持面板全部行/场时序不变，只降低 PLL3R 像素时钟。
 * 默认面板配置约为 60 FPS。RGB 面板通常对 PCLK 下限敏感，低于规格会失锁
 * 黑屏；本板验证后不能把实际扫描频率降到 20 FPS。
 */
#ifndef LCD_LTDC_PIXEL_CLOCK_DIVIDER
#define LCD_LTDC_PIXEL_CLOCK_DIVIDER  1U
#endif

typedef enum
{
    LCD_STATUS_OK = 0,
    LCD_STATUS_NO_PANEL,
    LCD_STATUS_UNSUPPORTED_PANEL,
    LCD_STATUS_CLOCK_ERROR,
    LCD_STATUS_INIT_ERROR,
    LCD_STATUS_LAYER_ERROR
} lcd_status_t;

lcd_status_t lcd_init(void);
bool lcd_is_ready(void);
uint16_t lcd_get_panel_id(void);
uint16_t lcd_get_width(void);
uint16_t lcd_get_height(void);
void lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
void lcd_write_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                    const uint16_t *colors);
void lcd_clear(uint16_t color);

#endif /* __LCD_H */
