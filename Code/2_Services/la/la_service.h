/**
 * @file la_service.h
 * @brief 逻辑分析仪服务头文件
 * @note 提供采集控制、状态管理和时间窗口翻转查询接口
 */

#ifndef LA_SERVICE_H
#define LA_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stdint.h"

/* ================================================== */

#define LA_SERVICE_CHANNEL_COUNT    6U /* 逻辑分析仪通道数量 */

/* ================================================== */

typedef enum
{
    LA_SERVICE_CHANNEL_1 = 0,
    LA_SERVICE_CHANNEL_2,
    LA_SERVICE_CHANNEL_3,
    LA_SERVICE_CHANNEL_4,
    LA_SERVICE_CHANNEL_5,
    LA_SERVICE_CHANNEL_6
} LA_Service_Channel_t;

typedef enum
{
    LA_SERVICE_STATE_RESET = 0,
    LA_SERVICE_STATE_IDLE,
    LA_SERVICE_STATE_STARTING,
    LA_SERVICE_STATE_RUNNING,
    LA_SERVICE_STATE_STOPPING,
    LA_SERVICE_STATE_DONE,
    LA_SERVICE_STATE_ERROR,
    LA_SERVICE_STATE_IMPORTING
} LA_Service_State_t;

typedef enum
{
    LA_SERVICE_STOP_NONE = 0,
    LA_SERVICE_STOP_DEADLINE,
    LA_SERVICE_STOP_USER,
    LA_SERVICE_STOP_STORAGE_FULL,
    LA_SERVICE_STOP_DMA_OVERRUN,
    LA_SERVICE_STOP_DMA_ERROR,
    LA_SERVICE_STOP_TIM_OVERCAPTURE,
    LA_SERVICE_STOP_HARDWARE_ERROR,
    LA_SERVICE_STOP_TRANSFER_TIMEOUT
} LA_Service_Stop_Reason_t;

typedef enum
{
    LA_SERVICE_OK = 0,
    LA_SERVICE_ERROR_BUSY,
    LA_SERVICE_ERROR_INVALID_PARAMETER,
    LA_SERVICE_ERROR_NOT_READY,
    LA_SERVICE_ERROR_HARDWARE,
    LA_SERVICE_ERROR_STORAGE,
    LA_SERVICE_ERROR_STATE,
    LA_SERVICE_ERROR_TIMEOUT
} LA_Service_Result_t;

typedef enum
{
    LA_SERVICE_EVENT_COMPLETE = 0,
    LA_SERVICE_EVENT_ERROR
} LA_Service_Event_Type_t;

typedef struct
{
    LA_Service_State_t state;              /* 服务当前状态 */
    LA_Service_Stop_Reason_t stop_reason;  /* 采集停止原因 */
    uint32_t capture_id;                   /* 非零采集代数，复位后不回退 */
    uint32_t timestamp_hz;                 /* 实际时间戳频率 */
    uint32_t deadline_tick;                /* 配置的采集截止计数值 */
    uint32_t cutoff_tick;                  /* 实际采集截止计数值 */
    uint32_t overcapture_mask;             /* TIM捕获溢出通道位图 */
    uint32_t data_count[LA_SERVICE_CHANNEL_COUNT]; /* 六路已存储时间戳数量 */
    uint8_t initial_levels;                /* 六路采集初始电平位图 */
    uint8_t finalize_failed;               /* BSP收尾失败，不能据此认为硬件已停稳 */
} LA_Service_Info_t;

typedef struct
{
    uint32_t next_timestamp; /* 第一个严格大于窗口结束值的翻转时间戳 */
    uint8_t has_transition;  /* 窗口内是否存在翻转 */
    uint8_t end_level;       /* 窗口内最后一次翻转后的电平 */
    uint8_t has_next;        /* 是否存在next_timestamp */
} LA_Service_Window_Result_t;

typedef struct
{
    uint32_t first_index; /* 第一个大于等于范围起点的记录下标 */
    uint32_t after_index; /* 第一个大于范围终点的记录下标 */
    uint8_t start_level;  /* 范围起点发生翻转前的电平 */
} LA_Service_Range_Result_t;

