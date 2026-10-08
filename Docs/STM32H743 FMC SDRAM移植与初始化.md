# STM32H743 FMC SDRAM 移植与初始化记录

本文记录阿波罗 STM32H743 开发板上 W9825G6KH-6 SDRAM 的移植和初始化流程。本工程使用 STM32 HAL、FMC、Cortex-M7 MPU、FreeRTOS，并将外部 SDRAM 用作 LTDC 帧缓冲等大容量数据存储区域。

本文代码对应当前工程目录：

```text
GUI/
├─ Code/
│  ├─ 3-Device/
│  │  ├─ sdram.c                 # W9825G6KH 参数、上电序列和自检
│  │  └─ sdram.h
│  ├─ 4-Bsp/
│  │  ├─ bsp_fmc_sdram.c         # FMC、GPIO 和 HAL 操作
│  │  └─ bsp_fmc_sdram.h
│  └─ 5-Platform/
│     ├─ platform_memory.c       # Cortex-M7 MPU 属性配置
│     └─ platform_memory.h
└─ Core/Src/main.c               # 上电初始化入口
```

参考资料：

- 正点原子阿波罗 H743 `实验15 LTDC LCD（RGB屏）实验/Drivers/BSP/SDRAM`；
- [ST 官方 STM32H743I-EVAL FMC SDRAM 示例](https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H743I-EVAL/Examples/FMC/FMC_SDRAM)；
- STM32H743 参考手册 RM0433 的 FMC 章节；
- W9825G6KH-6 数据手册。

> ST 官方开发板使用的 SDRAM 型号、Bank、总线宽度和时钟可能与本工程不同，因此只能参考初始化流程，不能直接照抄其中的参数。

## 1. 明确初始化流程

SDRAM 不能像片内 SRAM 一样上电后立即读写。完整流程如下：

1. 确认 SDRAM 容量、行列地址、内部 Bank 和数据总线宽度；
2. 配置 SDRAM 地址空间的 MPU 属性；
3. 开启 FMC、GPIO 时钟并配置 FMC 复用引脚；
4. 配置 FMC SDRAM 控制器和访问时序；
5. 使能 SDRAM 时钟并等待至少 100 μs；
6. 对全部 SDRAM Bank 执行预充电；
7. 执行 8 次自动刷新；
8. 写入 SDRAM 模式寄存器；
9. 设置 FMC 自动刷新计数器；
10. 写入并读回测试数据，确认 SDRAM 工作正常。

FreeRTOS 不负责初始化 SDRAM。应当在创建任务或启动调度器之前完成上述操作。

## 2. 确认器件参数和地址映射

本工程使用 W9825G6KH-6，主要参数如下：

| 参数 | 本工程配置 | 含义 |
| --- | --- | --- |
| 行地址 | 13 位 | `2^13 = 8192` 行 |
| 列地址 | 9 位 | `2^9 = 512` 列 |
| 内部 Bank | 4 个 | SDRAM 芯片内部存储 Bank |
| 数据总线 | 16 位 | 每个地址单元为 2 字节 |
| 总容量 | 32 MiB | `8192 × 512 × 4 × 2` 字节 |
| FMC SDRAM Bank | Bank1 | 映射起始地址为 `0xC0000000` |

在 `Code/4-Bsp/bsp_fmc_sdram.h` 中定义 FMC 映射地址：

```c
#define BSP_FMC_SDRAM_BANK_ADDR 0xC0000000UL
```

在 `Code/3-Device/sdram.h` 中定义具体器件容量：

```c
#define SDRAM_BANK_ADDR  BSP_FMC_SDRAM_BANK_ADDR
#define SDRAM_SIZE_BYTES (32UL * 1024UL * 1024UL)
```

`0xC0000000` 是 STM32H743 FMC SDRAM Bank1 的固定映射起始地址。FMC 配置行数、列数和数据宽度，但不会自动检测外部芯片容量，因此容量仍需由软件按照实际芯片定义和管理。

