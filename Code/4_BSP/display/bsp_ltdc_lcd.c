/**
 ******************************************************************************
 * @file    bsp_ltdc_lcd.c
 * @brief   阿波罗 H743 板级 LTDC、RGB GPIO、背光和中断实现
 ******************************************************************************
 */
#include "bsp_ltdc_lcd.h"
#include "memory_placement.h"

#define BSP_LTDC_WAVE_LINE_ALIGNMENT_BYTES    64U
#define BSP_LTDC_WAVE_BUFFER_ALIGNMENT_BYTES  1024U
#define BSP_LTDC_ALIGN_UP(value, alignment) \
    (((value) + (alignment) - 1U) & ~((alignment) - 1U))

/*
 * 三块 RGB565 帧缓冲由链接器放入 SDRAM 的 UI_DATA 区域。该区域为 UNINIT，
 * __main 不会在 FMC 初始化前访问；LCD/波形初始化负责在首次显示前写入像素。
 * 每块按支持的最大面板分辨率预留，避免运行时按裸地址推算写入未归属的内存。
 */
#define BSP_LTDC_MAX_FRAME_PIXELS \
    (BSP_LTDC_FRAMEBUFFER_MAX_WIDTH * BSP_LTDC_FRAMEBUFFER_MAX_HEIGHT)
#if (3UL * BSP_LTDC_MAX_FRAME_PIXELS * BSP_LTDC_PIXEL_SIZE_BYTES) > BSP_LTDC_SDRAM_SIZE_BYTES
#error "LTDC frame buffers exceed the SDRAM UI region"
#endif
PLATFORM_SDRAM_UI_ZI __attribute__((aligned(BSP_LTDC_WAVE_BUFFER_ALIGNMENT_BYTES)))
static uint16_t s_ui_framebuffer[BSP_LTDC_MAX_FRAME_PIXELS];
PLATFORM_SDRAM_UI_ZI __attribute__((aligned(BSP_LTDC_WAVE_BUFFER_ALIGNMENT_BYTES)))
static uint16_t s_wave_framebuffer_a[BSP_LTDC_MAX_FRAME_PIXELS];
PLATFORM_SDRAM_UI_ZI __attribute__((aligned(BSP_LTDC_WAVE_BUFFER_ALIGNMENT_BYTES)))
static uint16_t s_wave_framebuffer_b[BSP_LTDC_MAX_FRAME_PIXELS];

LTDC_HandleTypeDef g_ltdc_handle; /**< STM32 HAL的LTDC外设句柄。 */

static bool s_ready;                         /**< LTDC硬件是否初始化成功。 */
static volatile uint32_t s_frame_count = 0U; /**< LTDC帧起始事件计数。 */
static volatile uint32_t s_wave_present_count;
static volatile uint32_t s_fifo_underrun_count;
static volatile uint32_t s_transfer_error_count;
static volatile uint32_t s_error_irq_count;
static volatile uint32_t s_reload_event_count;
static volatile uint32_t s_last_error_flags;
static volatile uint32_t s_last_error_position;
static volatile uint32_t s_last_error_display_status;
static volatile uint32_t s_last_error_frame_count;
static uint16_t s_display_width;
static uint16_t s_display_height;
static uint16_t s_wave_width;
static uint16_t s_wave_height;
static uint16_t s_wave_stride;
static volatile uint32_t s_wave_front_address;
static volatile uint32_t s_wave_back_address;
static volatile bool s_wave_swap_pending;
static bool s_wave_layer_visible;
static bool s_wave_layer_ready;
static bsp_ltdc_frame_callback_t s_frame_callback;
static void *s_frame_callback_context;

/** @brief 以 CPU 连续写方式初始化一块 RGB565 波形帧缓冲。 */
static void bsp_ltdc_fill_buffer(uint16_t *buffer, uint32_t pixel_count,
                                 uint16_t color)
{
    uint32_t index;

    for(index = 0U; index < pixel_count; index++) {
        buffer[index] = color;
    }
}

