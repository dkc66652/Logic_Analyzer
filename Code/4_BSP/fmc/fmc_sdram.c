/**
 * @file fmc_sdram.c
 * @brief 板载FMC SDRAM接口实现
 */

/* ================================================== */

#include "fmc/fmc_sdram.h"
#include "bsp_fmc_sdram.h"
#include "mdma.h"
#include "stm32h7xx_hal.h"

/* ================================================== */

#define BSP_FMC_SDRAM_COMMAND_TIMEOUT       0xFFFFUL
#define BSP_FMC_SDRAM_CLOCK_DIVIDER         3UL
#define BSP_FMC_SDRAM_REFRESH_MARGIN        20UL
#define BSP_FMC_SDRAM_REFRESH_MIN_COUNT     41UL
#define BSP_FMC_SDRAM_REFRESH_MAX_COUNT     0x1FFFUL
#define BSP_FMC_SDRAM_ABORT_TIMEOUT_MS      5UL

/* ================================================== */

static uint8_t bsp_fmc_sdram_initialized = 0U;
static BSP_FMC_SDRAM_MDMA_Callback_t bsp_fmc_sdram_mdma_callback = NULL;
static void *bsp_fmc_sdram_mdma_context = NULL;

/* ================================================== */

static void BSP_FMC_SDRAM_MDMA_Complete_Callback(MDMA_HandleTypeDef *mdma)
{
    (void)mdma;

    if (bsp_fmc_sdram_mdma_callback != NULL)
    {
        bsp_fmc_sdram_mdma_callback(BSP_FMC_SDRAM_OK,
                                    bsp_fmc_sdram_mdma_context);
    }
}

static void BSP_FMC_SDRAM_MDMA_Error_Callback(MDMA_HandleTypeDef *mdma)
{
    (void)mdma;

    if (bsp_fmc_sdram_mdma_callback != NULL)
    {
        bsp_fmc_sdram_mdma_callback(BSP_FMC_SDRAM_ERROR_TRANSFER,
                                    bsp_fmc_sdram_mdma_context);
    }
}

/* ================================================== */

/**
 * @brief 初始化板载FMC SDRAM控制器和GPIO
 * @retval BSP_FMC_SDRAM_Result_t
 */
BSP_FMC_SDRAM_Result_t BSP_FMC_SDRAM_Init(void)
{
    if (bsp_fmc_sdram_initialized != 0U)
    {
        return BSP_FMC_SDRAM_OK;
    }

    /* FMC和SDRAM由GUI Device层初始化；这里接管同事服务需要的MDMA接口。 */
    if ((g_sdram_handle.Init.SDBank != FMC_SDRAM_BANK1) ||
        (g_sdram_handle.State != HAL_SDRAM_STATE_READY))
    {
        return BSP_FMC_SDRAM_ERROR_CONFIGURATION;
    }

    hmdma_mdma_channel0_sw_0.XferCpltCallback =
                                    BSP_FMC_SDRAM_MDMA_Complete_Callback;
    hmdma_mdma_channel0_sw_0.XferErrorCallback =
                                    BSP_FMC_SDRAM_MDMA_Error_Callback;

    bsp_fmc_sdram_initialized = 1U;
    return BSP_FMC_SDRAM_OK;
}

/**
 * @brief 设置SDRAM MDMA传输完成回调
 * @param callback 传输完成回调，NULL表示关闭回调
 * @param context 回调上下文
 * @retval 无
 */
void BSP_FMC_SDRAM_Set_MDMA_Callback(
        BSP_FMC_SDRAM_MDMA_Callback_t callback,
        void *context)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    bsp_fmc_sdram_mdma_callback = callback;
    bsp_fmc_sdram_mdma_context = context;
    __set_PRIMASK(primask);
}

/**
 * @brief 启动一次内存到SDRAM的MDMA传输
 * @note source和destination必须按uint32_t对齐，完成结果通过回调上报
 * @param source 源数据地址
 * @param destination SDRAM目标地址
 * @param count uint32_t数据数量
 * @retval BSP_FMC_SDRAM_Result_t
 */
BSP_FMC_SDRAM_Result_t BSP_FMC_SDRAM_Start_MDMA_Write(
        const uint32_t *source,
        uint32_t *destination,
        uint16_t count)
{
    HAL_StatusTypeDef hal_result;

    if ((source == NULL) || (destination == NULL) || (count == 0U))
    {
        return BSP_FMC_SDRAM_ERROR_PARAMETER;
    }
    if (bsp_fmc_sdram_initialized == 0U)
    {
        return BSP_FMC_SDRAM_ERROR_STATE;
    }

    hal_result = HAL_MDMA_Start_IT(&hmdma_mdma_channel0_sw_0,
                                   (uint32_t)source,
                                   (uint32_t)destination,
                                   (uint32_t)count * sizeof(uint32_t),
                                   1U);
    if (hal_result == HAL_BUSY)
    {
        return BSP_FMC_SDRAM_ERROR_BUSY;
    }
    if (hal_result != HAL_OK)
    {
        return BSP_FMC_SDRAM_ERROR_TRANSFER;
    }

    return BSP_FMC_SDRAM_OK;
}

/**
 * @brief 终止MDMA并确认目标区域不再被硬件写入
 * @note 必须在任务上下文调用，HAL使用TIM6时间基准限制停止等待。
 * @retval BSP_FMC_SDRAM_OK-已停止，其他值-不能保证硬件停稳
 */