## 3. 配置 SDRAM 区域的 MPU 属性

STM32H743 使用 Cortex-M7 内核并带有 D-Cache。CPU、LTDC 或 DMA 同时访问 SDRAM 时，如果缓存属性处理不当，可能出现 CPU 已经写入而 LTDC/DMA 仍读取旧数据的问题。

当前工程先把完整 32 MiB SDRAM 配置为不可缓存、不可执行、可读写区域，以优先保证功能正确：

```c
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
```

在 SDRAM 初始化入口中调用：

```c
platform_sdram_mpu_config(SDRAM_BANK_ADDR);
```

不可缓存配置简单可靠，但 CPU 访问速度可能低于启用缓存的配置。以后使用 DMA2D 或进行性能优化时，可以重新规划缓存区域，但必须同时正确维护 Cache 一致性。

## 4. 配置 FMC 时钟和 GPIO

SDRAM 的地址线、数据线、片选、时钟和控制信号都连接到 FMC 复用引脚。该部分与 PCB 走线直接相关，因此归入 BSP 层。

首先按照 GPIO 端口保存当前开发板使用的 FMC 引脚：

```c
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
```

然后开启 FMC 和各 GPIO 端口时钟，并将引脚配置为 AF12：

```c
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
```

如果更换开发板，应先根据原理图检查这里，而不是直接复制其他开发板的引脚表。

## 5. 配置器件几何参数和访问时序

### 5.1 配置器件几何参数

Device 层根据 W9825G6KH-6 的组织结构填写配置：

```c
bsp_fmc_sdram_config_t config = {0};

config.column_bits = FMC_SDRAM_COLUMN_BITS_NUM_9;
config.row_bits = FMC_SDRAM_ROW_BITS_NUM_13;
config.data_width = FMC_SDRAM_MEM_BUS_WIDTH_16;
config.internal_banks = FMC_SDRAM_INTERN_BANKS_NUM_4;
config.cas_latency = SDRAM_CAS_LATENCY;
config.clock_period = SDRAM_CLOCK_PERIOD;
config.read_pipe_delay = SDRAM_READ_PIPE_DELAY;
```

这些是外部 SDRAM 芯片的属性，不是 FreeRTOS 配置。

### 5.2 将纳秒时序换算为 SDRAM 时钟周期

当前工程设定 FMC 内核时钟为 240 MHz，经过三分频后得到 80 MHz SDRAM 时钟：

```c
#define SDRAM_HCLK3_HZ   240000000UL
#define SDRAM_SDCLK_DIV  3U
#define SDRAM_SDCLK_HZ   (SDRAM_HCLK3_HZ / SDRAM_SDCLK_DIV)
```

数据手册通常以纳秒给出最小时序，FMC 则要求填写时钟周期数，因此使用向上取整换算：

```c
#define SDRAM_NS_TO_CYCLES(ns) \
    ((((uint32_t)(ns) * (SDRAM_SDCLK_HZ / 1000000UL)) + 999UL) / 1000UL)

#define SDRAM_T_MRD_CYCLES  2U
#define SDRAM_T_XSR_CYCLES  SDRAM_NS_TO_CYCLES(72U)
#define SDRAM_T_RAS_CYCLES  SDRAM_NS_TO_CYCLES(42U)
#define SDRAM_T_RC_CYCLES   SDRAM_NS_TO_CYCLES(60U)
#define SDRAM_T_WR_CYCLES   SDRAM_NS_TO_CYCLES(14U)
#define SDRAM_T_RP_CYCLES   SDRAM_NS_TO_CYCLES(18U)
#define SDRAM_T_RCD_CYCLES  SDRAM_NS_TO_CYCLES(18U)
```

将换算结果填入 HAL 时序结构：