typedef struct
{
    uint32_t token; /* 由Begin/End管理，不得复制并当作独立视图使用 */
} LA_Service_Query_View_t;

typedef struct
{
    uint32_t token;      /* 单实例数据保留凭据，不得重复释放 */
    uint32_t capture_id; /* 保留的采集代数 */
} LA_Service_Capture_Lease_t;

typedef struct
{
    uint32_t token; /* 由Begin/Commit/Abort管理，不得复制或跨任务使用 */
} LA_Service_Import_t;

typedef struct
{
    LA_Service_Event_Type_t type; /* 服务事件类型 */
    LA_Service_Info_t info;       /* 事件发生时的采集信息 */
} LA_Service_Event_t;

/* 回调由逻辑分析仪服务任务调用，event指针只在回调执行期间有效 */
typedef void (*LA_Service_Event_Callback_t)(const LA_Service_Event_t *event, void *context);

/* ================================================== */

/*
 * 除初始化外，以下控制、信息及查询接口均在任务上下文调用。
 * 普通查询按需读取本次DMA弱快照，采集在复制期间继续运行，不重试。
 * 时间范围采用闭区间；指定时间戳恰好有边沿时，起点电平为翻转前的电平。
 */
uint8_t LA_Service_Init(void);
void LA_Service_Set_Event_Callback(LA_Service_Event_Callback_t callback, void *context);
/*
 * Start同步失败不产生终态回调。参数错误且BSP未启动时保持IDLE；
 * 已接受请求后的BSP硬件/状态错误进入ERROR，成功Reset后才能再次Start。
 * 即使返回BUSY，若BSP与已接受的服务启动状态不一致，也会进入ERROR。
 */
LA_Service_Result_t LA_Service_Start(uint32_t timestamp_hz, uint32_t duration_us);
/* Stop硬件失败仍通知后台尝试收尾，返回错误不表示服务继续采集。 */
LA_Service_Result_t LA_Service_Stop(void);
LA_Service_Result_t LA_Service_Reset(void);
LA_Service_Result_t LA_Service_Get_Info(LA_Service_Info_t *info);
LA_Service_Result_t LA_Service_Get_Current_Tick(uint32_t *current_tick);
LA_Service_Result_t LA_Service_Get_Levels(uint32_t timestamp, uint8_t *levels);
LA_Service_Result_t LA_Service_Query_Range(LA_Service_Channel_t channel,
                                           uint32_t start_tick,
                                           uint32_t end_tick,
                                           LA_Service_Range_Result_t *result);
LA_Service_Result_t LA_Service_Get_Data(LA_Service_Channel_t channel,
                                        uint32_t start_index,
                                        uint32_t *data,
                                        uint16_t max_count,
                                        uint16_t *data_count);

/**
 * @brief 保留已结束的采集数据并取得一致的采集信息
 * @note 仅支持一个活动租约；成功后Reset返回BUSY，直至Release。
 *       租约不持有任务互斥锁，可以由接受请求的任务交给后台任务释放。
 *       租约不占用查询视图；仍须按查询接口约束取得视图。
 */
LA_Service_Result_t LA_Service_Capture_Acquire(
                                        LA_Service_Capture_Lease_t *lease,
                                        LA_Service_Info_t *info);
LA_Service_Result_t LA_Service_Capture_Release(
                                        LA_Service_Capture_Lease_t *lease);

/**
 * @brief 从已验证的文件数据恢复采集，所有操作必须由同一任务完成
 * @note Begin仅接受IDLE；加载前须显式Reset，不自动丢弃当前采集。
 *       metadata.state只能为DONE或ERROR，capture_id为非零来源信息，不复用。
 *       IMPORTING期间不能Start、Reset、查询或取得采集租约；不长期持锁。
 *       Append数据为独立调用方缓冲，单调不减并允许重复时间戳。
 *       Append失败后调用Abort；Abort丢弃本次部分数据并回到IDLE。
 *       Commit要求各通道数量完整，分配本机新capture_id，不通知采集回调。
 *       本接口不保留旧SDRAM副本，不提供旧采集回滚。
 */
