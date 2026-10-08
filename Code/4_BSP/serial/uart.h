/**
  ******************************************************************************
  * @file    uart.h
  * @brief   调试串口 USART1 底层驱动（只发送）
  *
  *  职责边界：只做硬件层的事 —— 时钟、引脚、外设初始化，
  *            以及"把一段字节丢出去"这一个原语。
  *            缓冲、任务、格式化输出都属于应用层，不在本文件范围内。
  *
  *  引脚：PA9 = USART1_TX，接到板载 CH340。RX（PA10）不配置，保持默认状态。
  *
  *  与 CubeMX 的关系：本文件自带 GPIO 和时钟初始化，.ioc 里不需要勾 USART1。
  *        如果哪天在 CubeMX 里也开了 USART1，请删掉 uart.c 里的 GPIO 和时钟
  *        初始化，交给 HAL_UART_MspInit 做（两边都做不报错，只是多余）。
  ******************************************************************************
  */
#ifndef __UART_H
#define __UART_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/*====================== 底层配置（换板只改这一段） ========================*/
#define UART_INSTANCE               USART1
#define UART_BAUDRATE               115200U

#define UART_TX_GPIO_PORT           GPIOA
#define UART_TX_GPIO_PIN            GPIO_PIN_9
#define UART_TX_GPIO_CLK_ENABLE()   do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)

#define UART_CLK_ENABLE()           do{ __HAL_RCC_USART1_CLK_ENABLE(); }while(0)

/* USART1 TX uses DMA2 Stream7. */
#define UART_TX_DMA_INSTANCE         DMA2_Stream7
#define UART_TX_DMA_REQUEST          DMA_REQUEST_USART1_TX
#define UART_TX_DMA_IRQn             DMA2_Stream7_IRQn
#define UART_TX_DMA_BUFFER_SIZE       128U

/* Must be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY. */
#define UART_IRQ_PRIORITY            5U

/*========================== 接口 ==========================*/

/* 初始化：打开时钟、把 PA9 配成 USART1_TX、按 115200 8N1 初始化外设。
 * 返回 false 表示 HAL 报错，可查 g_uart_handle.ErrorCode。 */
bool uart_init(void);

/* DMA 发送一段数据；当前日志任务会在此阻塞等待最终 UART TC 或错误回调。
 * 仅允许 log_task 在调度器启动后调用，不能从 ISR 或其他任务直接调用。 */
uint16_t uart_write(const uint8_t *data, uint16_t len);

/* HAL 句柄 */
extern UART_HandleTypeDef g_uart_handle;
extern DMA_HandleTypeDef  g_uart_tx_dma_handle;

#endif /* __UART_H */