/** @brief 初始化LTDC RGB数据线、同步信号、像素时钟和背光GPIO。 */
static void bsp_ltdc_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF14_LTDC;

    gpio.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOF, &gpio);

    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOG, &gpio);

    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOH, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 |
               GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOI, &gpio);
}

/** @brief 配置PLL3作为LTDC像素时钟。 */
static bool bsp_ltdc_clock_config(uint16_t pll3_r)
{
    RCC_PeriphCLKInitTypeDef clock = {0};

    clock.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    clock.PLL3.PLL3M = 25U;
    clock.PLL3.PLL3N = 300U;
    clock.PLL3.PLL3P = 2U;
    clock.PLL3.PLL3Q = 2U;
    clock.PLL3.PLL3R = pll3_r;
    clock.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0;
    clock.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    clock.PLL3.PLL3FRACN = 0U;
    return (HAL_RCCEx_PeriphCLKConfig(&clock) == HAL_OK);
}

/**
 * @brief bsp_ltdc_lcd_read_id_bits：bsp ltdc lcd。
 */
uint8_t bsp_ltdc_lcd_read_id_bits(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t id_bits;

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOG, &gpio);
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOI, &gpio);
    HAL_Delay(1U);

    id_bits  = (uint8_t)HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_6);
    id_bits |= (uint8_t)HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_2) << 1;
    id_bits |= (uint8_t)HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_7) << 2;
    return id_bits;
}

/**
 * @brief HAL_LTDC_MspInit：bsp ltdc lcd。
 */