```c
FMC_SDRAM_TimingTypeDef timing = {0};

timing.LoadToActiveDelay = SDRAM_T_MRD_CYCLES;
timing.ExitSelfRefreshDelay = SDRAM_T_XSR_CYCLES;
timing.SelfRefreshTime = SDRAM_T_RAS_CYCLES;
timing.RowCycleDelay = SDRAM_T_RC_CYCLES;
timing.WriteRecoveryTime = SDRAM_T_WR_CYCLES;
timing.RPDelay = SDRAM_T_RP_CYCLES;
timing.RCDDelay = SDRAM_T_RCD_CYCLES;
```

修改系统时钟或 FMC 时钟源后，必须同步检查 `SDRAM_HCLK3_HZ`、分频、时序周期和刷新计数。

### 5.3 初始化 FMC 控制器

BSP 层接收 Device 层提供的参数并调用 HAL：

```c
bool bsp_fmc_sdram_init(const bsp_fmc_sdram_config_t *config,
                        const FMC_SDRAM_TimingTypeDef *timing)
{
    if (config == NULL || timing == NULL)
    {
        return false;
    }

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

    return HAL_SDRAM_Init(&g_sdram_handle,
                          (FMC_SDRAM_TimingTypeDef *)timing) == HAL_OK;
}
```

到这里完成的是 STM32 FMC 控制器配置，外部 SDRAM 芯片本身仍需执行规定的上电命令序列。

## 6. 执行 SDRAM 上电命令序列

### 6.1 使能 SDRAM 时钟并等待稳定

```c
command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
command.AutoRefreshNumber = 1U;
command.ModeRegisterDefinition = 0U;

command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
if (!bsp_fmc_sdram_send_command(&command))
{
    return false;
}

HAL_Delay(1U);
```

数据手册要求时钟稳定后至少等待 100 μs。这里等待 1 ms，满足最小时间要求。初始化发生在 FreeRTOS 调度器启动之前，此处使用的是 HAL 的毫秒时基，不需要 `vTaskDelay()`。

### 6.2 预充电全部 Bank

```c
command.CommandMode = FMC_SDRAM_CMD_PALL;
if (!bsp_fmc_sdram_send_command(&command))
{
    return false;
}
```

`PALL` 表示 Precharge All，为后续刷新和正常访问准备所有内部 Bank。

### 6.3 执行 8 次自动刷新

```c
command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
command.AutoRefreshNumber = 8U;
if (!bsp_fmc_sdram_send_command(&command))
{
    return false;
}
```

自动刷新次数来自 SDRAM 初始化规范。该操作只负责上电阶段的初始化刷新，运行期间仍需要 FMC 自动周期刷新。

### 6.4 写入模式寄存器

```c
mode = SDRAM_MODEREG_BURST_LENGTH_1 |
       SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
       SDRAM_MODEREG_CAS_LATENCY |
       SDRAM_MODEREG_OPERATING_MODE_STANDARD |
       SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
command.AutoRefreshNumber = 1U;
command.ModeRegisterDefinition = mode;

if (!bsp_fmc_sdram_send_command(&command))
{
    return false;
}
```

模式寄存器决定突发长度、突发类型、CAS 延迟、工作模式和写突发方式。这里的 CAS 配置必须与 FMC 控制器的 `CASLatency` 保持一致。

完整函数如下：

```c
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
```

## 7. 设置运行期间的自动刷新

SDRAM 使用电容保存数据，即使没有 CPU 访问，也必须不断刷新。W9825G6KH 有 8192 行，要求在 64 ms 内完成全部行刷新。

当前工程使用下式计算 FMC 刷新计数器：

```c
#define SDRAM_ROWS               8192U
#define SDRAM_REFRESH_PERIOD_MS  64U

#define SDRAM_REFRESH_COUNT \
    ((uint32_t)((((uint64_t)SDRAM_REFRESH_PERIOD_MS * \
                   (uint64_t)SDRAM_SDCLK_HZ) / 1000ULL) / \
                   (uint64_t)SDRAM_ROWS) - 20ULL)
```

80 MHz SDRAM 时钟下，计算结果约为：

