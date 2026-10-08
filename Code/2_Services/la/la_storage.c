/**
 * @file la_storage.c
 * @brief 逻辑分析仪采集数据存储实现
 * @note 通道内逻辑块连续，物理块可在共享块池中不连续分配
 */

/* ================================================== */

#include "la/la_storage.h"
#include "memory/memory_placement.h"
#include "stddef.h"
#include "string.h"

/* ================================================== */

#if (LA_STORAGE_BLOCK_COUNT >= UINT16_MAX)
    #error "LA_STORAGE_BLOCK_COUNT must be less than UINT16_MAX"
#endif

#if (LA_STORAGE_BLOCK_CAPACITY == 0U)
    #error "LA_STORAGE_BLOCK_CAPACITY must not be zero"
#endif

/* ================================================== */

PLATFORM_SDRAM_LA_ZI
PLATFORM_CACHE_ALIGNMENT_ATTRIBUTE
static uint32_t la_storage_data[LA_STORAGE_BLOCK_COUNT][LA_STORAGE_BLOCK_CAPACITY]; /* SDRAM物理数据块池 */
static uint16_t la_storage_block_map[LA_STORAGE_CHANNEL_COUNT][LA_STORAGE_BLOCK_COUNT]; /* 通道逻辑块映射 */
static uint32_t la_storage_channel_count[LA_STORAGE_CHANNEL_COUNT]; /* 通道有效记录数量 */
static uint16_t la_storage_channel_block_count[LA_STORAGE_CHANNEL_COUNT]; /* 通道已分配块数量 */
static uint16_t la_storage_next_block = 0U; /* 下一个可分配物理块 */

/* ================================================== */

/**
 * @brief 取得指定记录的时间戳地址
 * @note 调用者必须保证通道和记录下标有效
 * @param channel 通道编号
 * @param index 通道内记录下标
 * @retval 时间戳地址
 */
static const uint32_t *LA_Storage_Get_Address(uint8_t channel, uint32_t index)
{
    uint32_t logical_block = index / LA_STORAGE_BLOCK_CAPACITY;
    uint16_t block_offset = (uint16_t)(index % LA_STORAGE_BLOCK_CAPACITY);
    uint16_t physical_block = la_storage_block_map[channel][logical_block];

    return &la_storage_data[physical_block][block_offset];
}

/* ================================================== */

/**
 * @brief 逻辑分析仪存储初始化
 * @note 初始化共享存储块池和通道映射表
 * @param 无
 * @retval 无
 */
void LA_Storage_Init(void)
{
    LA_Storage_Reset();
}

/**
 * @brief 复位逻辑分析仪存储
 * @note 只复位有效数量和分配游标；Reserve在发布记录前覆盖有效映射。
 *       所有读接口受记录数量约束，旧映射与数据均不需要逐块清零。
 * @param 无
 * @retval 无
 */
void LA_Storage_Reset(void)
{
    for (uint8_t channel = 0U; channel < LA_STORAGE_CHANNEL_COUNT; channel++)
    {
        la_storage_channel_count[channel] = 0U;
        la_storage_channel_block_count[channel] = 0U;
    }

    la_storage_next_block = 0U;
}

/**
 * @brief 为通道预留一段可由MDMA直接写入的连续空间
 * @note 预留不会增加有效记录数，必须在MDMA完成后调用Commit
 * @param channel 通道编号
 * @param count 需要预留的记录数量，不得跨越物理块边界
 * @param destination 返回SDRAM写入地址
 * @retval LA_STORAGE_OK-成功
 *         其他值-失败
 */