void HAL_LTDC_MspInit(LTDC_HandleTypeDef *hltdc)
{
    (void)hltdc;
    __HAL_RCC_LTDC_CLK_ENABLE();
    bsp_ltdc_gpio_init();

    /*
     * STM32H7将行事件/重载和FIFO欠载/传输错误分配到两个NVIC入口。
     * 两个入口都必须启用，否则FUIF即使置位也不会进入HAL错误回调。
     */
    HAL_NVIC_SetPriority(LTDC_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(LTDC_IRQn);
    HAL_NVIC_SetPriority(LTDC_ER_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(LTDC_ER_IRQn);
}

/**
 * @brief bsp_ltdc_lcd_init：bsp ltdc lcd。
 */
bsp_ltdc_status_t bsp_ltdc_lcd_init(const bsp_ltdc_config_t *config)
{
    LTDC_LayerCfgTypeDef layer = {0};

    s_ready = false;
    if (config == NULL || config->width == 0U || config->height == 0U ||
        config->width > BSP_LTDC_FRAMEBUFFER_MAX_WIDTH ||
        config->height > BSP_LTDC_FRAMEBUFFER_MAX_HEIGHT)
    {
        return BSP_LTDC_STATUS_INIT_ERROR;
    }
    if (!bsp_ltdc_clock_config(config->pll3_r))
    {
        return BSP_LTDC_STATUS_CLOCK_ERROR;
    }

    g_ltdc_handle.Instance = LTDC;
    g_ltdc_handle.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    g_ltdc_handle.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    g_ltdc_handle.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    g_ltdc_handle.Init.PCPolarity = config->pixel_clock_polarity;
    g_ltdc_handle.Init.HorizontalSync = config->hsw - 1U;
    g_ltdc_handle.Init.VerticalSync = config->vsw - 1U;
    g_ltdc_handle.Init.AccumulatedHBP = config->hsw + config->hbp - 1U;
    g_ltdc_handle.Init.AccumulatedVBP = config->vsw + config->vbp - 1U;
    g_ltdc_handle.Init.AccumulatedActiveW = config->hsw + config->hbp + config->width - 1U;
    g_ltdc_handle.Init.AccumulatedActiveH = config->vsw + config->vbp + config->height - 1U;
    g_ltdc_handle.Init.TotalWidth = config->hsw + config->hbp + config->width + config->hfp - 1U;
    g_ltdc_handle.Init.TotalHeigh = config->vsw + config->vbp + config->height + config->vfp - 1U;
    g_ltdc_handle.Init.Backcolor.Red = 0U;
    g_ltdc_handle.Init.Backcolor.Green = 0U;
    g_ltdc_handle.Init.Backcolor.Blue = 0U;
    if (HAL_LTDC_Init(&g_ltdc_handle) != HAL_OK)
    {
        return BSP_LTDC_STATUS_INIT_ERROR;
    }

    layer.WindowX0 = 0U;
    layer.WindowX1 = config->width;
    layer.WindowY0 = 0U;
    layer.WindowY1 = config->height;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    layer.Alpha = 255U;
    layer.Alpha0 = 0U;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
    layer.FBStartAdress = (uint32_t)(uintptr_t)s_ui_framebuffer;
    layer.ImageWidth = config->width;
    layer.ImageHeight = config->height;
    if (HAL_LTDC_ConfigLayer(&g_ltdc_handle, &layer, 0U) != HAL_OK)
    {
        return BSP_LTDC_STATUS_LAYER_ERROR;
    }

    __HAL_LTDC_CLEAR_FLAG(&g_ltdc_handle, LTDC_FLAG_LI);
    if (HAL_LTDC_ProgramLineEvent(&g_ltdc_handle, 0U) != HAL_OK)
    {
        return BSP_LTDC_STATUS_INIT_ERROR;
    }

    s_frame_count = 0U;
    s_wave_present_count = 0U;
    s_fifo_underrun_count = 0U;
    s_transfer_error_count = 0U;
    s_error_irq_count = 0U;
    s_reload_event_count = 0U;
    s_last_error_flags = 0U;
    s_last_error_position = 0U;
    s_last_error_display_status = 0U;
    s_last_error_frame_count = 0U;
    s_display_width = config->width;
    s_display_height = config->height;
    s_wave_width = 0U;
    s_wave_height = 0U;
    s_wave_stride = 0U;
    s_wave_front_address = 0U;
    s_wave_back_address = 0U;
    s_wave_swap_pending = false;
    s_wave_layer_visible = false;
    s_wave_layer_ready = false;
    s_ready = true;
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    return BSP_LTDC_STATUS_OK;
}

/**
 * @brief bsp_ltdc_lcd_is_ready：bsp ltdc lcd。
 */
bool bsp_ltdc_lcd_is_ready(void)
{
    return s_ready;
}

/**
 * @brief bsp_ltdc_lcd_get_framebuffer：bsp ltdc lcd。
 */
uint16_t *bsp_ltdc_lcd_get_framebuffer(void)
{
    return s_ui_framebuffer;
}

/**
 * @brief HAL_LTDC_LineEventCallback：bsp ltdc lcd。
 */
void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *hltdc)
{
    if (hltdc->Instance == LTDC)
    {
        ++s_frame_count;
        (void)HAL_LTDC_ProgramLineEvent(hltdc, 0U);
        if(s_frame_callback != NULL) {
            s_frame_callback(s_frame_callback_context);
        }
    }
}

/**
 * @brief LTDC 已在垂直消隐期锁存 Layer 1 新地址后触发。
 *
 * 此处才把旧前缓冲移交给 CPU，保证 get_back_buffer() 永远不会返回 LTDC
 * 正在扫描的地址。
 */
void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *hltdc)
{
    if(hltdc->Instance != LTDC) {
        return;
    }

    ++s_reload_event_count;
    if(s_wave_swap_pending) {
        const uint32_t old_front_address = s_wave_front_address;

        s_wave_front_address = s_wave_back_address;
        s_wave_back_address = old_front_address;
        s_wave_layer_visible = true;
        s_wave_swap_pending = false;
        ++s_wave_present_count;
    }
}

/**
 * @brief bsp_ltdc_lcd_get_frame_count：bsp ltdc lcd。
 */
uint32_t bsp_ltdc_lcd_get_frame_count(void)
{
    return s_frame_count;
}

/**
 * @brief bsp_ltdc_lcd_get_error_code：bsp ltdc lcd。
 */
uint32_t bsp_ltdc_lcd_get_error_code(void)
{
    /* HAL 在 LTDC 中断中锁存 FU/TE；后续正常换帧不会清除这些诊断位。 */
    return g_ltdc_handle.ErrorCode;
}

