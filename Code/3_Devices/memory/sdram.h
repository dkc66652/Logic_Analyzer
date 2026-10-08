/**
 ******************************************************************************
 * @file    sdram.h
 * @brief   W9825G6KH-6 SDRAM设备参数、初始化序列和自检接口
 ******************************************************************************
 */
#ifndef __SDRAM_DEVICE_H
#define __SDRAM_DEVICE_H

#include "bsp_fmc_sdram.h"
#include <stdbool.h>
#include <stdint.h>

#define SDRAM_BANK_ADDR             BSP_FMC_SDRAM_BANK_ADDR     /**< SDRAM映射起始地址。 */
#define SDRAM_SIZE_BYTES            (32UL * 1024UL * 1024UL)    /**< W9825G6KH容量：32MiB。 */
#define SDRAM_HCLK3_HZ              240000000UL                  /**< FMC内核时钟频率。 */
#define SDRAM_SDCLK_DIV             3U                           /**< SDRAM时钟分频系数。 */
#define SDRAM_SDCLK_HZ              (SDRAM_HCLK3_HZ / SDRAM_SDCLK_DIV) /**< SDRAM工作时钟。 */
/** 把数据手册中的纳秒时序向上取整换算为SDCLK周期。 */
#define SDRAM_NS_TO_CYCLES(ns)      ((((uint32_t)(ns) * (SDRAM_SDCLK_HZ / 1000000UL)) + 999UL) / 1000UL)
#define SDRAM_T_MRD_CYCLES          2U                      /**< 模式寄存器命令延时。 */
#define SDRAM_T_XSR_CYCLES          SDRAM_NS_TO_CYCLES(72U) /**< 退出自刷新延时。 */
#define SDRAM_T_RAS_CYCLES          SDRAM_NS_TO_CYCLES(42U) /**< 行有效保持时间。 */
#define SDRAM_T_RC_CYCLES           SDRAM_NS_TO_CYCLES(60U) /**< 行周期时间。 */
#define SDRAM_T_WR_CYCLES           SDRAM_NS_TO_CYCLES(14U) /**< 写恢复时间。 */
#define SDRAM_T_RP_CYCLES           SDRAM_NS_TO_CYCLES(18U) /**< 预充电时间。 */
#define SDRAM_T_RCD_CYCLES          SDRAM_NS_TO_CYCLES(18U) /**< 行到列延时。 */
#define SDRAM_CAS_LATENCY           FMC_SDRAM_CAS_LATENCY_2 /**< CAS延迟为2个SDCLK。 */
#define SDRAM_MODEREG_CAS_LATENCY   ((uint32_t)SDRAM_CAS_LATENCY >> 3) /**< MRS的CAS位域。 */
#define SDRAM_MODEREG_BURST_LENGTH_1          ((uint32_t)0x0000U) /**< MRS突发长度1。 */
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   ((uint32_t)0x0000U) /**< MRS顺序突发。 */
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD ((uint32_t)0x0000U) /**< MRS标准模式。 */
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE  ((uint32_t)0x0200U) /**< MRS单次写突发。 */
#define SDRAM_CLOCK_PERIOD          FMC_SDRAM_CLOCK_PERIOD_3 /**< FMC三分频输出SDCLK。 */
#define SDRAM_READ_PIPE_DELAY       FMC_SDRAM_RPIPE_DELAY_1  /**< FMC读取管道延迟。 */
#define SDRAM_ROWS                  8192U /**< SDRAM刷新行数。 */
#define SDRAM_REFRESH_PERIOD_MS     64U   /**< 全部行最长刷新周期。 */
/** FMC刷新计数器值。 */
#define SDRAM_REFRESH_COUNT         ((uint32_t)((((uint64_t)SDRAM_REFRESH_PERIOD_MS * \
                                      (uint64_t)SDRAM_SDCLK_HZ) / 1000ULL) / \
                                      (uint64_t)SDRAM_ROWS) - 20ULL)
#define SDRAM_SELFTEST_STRIDE       65536UL /**< 地址扫描步长。 */
#define SDRAM_SELFTEST_BLOCK        65536UL /**< 连续棋盘格测试长度。 */

bool sdram_init(void);
bool sdram_selftest(void);
uint32_t sdram_get_error_addr(void);

#endif /* __SDRAM_DEVICE_H */