LA_Storage_Result_t LA_Storage_Reserve(uint8_t channel,
                                       uint16_t count,
                                       uint32_t **destination)
{
    uint16_t block_offset;
    uint16_t physical_block;

    if ((channel >= LA_STORAGE_CHANNEL_COUNT) ||
        (count == 0U) ||
        (count > LA_STORAGE_BLOCK_CAPACITY) ||
        (destination == NULL))
    {
        return LA_STORAGE_ERROR_INVALID_PARAMETER;
    }

    block_offset = (uint16_t)(la_storage_channel_count[channel] %
                              LA_STORAGE_BLOCK_CAPACITY);
    if (count > (uint16_t)(LA_STORAGE_BLOCK_CAPACITY - block_offset))
    {
        return LA_STORAGE_ERROR_INVALID_PARAMETER;
    }

    if (block_offset == 0U)
    {
        uint16_t logical_block;

        if (la_storage_next_block >= LA_STORAGE_BLOCK_COUNT)
        {
            return LA_STORAGE_ERROR_FULL;
        }

        logical_block = la_storage_channel_block_count[channel];
        physical_block = la_storage_next_block;
        la_storage_next_block++;
        la_storage_block_map[channel][logical_block] = physical_block;
        la_storage_channel_block_count[channel]++;
    }
    else
    {
        uint16_t logical_block =
                (uint16_t)(la_storage_channel_block_count[channel] - 1U);

        physical_block = la_storage_block_map[channel][logical_block];
    }

    *destination = &la_storage_data[physical_block][block_offset];
    return LA_STORAGE_OK;
}

/**
 * @brief 提交已经由MDMA写入完成的通道记录
 * @note 只能提交最近一次Reserve返回的连续空间
 * @param channel 通道编号
 * @param count 已完成写入的记录数量
 * @retval LA_STORAGE_OK-成功
 *         LA_STORAGE_ERROR_INVALID_PARAMETER-参数错误
 */
LA_Storage_Result_t LA_Storage_Commit(uint8_t channel, uint16_t count)
{
    uint16_t block_offset;

    if ((channel >= LA_STORAGE_CHANNEL_COUNT) ||
        (count == 0U) ||
        (count > LA_STORAGE_BLOCK_CAPACITY) ||
        (la_storage_channel_block_count[channel] == 0U))
    {
        return LA_STORAGE_ERROR_INVALID_PARAMETER;
    }

    block_offset = (uint16_t)(la_storage_channel_count[channel] %
                              LA_STORAGE_BLOCK_CAPACITY);
    if (count > (uint16_t)(LA_STORAGE_BLOCK_CAPACITY - block_offset))
    {
        return LA_STORAGE_ERROR_INVALID_PARAMETER;
    }

    la_storage_channel_count[channel] += count;
    return LA_STORAGE_OK;
}

/**
 * @brief 获取指定通道的时间戳数量
 * @param channel 通道编号
 * @retval 时间戳数量，通道编号无效时返回0
 */
uint32_t LA_Storage_Get_Count(uint8_t channel)
{
    if (channel >= LA_STORAGE_CHANNEL_COUNT)
    {
        return 0U;
    }

    return la_storage_channel_count[channel];
}

/**
 * @brief 获取指定通道和下标的时间戳
 * @param channel 通道编号
 * @param index 通道内记录下标
 * @param timestamp 返回时间戳
 * @retval LA_STORAGE_OK-成功
 *         LA_STORAGE_ERROR_INVALID_PARAMETER-参数错误
 */
LA_Storage_Result_t LA_Storage_Get(uint8_t channel,
                                   uint32_t index,
                                   uint32_t *timestamp)
{
    if ((channel >= LA_STORAGE_CHANNEL_COUNT) ||
        (timestamp == NULL) ||
        (index >= la_storage_channel_count[channel]))
    {
        return LA_STORAGE_ERROR_INVALID_PARAMETER;
    }

    *timestamp = *LA_Storage_Get_Address(channel, index);
    return LA_STORAGE_OK;
}

/**
 * @brief 批量复制指定通道的连续时间戳
 * @note 通道逻辑块连续，复制过程自动跨越不连续的物理块
 * @param channel 通道编号
 * @param index 起始记录下标
 * @param data 返回时间戳的缓冲区
 * @param count 需要复制的记录数量
 * @retval LA_STORAGE_OK-成功
 *         LA_STORAGE_ERROR_INVALID_PARAMETER-参数错误
 */