/**
 * @brief bsp_ltdc_lcd_get_fifo_underrun_count：bsp ltdc lcd。
 */
uint32_t bsp_ltdc_lcd_get_fifo_underrun_count(void)
{
    return s_fifo_underrun_count;
}

/**
 * @brief bsp_ltdc_lcd_get_transfer_error_count：bsp ltdc lcd。
 */
uint32_t bsp_ltdc_lcd_get_transfer_error_count(void)
{
    return s_transfer_error_count;
}

/**
 * @brief bsp_ltdc_lcd_capture_error_irq：bsp ltdc lcd。
 */
void bsp_ltdc_lcd_capture_error_irq(void)
{
    const uint32_t flags = LTDC->ISR & LTDC->IER &
                           (LTDC_ISR_FUIF | LTDC_ISR_TERRIF);

    ++s_error_irq_count;
    if(flags != 0U) {
        s_last_error_flags = flags;
        s_last_error_position = LTDC->CPSR;
        s_last_error_display_status = LTDC->CDSR;
        s_last_error_frame_count = s_frame_count;
    }
}

/**
 * @brief bsp_ltdc_lcd_get_error_diagnostic：bsp ltdc lcd。
 */
bool bsp_ltdc_lcd_get_error_diagnostic(
    bsp_ltdc_error_diagnostic_t *diagnostic)
{
    uint32_t primask;

    if(!s_ready || diagnostic == NULL) {
        return false;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    diagnostic->fifo_underrun_count = s_fifo_underrun_count;
    diagnostic->transfer_error_count = s_transfer_error_count;
    diagnostic->error_irq_count = s_error_irq_count;
    diagnostic->line_event_count = s_frame_count;
    diagnostic->reload_event_count = s_reload_event_count;
    diagnostic->present_count = s_wave_present_count;
    diagnostic->hal_error_code = g_ltdc_handle.ErrorCode;
    diagnostic->interrupt_flags = LTDC->ISR;
    diagnostic->interrupt_enable = LTDC->IER;
    diagnostic->last_error_flags = s_last_error_flags;
    diagnostic->last_error_position = s_last_error_position;
    diagnostic->last_error_display_status = s_last_error_display_status;
    diagnostic->last_error_frame_count = s_last_error_frame_count;
    diagnostic->swap_pending = s_wave_swap_pending;
    if(primask == 0U) {
        __enable_irq();
    }
    return true;
}

/**
 * @brief HAL_LTDC_ErrorCallback：bsp ltdc lcd。
 */
void HAL_LTDC_ErrorCallback(LTDC_HandleTypeDef *hltdc)
{
    uint32_t enabled_interrupts;

    if(hltdc == NULL || hltdc->Instance != LTDC) {
        return;
    }

    enabled_interrupts = READ_REG(hltdc->Instance->IER);

    /* HAL会在回调前关闭对应错误中断；计数后重新打开，才能统计后续事件。 */
    if((enabled_interrupts & LTDC_IER_FUIE) == 0U) {
        ++s_fifo_underrun_count;
        __HAL_LTDC_ENABLE_IT(hltdc, LTDC_IT_FU);
    }
    if((enabled_interrupts & LTDC_IER_TERRIE) == 0U) {
        ++s_transfer_error_count;
        __HAL_LTDC_ENABLE_IT(hltdc, LTDC_IT_TE);
    }
}

/**
 * @brief bsp_ltdc_lcd_set_frame_callback：bsp ltdc lcd。
 */
void bsp_ltdc_lcd_set_frame_callback(bsp_ltdc_frame_callback_t callback,
                                     void *context)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    s_frame_callback_context = context;
    s_frame_callback = callback;
    if(primask == 0U) {
        __enable_irq();
    }
}

/**
 * @brief bsp_ltdc_lcd_wave_layer_init：bsp ltdc lcd。
 */
