/**
 ******************************************************************************
 * @file    lcd.c
 * @brief   正点原子RGB LCD面板设备层实现
 ******************************************************************************
 */
#include "lcd.h"
#include <string.h>

/** 一种LCD面板的识别值和LTDC时序要求。 */
typedef struct
{
    uint8_t id_bits;
    uint16_t id;
    bsp_ltdc_config_t timing;
} lcd_panel_t;

/** 工程支持的LCD面板参数表。 */
static const lcd_panel_t s_panels[] =
{
    { 0U, 0x4342U, {  480U, 272U,  1U,  40U,   5U, 1U,  8U,  8U, 33U, LTDC_PCPOLARITY_IPC  } },
    { 1U, 0x7084U, {  800U, 480U,  1U,  46U, 210U, 1U, 23U, 22U,  9U, LTDC_PCPOLARITY_IPC  } },
    { 2U, 0x7016U, { 1024U, 600U, 20U, 140U, 160U, 3U, 20U, 12U,  6U, LTDC_PCPOLARITY_IPC  } },
    { 4U, 0x4384U, {  800U, 480U, 48U,  88U,  40U, 3U, 32U, 13U,  9U, LTDC_PCPOLARITY_IPC  } },
    { 5U, 0x1018U, { 1280U, 800U, 10U, 140U,  10U, 3U, 10U, 10U,  6U, LTDC_PCPOLARITY_IIPC } }
};

static const lcd_panel_t *s_panel; /**< 当前识别到的面板参数。 */
static bool s_ready;               /**< LCD设备是否初始化成功。 */

/** @brief 把板级M0/M1/M2状态转换成面板ID。 */
static uint16_t lcd_decode_panel_id(uint8_t id_bits)
{
    switch (id_bits)
    {
        case 0U: return 0x4342U;
        case 1U: return 0x7084U;
        case 2U: return 0x7016U;
        case 3U: return 0x7018U;
        case 4U: return 0x4384U;
        case 5U: return 0x1018U;
        default: return 0U;
    }
}

/** @brief 在支持列表中查找面板时序。 */
static const lcd_panel_t *lcd_find_panel(uint16_t panel_id)
{
    uint32_t i;
    for (i = 0U; i < (sizeof(s_panels) / sizeof(s_panels[0])); ++i)
    {
        if (s_panels[i].id == panel_id)
        {
            return &s_panels[i];
        }
    }
    return NULL;
}

/** @brief 识别LCD面板并请求BSP初始化LTDC。 */
lcd_status_t lcd_init(void)
{
    bsp_ltdc_status_t bsp_status;
    bsp_ltdc_config_t timing;
    uint16_t panel_id;
    uint32_t pll3_r;

    s_ready = false;
    panel_id = lcd_get_panel_id();
    if (panel_id == 0U)
    {
        return LCD_STATUS_NO_PANEL;
    }
    s_panel = lcd_find_panel(panel_id);
    if (s_panel == NULL)
    {
        return LCD_STATUS_UNSUPPORTED_PANEL;
    }

    timing = s_panel->timing;
    pll3_r = (uint32_t)timing.pll3_r * LCD_LTDC_PIXEL_CLOCK_DIVIDER;
    /* STM32H743 的 PLL3R 合法范围为 1~128。 */
    if (pll3_r == 0U || pll3_r > 128U)
    {
        return LCD_STATUS_CLOCK_ERROR;
    }
    timing.pll3_r = (uint16_t)pll3_r;

    bsp_status = bsp_ltdc_lcd_init(&timing);
    if (bsp_status == BSP_LTDC_STATUS_CLOCK_ERROR) return LCD_STATUS_CLOCK_ERROR;
    if (bsp_status == BSP_LTDC_STATUS_LAYER_ERROR) return LCD_STATUS_LAYER_ERROR;
    if (bsp_status != BSP_LTDC_STATUS_OK) return LCD_STATUS_INIT_ERROR;

    s_ready = true;
    lcd_clear(0x0000U);
    return LCD_STATUS_OK;
}

/** @brief 查询LCD设备和LTDC硬件是否就绪。 */
bool lcd_is_ready(void)
{
    return s_ready && bsp_ltdc_lcd_is_ready();
}

/** @brief 获取当前LCD面板ID。 */
uint16_t lcd_get_panel_id(void)
{
    return lcd_decode_panel_id(bsp_ltdc_lcd_read_id_bits());
}

/** @brief 获取当前面板宽度。 */
uint16_t lcd_get_width(void)
{
    return (s_panel == NULL) ? 0U : s_panel->timing.width;
}

/** @brief 获取当前面板高度。 */
uint16_t lcd_get_height(void)
{
    return (s_panel == NULL) ? 0U : s_panel->timing.height;
}

/** @brief 向SDRAM帧缓冲写入一个RGB565像素。 */
void lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    uint16_t *framebuffer;
    if (!lcd_is_ready() || x >= lcd_get_width() || y >= lcd_get_height()) return;
    framebuffer = bsp_ltdc_lcd_get_framebuffer();
    framebuffer[(uint32_t)y * lcd_get_width() + x] = color;
}

/** @brief 使用同一种RGB565颜色填充矩形。 */
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    uint16_t *framebuffer;
    uint32_t row;
    uint32_t col;
    uint32_t x_end;
    uint32_t y_end;

    if (!lcd_is_ready() || x >= lcd_get_width() || y >= lcd_get_height() ||
        width == 0U || height == 0U) return;
    x_end = (uint32_t)x + width;
    y_end = (uint32_t)y + height;
    if (x_end > lcd_get_width()) x_end = lcd_get_width();
    if (y_end > lcd_get_height()) y_end = lcd_get_height();

    framebuffer = bsp_ltdc_lcd_get_framebuffer();
    for (row = y; row < y_end; ++row)
    {
        uint16_t *pixel = &framebuffer[row * lcd_get_width() + x];
        for (col = x; col < x_end; ++col) *pixel++ = color;
    }
}

/** @brief 把连续RGB565像素复制到指定帧缓冲区域。 */
void lcd_write_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                    const uint16_t *colors)
{
    uint16_t *framebuffer;
    uint32_t source_width;
    uint32_t copy_width;
    uint32_t row;
    uint32_t x_end;
    uint32_t y_end;

    if (!lcd_is_ready() || colors == NULL || x1 > x2 || y1 > y2 ||
        x1 >= lcd_get_width() || y1 >= lcd_get_height()) return;
    source_width = (uint32_t)x2 - x1 + 1U;
    x_end = x2;
    y_end = y2;
    if (x_end >= lcd_get_width()) x_end = lcd_get_width() - 1U;
    if (y_end >= lcd_get_height()) y_end = lcd_get_height() - 1U;
    copy_width = x_end - x1 + 1U;

    framebuffer = bsp_ltdc_lcd_get_framebuffer();
    for (row = y1; row <= y_end; ++row)
    {
        uint16_t *destination = &framebuffer[row * lcd_get_width() + x1];
        memcpy(destination, colors, copy_width * sizeof(uint16_t));
        colors += source_width;
    }
}

/** @brief 使用指定RGB565颜色清除整个显示区域。 */
void lcd_clear(uint16_t color)
{
    if (lcd_is_ready()) lcd_fill_rect(0U, 0U, lcd_get_width(), lcd_get_height(), color);
}