```text
64 ms × 80 MHz ÷ 8192 - 20 = 605
```

最后写入 FMC 刷新寄存器：

```c
return bsp_fmc_sdram_set_refresh(SDRAM_REFRESH_COUNT);
```

BSP 层对应实现：

```c
bool bsp_fmc_sdram_set_refresh(uint32_t refresh_count)
{
    return HAL_SDRAM_ProgramRefreshRate(&g_sdram_handle,
                                        refresh_count) == HAL_OK;
}
```

刷新计数设置错误可能表现为刚启动时正常，运行一段时间后数据随机损坏。

## 8. 组合完整初始化入口

Device 层把 MPU、FMC、上电命令和周期刷新组合成一个完整接口：

```c
bool sdram_init(void)
{
    bsp_fmc_sdram_config_t config = {0};
    FMC_SDRAM_TimingTypeDef timing = {0};

    platform_sdram_mpu_config(SDRAM_BANK_ADDR);

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

    if (!bsp_fmc_sdram_init(&config, &timing))
    {
        return false;
    }

    if (!sdram_power_up_sequence())
    {
        return false;
    }

    return bsp_fmc_sdram_set_refresh(SDRAM_REFRESH_COUNT);
}
```

该函数成功返回后，CPU 才能将 `0xC0000000` 开始的地址空间当作普通内存访问。

## 9. 增加 SDRAM 自检

初始化函数返回成功只代表 HAL 命令执行完成，并不能证明所有地址线、数据线和器件参数都正确。因此应在首次移植和修改时钟后执行读写自检。

当前工程包含三类检查：

1. 跨步地址模式：覆盖整片存储器的行、列和 Bank 组合；
2. 棋盘格模式：检测相邻数据位粘连；
3. 固定位模式：检测某些数据位恒为 0 或恒为 1。

核心结构如下：

```c
bool sdram_selftest(void)
{
    volatile uint32_t *base = (volatile uint32_t *)SDRAM_BANK_ADDR;
    uint32_t words = SDRAM_SIZE_BYTES / 4UL;
    uint32_t step = SDRAM_SELFTEST_STRIDE / 4UL;
    uint32_t i;
    uint32_t expect;

    s_error_addr = 0U;

    for (i = 0U; i < words; i += step)
    {
        base[i] = i * 2654435761UL;
    }

    for (i = 0U; i < words; i += step)
    {
        expect = i * 2654435761UL;
        if (base[i] != expect)
        {
            s_error_addr = SDRAM_BANK_ADDR + i * 4UL;
            return false;
        }
    }

    /* 工程源码中随后还会执行棋盘格和固定位模式测试。 */
    return true;
}
```

出现错误时可以获取首先失败的绝对地址：

```c
uint32_t error_address = sdram_get_error_addr();
```

自检会覆盖测试区域原有数据，因此不能在 LTDC 已开始显示、LVGL 已分配缓冲区或其他任务正在使用 SDRAM 时执行。

## 10. 在 main 中初始化并与 FreeRTOS 配合

当前工程在启动 FreeRTOS 之前初始化并自检 SDRAM：

```c
#include "sdram.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* 初始化工程中的其他基础外设。 */

    if (!sdram_init() || !sdram_selftest())
    {
        Error_Handler();
    }

    if (lcd_init() != LCD_STATUS_OK)
    {
        Error_Handler();
    }

    /* 创建任务并启动FreeRTOS调度器。 */
}
```

推荐顺序为：

```text
HAL和系统时钟
      ↓
SDRAM初始化与自检
      ↓
LTDC/LCD初始化
      ↓
LVGL显示驱动和缓冲区
      ↓
创建FreeRTOS任务
      ↓
启动调度器
```

FreeRTOS 与 SDRAM 初始化没有额外的专用接口。初始化完成后，SDRAM 对任务来说就是普通内存，但仍需注意以下情况：

