/**
 ******************************************************************************
 * @file    bsp_dma2d.h
 * @brief   STM32H7 DMA2D 异步 RGB565 搬运和填充接口。
 ******************************************************************************
 */
#ifndef __BSP_DMA2D_H
#define __BSP_DMA2D_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* 必须不高于FreeRTOS允许调用FromISR API的最高逻辑优先级，项目可在编译参数中覆盖。 */
#ifndef BSP_DMA2D_IRQ_PRIORITY
#define BSP_DMA2D_IRQ_PRIORITY  5U
#endif

/*
 * DMA2D 总线访问节流：每次突发访问之间至少留出若干总线时钟，避免长时间
 * 挤占 LTDC 从 SDRAM 读取像素所需的带宽。设为 0 可关闭该功能。
 */
#ifndef BSP_DMA2D_AXI_DEAD_TIME
#define BSP_DMA2D_AXI_DEAD_TIME  0U
#endif

/**
 * @brief DMA2D 传输结束回调。
 * @note  回调在 DMA2D 中断上下文执行，只能使用 FromISR API 或做短操作。
 */
typedef void (*bsp_dma2d_callback_t)(void *context, bool success);

extern DMA2D_HandleTypeDef g_dma2d_handle; /**< HAL DMA2D句柄，供中断入口使用。 */

/** @brief 初始化 DMA2D 外设及其中断。 */
bool bsp_dma2d_init(void);

/** @return HAL 锁存的 DMA2D 错误码。 */
uint32_t bsp_dma2d_get_error_code(void);

/** @return true 表示 Cortex-M7 D-Cache 当前处于开启状态。 */
bool bsp_dma2d_is_data_cache_enabled(void);

/**
 * @brief 异步搬运连续的 RGB565 像素块。
 * @param destination_stride 目标缓冲相邻两行的像素步长，必须不小于width。
 * @note  DMA2D 为单实例；上一次回调到达前不能再次调用本接口。
 */
bool bsp_dma2d_copy_rgb565_async(const uint16_t *source,
                                 uint16_t *destination,
                                 uint16_t width,
                                 uint16_t height,
                                 uint16_t destination_stride,
                                 bsp_dma2d_callback_t callback,
                                 void *context);

/**
 * @brief 使用寄存器到存储器模式异步填充 RGB565 区域，目标每行可带尾部填充。
 * @note  该模式不读取源缓冲，适合清空帧缓冲和纯色背景。
 */
bool bsp_dma2d_fill_rgb565_async(uint16_t *destination,
                                 uint16_t width,
                                 uint16_t height,
                                 uint16_t destination_stride,
                                 uint16_t color,
                                 bsp_dma2d_callback_t callback,
                                 void *context);

#endif /* __BSP_DMA2D_H */
