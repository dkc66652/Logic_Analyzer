/**
 ******************************************************************************
 * @file    bsp_fmc_sdram.h
 * @brief   阿波罗 H743板级FMC SDRAM总线接口
 ******************************************************************************
 */
#ifndef __BSP_FMC_SDRAM_H
#define __BSP_FMC_SDRAM_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define BSP_FMC_SDRAM_BANK_ADDR 0xC0000000UL /**< FMC SDRAM Bank1默认映射起始地址。 */

typedef struct
{
    uint32_t column_bits;
    uint32_t row_bits;
    uint32_t data_width;
    uint32_t internal_banks;
    uint32_t cas_latency;
    uint32_t clock_period;
    uint32_t read_pipe_delay;
} bsp_fmc_sdram_config_t;

extern SDRAM_HandleTypeDef g_sdram_handle;

bool bsp_fmc_sdram_init(const bsp_fmc_sdram_config_t *config,
                        const FMC_SDRAM_TimingTypeDef *timing);
bool bsp_fmc_sdram_send_command(FMC_SDRAM_CommandTypeDef *command);
bool bsp_fmc_sdram_set_refresh(uint32_t refresh_count);

#endif /* __BSP_FMC_SDRAM_H */
