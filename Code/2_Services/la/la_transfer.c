/**
 * @file la_transfer.c
 * @brief 逻辑分析仪采集数据存储搬运适配实现
 * @note 在采集服务任务中用CPU把DMA半缓冲追加到SDRAM，保持“预留、复制、
 *       校验、提交”的顺序。定时器DMA仍负责捕获边沿；本模块不使用MDMA，
 *       避免与LTDC和SDRAM显示缓冲竞争总线。
 */

#include "la/la_transfer_internal.h"
#include "la/la_storage.h"
#include "stm32h7xx.h"
#include <stddef.h>
#include <string.h>

/**
 * @brief 初始化采集存储搬运适配。
 * @note CPU复制为同步操作，不依赖任务通知；保留参数以维持服务初始化接口。
 * @param task_handle 服务任务句柄。
 */
void LA_Transfer_Init(TaskHandle_t task_handle)
{
    (void)task_handle;
}

/**
 * @brief 复位搬运状态。
 * @note CPU复制返回时已完成，无异步状态需要复位。
 */
void LA_Transfer_Reset(void)
{
}

/**
 * @brief 终止可能在途的存储搬运。
 * @note CPU复制只在服务任务中同步执行，调用本函数时不存在硬件传输。
 * @retval LA_SERVICE_OK。
 */
LA_Service_Result_t LA_Transfer_Abort(void)
{
    return LA_SERVICE_OK;
}

/**
 * @brief 取得截止计数值之前的有效记录数量。
 * @note DMA时间戳按升序排列，使用二分查找排除截止点及其之后的数据。
 * @param data 时间戳数组。
 * @param count 数组记录数量。
 * @param cutoff_tick 截止计数值，不保存等于该值的记录。
 * @retval 有效记录数量。
 */
static uint16_t LA_Transfer_Get_Valid_Count(const uint32_t *data,
                                            uint16_t count,
                                            uint32_t cutoff_tick)
{
    uint16_t left = 0U;
    uint16_t right = count;

    if ((count == 0U) || (data[0] >= cutoff_tick)) return 0U;
    if (data[count - 1U] < cutoff_tick) return count;

    while (left < right)
    {
        uint16_t middle = (uint16_t)(left + ((right - left) / 2U));

        if (data[middle] < cutoff_tick) left = (uint16_t)(middle + 1U);
        else right = middle;
    }
    return left;
}

/**
 * @brief 使用CPU完成一段时间戳到SDRAM的同步搬运。
 * @param source 源时间戳地址，来自DMA循环半缓冲。
 * @param destination SDRAM目标地址，来自存储预留。
 * @param count 时间戳数量。
 * @retval LA_SERVICE_OK-复制完成；LA_SERVICE_ERROR_HARDWARE-参数无效。
 */
static LA_Service_Result_t LA_Transfer_Copy(const uint32_t *source,
                                            uint32_t *destination,
                                            uint16_t count)
{
    if ((source == NULL) || (destination == NULL) || (count == 0U))
    {
        return LA_SERVICE_ERROR_HARDWARE;
    }

    memcpy(destination, source, (size_t)count * sizeof(*source));

    /* 先完成对SDRAM的所有写入，再允许校验源半区并发布存储记录。 */
    __DSB();
    return LA_SERVICE_OK;
}

/**
 * @brief 将一段有效时间戳通过CPU追加到通道存储。
 * @note 调用者必须持有存储互斥锁。
 */
LA_Service_Result_t LA_Transfer_Append(uint8_t channel,
                                       const uint32_t *data,
                                       uint16_t count,
                                       uint32_t cutoff_tick)
{
    return LA_Transfer_Append_Checked(channel, data, count, cutoff_tick,
                                      NULL, NULL);
}

/**
 * @brief CPU复制后校验源数据有效性并提交。
 * @note 中断可在CPU复制期间标记半区污染，受污染记录不得发布。
 */
LA_Service_Result_t LA_Transfer_Append_Checked(
        uint8_t channel,
        const uint32_t *data,
        uint16_t count,
        uint32_t cutoff_tick,
        LA_Transfer_Commit_Callback_t commit,
        void *context)
{
    uint32_t *destination;
    uint16_t valid_count = LA_Transfer_Get_Valid_Count(data, count, cutoff_tick);
    LA_Service_Result_t service_result;

    if (valid_count == 0U)
    {
        return (commit == NULL) ? LA_SERVICE_OK : commit(channel, 0U, context);
    }
    if (LA_Storage_Reserve(channel, valid_count, &destination) != LA_STORAGE_OK)
    {
        return LA_SERVICE_ERROR_STORAGE;
    }

    service_result = LA_Transfer_Copy(data, destination, valid_count);
    if (service_result != LA_SERVICE_OK) return service_result;

    if (commit != NULL) return commit(channel, valid_count, context);
    return (LA_Storage_Commit(channel, valid_count) == LA_STORAGE_OK) ?
           LA_SERVICE_OK : LA_SERVICE_ERROR_STORAGE;
}
