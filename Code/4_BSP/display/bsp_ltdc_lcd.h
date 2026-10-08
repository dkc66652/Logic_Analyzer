/**
 ******************************************************************************
 * @file    bsp_ltdc_lcd.h
 * @brief   阿波罗 H743 板级 LTDC、RGB 接口和帧缓冲支持
 ******************************************************************************
 */
#ifndef __BSP_LTDC_LCD_H
#define __BSP_LTDC_LCD_H

#include "stm32h7xx_hal.h"
#include "bsp_fmc_sdram.h"
#include <stdbool.h>
#include <stdint.h>

#define BSP_LTDC_FRAMEBUFFER_MAX_WIDTH  1280U                   /**< 板级显示接口支持的最大宽度。 */
#define BSP_LTDC_FRAMEBUFFER_MAX_HEIGHT 800U                    /**< 板级显示接口支持的最大高度。 */
#define BSP_LTDC_PIXEL_SIZE_BYTES       2U                      /**< RGB565单像素字节数。 */
#define BSP_LTDC_SDRAM_SIZE_BYTES        (8UL * 1024UL * 1024UL) /**< 链接脚本为 UI_DATA 预留的 SDRAM 容量。 */

/** Device层传给BSP的LTDC时序配置。 */
typedef struct
{
    uint16_t width;                /**< 有效显示宽度，单位为像素。 */
    uint16_t height;               /**< 有效显示高度，单位为像素。 */
    uint16_t hsw;                  /**< 水平同步宽度，单位为像素时钟。 */
    uint16_t hbp;                  /**< 水平后沿，单位为像素时钟。 */
    uint16_t hfp;                  /**< 水平前沿，单位为像素时钟。 */
    uint16_t vsw;                  /**< 垂直同步宽度，单位为行。 */
    uint16_t vbp;                  /**< 垂直后沿，单位为行。 */
    uint16_t vfp;                  /**< 垂直前沿，单位为行。 */
    uint16_t pll3_r;               /**< PLL3 R分频值。 */
    uint32_t pixel_clock_polarity; /**< 面板要求的像素时钟极性。 */
} bsp_ltdc_config_t;

/** BSP LTDC初始化结果。 */
typedef enum
{
    BSP_LTDC_STATUS_OK = 0,
    BSP_LTDC_STATUS_CLOCK_ERROR,
    BSP_LTDC_STATUS_INIT_ERROR,
    BSP_LTDC_STATUS_LAYER_ERROR
} bsp_ltdc_status_t;

/**
 * @brief LTDC 每帧起始回调。
 * @note  回调在 LTDC 中断上下文执行，只能做时间戳、FromISR 通知等短操作。
 */
typedef void (*bsp_ltdc_frame_callback_t)(void *context);

/** Layer 1软件缓冲状态与LTDC硬件寄存器的一次快照。 */
typedef struct
{
    uint32_t software_front_address;
    uint32_t software_back_address;
    uint32_t hardware_frame_address;
    uint32_t frame_buffer_length;
    uint32_t frame_buffer_line_count;
    uint32_t horizontal_window;
    uint32_t vertical_window;
    uint32_t control;
    uint32_t pixel_format;
    uint32_t constant_alpha;
    uint32_t blending;
    uint32_t color_key;
    uint32_t present_count;
    bool swap_pending;
} bsp_ltdc_wave_diagnostic_t;

/** LTDC错误中断和换帧状态快照；错误位置在IRQ入口、HAL清标志前保存。 */
typedef struct
{
    uint32_t fifo_underrun_count;
    uint32_t transfer_error_count;
    uint32_t error_irq_count;
    uint32_t line_event_count;
    uint32_t reload_event_count;
    uint32_t present_count;
    uint32_t hal_error_code;
    uint32_t interrupt_flags;
    uint32_t interrupt_enable;
    uint32_t last_error_flags;
    uint32_t last_error_position;
    uint32_t last_error_display_status;
    uint32_t last_error_frame_count;
    bool swap_pending;
} bsp_ltdc_error_diagnostic_t;

