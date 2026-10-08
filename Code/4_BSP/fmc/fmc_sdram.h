/**
 * @file fmc_sdram.h
 * @brief 板载FMC SDRAM接口
 * @note 隔离CubeMX生成的FMC句柄和STM32 HAL类型
 */

#ifndef FMC_SDRAM_H
#define FMC_SDRAM_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdint.h>

/* ================================================== */

typedef enum
{
    BSP_FMC_SDRAM_OK = 0,
    BSP_FMC_SDRAM_ERROR_PARAMETER,
    BSP_FMC_SDRAM_ERROR_STATE,
    BSP_FMC_SDRAM_ERROR_COMMAND,
    BSP_FMC_SDRAM_ERROR_REFRESH,
    BSP_FMC_SDRAM_ERROR_CONFIGURATION,
    BSP_FMC_SDRAM_ERROR_BUSY,
    BSP_FMC_SDRAM_ERROR_TRANSFER
} BSP_FMC_SDRAM_Result_t;

typedef enum
{
    BSP_FMC_SDRAM_COMMAND_CLOCK_ENABLE = 0,
    BSP_FMC_SDRAM_COMMAND_PRECHARGE_ALL,
    BSP_FMC_SDRAM_COMMAND_AUTO_REFRESH,
    BSP_FMC_SDRAM_COMMAND_LOAD_MODE
} BSP_FMC_SDRAM_Command_t;

/* 回调在MDMA中断上下文执行 */
typedef void (*BSP_FMC_SDRAM_MDMA_Callback_t)(
        BSP_FMC_SDRAM_Result_t result,
        void *context);

/* ================================================== */

BSP_FMC_SDRAM_Result_t BSP_FMC_SDRAM_Init(void);
BSP_FMC_SDRAM_Result_t BSP_FMC_SDRAM_Send_Command(
        BSP_FMC_SDRAM_Command_t command,
        uint32_t auto_refresh_count,
        uint32_t mode_register);
BSP_FMC_SDRAM_Result_t BSP_FMC_SDRAM_Program_Refresh(
        uint32_t refresh_period_us,
        uint32_t row_count);
void BSP_FMC_SDRAM_Set_MDMA_Callback(
        BSP_FMC_SDRAM_MDMA_Callback_t callback,
        void *context);
BSP_FMC_SDRAM_Result_t BSP_FMC_SDRAM_Start_MDMA_Write(
        const uint32_t *source,
        uint32_t *destination,
        uint16_t count);
/* 任务上下文调用；返回成功保证MDMA已禁用且不会继续写入目标区域。 */
BSP_FMC_SDRAM_Result_t BSP_FMC_SDRAM_Abort_MDMA_Write(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* FMC_SDRAM_H */