- 如果将 FreeRTOS 堆或任务栈放入 SDRAM，必须保证分配任务内存之前 SDRAM 已可用；
- 不要把依赖 C 运行库在进入 `main()` 前自动初始化的数据直接放入尚未初始化的 SDRAM；
- 多个任务共享 SDRAM 中的数据结构时，仍需使用互斥锁或其他同步机制；
- LTDC、DMA2D、DMA 与 CPU 共享缓冲区时，必须考虑 MPU 和 D-Cache 一致性。

## 11. Keil 工程分组和包含路径

建议在 Keil 中按照实际分层加入文件：

```text
Code/Device
├─ sdram.c
└─ sdram.h

Code/Bsp
├─ bsp_fmc_sdram.c
└─ bsp_fmc_sdram.h

Code/Platform
├─ platform_memory.c
└─ platform_memory.h
```

并确保 C/C++ Include Paths 中包含：

```text
../Code/3-Device
../Code/4-Bsp
../Code/5-Platform
```

头文件的推荐依赖关系如下：

```text
main.c
  └─ sdram.h
       └─ bsp_fmc_sdram.h

sdram.c
  ├─ sdram.h
  └─ platform_memory.h
```

应用层只需要调用 `sdram_init()`、`sdram_selftest()` 和 `sdram_get_error_addr()`，不应直接操作 `g_sdram_handle` 或发送 FMC 命令。

## 12. 移植时必须重新核对的内容

以下内容不能因为示例能编译就默认正确：

- 开发板原理图上的 FMC 引脚是否与 `s_sdram_pins` 一致；
- SDRAM 型号、行列数量、内部 Bank 数和数据宽度；
- FMC 内核实际时钟是否真的是 `SDRAM_HCLK3_HZ`；
- SDRAM 时钟分频与 `SDRAM_CLOCK_PERIOD` 是否一致；
- 各项纳秒时序是否满足当前 SDRAM 数据手册；
- CAS 延迟在 FMC 和 SDRAM 模式寄存器中是否一致；
- 刷新行数、刷新周期和刷新计数；
- MPU 区域大小、基地址以及 Cache 属性；
- SDRAM Bank 选择与 `0xC0000000` 地址映射是否一致；
- 初始化是否发生在 LTDC、LVGL、DMA 和 FreeRTOS 使用 SDRAM之前。

## 13. 常见故障现象

| 现象 | 优先检查 |
| --- | --- |
| 一访问 `0xC0000000` 就进入 HardFault | FMC/GPIO 未初始化、地址错误、MPU 权限错误 |
| 少量读写正确，大块数据错误 | 行列位数、总线宽度、地址线或时序错误 |
| 刚启动正常，运行一段时间后数据损坏 | 刷新计数错误、时钟不匹配 |
| 屏幕撕裂或显示旧画面 | D-Cache 与 LTDC/DMA 数据不一致 |
| 某些颜色位始终错误 | SDRAM 数据线、焊接或数据宽度配置错误 |
| 修改系统时钟后才出现随机错误 | SDRAM 时序周期和刷新值没有重新计算 |

## 14. 需要掌握到什么程度

进行 MCU + FreeRTOS 开发，不需要背下全部 FMC 寄存器，也不需要从零设计 SDRAM 控制器。实际项目通常从芯片厂商或开发板例程开始，再根据原理图、时钟树和器件数据手册修改。

至少需要理解：

- FMC 是 CPU 与外部 SDRAM 之间的控制器；
- 地址映射、容量、行列、Bank 和数据宽度之间的关系；
- SDRAM 上电命令的先后顺序；
- 纳秒时序需要换算成 SDRAM 时钟周期；
- SDRAM 必须持续刷新；
- STM32H7 上 MPU、Cache、LTDC 和 DMA 之间的关系；
- 如何通过自检和错误地址定位问题。

HAL 内部寄存器操作、SDRAM 存储单元电气原理和完整 JEDEC 状态机可以在需要排查底层问题时再深入学习。