extern LTDC_HandleTypeDef g_ltdc_handle; /**< HAL LTDC外设句柄，供中断入口使用。 */

uint8_t bsp_ltdc_lcd_read_id_bits(void);
bsp_ltdc_status_t bsp_ltdc_lcd_init(const bsp_ltdc_config_t *config);
bool bsp_ltdc_lcd_is_ready(void);
/**
 * @brief 返回整屏 RGB565 帧缓冲数组的首地址。
 * @return UI_DATA 段中的静态数组；仅在 SDRAM 初始化成功后访问其内容。
 * @note 数组容量按 BSP 支持的最大面板预留，实际读写范围由当前面板宽高决定。
 */
uint16_t *bsp_ltdc_lcd_get_framebuffer(void);
uint32_t bsp_ltdc_lcd_get_frame_count(void);

/**
 * @brief 返回 HAL 锁存的 LTDC 错误位。
 * @note  FIFO 欠载对应 HAL_LTDC_ERROR_FU；错误位保持到 LTDC 重新初始化。
 */
uint32_t bsp_ltdc_lcd_get_error_code(void);

/** @return LTDC运行期间累计发生的FIFO欠载次数。 */
uint32_t bsp_ltdc_lcd_get_fifo_underrun_count(void);

/** @return LTDC运行期间累计发生的传输错误次数。 */
uint32_t bsp_ltdc_lcd_get_transfer_error_count(void);

/** 在LTDC错误IRQ入口、HAL清除标志前保存硬件现场。 */
void bsp_ltdc_lcd_capture_error_irq(void);

/** 读取LTDC错误、IRQ及换帧状态，用于低频诊断日志。 */
bool bsp_ltdc_lcd_get_error_diagnostic(
    bsp_ltdc_error_diagnostic_t *diagnostic);

/** 注册每帧起始回调；传入 NULL 可取消注册。 */
void bsp_ltdc_lcd_set_frame_callback(bsp_ltdc_frame_callback_t callback,
                                     void *context);

/**
 * @brief 配置 LTDC Layer 1 为独立的 RGB565 波形显示窗口，并启用两块 SDRAM 后台缓冲。
 *
 * Layer 0 继续作为 LVGL 的整屏 UI 帧缓冲；Layer 1 只覆盖指定波形矩形。两块波形
 * 缓冲是 UI_DATA 段中独立声明的数组，按 LTDC 突发读取要求对齐；调用者只需修改
 * get_back_buffer() 返回的数组地址。
 */
bool bsp_ltdc_lcd_wave_layer_init(uint16_t x, uint16_t y,
                                  uint16_t width, uint16_t height,
                                  uint16_t background_color);

/** @return 当前未被 LTDC 扫描的波形后缓冲；等待垂直同步切换期间返回 NULL。 */
uint16_t *bsp_ltdc_lcd_wave_layer_get_back_buffer(void);

/** @return 波形 Layer 1 的一行像素数量；Layer 未初始化时返回 0。 */
uint16_t bsp_ltdc_lcd_wave_layer_get_width(void);

/** @return 波形 Layer 1 的行数；Layer 未初始化时返回 0。 */
uint16_t bsp_ltdc_lcd_wave_layer_get_height(void);

/** @return 波形帧缓冲相邻两行的像素步长，包含行尾对齐填充。 */
uint16_t bsp_ltdc_lcd_wave_layer_get_stride(void);

/**
 * @brief 请求在下一个垂直消隐期显示当前后缓冲。
 * @return true 表示已经提交一次交换请求；false 表示 Layer 未初始化或上一帧尚未切换。
 */
bool bsp_ltdc_lcd_wave_layer_present(void);

/** @return Layer 1 已在垂直消隐期完成的实际换帧次数。 */
uint32_t bsp_ltdc_lcd_wave_layer_get_present_count(void);

/** 读取Layer 1地址、行距、窗口和混合寄存器，仅供调试。 */
bool bsp_ltdc_lcd_wave_layer_get_diagnostic(
    bsp_ltdc_wave_diagnostic_t *diagnostic);

#endif /* __BSP_LTDC_LCD_H */
