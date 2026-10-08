/**
 * @file la_query.c
 * @brief 逻辑分析仪边沿数据查询实现
 * @note 普通查询在存储锁内按需生成DMA弱快照；
 *       固定查询视图由Begin至End持有查询锁，允许采集继续追加数据
 */

/* ================================================== */

#include "la/la_service_internal.h"
#include "la/la_storage.h"
#include "stddef.h"
#include "string.h"

/* ================================================== */

static uint32_t la_service_query_data[BSP_LA_IC_DMA_BUFFER_COUNT];     /* DMA实时数据快照 */

typedef struct
{
    uint32_t stored_count; /* 已搬运至存储块的记录数量 */
    uint16_t live_count;   /* DMA缓冲区中未搬运记录数量 */
    uint8_t running;       /* 快照创建时是否正在采集 */
    uint8_t live_ready;    /* DMA实时数据是否已生成 */
} LA_Service_Query_Snapshot_t;

typedef struct
{
    uint32_t stored_count[LA_SERVICE_CHANNEL_COUNT]; /* 六路已存储记录数量 */
    uint16_t live_count[LA_SERVICE_CHANNEL_COUNT];   /* 六路DMA弱快照记录数量 */
    uint32_t live_data[LA_SERVICE_CHANNEL_COUNT][BSP_LA_IC_DMA_BUFFER_COUNT]; /* 六路DMA弱快照 */
    uint32_t token;        /* 当前查询视图标识 */
    uint8_t initial_levels;/* 查询视图中的六路初始电平 */
} LA_Service_Frame_Query_t;

static LA_Service_Frame_Query_t la_service_frame_query; /* 当前帧查询视图 */
static uint32_t la_service_next_query_token = 0U;        /* 下一个查询视图标识 */
static TaskHandle_t la_service_query_owner = NULL;      /* 唯一视图拥有者 */
static uint32_t la_service_next_lease_token = 0U;        /* 下一个数据保留凭据 */

/* ================================================== */

/**
 * @brief 在DMA实时快照中查找第一个大于等于指定时间戳的记录
 * @param count 快照记录数量
 * @param timestamp 指定时间戳
 * @retval 符合条件的相对下标，允许等于count
 */
