/**
 ******************************************************************************
 * @file    bsp_fmc_sdram.c
 * @brief   阿波罗 H743板载SDRAM的FMC时钟、GPIO和控制器实现
 ******************************************************************************
 */
#include "bsp_fmc_sdram.h"

SDRAM_HandleTypeDef g_sdram_handle; /**< HAL FMC SDRAM外设句柄。 */

/** 同一GPIO端口上的一组FMC复用引脚。 */
typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pins;
} bsp_fmc_pin_group_t;

/** 当前PCB上SDRAM连接使用的全部FMC引脚。 */
static const bsp_fmc_pin_group_t s_sdram_pins[] =
{
    { GPIOC, GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_3 },
    { GPIOD, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
             GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15 },
    { GPIOE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
             GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
             GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15 },
    { GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
             GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
             GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15 },
    { GPIOG, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 |
             GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15 },
    { NULL, 0U }
};

/** @brief 开启FMC和GPIO时钟并把SDRAM引脚配置为AF12。 */
static void bsp_fmc_sdram_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t i;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_FMC_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;
    for (i = 0U; s_sdram_pins[i].port != NULL; ++i)
    {
        gpio.Pin = s_sdram_pins[i].pins;
        HAL_GPIO_Init(s_sdram_pins[i].port, &gpio);
    }
}

/** @brief 按Device层提供的几何和时序参数初始化FMC SDRAM Bank1。 */
bool bsp_fmc_sdram_init(const bsp_fmc_sdram_config_t *config,
                        const FMC_SDRAM_TimingTypeDef *timing)
{
    if (config == NULL || timing == NULL) return false;
    bsp_fmc_sdram_gpio_init();

    g_sdram_handle.Instance = FMC_Bank5_6_R;
    g_sdram_handle.Init.SDBank = FMC_SDRAM_BANK1;
    g_sdram_handle.Init.ColumnBitsNumber = config->column_bits;
    g_sdram_handle.Init.RowBitsNumber = config->row_bits;
    g_sdram_handle.Init.MemoryDataWidth = config->data_width;
    g_sdram_handle.Init.InternalBankNumber = config->internal_banks;
    g_sdram_handle.Init.CASLatency = config->cas_latency;
    g_sdram_handle.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    g_sdram_handle.Init.SDClockPeriod = config->clock_period;
    g_sdram_handle.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
    g_sdram_handle.Init.ReadPipeDelay = config->read_pipe_delay;
    return (HAL_SDRAM_Init(&g_sdram_handle, (FMC_SDRAM_TimingTypeDef *)timing) == HAL_OK);
}

/** @brief 通过FMC向外部SDRAM发送一条命令。 */
bool bsp_fmc_sdram_send_command(FMC_SDRAM_CommandTypeDef *command)
{
    return (command != NULL) &&
           (HAL_SDRAM_SendCommand(&g_sdram_handle, command, 0x1000U) == HAL_OK);
}

/** @brief 设置FMC SDRAM自动刷新计数值。 */
bool bsp_fmc_sdram_set_refresh(uint32_t refresh_count)
{
    return (HAL_SDRAM_ProgramRefreshRate(&g_sdram_handle, refresh_count) == HAL_OK);
}