BSP_FMC_SDRAM_Result_t BSP_FMC_SDRAM_Abort_MDMA_Write(void)
{
    MDMA_HandleTypeDef *mdma = &hmdma_mdma_channel0_sw_0;

    if (bsp_fmc_sdram_initialized == 0U)
    {
        return BSP_FMC_SDRAM_ERROR_STATE;
    }
    if (mdma->State == HAL_MDMA_STATE_BUSY)
    {
        if (HAL_MDMA_Abort(mdma) != HAL_OK)
        {
            return BSP_FMC_SDRAM_ERROR_TRANSFER;
        }
    }
    else if (mdma->State == HAL_MDMA_STATE_ERROR)
    {
        uint32_t start_tick = HAL_GetTick();

        __HAL_MDMA_DISABLE(mdma);
        while ((mdma->Instance->CCR & MDMA_CCR_EN) != 0U)
        {
            if ((HAL_GetTick() - start_tick) >= BSP_FMC_SDRAM_ABORT_TIMEOUT_MS)
            {
                return BSP_FMC_SDRAM_ERROR_TRANSFER;
            }
        }
        if (HAL_MDMA_Init(mdma) != HAL_OK)
        {
            return BSP_FMC_SDRAM_ERROR_TRANSFER;
        }
    }
    if (((mdma->Instance->CCR & MDMA_CCR_EN) != 0U) ||
        (mdma->State != HAL_MDMA_STATE_READY))
    {
        return BSP_FMC_SDRAM_ERROR_TRANSFER;
    }
    __DSB();
    return BSP_FMC_SDRAM_OK;
}

/**
 * @brief 向板载SDRAM发送FMC控制命令
 * @param command 板级抽象命令
 * @param auto_refresh_count 连续自动刷新次数，其他命令传1
 * @param mode_register LOAD MODE命令使用的模式寄存器值
 * @retval BSP_FMC_SDRAM_Result_t
 */
BSP_FMC_SDRAM_Result_t BSP_FMC_SDRAM_Send_Command(
        BSP_FMC_SDRAM_Command_t command,
        uint32_t auto_refresh_count,
        uint32_t mode_register)
{
    FMC_SDRAM_CommandTypeDef hal_command = {0};

    if (bsp_fmc_sdram_initialized == 0U)
    {
        return BSP_FMC_SDRAM_ERROR_STATE;
    }

    if ((auto_refresh_count == 0U) || (auto_refresh_count > 16U))
    {
        return BSP_FMC_SDRAM_ERROR_PARAMETER;
    }

    switch (command)
    {
        case BSP_FMC_SDRAM_COMMAND_CLOCK_ENABLE:
            hal_command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
            break;

        case BSP_FMC_SDRAM_COMMAND_PRECHARGE_ALL:
            hal_command.CommandMode = FMC_SDRAM_CMD_PALL;
            break;

        case BSP_FMC_SDRAM_COMMAND_AUTO_REFRESH:
            hal_command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
            break;

        case BSP_FMC_SDRAM_COMMAND_LOAD_MODE:
            hal_command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
            break;

        default:
            return BSP_FMC_SDRAM_ERROR_PARAMETER;
    }

    hal_command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    hal_command.AutoRefreshNumber = auto_refresh_count;
    hal_command.ModeRegisterDefinition = mode_register;

    if (HAL_SDRAM_SendCommand(&g_sdram_handle,
                              &hal_command,
                              BSP_FMC_SDRAM_COMMAND_TIMEOUT) != HAL_OK)
    {
        return BSP_FMC_SDRAM_ERROR_COMMAND;
    }

    return BSP_FMC_SDRAM_OK;
}

/**
 * @brief 根据器件刷新要求计算并设置FMC刷新计数器
 * @param refresh_period_us 完成全部行刷新的最长周期，单位us
 * @param row_count SDRAM刷新行数
 * @retval BSP_FMC_SDRAM_Result_t
 */
BSP_FMC_SDRAM_Result_t BSP_FMC_SDRAM_Program_Refresh(
        uint32_t refresh_period_us,
        uint32_t row_count)
{
    uint32_t sdram_clock_hz;
    uint64_t cycles_per_row;
    uint32_t refresh_count;

    if (bsp_fmc_sdram_initialized == 0U)
    {
        return BSP_FMC_SDRAM_ERROR_STATE;
    }

    if ((refresh_period_us == 0U) || (row_count == 0U))
    {
        return BSP_FMC_SDRAM_ERROR_PARAMETER;
    }

    sdram_clock_hz = HAL_RCC_GetHCLKFreq() / BSP_FMC_SDRAM_CLOCK_DIVIDER;
    cycles_per_row = ((uint64_t)sdram_clock_hz * refresh_period_us) /
                     (1000000ULL * row_count);

    if (cycles_per_row <= BSP_FMC_SDRAM_REFRESH_MARGIN)
    {
        return BSP_FMC_SDRAM_ERROR_CONFIGURATION;
    }

    refresh_count = (uint32_t)(cycles_per_row - BSP_FMC_SDRAM_REFRESH_MARGIN);
    if ((refresh_count < BSP_FMC_SDRAM_REFRESH_MIN_COUNT) ||
        (refresh_count > BSP_FMC_SDRAM_REFRESH_MAX_COUNT))
    {
        return BSP_FMC_SDRAM_ERROR_CONFIGURATION;
    }

    if (HAL_SDRAM_ProgramRefreshRate(&g_sdram_handle, refresh_count) != HAL_OK)
    {
        return BSP_FMC_SDRAM_ERROR_REFRESH;
    }

    return BSP_FMC_SDRAM_OK;
}
