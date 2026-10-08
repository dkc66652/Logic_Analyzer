/**
 * @file la_ic.h
 * @brief 逻辑分析仪采集驱动头文件
 * @note 提供逻辑分析仪输入捕获硬件的启动、停止、收尾和事件回调接口
 */

#ifndef LA_IC_H
#define LA_IC_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stdint.h"

/* ================================================== */

#define BSP_LA_IC_CHANNEL_COUNT       6U    /* 输入捕获通道数量 */
#define BSP_LA_IC_DMA_BUFFER_COUNT    1024U /* 每路DMA循环缓冲区记录数，必须为偶数 */

/* ================================================== */

typedef enum
{
    BSP_LA_IC_CHANNEL_TIM2_CH1 = 0,
    BSP_LA_IC_CHANNEL_TIM2_CH3,
    BSP_LA_IC_CHANNEL_TIM2_CH4,
    BSP_LA_IC_CHANNEL_TIM5_CH1,
    BSP_LA_IC_CHANNEL_TIM5_CH3,
    BSP_LA_IC_CHANNEL_TIM5_CH4,
    BSP_LA_IC_CHANNEL_NONE
} BSP_LA_IC_Channel_t;

typedef enum
{
    BSP_LA_IC_EVENT_DMA_HALF = 0,
    BSP_LA_IC_EVENT_DMA_FULL,
    BSP_LA_IC_EVENT_DEADLINE,
    BSP_LA_IC_EVENT_DMA_ERROR
} BSP_LA_IC_Event_Type_t;

typedef enum
{
    BSP_LA_IC_OK = 0,
    BSP_LA_IC_ERROR_BUSY,
    BSP_LA_IC_ERROR_INVALID_PARAMETER,
    BSP_LA_IC_ERROR_DMA,
    BSP_LA_IC_ERROR_START_COLLISION,
    BSP_LA_IC_ERROR_STATE
} BSP_LA_IC_Result_t;

typedef struct
{
    BSP_LA_IC_Event_Type_t type; /* 事件类型 */
    BSP_LA_IC_Channel_t channel; /* 事件对应通道 */
    const uint32_t *data;        /* DMA半区首地址，无数据时为NULL */
    uint16_t count;              /* DMA半区记录数，无数据时为0 */
} BSP_LA_IC_Event_t;

typedef struct
{
    uint32_t timestamp_hz;  /* 实际时间戳频率 */
    uint32_t deadline_tick; /* 采集截止计数值 */
    uint8_t initial_levels; /* 六路初始电平，bit0..5对应通道枚举0..5 */
} BSP_LA_IC_Start_Result_t;

typedef struct
{
    const uint32_t *buffer[BSP_LA_IC_CHANNEL_COUNT]; /* 六路DMA循环缓冲区首地址 */
    uint16_t write_index[BSP_LA_IC_CHANNEL_COUNT];   /* 停止时六路DMA写入位置，等于缓冲区容量表示末尾刚写满 */
    uint32_t cutoff_tick;                            /* 统一采集截止计数值 */
    uint32_t overcapture_mask;                       /* TIM捕获溢出通道位图 */
    uint32_t completed_half_mask;                    /* 停稳时未被回调消费的HT/TC，bit(channel*2+half) */
} BSP_LA_IC_Final_Result_t;

typedef struct
{
    const uint32_t *buffer; /* 通道DMA循环缓冲区首地址 */
    uint16_t write_index;   /* 快照时DMA写入位置，等于缓冲区容量表示末尾刚写满 */
} BSP_LA_IC_Live_Result_t;

/* event指针只在回调执行期间有效，回调在DMA或TIM中断上下文执行 */
typedef void (*BSP_LA_IC_Event_Callback_t)(const BSP_LA_IC_Event_t *event, void *context);

/* ================================================== */

void BSP_LA_IC_Init(void);
void BSP_LA_IC_Set_Event_Callback(BSP_LA_IC_Event_Callback_t callback, void *context);
BSP_LA_IC_Result_t BSP_LA_IC_Start(uint32_t timestamp_hz,
                                   uint32_t duration_us,
                                   BSP_LA_IC_Start_Result_t *result);
BSP_LA_IC_Result_t BSP_LA_IC_Stop(uint32_t *cutoff_tick);
BSP_LA_IC_Result_t BSP_LA_IC_Get_Live(BSP_LA_IC_Channel_t channel,
                                      BSP_LA_IC_Live_Result_t *result);
BSP_LA_IC_Result_t BSP_LA_IC_Get_Current_Tick(uint32_t *current_tick);
BSP_LA_IC_Result_t BSP_LA_IC_Finalize(BSP_LA_IC_Final_Result_t *result);
BSP_LA_IC_Result_t BSP_LA_IC_Reset(void);
/* 校验待提交半区未被复用；运行时读取硬件，Finalize后使用停稳快照。 */
BSP_LA_IC_Result_t BSP_LA_IC_Validate_DMA_Half(
        BSP_LA_IC_Channel_t channel, uint8_t half);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LA_IC_H */
