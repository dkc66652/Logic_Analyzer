/**
 * @file la_ic_internal.h
 * @brief DMA半区有效性判定的私有纯函数
 * @note 仅供采集BSP及主机测试使用；寄存器采样与停稳由BSP实现负责
 */

#ifndef LA_IC_INTERNAL_H
#define LA_IC_INTERNAL_H

/* ================================================== */

#include "la/la_ic.h"

/**
 * @brief 保存停稳期间完成的半区，忽略禁用Stream后可能人工置位的TC
 * @note HT无人工完成语义；NDTR可证明最后在途请求跨HT或自然TC边界。
 * @retval bit0-前半区完成，bit1-后半区完成
 */
static inline uint8_t BSP_LA_IC_Frozen_Completed_Halves(
        uint16_t remaining_before,
        uint16_t remaining_after,
        uint8_t ht_before,
        uint8_t tc_before,
        uint8_t ht_after)
{
    const uint16_t half_count = BSP_LA_IC_DMA_BUFFER_COUNT / 2U;
    uint8_t completed = 0U;

    if ((remaining_before > BSP_LA_IC_DMA_BUFFER_COUNT) ||
        (remaining_after > BSP_LA_IC_DMA_BUFFER_COUNT))
    {
        return 0U;
    }
    if ((ht_before != 0U) || (ht_after != 0U) ||
        ((remaining_before > half_count) && (remaining_after <= half_count)))
    {
        completed |= 1U;
    }
    if ((tc_before != 0U) || (remaining_after > remaining_before) ||
        (remaining_after == 0U))
    {
        completed |= 2U;
    }
    return completed;
}

/* ================================================== */

/**
 * @brief 根据循环写入位置及未处理标志判定源半区是否仍未复用
 * @note 停稳时允许恰好位于半区边界；多圈含糊状态保守拒绝
 */
static inline uint8_t BSP_LA_IC_Half_Is_Valid(uint16_t remaining,
                                             uint8_t ht_pending,
                                             uint8_t tc_pending,
                                             uint8_t half,
                                             uint8_t stopped)
{
    uint16_t write_index;
    const uint16_t half_count = BSP_LA_IC_DMA_BUFFER_COUNT / 2U;

    if ((remaining > BSP_LA_IC_DMA_BUFFER_COUNT) || (half > 1U))
    {
        return 0U;
    }
    write_index = (uint16_t)(BSP_LA_IC_DMA_BUFFER_COUNT - remaining);
    if (write_index == BSP_LA_IC_DMA_BUFFER_COUNT)
    {
        write_index = 0U;
    }
    if (half == 0U)
    {
        if ((write_index >= half_count) && (tc_pending == 0U))
        {
            return 1U;
        }
        return ((stopped != 0U) && (write_index == 0U) &&
                (tc_pending != 0U) && (ht_pending == 0U)) ? 1U : 0U;
    }
    if ((write_index < half_count) && (ht_pending == 0U))
    {
        return 1U;
    }
    return ((stopped != 0U) && (write_index == half_count) &&
            (ht_pending != 0U) && (tc_pending == 0U)) ? 1U : 0U;
}

/* ================================================== */

#endif /* LA_IC_INTERNAL_H */
