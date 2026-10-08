/** @file platform_memory.c @brief Cortex-M7外部内存MPU属性配置。 */
#include "platform_memory.h"
#include "stm32h7xx_hal.h"

/**
 * @brief 配置与链接脚本 RW_DMA_NOCACHE 对应的 SRAM1 MPU 区域。
 * @note 该区域在 SystemInit 开启 D2 SRAM 时钟后可用，DMA 缓冲的内容由使用者写入。
 */
void platform_dma_nocache_mpu_config(void)
{
    MPU_Region_InitTypeDef mpu = {0};

    HAL_MPU_Disable();
    mpu.Enable = MPU_REGION_ENABLE;
    mpu.Number = MPU_REGION_NUMBER2;
    mpu.BaseAddress = 0x30000000U;
    mpu.Size = MPU_REGION_SIZE_32KB;
    mpu.SubRegionDisable = 0x00U;
    mpu.TypeExtField = MPU_TEX_LEVEL1;
    mpu.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    mpu.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    mpu.IsShareable = MPU_ACCESS_SHAREABLE;
    mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&mpu);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
 * @brief platform_sdram_mpu_config：外部 SDRAM MPU 属性配置。
 */
void platform_sdram_mpu_config(uint32_t base_address)
{
    MPU_Region_InitTypeDef mpu = {0};

    HAL_MPU_Disable();
    mpu.Enable = MPU_REGION_ENABLE;
    mpu.Number = MPU_REGION_NUMBER1;
    mpu.BaseAddress = base_address;
    mpu.Size = MPU_REGION_SIZE_32MB;
    mpu.SubRegionDisable = 0x00U;
    mpu.TypeExtField = MPU_TEX_LEVEL1;
    mpu.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    mpu.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    mpu.IsShareable = MPU_ACCESS_SHAREABLE;
    mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&mpu);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
