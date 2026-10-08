/** @file platform_init.c @brief 内核、时钟和平台时基初始化。 */

#include "platform_init.h"
#include "platform_config.h"

#include "main.h"
#include "platform_dwt.h"
#include "platform_memory.h"

void SystemClock_Config(void);
void MPU_Config(void);

/**
 * @brief Platform_Init：芯片基础初始化。
 */
bool Platform_Init(void)
{
    MPU_Config();
    platform_dma_nocache_mpu_config();
    if (HAL_Init() != HAL_OK) {
        return false;
    }

    SystemClock_Config();
    if (SystemCoreClock != CPU_CYCLES_HZ) {
        return false;
    }
    return platform_dwt_init();
}
