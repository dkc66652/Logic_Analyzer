/** @file platform_dwt.h @brief DWT 周期计数和时间转换。 */

#ifndef PLATFORM_DWT_H
#define PLATFORM_DWT_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 初始化 Cortex-M DWT 的 CYCCNT 时基。 */
bool platform_dwt_init(void);

/**
 * @brief 维护 64 位软件扩展计数器。
 * @note  必须在 32 位 CYCCNT 单次回绕周期内调用至少一次；480 MHz 时约为 8.9 秒。
 */
void platform_dwt_maintain(void);

/** @brief DWT 周期计数器是否已经启用并完成软件时基初始化。 */
bool platform_dwt_is_enabled(void);

/** @brief 获取当前 32 位 CPU 周期计数值。 */
uint32_t platform_dwt_cycles(void);

/**
 * @brief 计算自 start_cycle 起经过的周期数。
 *
 * 无符号减法可跨越一次 32 位回绕。480 MHz 下，单次测量请保持在约 8.9 秒以内。
 */
uint32_t platform_dwt_elapsed(uint32_t start_cycle);

/** @brief 读取自 platform_dwt_init() 起累计的 64 位 CPU 周期数。 */
uint64_t platform_dwt_cycles64(void);

/** @brief 获取自 platform_dwt_init() 起的微秒时间。 */
uint64_t platform_dwt_time_us(void);

/** @brief 获取自 platform_dwt_init() 起的毫秒时间。 */
uint64_t platform_dwt_time_ms(void);

/** @brief 将周期数换算为微秒，使用 Platform 配置中的 DWT 时钟频率。 */
uint32_t platform_dwt_cycles_to_us(uint32_t cycles);

/** @brief 返回在当前配置时钟下，32 位计数器回绕一次所需的最长维护间隔。 */
uint32_t platform_dwt_max_maintain_interval_us(void);

#endif /* PLATFORM_DWT_H */