/* 纯只读元数据与共享块池容量检查，不依赖硬件或服务初始化。 */
LA_Service_Result_t LA_Service_Import_Validate(const LA_Service_Info_t *metadata);
LA_Service_Result_t LA_Service_Import_Begin(
                                        LA_Service_Import_t *import,
                                        const LA_Service_Info_t *metadata);
LA_Service_Result_t LA_Service_Import_Append(
                                        const LA_Service_Import_t *import,
                                        LA_Service_Channel_t channel,
                                        const uint32_t *data,
                                        uint16_t count);
LA_Service_Result_t LA_Service_Import_Commit(LA_Service_Import_t *import);
LA_Service_Result_t LA_Service_Import_Abort(LA_Service_Import_t *import);

/**
 * @brief 建立六通道固定查询视图
 * @note Begin成功后必须由同一任务调用End，所有失败分支也须配对释放。
 *       仅支持一个活动视图，嵌套Begin返回BUSY，持有视图时Reset返回BUSY。
 *       视图持有查询锁，阻止Reset；仅建立快照时持有存储锁，允许采集继续提交。
 *       运行中的六路数据各自只读取一次DMA位置，不保证六路同时刻的强快照。
 * @param view 返回本次视图标识，失败时不持有查询锁
 * @retval LA_SERVICE_OK-成功，其他值-参数或采集状态不允许查询
 */
LA_Service_Result_t LA_Service_Query_Begin(LA_Service_Query_View_t *view);

/**
 * @brief 有界等待建立查询视图
 * @note timeout_ms为两把锁的总等待预算，0表示立即尝试。
 *       UINT32_MAX使用portMAX_DELAY；是否无限等待取决于RTOS配置。
 *       任务拥有者规则与Begin相同。
 * @retval LA_SERVICE_ERROR_BUSY-立即尝试失败或同任务重复获取
 *         LA_SERVICE_ERROR_TIMEOUT-有界等待超时，其他值同Begin
 */
LA_Service_Result_t LA_Service_Query_Begin_Timeout(
                                        LA_Service_Query_View_t *view,
                                        uint32_t timeout_ms);
LA_Service_Result_t LA_Service_Query_Try_Begin(LA_Service_Query_View_t *view);

/**
 * @brief 结束当前查询视图并释放查询锁
 * @note End之后原视图以及其副本均失效。View查询接口只能在Begin至End期间，
 *       由持有视图的任务调用；不要将视图交给其他任务或中断使用。
 * @param view 本任务Begin成功取得的视图
 * @retval LA_SERVICE_OK-成功，LA_SERVICE_ERROR_INVALID_PARAMETER-视图无效
 */
LA_Service_Result_t LA_Service_Query_End(LA_Service_Query_View_t *view);
LA_Service_Result_t LA_Service_Query_View_Range(
                                        const LA_Service_Query_View_t *view,
                                        LA_Service_Channel_t channel,
                                        uint32_t start_tick,
                                        uint32_t end_tick,
                                        LA_Service_Range_Result_t *result);
LA_Service_Result_t LA_Service_Query_View_Upper_Bound(
                                        const LA_Service_Query_View_t *view,
                                        LA_Service_Channel_t channel,
                                        uint32_t timestamp,
                                        uint32_t minimum_index,
                                        uint32_t maximum_index,
                                        uint32_t *index);
LA_Service_Result_t LA_Service_Query_View_Get_Data(
                                        const LA_Service_Query_View_t *view,
                                        LA_Service_Channel_t channel,
                                        uint32_t start_index,
                                        uint32_t *data,
                                        uint16_t max_count,
                                        uint16_t *data_count);
LA_Service_Result_t LA_Service_Query_Window(LA_Service_Channel_t channel,
                                            uint32_t start_tick,
                                            uint32_t end_tick,
                                            LA_Service_Window_Result_t *result);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LA_SERVICE_H */
