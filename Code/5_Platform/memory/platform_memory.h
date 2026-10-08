/** @file platform_memory.h @brief Cortex-M7外部内存MPU属性配置。 */
#ifndef __PLATFORM_MEMORY_H
#define __PLATFORM_MEMORY_H

#include <stdint.h>

void platform_sdram_mpu_config(uint32_t base_address);

/**
 * @brief 将 D2 SRAM1 前 128 KiB 配置为 DMA 使用的不可缓存普通内存。
 * @note 与 STM32H743II.sct 的 RW_DMA_NOCACHE 区域一致，需在 DMA 使用前调用。
 */
void platform_dma_nocache_mpu_config(void);

#endif /* __PLATFORM_MEMORY_H */