LA_Storage_Result_t LA_Storage_Copy(uint8_t channel,
                                    uint32_t index,
                                    uint32_t *data,
                                    uint16_t count)
{
    uint16_t copied_count = 0U;

    if ((channel >= LA_STORAGE_CHANNEL_COUNT) ||
        ((data == NULL) && (count != 0U)) ||
        (index > la_storage_channel_count[channel]) ||
        ((uint32_t)count > (la_storage_channel_count[channel] - index)))
    {
        return LA_STORAGE_ERROR_INVALID_PARAMETER;
    }

    while (copied_count < count)
    {
        uint32_t current_index = index + copied_count;
        uint32_t logical_block = current_index / LA_STORAGE_BLOCK_CAPACITY;
        uint16_t block_offset = (uint16_t)(current_index %
                                           LA_STORAGE_BLOCK_CAPACITY);
        uint16_t physical_block = la_storage_block_map[channel][logical_block];
        uint16_t copy_count = (uint16_t)(count - copied_count);

        if (copy_count > (uint16_t)(LA_STORAGE_BLOCK_CAPACITY - block_offset))
        {
            copy_count = (uint16_t)(LA_STORAGE_BLOCK_CAPACITY - block_offset);
        }

        memcpy(data + copied_count,
                &la_storage_data[physical_block][block_offset],
                (size_t)copy_count * sizeof(*data));
        copied_count = (uint16_t)(copied_count + copy_count);
    }

    return LA_STORAGE_OK;
}

/**
 * @brief 查找第一个大于等于指定时间戳的记录下标
 * @note 返回值允许等于通道记录总数
 * @param channel 通道编号
 * @param timestamp 指定时间戳
 * @param index 返回记录下标
 * @retval LA_STORAGE_OK-成功
 *         LA_STORAGE_ERROR_INVALID_PARAMETER-参数错误
 */
LA_Storage_Result_t LA_Storage_Lower_Bound(uint8_t channel,
                                           uint32_t timestamp,
                                           uint32_t *index)
{
    uint32_t left = 0U;
    uint32_t right;

    if ((channel >= LA_STORAGE_CHANNEL_COUNT) || (index == NULL))
    {
        return LA_STORAGE_ERROR_INVALID_PARAMETER;
    }

    right = la_storage_channel_count[channel];
    while (left < right)
    {
        uint32_t middle = left + ((right - left) / 2U);

        if (*LA_Storage_Get_Address(channel, middle) < timestamp)
        {
            left = middle + 1U;
        }
        else
        {
            right = middle;
        }
    }

    *index = left;
    return LA_STORAGE_OK;
}

/**
 * @brief 查找第一个大于指定时间戳的记录下标
 * @note 返回值允许等于通道记录总数
 * @param channel 通道编号
 * @param timestamp 指定时间戳
 * @param index 返回记录下标
 * @retval LA_STORAGE_OK-成功
 *         LA_STORAGE_ERROR_INVALID_PARAMETER-参数错误
 */
LA_Storage_Result_t LA_Storage_Upper_Bound(uint8_t channel,
                                           uint32_t timestamp,
                                           uint32_t *index)
{
    uint32_t left = 0U;
    uint32_t right;

    if ((channel >= LA_STORAGE_CHANNEL_COUNT) || (index == NULL))
    {
        return LA_STORAGE_ERROR_INVALID_PARAMETER;
    }

    right = la_storage_channel_count[channel];
    while (left < right)
    {
        uint32_t middle = left + ((right - left) / 2U);

        if (*LA_Storage_Get_Address(channel, middle) <= timestamp)
        {
            left = middle + 1U;
        }
        else
        {
            right = middle;
        }
    }

    *index = left;
    return LA_STORAGE_OK;
}