bool bsp_ltdc_lcd_wave_layer_init(uint16_t x, uint16_t y,
                                  uint16_t width, uint16_t height,
                                  uint16_t background_color)
{
    LTDC_LayerCfgTypeDef layer = {0};
    uint32_t wave_line_bytes;
    uint32_t wave_stride_bytes;
    uint32_t wave_buffer_bytes;
    uint32_t front_address;
    uint32_t back_address;

    if(!s_ready || width == 0U || height == 0U ||
       x >= s_display_width || y >= s_display_height ||
       ((uint32_t)x + width) > s_display_width ||
       ((uint32_t)y + height) > s_display_height) {
        return false;
    }

    /*
     * LTDC以突发方式读取帧缓冲。将1000字节可见行补齐到1024字节，避免
     * 相邻行不断改变突发边界；两块缓冲也分别从1KB边界开始。
     */
    wave_line_bytes = (uint32_t)width * BSP_LTDC_PIXEL_SIZE_BYTES;
    wave_stride_bytes = BSP_LTDC_ALIGN_UP(
        wave_line_bytes, BSP_LTDC_WAVE_LINE_ALIGNMENT_BYTES);
    wave_buffer_bytes = wave_stride_bytes * height;

    if(wave_buffer_bytes > sizeof(s_wave_framebuffer_a) ||
       wave_buffer_bytes > sizeof(s_wave_framebuffer_b)) {
        return false;
    }
    front_address = (uint32_t)(uintptr_t)s_wave_framebuffer_a;
    back_address = (uint32_t)(uintptr_t)s_wave_framebuffer_b;

    /* 先写背景，再使 Layer 1 可见，避免首次启用时扫描到未初始化 SDRAM。 */
    bsp_ltdc_fill_buffer((uint16_t *)front_address,
                         (wave_stride_bytes / BSP_LTDC_PIXEL_SIZE_BYTES) * height,
                         background_color);
    bsp_ltdc_fill_buffer((uint16_t *)back_address,
                         (wave_stride_bytes / BSP_LTDC_PIXEL_SIZE_BYTES) * height,
                         background_color);

    layer.WindowX0 = x;
    layer.WindowX1 = (uint32_t)x + width;
    layer.WindowY0 = y;
    layer.WindowY1 = (uint32_t)y + height;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    /* 启动阶段先完全透明，首次 present 时才在垂直同步中显示波形 Layer。 */
    layer.Alpha = 0U;
    layer.Alpha0 = 0U;
    /*
     * Layer 2 窗口外使用默认颜色的 Alpha0。若使用 CA，常量 Alpha=255 会
     * 使窗口外的默认黑色仍然不透明，从而覆盖整个 Layer 0。PAxCA 才会让
     * 窗口内 RGB565 像素保持可见、窗口外 Alpha0=0 的默认色完全透明。
     */
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    layer.FBStartAdress = front_address;
    /* ImageWidth决定CFBP物理步长；WindowX仍保持500像素可见宽度。 */
    layer.ImageWidth = wave_stride_bytes / BSP_LTDC_PIXEL_SIZE_BYTES;
    layer.ImageHeight = height;
    if(HAL_LTDC_ConfigLayer(&g_ltdc_handle, &layer, 1U) != HAL_OK) {
        return false;
    }
    /*
     * Layer 1 只承载彩色波形线。RGB565 的黑色像素作为色键透明，波形背景
     * 仍由 Layer 0 的 wave_root 提供，故 Layer 1 的空白永不遮住右侧控件。
     */
    if(HAL_LTDC_ConfigColorKeying(&g_ltdc_handle, 0x00000000UL, 1U) != HAL_OK ||
       HAL_LTDC_EnableColorKeying(&g_ltdc_handle, 1U) != HAL_OK) {
        return false;
    }

    s_wave_width = width;
    s_wave_height = height;
    s_wave_stride = (uint16_t)(wave_stride_bytes / BSP_LTDC_PIXEL_SIZE_BYTES);
    s_wave_front_address = front_address;
    s_wave_back_address = back_address;
    s_wave_swap_pending = false;
    s_wave_present_count = 0U;
    s_wave_layer_visible = false;
    s_wave_layer_ready = true;
    return true;
}

/**
 * @brief bsp_ltdc_lcd_wave_layer_get_back_buffer：bsp ltdc lcd。
 */