static uint16_t LA_Service_Live_Lower_Bound(uint16_t count, uint32_t timestamp)
{
    uint16_t left = 0U;
    uint16_t right = count;

    while (left < right)
    {
        uint16_t middle = (uint16_t)(left + ((right - left) / 2U));

        if (la_service_query_data[middle] < timestamp)
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
 * @brief 在DMA实时快照中查找第一个大于指定时间戳的记录
 * @param count 快照记录数量
 * @param timestamp 指定时间戳
 * @retval 符合条件的相对下标，允许等于count
 */
static uint16_t LA_Service_Live_Upper_Bound(uint16_t count, uint32_t timestamp)
{
    uint16_t left = 0U;
    uint16_t right = count;

    while (left < right)
    {
        uint16_t middle = (uint16_t)(left + ((right - left) / 2U));

        if (la_service_query_data[middle] <= timestamp)
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
 * @brief 准备指定通道的查询上下文
 * @note 此阶段只记录存储数量，只有存储数据无法完成查询时才生成DMA快照
 * @param channel 通道编号
 * @param snapshot 查询快照信息
 * @retval LA_SERVICE_OK-成功
 *         LA_SERVICE_ERROR_NOT_READY-当前状态不允许查询
 */
static LA_Service_Result_t LA_Service_Prepare_Query(
                                    uint8_t channel,
                                    LA_Service_Query_Snapshot_t *snapshot)
{
    LA_Service_State_t state = la_service_shared.state;

    snapshot->stored_count = LA_Storage_Get_Count(channel);
    snapshot->live_count = 0U;
    snapshot->live_ready = 0U;
    snapshot->running = 0U;

    if ((state == LA_SERVICE_STATE_DONE) ||
        (state == LA_SERVICE_STATE_ERROR))
    {
        snapshot->live_ready = 1U;
        return LA_SERVICE_OK;
    }
    if (state != LA_SERVICE_STATE_RUNNING)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }

    snapshot->running = 1U;
    return LA_SERVICE_OK;
}

/**
 * @brief 按需生成指定通道的DMA实时数据快照
 * @note DMA位置只读取一次，复制期间DMA可继续写入且不重试
 * @param channel 通道编号
 * @param snapshot 查询快照信息
 * @retval LA_SERVICE_OK-成功
 *         LA_SERVICE_ERROR_NOT_READY-DMA实时数据不可用
 */
static LA_Service_Result_t LA_Service_Prepare_Live(
                                    uint8_t channel,
                                    LA_Service_Query_Snapshot_t *snapshot)
{
    BSP_LA_IC_Live_Result_t live_result;
    uint16_t consumed_index;
    uint16_t live_count;
    uint32_t pending_mask;

    if (snapshot->live_ready != 0U)
    {
        return LA_SERVICE_OK;
    }
    if (snapshot->running == 0U)
    {
        snapshot->live_ready = 1U;
        return LA_SERVICE_OK;
    }

    taskENTER_CRITICAL();
    consumed_index = la_service_shared.consumed_index[channel];
    pending_mask = la_service_shared.pending_mask &
                   (LA_SERVICE_PENDING_BIT(channel, 0U) |
                    LA_SERVICE_PENDING_BIT(channel, 1U));
    taskEXIT_CRITICAL();

    if (BSP_LA_IC_Get_Live((BSP_LA_IC_Channel_t)channel,
                           &live_result) != BSP_LA_IC_OK)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }

    if (live_result.write_index > BSP_LA_IC_DMA_BUFFER_COUNT)
    {
        live_result.write_index = 0U;
    }

    if (live_result.write_index > consumed_index)
    {
        live_count = (uint16_t)(live_result.write_index - consumed_index);
    }
    else if (live_result.write_index < consumed_index)
    {
        live_count = (uint16_t)(BSP_LA_IC_DMA_BUFFER_COUNT - consumed_index +
                                live_result.write_index);
    }
    else if (pending_mask != 0U)
    {
        live_count = BSP_LA_IC_DMA_BUFFER_COUNT;
    }
    else
    {
        live_count = 0U;
    }

    if (live_count != 0U)
    {
        uint16_t first_count =
                (uint16_t)(BSP_LA_IC_DMA_BUFFER_COUNT - consumed_index);

        if (first_count > live_count)
        {
            first_count = live_count;
        }
        memcpy(la_service_query_data,
                live_result.buffer + consumed_index,
                (size_t)first_count * sizeof(*la_service_query_data));
        if (first_count < live_count)
        {
            memcpy(la_service_query_data + first_count,
                    live_result.buffer,
                    (size_t)(live_count - first_count) *
                    sizeof(*la_service_query_data));
        }
    }

    while ((live_count > 0U) &&
           (la_service_query_data[live_count - 1U] >= la_service_shared.deadline_tick))
    {
        live_count--;
    }

    snapshot->live_count = live_count;
    snapshot->live_ready = 1U;
    return LA_SERVICE_OK;
}

/**
 * @brief 在完整查询数据中取得指定下标的时间戳
 * @param channel 通道编号
 * @param snapshot 查询快照信息
 * @param index 全局记录下标
 * @param timestamp 返回时间戳
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
static LA_Service_Result_t LA_Service_Query_Get(
                                    uint8_t channel,
                                    LA_Service_Query_Snapshot_t *snapshot,
                                    uint32_t index,
                                    uint32_t *timestamp)
{
    LA_Service_Result_t result;

    if (index < snapshot->stored_count)
    {
        if (LA_Storage_Get(channel, index, timestamp) != LA_STORAGE_OK)
        {
            return LA_SERVICE_ERROR_STORAGE;
        }
        return LA_SERVICE_OK;
    }

    result = LA_Service_Prepare_Live(channel, snapshot);
    if (result != LA_SERVICE_OK)
    {
        return result;
    }
    if ((index - snapshot->stored_count) >= snapshot->live_count)
    {
        return LA_SERVICE_ERROR_STORAGE;
    }

    *timestamp = la_service_query_data[index - snapshot->stored_count];
    return LA_SERVICE_OK;
}

/**
 * @brief 在完整查询数据中查找第一个大于等于指定时间戳的记录
 * @param channel 通道编号
 * @param snapshot 查询快照信息
 * @param timestamp 指定时间戳
 * @param index 返回全局记录下标
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
static LA_Service_Result_t LA_Service_Query_Lower_Bound(
                                    uint8_t channel,
                                    LA_Service_Query_Snapshot_t *snapshot,
                                    uint32_t timestamp,
                                    uint32_t *index)
{
    LA_Service_Result_t result;

    if (LA_Storage_Lower_Bound(channel, timestamp, index) != LA_STORAGE_OK)
    {
        return LA_SERVICE_ERROR_STORAGE;
    }
    if (*index != snapshot->stored_count)
    {
        return LA_SERVICE_OK;
    }

    result = LA_Service_Prepare_Live(channel, snapshot);
    if (result != LA_SERVICE_OK)
    {
        return result;
    }

    *index += LA_Service_Live_Lower_Bound(snapshot->live_count, timestamp);
    return LA_SERVICE_OK;
}

/**
 * @brief 在完整查询数据中查找第一个大于指定时间戳的记录
 * @param channel 通道编号
 * @param snapshot 查询快照信息
 * @param timestamp 指定时间戳
 * @param index 返回全局记录下标
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
static LA_Service_Result_t LA_Service_Query_Upper_Bound(
                                    uint8_t channel,
                                    LA_Service_Query_Snapshot_t *snapshot,
                                    uint32_t timestamp,
                                    uint32_t *index)
{
    LA_Service_Result_t result;

    if (LA_Storage_Upper_Bound(channel, timestamp, index) != LA_STORAGE_OK)
    {
        return LA_SERVICE_ERROR_STORAGE;
    }
    if (*index != snapshot->stored_count)
    {
        return LA_SERVICE_OK;
    }

    result = LA_Service_Prepare_Live(channel, snapshot);
    if (result != LA_SERVICE_OK)
    {
        return result;
    }

    *index += LA_Service_Live_Upper_Bound(snapshot->live_count, timestamp);
    return LA_SERVICE_OK;
}

/* ================================================== */

/**
 * @brief 获取六个通道在指定时间戳的电平
 * @note 只能在任务上下文调用，时间戳恰好对应翻转时返回翻转前一刻的电平
 * @param timestamp 指定时间戳
 * @param levels 返回六路电平位图，bit0..5对应通道枚举0..5
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
LA_Service_Result_t LA_Service_Get_Levels(uint32_t timestamp, uint8_t *levels)
{
    LA_Service_Result_t service_result = LA_SERVICE_OK;
    uint8_t result_levels = 0U;

    if (levels == NULL)
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if (la_service_shared.storage_mutex == NULL)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }

    (void)xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY);
    for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
    {
        LA_Service_Query_Snapshot_t snapshot;
        uint32_t transition_count;
        uint8_t level;

        service_result = LA_Service_Prepare_Query(channel, &snapshot);
        if (service_result == LA_SERVICE_OK)
        {
            service_result = LA_Service_Query_Lower_Bound(channel,
                                                          &snapshot,
                                                          timestamp,
                                                          &transition_count);
        }
        if (service_result != LA_SERVICE_OK)
        {
            break;
        }

        level = (uint8_t)(((la_service_shared.initial_levels >> channel) & 1U) ^
                          (transition_count & 1U));
        result_levels |= (uint8_t)(level << channel);
    }

    if (service_result == LA_SERVICE_OK)
    {
        *levels = result_levels;
    }
    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    return service_result;
}

/**
 * @brief 查询指定通道在时间范围内对应的记录下标
 * @note 查询范围为包含起止边界的闭区间
 * @note first_index允许等于after_index，表示范围内没有翻转记录
 * @param channel 逻辑分析仪通道
 * @param start_tick 范围起始计数值
 * @param end_tick 范围结束计数值
 * @param result 返回记录下标范围和范围起点电平
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
LA_Service_Result_t LA_Service_Query_Range(LA_Service_Channel_t channel,
                                           uint32_t start_tick,
                                           uint32_t end_tick,
                                           LA_Service_Range_Result_t *result)
{
    LA_Service_Query_Snapshot_t snapshot;
    LA_Service_Result_t service_result;
    uint8_t initial_level;

    if (((uint32_t)channel >= LA_SERVICE_CHANNEL_COUNT) ||
        (start_tick > end_tick) ||
        (result == NULL))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if (la_service_shared.storage_mutex == NULL)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }

    result->first_index = 0U;
    result->after_index = 0U;
    result->start_level = 0U;
    (void)xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY);
    initial_level = (uint8_t)((la_service_shared.initial_levels >>
                              (uint8_t)channel) & 1U);
    service_result = LA_Service_Prepare_Query((uint8_t)channel, &snapshot);
    if (service_result == LA_SERVICE_OK)
    {
        service_result = LA_Service_Query_Lower_Bound(
                                                (uint8_t)channel,
                                                &snapshot,
                                                start_tick,
                                                &result->first_index);
    }
    if (service_result == LA_SERVICE_OK)
    {
        service_result = LA_Service_Query_Upper_Bound(
                                                (uint8_t)channel,
                                                &snapshot,
                                                end_tick,
                                                &result->after_index);
    }
    if (service_result == LA_SERVICE_OK)
    {
        result->start_level = (uint8_t)(initial_level ^
                                       (result->first_index & 1U));
    }

    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    return service_result;
}

/**
 * @brief 从指定通道记录下标开始批量读取时间戳
 * @note 采集过程中只对本次读取生成一次DMA弱快照，期间发生写入不重试
 * @param channel 逻辑分析仪通道
 * @param start_index 起始记录下标
 * @param data 返回时间戳的缓冲区
 * @param max_count 缓冲区最多可接收的记录数量
 * @param data_count 返回实际读取的记录数量
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
LA_Service_Result_t LA_Service_Get_Data(LA_Service_Channel_t channel,
                                        uint32_t start_index,
                                        uint32_t *data,
                                        uint16_t max_count,
                                        uint16_t *data_count)
{
    LA_Service_Query_Snapshot_t snapshot;
    LA_Service_Result_t service_result;
    uint32_t total_count;
    uint16_t copy_count;
    uint16_t stored_copy_count = 0U;

    if (((uint32_t)channel >= LA_SERVICE_CHANNEL_COUNT) ||
        (data == NULL) ||
        (max_count == 0U) ||
        (data_count == NULL))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if (la_service_shared.storage_mutex == NULL)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }

    *data_count = 0U;
    (void)xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY);
    service_result = LA_Service_Prepare_Query((uint8_t)channel, &snapshot);
    if (service_result != LA_SERVICE_OK)
    {
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        return service_result;
    }

    if ((start_index >= snapshot.stored_count) ||
        ((uint32_t)max_count > (snapshot.stored_count - start_index)))
    {
        service_result = LA_Service_Prepare_Live((uint8_t)channel, &snapshot);
        if (service_result != LA_SERVICE_OK)
        {
            (void)xSemaphoreGive(la_service_shared.storage_mutex);
            return service_result;
        }
    }

    total_count = snapshot.stored_count + snapshot.live_count;
    if (start_index >= total_count)
    {
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        return LA_SERVICE_OK;
    }

    copy_count = max_count;
    if ((uint32_t)copy_count > (total_count - start_index))
    {
        copy_count = (uint16_t)(total_count - start_index);
    }

    if (start_index < snapshot.stored_count)
    {
        uint32_t stored_remaining = snapshot.stored_count - start_index;

        stored_copy_count = copy_count;
        if ((uint32_t)stored_copy_count > stored_remaining)
        {
            stored_copy_count = (uint16_t)stored_remaining;
        }
        if (LA_Storage_Copy((uint8_t)channel,
                            start_index,
                            data,
                            stored_copy_count) != LA_STORAGE_OK)
        {
            (void)xSemaphoreGive(la_service_shared.storage_mutex);
            return LA_SERVICE_ERROR_STORAGE;
        }
    }

    if (stored_copy_count < copy_count)
    {
        uint32_t live_index = start_index + stored_copy_count -
                              snapshot.stored_count;

        memcpy(data + stored_copy_count,
                la_service_query_data + live_index,
                (size_t)(copy_count - stored_copy_count) * sizeof(*data));
    }

    *data_count = copy_count;
    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    return LA_SERVICE_OK;
}

/**
 * @brief 检查查询视图是否为当前有效视图
 * @param view 查询视图
 * @retval 0-无效
 *         1-有效
 */
static uint8_t LA_Service_Query_View_Is_Valid(
                                    const LA_Service_Query_View_t *view)
{
    return ((view != NULL) &&
            (view->token != 0U) &&
            (view->token == la_service_frame_query.token) &&
            (la_service_query_owner == xTaskGetCurrentTaskHandle())) ? 1U : 0U;
}

/**
 * @brief 从查询视图取得指定记录的时间戳
 * @param channel 通道编号
 * @param index 记录下标
 * @param timestamp 返回时间戳
 * @retval LA_SERVICE_OK-成功
 *         LA_SERVICE_ERROR_STORAGE-记录下标无效
 */
static LA_Service_Result_t LA_Service_Query_View_Get(
                                    uint8_t channel,
                                    uint32_t index,
                                    uint32_t *timestamp)
{
    if (index < la_service_frame_query.stored_count[channel])
    {
        if (LA_Storage_Get(channel, index, timestamp) != LA_STORAGE_OK)
        {
            return LA_SERVICE_ERROR_STORAGE;
        }
        return LA_SERVICE_OK;
    }

    index -= la_service_frame_query.stored_count[channel];
    if (index >= la_service_frame_query.live_count[channel])
    {
        return LA_SERVICE_ERROR_STORAGE;
    }

    *timestamp = la_service_frame_query.live_data[channel][index];
    return LA_SERVICE_OK;
}

/**
 * @brief 在查询视图中从指定下标开始执行二分查找
 * @param channel 通道编号
 * @param timestamp 指定时间戳
 * @param minimum_index 二分查找的最小下标
 * @param maximum_index 二分查找的最大开区间下标
 * @param upper 0-查找第一个大于等于值的下标，1-查找第一个大于值的下标
 * @param index 返回记录下标
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
static LA_Service_Result_t LA_Service_Query_View_Bound(
                                    uint8_t channel,
                                    uint32_t timestamp,
                                    uint32_t minimum_index,
                                    uint32_t maximum_index,
                                    uint8_t upper,
                                    uint32_t *index)
{
    uint32_t left = minimum_index;
    uint32_t right = maximum_index;
    uint32_t total_count = la_service_frame_query.stored_count[channel] +
                           la_service_frame_query.live_count[channel];

    if ((left > right) || (right > total_count))
    {
        return LA_SERVICE_ERROR_STORAGE;
    }

    while (left < right)
    {
        uint32_t middle = left + ((right - left) / 2U);
        uint32_t value;
        LA_Service_Result_t service_result = LA_Service_Query_View_Get(
                                                channel,
                                                middle,
                                                &value);

        if (service_result != LA_SERVICE_OK)
        {
            return service_result;
        }

        if ((value < timestamp) ||
            ((upper != 0U) && (value == timestamp)))
        {
            left = middle + 1U;
        }
        else
        {
            right = middle;
        }
    }

    *index = left;
    return LA_SERVICE_OK;
}

/**
 * @brief 建立一次完整波形查询使用的六通道固定视图
 * @note 运行中每个通道只读取一次DMA位置并生成一次弱快照
 * @param view 返回查询视图
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
static LA_Service_Result_t LA_Service_Query_Begin_Ticks(
                                    LA_Service_Query_View_t *view,
                                    TickType_t timeout)
{
    LA_Service_State_t state;
    LA_Service_Result_t service_result = LA_SERVICE_OK;
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    TickType_t start_tick = 0U;
    TickType_t remaining = timeout;

    if (view == NULL)
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if ((la_service_shared.storage_mutex == NULL) ||
        (la_service_shared.query_mutex == NULL))
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }

    taskENTER_CRITICAL();
    if ((la_service_query_owner == current_task) &&
        (la_service_frame_query.token != 0U))
    {
        taskEXIT_CRITICAL();
        return LA_SERVICE_ERROR_BUSY;
    }
    taskEXIT_CRITICAL();

    view->token = 0U;
    if (timeout != portMAX_DELAY)
    {
        start_tick = xTaskGetTickCount();
    }
    if (xSemaphoreTake(la_service_shared.query_mutex, timeout) != pdTRUE)
    {
        return (timeout == 0U) ? LA_SERVICE_ERROR_BUSY : LA_SERVICE_ERROR_TIMEOUT;
    }
    if (timeout != portMAX_DELAY)
    {
        TickType_t elapsed = xTaskGetTickCount() - start_tick;

        remaining = (elapsed < timeout) ? timeout - elapsed : 0U;
    }
    if (xSemaphoreTake(la_service_shared.storage_mutex, remaining) != pdTRUE)
    {
        (void)xSemaphoreGive(la_service_shared.query_mutex);
        return (timeout == 0U) ? LA_SERVICE_ERROR_BUSY : LA_SERVICE_ERROR_TIMEOUT;
    }
    state = la_service_shared.state;
    if ((state != LA_SERVICE_STATE_RUNNING) &&
        (state != LA_SERVICE_STATE_DONE) &&
        (state != LA_SERVICE_STATE_ERROR))
    {
        service_result = LA_SERVICE_ERROR_NOT_READY;
    }

    la_service_frame_query.initial_levels = la_service_shared.initial_levels;
    for (uint8_t channel = 0U;
         (channel < LA_SERVICE_CHANNEL_COUNT) &&
         (service_result == LA_SERVICE_OK);
         channel++)
    {
        LA_Service_Query_Snapshot_t snapshot;

        snapshot.stored_count = LA_Storage_Get_Count(channel);
        snapshot.live_count = 0U;
        snapshot.running = (state == LA_SERVICE_STATE_RUNNING) ? 1U : 0U;
        snapshot.live_ready = (snapshot.running == 0U) ? 1U : 0U;
        service_result = LA_Service_Prepare_Live(channel, &snapshot);
        if (service_result == LA_SERVICE_OK)
        {
            la_service_frame_query.stored_count[channel] = snapshot.stored_count;
            la_service_frame_query.live_count[channel] = snapshot.live_count;
            if (snapshot.live_count != 0U)
            {
                memcpy(la_service_frame_query.live_data[channel],
                        la_service_query_data,
                        (size_t)snapshot.live_count *
                        sizeof(*la_service_query_data));
            }
        }
    }

    if (service_result == LA_SERVICE_OK)
    {
        la_service_next_query_token++;
        if (la_service_next_query_token == 0U)
        {
            la_service_next_query_token = 1U;
        }
        la_service_frame_query.token = la_service_next_query_token;
        la_service_query_owner = current_task;
        view->token = la_service_frame_query.token;
    }

    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    if (service_result != LA_SERVICE_OK)
    {
        la_service_frame_query.token = 0U;
        (void)xSemaphoreGive(la_service_shared.query_mutex);
    }
    return service_result;
}

/**
 * @brief 按原接口等待建立固定查询视图
 * @note 同任务嵌套立即返回BUSY，不破坏已经持有的视图
 */
LA_Service_Result_t LA_Service_Query_Begin(LA_Service_Query_View_t *view)
{
    return LA_Service_Query_Begin_Ticks(view, portMAX_DELAY);
}

/**
 * @brief 按毫秒总预算取得查询锁和存储锁
 * @note 毫秒向上换算为RTOS Tick，避免非零预算被截为0
 */
LA_Service_Result_t LA_Service_Query_Begin_Timeout(
                                    LA_Service_Query_View_t *view,
                                    uint32_t timeout_ms)
{
    uint64_t ticks;

    if (timeout_ms == UINT32_MAX)
    {
        return LA_Service_Query_Begin_Ticks(view, portMAX_DELAY);
    }
    ticks = (((uint64_t)timeout_ms * configTICK_RATE_HZ) + 999ULL) / 1000ULL;
    if (ticks >= portMAX_DELAY)
    {
        ticks = portMAX_DELAY - 1U;
    }
    return LA_Service_Query_Begin_Ticks(view, (TickType_t)ticks);
}

/**
 * @brief 立即尝试建立固定查询视图
 */
LA_Service_Result_t LA_Service_Query_Try_Begin(LA_Service_Query_View_t *view)
{
    return LA_Service_Query_Begin_Ticks(view, 0U);
}

/**
 * @brief 保留当前终态采集，避免异步消费者取得另一代数据
 * @note 先在存储锁内登记租约，再取得信息；租约期间Reset不可修改数据
 */
LA_Service_Result_t LA_Service_Capture_Acquire(
                                    LA_Service_Capture_Lease_t *lease,
                                    LA_Service_Info_t *info)
{
    LA_Service_Result_t result;

    if ((lease == NULL) || (info == NULL))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if (la_service_shared.storage_mutex == NULL)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }

    (void)xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY);
    taskENTER_CRITICAL();
    if (la_service_shared.capture_lease_token != 0U)
    {
        taskEXIT_CRITICAL();
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        return LA_SERVICE_ERROR_BUSY;
    }
    if (((la_service_shared.state != LA_SERVICE_STATE_DONE) &&
         (la_service_shared.state != LA_SERVICE_STATE_ERROR)) ||
        (la_service_shared.capture_id == 0U))
    {
        taskEXIT_CRITICAL();
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        return LA_SERVICE_ERROR_NOT_READY;
    }

    la_service_next_lease_token++;
    if (la_service_next_lease_token == 0U)
    {
        la_service_next_lease_token = 1U;
    }
    la_service_shared.capture_lease_token = la_service_next_lease_token;
    lease->token = la_service_next_lease_token;
    lease->capture_id = la_service_shared.capture_id;
    taskEXIT_CRITICAL();
    (void)xSemaphoreGive(la_service_shared.storage_mutex);

    result = LA_Service_Get_Info(info);
    if (result != LA_SERVICE_OK)
    {
        (void)LA_Service_Capture_Release(lease);
    }
    return result;
}

