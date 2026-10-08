/**
 ******************************************************************************
 * @file    bsp_dma2d.c
 * @brief   STM32H7 DMA2D 异步 RGB565 搬运和填充实现。
 ******************************************************************************
 */

#include "bsp_dma2d.h"
#include "bsp_fmc_sdram.h"
#include "bsp_ltdc_lcd.h"

#include <stddef.h>

#define BSP_DMA2D_CACHE_LINE    32U

DMA2D_HandleTypeDef g_dma2d_handle;

static bsp_dma2d_callback_t s_transfer_callback;
static void *s_transfer_context;
static uint32_t s_transfer_mode;
static uint32_t s_output_offset;
static bool s_ready;

/** @brief D-Cache启用时，把CPU刚写好的源数据提交到DMA2D可见的存储器。 */
static void bsp_dma2d_clean_source(const void *address, uint32_t size)
{
#if (__DCACHE_PRESENT == 1U)
    uintptr_t start;
    uintptr_t end;

    if((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        start = (uintptr_t)address & ~(uintptr_t)(BSP_DMA2D_CACHE_LINE - 1U);
        end = ((uintptr_t)address + size + BSP_DMA2D_CACHE_LINE - 1U) &
              ~(uintptr_t)(BSP_DMA2D_CACHE_LINE - 1U);
        /* 先完成CPU对源缓冲的全部写入，再把完整Cache Line提交给DMA2D。 */
        __DMB();
        SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
        __DSB();
        __ISB();
        return;
    }
#endif

    /*
     * D-Cache关闭时CPU仍然有写缓冲。如果不在启动DMA2D前排空，
     * DMA2D可能先读到源条带开头尚未提交到AXI SRAM的旧像素。
     */
    (void)address;
    (void)size;
    __DMB();
    __DSB();
}

/**
 * @brief DMA写目标区前清除可能残留的脏缓存行。
 * @note  当前SDRAM被MPU配置为不可缓存，此分支不会产生额外开销。
 */
static void bsp_dma2d_prepare_destination(void *address, uint32_t size)
{
#if (__DCACHE_PRESENT == 1U)
    uintptr_t start;
    uintptr_t end;

    if((SCB->CCR & SCB_CCR_DC_Msk) == 0U) {
        return;
    }

    /* UI_DATA 整段由 MPU 配置为不可缓存，无需对横向条带反复维护缓存。 */
    if((uintptr_t)address >= (uintptr_t)BSP_FMC_SDRAM_BANK_ADDR &&
       (uintptr_t)address + size <=
           (uintptr_t)BSP_FMC_SDRAM_BANK_ADDR +
           (uintptr_t)BSP_LTDC_SDRAM_SIZE_BYTES) {
        __DMB();
        return;
    }

    start = (uintptr_t)address & ~(uintptr_t)(BSP_DMA2D_CACHE_LINE - 1U);
    end = ((uintptr_t)address + size + BSP_DMA2D_CACHE_LINE - 1U) &
          ~(uintptr_t)(BSP_DMA2D_CACHE_LINE - 1U);
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)start,
                                      (int32_t)(end - start));
#else
    (void)address;
    (void)size;
#endif
}

/** @brief 配置下一笔任务的模式和目标行尾偏移；参数不变时不重复初始化。 */
static bool bsp_dma2d_configure(uint32_t mode, uint32_t output_offset)
{
    if(s_transfer_mode == mode && s_output_offset == output_offset) {
        return true;
    }

    g_dma2d_handle.Init.Mode = mode;
    g_dma2d_handle.Init.OutputOffset = output_offset;
    if(HAL_DMA2D_Init(&g_dma2d_handle) != HAL_OK) {
        return false;
    }

    if(mode == DMA2D_M2M &&
       HAL_DMA2D_ConfigLayer(&g_dma2d_handle, 1U) != HAL_OK) {
        return false;
    }

    s_transfer_mode = mode;
    s_output_offset = output_offset;
    return true;
}

