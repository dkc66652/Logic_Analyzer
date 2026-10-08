/**
  ******************************************************************************
  * @file    uart.c
  * @brief   调试串口 USART1 底层驱动（只发送）
  ******************************************************************************
  */
#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "memory_placement.h"
#include <string.h>

UART_HandleTypeDef g_uart_handle;
DMA_HandleTypeDef  g_uart_tx_dma_handle;

/* 放在 SRAM1 的 DMA_NOCACHE 区；MPU 将该区配置为不可缓存。 */
PLATFORM_DMA_NOCACHE_ZI PLATFORM_CACHE_ALIGNMENT_ATTRIBUTE
static uint8_t s_uart_dma_tx_buffer[UART_TX_DMA_BUFFER_SIZE];


/**
 * @brief uart_clean_tx_dcache：uart。
 */
static void uart_clean_tx_dcache(const uint8_t *data, uint16_t len)
{
#if (__DCACHE_PRESENT == 1U)
    uintptr_t start;
    uintptr_t end;

    if ((SCB->CCR & SCB_CCR_DC_Msk) == 0U)
    {
        return;
    }

    /* STM32H743 uses 32-byte D-Cache lines. */
    start = (uintptr_t)data & ~(uintptr_t)31U;
    end = ((uintptr_t)data + len + 31U) & ~(uintptr_t)31U;
    SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
#else
    (void)data;
    (void)len;
#endif
}


/**
  * @brief  初始化调试串口 USART1（只发送）
	*
  * @note   内部会打开 USART1 和 GPIOA 的时钟，并把 PA9 配成复用推挽输出。
  *         
  * @return true  初始化成功
  *         false HAL 报错，原因见 g_uart_handle.ErrorCode
  */
bool uart_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    UART_CLK_ENABLE();
    UART_TX_GPIO_CLK_ENABLE();

    gpio.Pin       = UART_TX_GPIO_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;               /* 空闲时线上是高电平 */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(UART_TX_GPIO_PORT, &gpio);

    g_uart_handle.Instance                    = UART_INSTANCE;
    g_uart_handle.Init.BaudRate               = UART_BAUDRATE;
    g_uart_handle.Init.WordLength             = UART_WORDLENGTH_8B;
    g_uart_handle.Init.StopBits               = UART_STOPBITS_1;
    g_uart_handle.Init.Parity                 = UART_PARITY_NONE;
    g_uart_handle.Init.Mode                   = UART_MODE_TX;   /* 只发送 */
    g_uart_handle.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    g_uart_handle.Init.OverSampling           = UART_OVERSAMPLING_16;
    g_uart_handle.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    g_uart_handle.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    g_uart_handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    g_uart_handle.FifoMode                    = UART_FIFOMODE_DISABLE;

    if (HAL_UART_Init(&g_uart_handle) != HAL_OK)
    {
        return false;
    }

    __HAL_RCC_DMA2_CLK_ENABLE();

    g_uart_tx_dma_handle.Instance                 = UART_TX_DMA_INSTANCE;
    g_uart_tx_dma_handle.Init.Request             = UART_TX_DMA_REQUEST;
    g_uart_tx_dma_handle.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    g_uart_tx_dma_handle.Init.PeriphInc           = DMA_PINC_DISABLE;
    g_uart_tx_dma_handle.Init.MemInc              = DMA_MINC_ENABLE;
    g_uart_tx_dma_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    g_uart_tx_dma_handle.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    g_uart_tx_dma_handle.Init.Mode                = DMA_NORMAL;
    g_uart_tx_dma_handle.Init.Priority            = DMA_PRIORITY_LOW;
    g_uart_tx_dma_handle.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    g_uart_tx_dma_handle.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    g_uart_tx_dma_handle.Init.MemBurst            = DMA_MBURST_SINGLE;
    g_uart_tx_dma_handle.Init.PeriphBurst         = DMA_PBURST_SINGLE;

    if (HAL_DMA_Init(&g_uart_tx_dma_handle) != HAL_OK)
    {
        return false;
    }

    __HAL_LINKDMA(&g_uart_handle, hdmatx, g_uart_tx_dma_handle);

    HAL_NVIC_SetPriority(UART_TX_DMA_IRQn, UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(UART_TX_DMA_IRQn);
    HAL_NVIC_SetPriority(USART1_IRQn, UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    return true;
}

/**
  * @brief  串口发送函数
	*
	* @param  data 数组名 len 数据长度
	*
  * @return true  发送成功
  *         false 失败
  */

uint16_t uart_write(const uint8_t* data,uint16_t len)
{
	if(data == NULL || len == 0U || len > UART_TX_DMA_BUFFER_SIZE)
	{
		return 0U;
	}

	memcpy(s_uart_dma_tx_buffer, data, len);
	uart_clean_tx_dcache(s_uart_dma_tx_buffer, len);

	if(HAL_UART_Transmit_DMA(&g_uart_handle, s_uart_dma_tx_buffer, len) != HAL_OK)
	{
		return 0U;
	}


	/* Debugging policy: preserve DMA/UART registers until an ISR wakes log_task. */
	(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

	return (g_uart_handle.ErrorCode == HAL_UART_ERROR_NONE) ? len : 0U;
}