/**
 * @brief 释放保留的数据，允许令牌从请求任务交给后台任务
 */
LA_Service_Result_t LA_Service_Capture_Release(
                                    LA_Service_Capture_Lease_t *lease)
{
    if (lease == NULL)
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }

    taskENTER_CRITICAL();
    if ((lease->token == 0U) ||
        (lease->token != la_service_shared.capture_lease_token) ||
        (lease->capture_id != la_service_shared.capture_id))
    {
        taskEXIT_CRITICAL();
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    la_service_shared.capture_lease_token = 0U;
    lease->token = 0U;
    lease->capture_id = 0U;
    taskEXIT_CRITICAL();
    return LA_SERVICE_OK;
}

/**
 * @brief 结束一次完整波形查询并释放查询视图
 * @param view 查询视图
 * @retval LA_SERVICE_OK-成功
 *         LA_SERVICE_ERROR_INVALID_PARAMETER-查询视图无效
 */
LA_Service_Result_t LA_Service_Query_End(LA_Service_Query_View_t *view)
{
    if (LA_Service_Query_View_Is_Valid(view) == 0U)
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }

    la_service_frame_query.token = 0U;
    la_service_query_owner = NULL;
    view->token = 0U;
    (void)xSemaphoreGive(la_service_shared.query_mutex);
    return LA_SERVICE_OK;
}

