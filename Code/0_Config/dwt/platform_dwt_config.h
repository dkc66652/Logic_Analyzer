/**
 * @file    platform_dwt_config.h
 * @brief   DWT 时基适配配置；移植到另一颗 Cortex-M 时只需修改本文件。
 */
#ifndef PLATFORM_DWT_CONFIG_H
#define PLATFORM_DWT_CONFIG_H

#include "platform_config.h"

/* 提供 CoreDebug、DWT 和 __get_PRIMASK() 等 CMSIS 定义的芯片头文件。 */
#define PLATFORM_DWT_CMSIS_HEADER       "stm32h7xx.h"

/* DWT CYCCNT 的实际输入频率，必须与芯片时钟配置一致。 */
#define PLATFORM_DWT_CLOCK_HZ           CPU_CYCLES_HZ

#endif /* PLATFORM_DWT_CONFIG_H */
