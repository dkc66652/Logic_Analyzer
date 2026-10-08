/**
 * @file la_service_internal.h
 * @brief 逻辑分析仪采集与查询实现共享的私有状态
 * @note 仅供LA模块内部使用，外部模块应包含la_service.h
 */

#ifndef LA_SERVICE_INTERNAL_H
#define LA_SERVICE_INTERNAL_H

/* ================================================== */

#include "la/la_service.h"
#include "la/la_ic.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ========================= 配置 ========================= */

#define LA_SERVICE_PENDING_BIT(channel, half) \
    (1UL << (((uint32_t)(channel) * 2U) + (uint32_t)(half)))

/* ================================================== */

typedef struct
{
    SemaphoreHandle_t storage_mutex; /* 串行化存储提交与普通查询 */
    SemaphoreHandle_t query_mutex;   /* Begin至End持有，阻止Reset覆盖查询视图 */
    volatile LA_Service_State_t state; /* 采集任务、中断和查询共享的状态 */
    volatile uint32_t pending_mask;  /* DMA待搬运半区位图，临界区保护 */
    uint32_t capture_id;             /* 已接受的采集代数，临界区保护 */
    uint32_t capture_lease_token;    /* 非零表示终态数据正被保留，临界区保护 */
    uint16_t consumed_index[LA_SERVICE_CHANNEL_COUNT]; /* 各通道已提交的DMA位置 */
    uint32_t deadline_tick; /* 启动成功后发布的采集截止计数值 */
    uint8_t initial_levels; /* 本次采集的六路初始电平 */
} LA_Service_Shared_t;

/* 存储读写持有storage_mutex；中断可变状态按调用上下文使用临界区。 */
extern LA_Service_Shared_t la_service_shared;

/* ================================================== */

#endif /* LA_SERVICE_INTERNAL_H */