/**
 * @brief 在固定查询视图中查询时间范围对应的记录下标
 * @param view 查询视图
 * @param channel 逻辑分析仪通道
 * @param start_tick 范围起始计数值
 * @param end_tick 范围结束计数值
 * @param result 返回记录下标范围和范围起点电平
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
LA_Service_Result_t LA_Service_Query_View_Range(
                                        const LA_Service_Query_View_t *view,
                                        LA_Service_Channel_t channel,
                                        uint32_t start_tick,
                                        uint32_t end_tick,
                                        LA_Service_Range_Result_t *result)
{
    LA_Service_Result_t service_result;
    uint32_t total_count;
    uint8_t initial_level;

    if ((LA_Service_Query_View_Is_Valid(view) == 0U) ||
        ((uint32_t)channel >= LA_SERVICE_CHANNEL_COUNT) ||
        (start_tick > end_tick) ||
        (result == NULL))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }

    total_count = la_service_frame_query.stored_count[(uint8_t)channel] +
                  la_service_frame_query.live_count[(uint8_t)channel];
    service_result = LA_Service_Query_View_Bound((uint8_t)channel,
                                                  start_tick,
                                                  0U,
                                                  total_count,
                                                  0U,
                                                  &result->first_index);
    if (service_result == LA_SERVICE_OK)
    {
        service_result = LA_Service_Query_View_Bound(
                                                  (uint8_t)channel,
                                                  end_tick,
                                                  result->first_index,
                                                  total_count,
                                                  1U,
                                                  &result->after_index);
    }
    if (service_result == LA_SERVICE_OK)
    {
        initial_level = (uint8_t)((la_service_frame_query.initial_levels >>
                                  (uint8_t)channel) & 1U);
        result->start_level = (uint8_t)(initial_level ^
                                       (result->first_index & 1U));
    }
    return service_result;
}

/**
 * @brief 在固定查询视图中从指定下标开始查询upper bound
 * @param view 查询视图
 * @param channel 逻辑分析仪通道
 * @param timestamp 指定时间戳
 * @param minimum_index 二分查找的最小下标
 * @param maximum_index 二分查找的最大开区间下标
 * @param index 返回第一个大于指定时间戳的记录下标
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
LA_Service_Result_t LA_Service_Query_View_Upper_Bound(
                                        const LA_Service_Query_View_t *view,
                                        LA_Service_Channel_t channel,
                                        uint32_t timestamp,
                                        uint32_t minimum_index,
                                        uint32_t maximum_index,
                                        uint32_t *index)
{
    if ((LA_Service_Query_View_Is_Valid(view) == 0U) ||
        ((uint32_t)channel >= LA_SERVICE_CHANNEL_COUNT) ||
        (index == NULL))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }

    return LA_Service_Query_View_Bound((uint8_t)channel,
                                        timestamp,
                                        minimum_index,
                                        maximum_index,
                                        1U,
                                        index);
}

/**
 * @brief 从固定查询视图批量读取时间戳
 * @param view 查询视图
 * @param channel 逻辑分析仪通道
 * @param start_index 起始记录下标
 * @param data 返回时间戳的缓冲区
 * @param max_count 缓冲区最多可接收的记录数量
 * @param data_count 返回实际读取的记录数量
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
LA_Service_Result_t LA_Service_Query_View_Get_Data(
                                        const LA_Service_Query_View_t *view,
                                        LA_Service_Channel_t channel,
                                        uint32_t start_index,
                                        uint32_t *data,
                                        uint16_t max_count,
                                        uint16_t *data_count)
{
    uint32_t stored_count;
    uint32_t total_count;
    uint16_t copy_count;
    uint16_t stored_copy_count = 0U;

    if ((LA_Service_Query_View_Is_Valid(view) == 0U) ||
        ((uint32_t)channel >= LA_SERVICE_CHANNEL_COUNT) ||
        (data == NULL) ||
        (max_count == 0U) ||
        (data_count == NULL))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }

    *data_count = 0U;
    stored_count = la_service_frame_query.stored_count[(uint8_t)channel];
    total_count = stored_count +
                  la_service_frame_query.live_count[(uint8_t)channel];
    if (start_index >= total_count)
    {
        return LA_SERVICE_OK;
    }

    copy_count = max_count;
    if ((uint32_t)copy_count > (total_count - start_index))
    {
        copy_count = (uint16_t)(total_count - start_index);
    }
    if (start_index < stored_count)
    {
        uint32_t stored_remaining = stored_count - start_index;

        stored_copy_count = copy_count;
        if ((uint32_t)stored_copy_count > stored_remaining)
        {
            stored_copy_count = (uint16_t)stored_remaining;
        }
        if (LA_Storage_Copy((uint8_t)channel,
                            start_index,
                            data,
                            stored_copy_count) != LA_STORAGE_OK)
        {
            return LA_SERVICE_ERROR_STORAGE;
        }
    }

    if (stored_copy_count < copy_count)
    {
        uint32_t live_index = start_index + stored_copy_count - stored_count;

        memcpy(data + stored_copy_count,
                la_service_frame_query.live_data[(uint8_t)channel] + live_index,
                (size_t)(copy_count - stored_copy_count) * sizeof(*data));
    }

    *data_count = copy_count;
    return LA_SERVICE_OK;
}

/**
 * @brief 查询指定通道在时间窗口内的翻转结果
 * @note 只能在任务上下文调用，查询窗口为包含起止边界的闭区间
 * @param channel 逻辑分析仪通道
 * @param start_tick 窗口起始计数值
 * @param end_tick 窗口结束计数值
 * @param result 返回窗口翻转结果和下一条翻转时间戳
 * @retval LA_SERVICE_OK-成功
 *         其他值-失败
 */
