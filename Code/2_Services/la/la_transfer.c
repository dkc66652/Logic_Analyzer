/**
 * @file la_transfer.c
 * @brief 逻辑分析仪采集数据存储搬运适配实现
 * @note 将FMC/MDMA细节收敛到本文件，保持预留、完成后提交的顺序；
 *       源数据及存储区的非缓存属性由BSP和Platform内存布局保证
 */

/* ================================================== */

#include "la/la_transfer_internal.h"
#include "la/la_storage.h"
#include "fmc/fmc_sdram.h"
#include "stddef.h"

/* ================================================== */

#define LA_TRANSFER_TIMEOUT_MS    100U /* 一次MDMA累计等待上限，不因采集通知重置 */

typedef enum
{
    LA_TRANSFER_IDLE = 0,
    LA_TRANSFER_BUSY,
    LA_TRANSFER_COMPLETE,
    LA_TRANSFER_ERROR
} LA_Transfer_State_t;

typedef struct
{
    TaskHandle_t task_handle; /* 唯一搬运任务，也接收采集事件通知 */
    volatile LA_Transfer_State_t state; /* 任务与MDMA中断共享的结果 */
} LA_Transfer_Handle_t;

static LA_Transfer_Handle_t transfer;

/* ================================================== */

/**
 * @brief SDRAM MDMA传输完成回调
 * @note 该函数在MDMA中断上下文调用，只更新结果并唤醒服务任务
 * @param result MDMA传输结果
 * @param context 回调上下文
 * @retval 无
 */