uint16_t *bsp_ltdc_lcd_wave_layer_get_back_buffer(void)
{
    if(!s_wave_layer_ready || s_wave_swap_pending) {
        return NULL;
    }

    return (uint16_t *)s_wave_back_address;
}

/**
 * @brief bsp_ltdc_lcd_wave_layer_get_width：bsp ltdc lcd。
 */
uint16_t bsp_ltdc_lcd_wave_layer_get_width(void)
{
    return s_wave_layer_ready ? s_wave_width : 0U;
}

/**
 * @brief bsp_ltdc_lcd_wave_layer_get_height：bsp ltdc lcd。
 */
uint16_t bsp_ltdc_lcd_wave_layer_get_height(void)
{
    return s_wave_layer_ready ? s_wave_height : 0U;
}

/**
 * @brief bsp_ltdc_lcd_wave_layer_get_stride：bsp ltdc lcd。
 */
uint16_t bsp_ltdc_lcd_wave_layer_get_stride(void)
{
    return s_wave_layer_ready ? s_wave_stride : 0U;
}

/**
 * @brief bsp_ltdc_lcd_wave_layer_present：bsp ltdc lcd。
 */
bool bsp_ltdc_lcd_wave_layer_present(void)
{
    uint32_t primask;

    if(!s_wave_layer_ready || s_wave_swap_pending) {
        return false;
    }

    /*
     * 防止VBlank重载中断在HAL_LTDC_Reload()返回后、pending置位前插入。
     * 否则硬件已换帧，软件却会继续把新前缓冲当作后缓冲。
     */
    primask = __get_PRIMASK();
    __disable_irq();
    if(HAL_LTDC_SetAddress_NoReload(&g_ltdc_handle,
                                    s_wave_back_address, 1U) != HAL_OK) {
        if(primask == 0U) {
            __enable_irq();
        }
        return false;
    }
    if(!s_wave_layer_visible &&
       HAL_LTDC_SetAlpha_NoReload(&g_ltdc_handle, 255U, 1U) != HAL_OK) {
        if(primask == 0U) {
            __enable_irq();
        }
        return false;
    }
    s_wave_swap_pending = true;
    if(HAL_LTDC_Reload(&g_ltdc_handle, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK) {
        s_wave_swap_pending = false;
        if(primask == 0U) {
            __enable_irq();
        }
        return false;
    }
    if(primask == 0U) {
        __enable_irq();
    }
    return true;
}

/**
 * @brief bsp_ltdc_lcd_wave_layer_get_present_count：bsp ltdc lcd。
 */
uint32_t bsp_ltdc_lcd_wave_layer_get_present_count(void)
{
    return s_wave_present_count;
}

/**
 * @brief bsp_ltdc_lcd_wave_layer_get_diagnostic：bsp ltdc lcd。
 */
bool bsp_ltdc_lcd_wave_layer_get_diagnostic(
    bsp_ltdc_wave_diagnostic_t *diagnostic)
{
    if(!s_wave_layer_ready || diagnostic == NULL) {
        return false;
    }

    diagnostic->software_front_address = s_wave_front_address;
    diagnostic->software_back_address = s_wave_back_address;
    diagnostic->hardware_frame_address = LTDC_Layer2->CFBAR;
    diagnostic->frame_buffer_length = LTDC_Layer2->CFBLR;
    diagnostic->frame_buffer_line_count = LTDC_Layer2->CFBLNR;
    diagnostic->horizontal_window = LTDC_Layer2->WHPCR;
    diagnostic->vertical_window = LTDC_Layer2->WVPCR;
    diagnostic->control = LTDC_Layer2->CR;
    diagnostic->pixel_format = LTDC_Layer2->PFCR;
    diagnostic->constant_alpha = LTDC_Layer2->CACR;
    diagnostic->blending = LTDC_Layer2->BFCR;
    diagnostic->color_key = LTDC_Layer2->CKCR;
    diagnostic->present_count = s_wave_present_count;
    diagnostic->swap_pending = s_wave_swap_pending;
    return true;
}