/** @brief HAL传输完成入口；把结果转交给当前DMA2D使用者。 */
static void bsp_dma2d_transfer_complete(DMA2D_HandleTypeDef *handle)
{
    bsp_dma2d_callback_t callback = s_transfer_callback;
    void *context = s_transfer_context;

    (void)handle;
    /* DMA2D已报告传输完成后，再允许任务读取目标区或提交LTDC换帧。 */
    __DSB();
    s_transfer_callback = NULL;
    s_transfer_context = NULL;
    if(callback != NULL) {
        callback(context, true);
    }
}

/** @brief HAL传输错误入口；与完成回调共用一次性回调所有权。 */
static void bsp_dma2d_transfer_error(DMA2D_HandleTypeDef *handle)
{
    bsp_dma2d_callback_t callback = s_transfer_callback;
    void *context = s_transfer_context;

    (void)handle;
    s_transfer_callback = NULL;
    s_transfer_context = NULL;
    if(callback != NULL) {
        callback(context, false);
    }
}

/**
 * @brief bsp_dma2d_init：bsp dma2d。
 */
bool bsp_dma2d_init(void)
{
    DMA2D_LayerCfgTypeDef foreground = {0};

    if(s_ready) {
        return true;
    }

    HAL_NVIC_DisableIRQ(DMA2D_IRQn);
    __HAL_RCC_DMA2D_CLK_ENABLE();
    __HAL_RCC_DMA2D_FORCE_RESET();
    __HAL_RCC_DMA2D_RELEASE_RESET();

    g_dma2d_handle.Instance = DMA2D;
    g_dma2d_handle.Init.Mode = DMA2D_M2M;
    g_dma2d_handle.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    g_dma2d_handle.Init.OutputOffset = 0U;
    g_dma2d_handle.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;
    g_dma2d_handle.Init.RedBlueSwap = DMA2D_RB_REGULAR;
    g_dma2d_handle.Init.LineOffsetMode = DMA2D_LOM_PIXELS;
    g_dma2d_handle.Init.BytesSwap = DMA2D_BYTES_REGULAR;

    if(HAL_DMA2D_Init(&g_dma2d_handle) != HAL_OK) {
        return false;
    }

#if BSP_DMA2D_AXI_DEAD_TIME > 0U
#if BSP_DMA2D_AXI_DEAD_TIME > 255U
#error "BSP_DMA2D_AXI_DEAD_TIME must be in the range 0..255"
#endif
    /*
     * 限制 DMA2D 连续占用总线的强度，给实时扫描的 LTDC 留出访问窗口。
     * 该寄存器不改变像素内容和搬运地址，只会略微延长 DMA2D 传输时间。
     */
    MODIFY_REG(DMA2D->AMTCR,
               DMA2D_AMTCR_EN | DMA2D_AMTCR_DT_Msk,
               DMA2D_AMTCR_EN |
               ((uint32_t)BSP_DMA2D_AXI_DEAD_TIME << DMA2D_AMTCR_DT_Pos));
#else
    CLEAR_BIT(DMA2D->AMTCR, DMA2D_AMTCR_EN | DMA2D_AMTCR_DT_Msk);
#endif

    foreground.InputOffset = 0U;
    foreground.InputColorMode = DMA2D_INPUT_RGB565;
    foreground.AlphaMode = DMA2D_NO_MODIF_ALPHA;
    foreground.InputAlpha = 0xFFU;
    foreground.AlphaInverted = DMA2D_REGULAR_ALPHA;
    foreground.RedBlueSwap = DMA2D_RB_REGULAR;
    foreground.ChromaSubSampling = DMA2D_NO_CSS;
    if(HAL_DMA2D_ConfigLayer(&g_dma2d_handle, 1U) != HAL_OK) {
        return false;
    }

    /* HAL未启用注册接口时，传输回调仍由句柄中的函数指针分发。 */
    g_dma2d_handle.XferCpltCallback = bsp_dma2d_transfer_complete;
    g_dma2d_handle.XferErrorCallback = bsp_dma2d_transfer_error;
    s_transfer_mode = DMA2D_M2M;
    s_output_offset = 0U;

    HAL_NVIC_ClearPendingIRQ(DMA2D_IRQn);
    HAL_NVIC_SetPriority(DMA2D_IRQn, BSP_DMA2D_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(DMA2D_IRQn);
    s_ready = true;
    return true;
}

/**
 * @brief bsp_dma2d_get_error_code：bsp dma2d。
 */
uint32_t bsp_dma2d_get_error_code(void)
{
    return g_dma2d_handle.ErrorCode;
}

/**
 * @brief bsp_dma2d_is_data_cache_enabled：bsp dma2d。
 */
bool bsp_dma2d_is_data_cache_enabled(void)
{
#if (__DCACHE_PRESENT == 1U)
    return (SCB->CCR & SCB_CCR_DC_Msk) != 0U;
#else
    return false;
#endif
}

/**
 * @brief bsp_dma2d_copy_rgb565_async：bsp dma2d。
 */
bool bsp_dma2d_copy_rgb565_async(const uint16_t *source,
                                 uint16_t *destination,
                                 uint16_t width,
                                 uint16_t height,
                                 uint16_t destination_stride,
                                 bsp_dma2d_callback_t callback,
                                 void *context)
{
    uint32_t source_bytes;
    uint32_t destination_bytes;
    uint32_t output_offset;

    if(!s_ready || source == NULL || destination == NULL || callback == NULL ||
       width == 0U || height == 0U || destination_stride < width ||
       s_transfer_callback != NULL) {
        return false;
    }

    output_offset = (uint32_t)destination_stride - width;
    if(!IS_DMA2D_OFFSET(output_offset)) {
        return false;
    }
    if(!bsp_dma2d_configure(DMA2D_M2M, output_offset)) {
        return false;
    }

    source_bytes = (uint32_t)width * height * sizeof(source[0]);
    destination_bytes = (((uint32_t)(height - 1U) * destination_stride) +
                         width) * sizeof(destination[0]);
    bsp_dma2d_clean_source(source, source_bytes);
    bsp_dma2d_prepare_destination(destination, destination_bytes);

    s_transfer_callback = callback;
    s_transfer_context = context;
    if(HAL_DMA2D_Start_IT(&g_dma2d_handle,
                          (uint32_t)source,
                          (uint32_t)destination,
                          width,
                          height) != HAL_OK) {
        s_transfer_callback = NULL;
        s_transfer_context = NULL;
        return false;
    }

    return true;
}

/**
 * @brief bsp_dma2d_fill_rgb565_async：bsp dma2d。
 */
bool bsp_dma2d_fill_rgb565_async(uint16_t *destination,
                                 uint16_t width,
                                 uint16_t height,
                                 uint16_t destination_stride,
                                 uint16_t color,
                                 bsp_dma2d_callback_t callback,
                                 void *context)
{
    uint32_t destination_bytes;
    uint32_t output_offset;

    if(!s_ready || destination == NULL || callback == NULL ||
       width == 0U || height == 0U || destination_stride < width ||
       s_transfer_callback != NULL) {
        return false;
    }

    output_offset = (uint32_t)destination_stride - width;
    if(!IS_DMA2D_OFFSET(output_offset)) {
        return false;
    }
    if(!bsp_dma2d_configure(DMA2D_R2M, output_offset)) {
        return false;
    }

    destination_bytes = (((uint32_t)(height - 1U) * destination_stride) +
                         width) * sizeof(destination[0]);
    bsp_dma2d_prepare_destination(destination, destination_bytes);

    s_transfer_callback = callback;
    s_transfer_context = context;
    if(HAL_DMA2D_Start_IT(&g_dma2d_handle,
                          color,
                          (uint32_t)destination,
                          width,
                          height) != HAL_OK) {
        s_transfer_callback = NULL;
        s_transfer_context = NULL;
        return false;
    }

    return true;
}