static void LA_Transfer_MDMA_Callback(BSP_FMC_SDRAM_Result_t result,
                                       void *context)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)context;
    if (transfer.state != LA_TRANSFER_BUSY)
    {
        return;
    }
    transfer.state = (result == BSP_FMC_SDRAM_OK) ?
                     LA_TRANSFER_COMPLETE : LA_TRANSFER_ERROR;
    if (transfer.task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(transfer.task_handle,
                               &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/* ================================================== */

/**
 * @brief 初始化采集存储搬运适配
 * @note 服务任务创建成功后、开始采集前调用
 * @param task_handle 唯一搬运任务句柄
 * @retval 无
 */
void LA_Transfer_Init(TaskHandle_t task_handle)
{
    transfer.task_handle = task_handle;
    transfer.state = LA_TRANSFER_IDLE;
    BSP_FMC_SDRAM_Set_MDMA_Callback(LA_Transfer_MDMA_Callback, NULL);
}

/**
 * @brief 复位搬运结果状态
 * @note 调用者保证没有传输在途
 * @param 无
 * @retval 无
 */
void LA_Transfer_Reset(void)
{
    transfer.state = LA_TRANSFER_IDLE;
}

LA_Service_Result_t LA_Transfer_Abort(void)
{
    transfer.state = LA_TRANSFER_IDLE;
    return (BSP_FMC_SDRAM_Abort_MDMA_Write() == BSP_FMC_SDRAM_OK) ?
           LA_SERVICE_OK : LA_SERVICE_ERROR_HARDWARE;
}

/* ================================================== */

/**
 * @brief 取得截止计数值之前的有效记录数量
 * @note DMA时间戳按升序排列，通常只需检查最后一条记录
 * @param data 时间戳数组
 * @param count 数组记录数量
 * @param cutoff_tick 截止计数值，不保存等于该值的记录
 * @retval 有效记录数量
 */
static uint16_t LA_Transfer_Get_Valid_Count(const uint32_t *data,
                                            uint16_t count,
                                            uint32_t cutoff_tick)
{
    uint16_t left = 0U;
    uint16_t right = count;

    if ((count == 0U) || (data[0] >= cutoff_tick))
    {
        return 0U;
    }
    if (data[count - 1U] < cutoff_tick)
    {
        return count;
    }

    while (left < right)
    {
        uint16_t middle = (uint16_t)(left + ((right - left) / 2U));

        if (data[middle] < cutoff_tick)
        {
            left = (uint16_t)(middle + 1U);
        }
        else
        {
            right = middle;
        }
    }
    return left;
}

/**
 * @brief 使用MDMA完成一段时间戳到SDRAM的搬运
 * @param source 源时间戳地址
 * @param destination SDRAM目标地址
 * @param count 时间戳数量
 * @retval LA_SERVICE_OK-成功
 *         LA_SERVICE_ERROR_HARDWARE-MDMA启动或传输失败
 */
static LA_Service_Result_t LA_Transfer_Copy(const uint32_t *source,
                                           uint32_t *destination,
                                           uint16_t count)
{
    LA_Transfer_State_t mdma_state;
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(LA_TRANSFER_TIMEOUT_MS);

    if (timeout == 0U)
    {
        timeout = 1U;
    }

    taskENTER_CRITICAL();
    transfer.state = LA_TRANSFER_BUSY;
    taskEXIT_CRITICAL();

    if (BSP_FMC_SDRAM_Start_MDMA_Write(source,
                                      destination,
                                      count) != BSP_FMC_SDRAM_OK)
    {
        transfer.state = LA_TRANSFER_IDLE;
        return LA_SERVICE_ERROR_HARDWARE;
    }

    for (;;)
    {
        taskENTER_CRITICAL();
        mdma_state = transfer.state;
        taskEXIT_CRITICAL();
        if (mdma_state != LA_TRANSFER_BUSY)
        {
            break;
        }

        TickType_t elapsed = xTaskGetTickCount() - start_tick;
        if (elapsed >= timeout)
        {
            taskENTER_CRITICAL();
            mdma_state = transfer.state;
            if (mdma_state == LA_TRANSFER_BUSY)
            {
                transfer.state = LA_TRANSFER_IDLE;
            }
            taskEXIT_CRITICAL();
            if (mdma_state != LA_TRANSFER_BUSY)
            {
                break;
            }
            return (BSP_FMC_SDRAM_Abort_MDMA_Write() == BSP_FMC_SDRAM_OK) ?
                   LA_SERVICE_ERROR_TIMEOUT : LA_SERVICE_ERROR_HARDWARE;
        }
        (void)ulTaskNotifyTake(pdTRUE, timeout - elapsed);
    }

    transfer.state = LA_TRANSFER_IDLE;
    if ((mdma_state != LA_TRANSFER_COMPLETE) &&
        (BSP_FMC_SDRAM_Abort_MDMA_Write() != BSP_FMC_SDRAM_OK))
    {
        return LA_SERVICE_ERROR_HARDWARE;
    }
    return (mdma_state == LA_TRANSFER_COMPLETE) ?
           LA_SERVICE_OK : LA_SERVICE_ERROR_HARDWARE;
}

/**
 * @brief 将一段有效时间戳通过MDMA追加到通道存储
 * @note 调用者必须持有存储互斥锁
 * @param channel 通道编号
 * @param data 时间戳数组
 * @param count 数组记录数量
 * @param cutoff_tick 截止计数值
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
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
 * @brief 搬运后校验源数据有效性并提交
 * @note 中断可在搬运期间标记半区污染，受污染记录不得发布。
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
    uint16_t valid_count = LA_Transfer_Get_Valid_Count(data,
                                                     count,
                                                     cutoff_tick);
    LA_Service_Result_t service_result;

    if (valid_count == 0U)
    {
        return (commit == NULL) ? LA_SERVICE_OK : commit(channel, 0U, context);
    }
    if (LA_Storage_Reserve(channel,
                          valid_count,
                          &destination) != LA_STORAGE_OK)
    {
        return LA_SERVICE_ERROR_STORAGE;
    }

    service_result = LA_Transfer_Copy(data,
                                      destination,
                                      valid_count);
    if (service_result != LA_SERVICE_OK)
    {
        return service_result;
    }
    if (commit != NULL)
    {
        return commit(channel, valid_count, context);
    }
    if (LA_Storage_Commit(channel, valid_count) != LA_STORAGE_OK)
    {
        return LA_SERVICE_ERROR_STORAGE;
    }
    return LA_SERVICE_OK;
}
