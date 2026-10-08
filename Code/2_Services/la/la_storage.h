/**
 * @file la_storage.h
 * @brief 逻辑分析仪采集数据存储头文件
 * @note LA服务内部接口，使用共享固定块池和通道逻辑块映射保存边沿时间戳。
 *       本模块不自行加锁，由采集与查询实现统一管理存储和查询锁。
 *       App及其他服务使用la_service.h，不直接预留或提交物理存储。
 */

#ifndef LA_STORAGE_H
#define LA_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stdint.h"

/* ================================================== */

#define LA_STORAGE_CHANNEL_COUNT    6U     /* 存储通道数量 */
#define LA_STORAGE_BLOCK_COUNT      11264U /* 22MB共享存储块数量 */
#define LA_STORAGE_BLOCK_CAPACITY   512U   /* 每个存储块的时间戳数量 */

/* ================================================== */

typedef enum
{
    LA_STORAGE_OK = 0,
    LA_STORAGE_ERROR_INVALID_PARAMETER,
    LA_STORAGE_ERROR_FULL
} LA_Storage_Result_t;

/* ================================================== */

void LA_Storage_Init(void);
void LA_Storage_Reset(void);
LA_Storage_Result_t LA_Storage_Reserve(uint8_t channel,
                                       uint16_t count,
                                       uint32_t **destination);
LA_Storage_Result_t LA_Storage_Commit(uint8_t channel, uint16_t count);
uint32_t LA_Storage_Get_Count(uint8_t channel);
LA_Storage_Result_t LA_Storage_Get(uint8_t channel,
                                   uint32_t index,
                                   uint32_t *timestamp);
/* data为独立调用方缓冲，不能与内部存储池重叠；复制按物理块分段。 */
LA_Storage_Result_t LA_Storage_Copy(uint8_t channel,
                                    uint32_t index,
                                    uint32_t *data,
                                    uint16_t count);
LA_Storage_Result_t LA_Storage_Lower_Bound(uint8_t channel,
                                           uint32_t timestamp,
                                           uint32_t *index);
LA_Storage_Result_t LA_Storage_Upper_Bound(uint8_t channel,
                                           uint32_t timestamp,
                                           uint32_t *index);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LA_STORAGE_H */