LA_Service_Result_t LA_Service_Query_Window(LA_Service_Channel_t channel,
                                            uint32_t start_tick,
                                            uint32_t end_tick,
                                            LA_Service_Window_Result_t *result)
{
    LA_Service_Query_Snapshot_t snapshot;
    LA_Service_Result_t service_result;
    uint32_t first_index;
    uint32_t after_index;
    uint32_t total_count;
    uint32_t timestamp;
    uint8_t initial_level;

    if (((uint32_t)channel >= LA_SERVICE_CHANNEL_COUNT) ||
        (start_tick > end_tick) ||
        (result == NULL))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if (la_service_shared.storage_mutex == NULL)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }

    result->next_timestamp = 0U;
    result->has_transition = 0U;
    result->end_level = 0U;
    result->has_next = 0U;
    (void)xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY);
    initial_level = (uint8_t)((la_service_shared.initial_levels >> (uint8_t)channel) & 1U);
    service_result = LA_Service_Prepare_Query((uint8_t)channel, &snapshot);
    if (service_result == LA_SERVICE_OK)
    {
        service_result = LA_Service_Query_Lower_Bound((uint8_t)channel,
                                                      &snapshot,
                                                      start_tick,
                                                      &first_index);
    }
    if (service_result != LA_SERVICE_OK)
    {
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        return service_result;
    }

    total_count = snapshot.stored_count + snapshot.live_count;
    result->end_level = (uint8_t)(initial_level ^ (first_index & 1U));
    if (first_index >= total_count)
    {
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        return LA_SERVICE_OK;
    }

    service_result = LA_Service_Query_Get((uint8_t)channel,
                                          &snapshot,
                                          first_index,
                                          &timestamp);
    if (service_result != LA_SERVICE_OK)
    {
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        return service_result;
    }
    if (timestamp > end_tick)
    {
        result->next_timestamp = timestamp;
        result->has_next = 1U;
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        return LA_SERVICE_OK;
    }

    service_result = LA_Service_Query_Upper_Bound((uint8_t)channel,
                                                  &snapshot,
                                                  end_tick,
                                                  &after_index);
    if (service_result != LA_SERVICE_OK)
    {
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        return service_result;
    }

    result->has_transition = 1U;
    result->end_level = (uint8_t)(initial_level ^ (after_index & 1U));
    total_count = snapshot.stored_count + snapshot.live_count;
    if (after_index < total_count)
    {
        service_result = LA_Service_Query_Get((uint8_t)channel,
                                              &snapshot,
                                              after_index,
                                              &result->next_timestamp);
        if (service_result == LA_SERVICE_OK)
        {
            result->has_next = 1U;
        }
    }

    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    return service_result;
}
