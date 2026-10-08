/**
 ******************************************************************************
 * @file    sdram.c
 * @brief   W9825G6KH-6 SDRAM设备初始化序列、刷新和自检
 ******************************************************************************
 */
#include "sdram.h"
#include "platform_memory.h"

static volatile uint32_t s_error_addr; /**< 最近一次自检首先出错的绝对地址。 */

/** @brief 执行SDRAM规定的时钟、预充电、自动刷新和MRS上电序列。 */
static bool sdram_power_up_sequence(void)
{
    FMC_SDRAM_CommandTypeDef command = {0};
    uint32_t mode;

    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command.AutoRefreshNumber = 1U;
    command.ModeRegisterDefinition = 0U;

    command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    if (!bsp_fmc_sdram_send_command(&command)) return false;
    HAL_Delay(1U);

    command.CommandMode = FMC_SDRAM_CMD_PALL;
    if (!bsp_fmc_sdram_send_command(&command)) return false;

    command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    command.AutoRefreshNumber = 8U;
    if (!bsp_fmc_sdram_send_command(&command)) return false;

    mode = SDRAM_MODEREG_BURST_LENGTH_1 |
           SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
           SDRAM_MODEREG_CAS_LATENCY |
           SDRAM_MODEREG_OPERATING_MODE_STANDARD |
           SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;
    command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    command.AutoRefreshNumber = 1U;
    command.ModeRegisterDefinition = mode;
    return bsp_fmc_sdram_send_command(&command);
}

/** @brief 配置MPU和FMC并完成W9825G6KH初始化。 */
bool sdram_init(void)
{
    bsp_fmc_sdram_config_t config = {0};
    FMC_SDRAM_TimingTypeDef timing = {0};

    config.column_bits = FMC_SDRAM_COLUMN_BITS_NUM_9;
    config.row_bits = FMC_SDRAM_ROW_BITS_NUM_13;
    config.data_width = FMC_SDRAM_MEM_BUS_WIDTH_16;
    config.internal_banks = FMC_SDRAM_INTERN_BANKS_NUM_4;
    config.cas_latency = SDRAM_CAS_LATENCY;
    config.clock_period = SDRAM_CLOCK_PERIOD;
    config.read_pipe_delay = SDRAM_READ_PIPE_DELAY;

    timing.LoadToActiveDelay = SDRAM_T_MRD_CYCLES;
    timing.ExitSelfRefreshDelay = SDRAM_T_XSR_CYCLES;
    timing.SelfRefreshTime = SDRAM_T_RAS_CYCLES;
    timing.RowCycleDelay = SDRAM_T_RC_CYCLES;
    timing.WriteRecoveryTime = SDRAM_T_WR_CYCLES;
    timing.RPDelay = SDRAM_T_RP_CYCLES;
    timing.RCDDelay = SDRAM_T_RCD_CYCLES;

    if (!bsp_fmc_sdram_init(&config, &timing)) return false;
    if (!sdram_power_up_sequence()) return false;
    if (!bsp_fmc_sdram_set_refresh(SDRAM_REFRESH_COUNT)) return false;

    /* FMC and SDRAM must be ready before opening the MPU window. */
    platform_sdram_mpu_config(SDRAM_BANK_ADDR);
    return true;
}

/** @brief 使用地址模式、棋盘格和位模式检测SDRAM。 */
bool sdram_selftest(void)
{
    volatile uint32_t *base = (volatile uint32_t *)SDRAM_BANK_ADDR;
    uint32_t words = SDRAM_SIZE_BYTES / 4UL;
    uint32_t step = SDRAM_SELFTEST_STRIDE / 4UL;
    uint32_t blocks = SDRAM_SELFTEST_BLOCK / 4UL;
    uint32_t i;
    uint32_t expect;
    static const uint32_t patterns[] =
    {
        0x00000000UL, 0xFFFFFFFFUL, 0xAAAAAAAAUL,
        0x55555555UL, 0xFFFF0000UL, 0x0000FFFFUL
    };

    s_error_addr = 0U;
    for (i = 0U; i < words; i += step) base[i] = i * 2654435761UL;
    for (i = 0U; i < words; i += step)
    {
        expect = i * 2654435761UL;
        if (base[i] != expect)
        {
            s_error_addr = SDRAM_BANK_ADDR + i * 4UL;
            return false;
        }
    }

    for (i = 0U; i < blocks; ++i)
        base[i] = ((i & 1U) != 0U) ? 0xAAAAAAAAUL : 0x55555555UL;
    for (i = 0U; i < blocks; ++i)
    {
        expect = ((i & 1U) != 0U) ? 0xAAAAAAAAUL : 0x55555555UL;
        if (base[i] != expect)
        {
            s_error_addr = SDRAM_BANK_ADDR + i * 4UL;
            return false;
        }
    }

    for (i = 0U; i < (sizeof(patterns) / sizeof(patterns[0])); ++i) base[i] = patterns[i];
    for (i = 0U; i < (sizeof(patterns) / sizeof(patterns[0])); ++i)
    {
        if (base[i] != patterns[i])
        {
            s_error_addr = SDRAM_BANK_ADDR + i * 4UL;
            return false;
        }
    }
    return true;
}

/** @brief 获取自检首先出错的绝对地址，0表示无错误。 */
uint32_t sdram_get_error_addr(void)
{
    return s_error_addr;
}
