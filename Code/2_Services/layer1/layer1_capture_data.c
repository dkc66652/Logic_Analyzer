/**
 ******************************************************************************
 * @file    layer1_capture_data.c
 * @brief   同事LA Service的采集控制、固定视图查询和显示列转换实现。
 ******************************************************************************
 */
#include "layer1_capture_data.h"
#include "platform_config.h"

#include <string.h>

#define WAVE_CAPTURE_TIMESTAMP_HZ       1000000U
#define WAVE_CAPTURE_EDGE_BATCH_COUNT         128U

#if !PLATFORM_LA_CAPTURE_ENABLE && !WAVE_CAPTURE_DATA_USE_CNT_DEMO
#error "CNT Demo must remain enabled while the LA capture platform is disabled"
#endif

static uint32_t s_last_tick;

#if WAVE_CAPTURE_DATA_USE_CNT_DEMO

#include "app_wave_cnt_demo.h"
#include "FreeRTOS.h"
#include "task.h"

static TickType_t s_demo_start_rtos_tick;
static uint32_t s_demo_duration_cnt;
static bool s_demo_running;

/** 把FreeRTOS经过时间换算成1MHz CNT，并限制在本轮演示时长内。 */
static uint32_t wave_capture_demo_cnt_at_tick(TickType_t rtos_tick)
{
    const TickType_t elapsed_tick = rtos_tick - s_demo_start_rtos_tick;
    uint64_t elapsed_cnt;

    if(!s_demo_running) return s_last_tick;
    elapsed_cnt = ((uint64_t)elapsed_tick * WAVE_CAPTURE_TIMESTAMP_HZ) /
                  configTICK_RATE_HZ;
    if(elapsed_cnt >= s_demo_duration_cnt) {
        s_last_tick = s_demo_duration_cnt;
        s_demo_running = false;
    }
    else {
        s_last_tick = (uint32_t)elapsed_cnt;
    }
    return s_last_tick;
}

#else

#include "la/la_service.h"

static uint32_t s_edge_batch[WAVE_CAPTURE_EDGE_BATCH_COUNT];

#endif

/** 初始化显示侧记录的截止CNT。 */
void wave_capture_data_init(void)
{
    s_last_tick = 0U;
#if WAVE_CAPTURE_DATA_USE_CNT_DEMO
    s_demo_start_rtos_tick = 0U;
    s_demo_duration_cnt = 0U;
    s_demo_running = false;
#endif
}

/** 启动当前选择的数据源；Demo模式不会启动TIM、DMA1或LA Service采集。 */
bool wave_capture_data_begin(uint32_t duration_cnt)
{
#if WAVE_CAPTURE_DATA_USE_CNT_DEMO
    if(duration_cnt == 0U) return false;
    s_last_tick = 0U;
    s_demo_duration_cnt = duration_cnt;
    s_demo_start_rtos_tick = xTaskGetTickCount();
    s_demo_running = true;
    return true;
#else
    LA_Service_Info_t info;

    if(duration_cnt == 0U) return false;
    if(LA_Service_Get_Info(&info) == LA_SERVICE_OK &&
       info.state != LA_SERVICE_STATE_IDLE) {
        if(LA_Service_Reset() != LA_SERVICE_OK) return false;
    }
    s_last_tick = 0U;
    return LA_Service_Start(WAVE_CAPTURE_TIMESTAMP_HZ, duration_cnt) ==
           LA_SERVICE_OK;
#endif
}

/** 停止采集；终态时保留原数据并返回成功。 */
bool wave_capture_data_stop(void)
{
#if WAVE_CAPTURE_DATA_USE_CNT_DEMO
    if(s_demo_running) {
        (void)wave_capture_demo_cnt_at_tick(xTaskGetTickCount());
        s_demo_running = false;
    }
    return true;
#else
    LA_Service_Info_t info;

    if(LA_Service_Get_Info(&info) != LA_SERVICE_OK) return false;
    if(info.state == LA_SERVICE_STATE_RUNNING ||
       info.state == LA_SERVICE_STATE_STARTING) {
        return LA_Service_Stop() == LA_SERVICE_OK;
    }
    return info.state == LA_SERVICE_STATE_STOPPING ||
           info.state == LA_SERVICE_STATE_DONE ||
           info.state == LA_SERVICE_STATE_ERROR;
#endif
}

/** 读取运行CNT；终态使用服务保存的实际截止CNT。 */
uint32_t wave_capture_data_get_now_cnt(void)
{
#if WAVE_CAPTURE_DATA_USE_CNT_DEMO
    return wave_capture_demo_cnt_at_tick(xTaskGetTickCount());
#else
    uint32_t tick;
    LA_Service_Info_t info;

    if(LA_Service_Get_Current_Tick(&tick) == LA_SERVICE_OK) {
        s_last_tick = tick;
    }
    else if(LA_Service_Get_Info(&info) == LA_SERVICE_OK &&
            (info.state == LA_SERVICE_STATE_DONE ||
             info.state == LA_SERVICE_STATE_ERROR)) {
        s_last_tick = info.cutoff_tick;
    }
    return s_last_tick;
#endif
}

/** 返回指定帧时刻对应的CNT；真实模式仍直接读取LA硬件计数器。 */
uint32_t wave_capture_data_get_cnt_at_tick(uint32_t rtos_tick)
{
#if WAVE_CAPTURE_DATA_USE_CNT_DEMO
    return wave_capture_demo_cnt_at_tick((TickType_t)rtos_tick);
#else
    (void)rtos_tick;
    return wave_capture_data_get_now_cnt();
#endif
}

/**
 * @brief 在同一个固定查询视图内分批读取六路边沿并压缩为500列。
 * @details 每列uint16_t为六路共享结果，各通道使用相邻2 bit。转换按通道
 *          顺序完成并通过按位或写入，因此不需要保存六份中间列数组。
 */
bool wave_capture_data_request_window(uint32_t s_tick,
                                      uint32_t e_tick,
                                      uint32_t window_tick,
                                      uint16_t *columns,
                                      uint32_t *valid_columns,
                                      uint8_t *initial_levels)
{
#if WAVE_CAPTURE_DATA_USE_CNT_DEMO
    return App_Wave_Cnt_Demo_Query_Window(s_tick, e_tick, window_tick,
                                          columns,
                                          WAVE_CAPTURE_DATA_COLUMN_COUNT,
                                          valid_columns, initial_levels);
#else
    LA_Service_Query_View_t view = {0};
    LA_Service_Result_t result = LA_SERVICE_OK;
    uint32_t channel_index;
    bool success = false;

    if(columns == NULL || valid_columns == NULL || initial_levels == NULL ||
       window_tick == 0U || e_tick < s_tick) return false;

    memset(columns, 0, sizeof(columns[0]) * WAVE_CAPTURE_DATA_COLUMN_COUNT);
    *initial_levels = 0U;
    *valid_columns = (e_tick == s_tick) ? 0U :
        (((e_tick - s_tick) >= window_tick) ? WAVE_CAPTURE_DATA_COLUMN_COUNT :
         (uint32_t)((((uint64_t)(e_tick - s_tick) *
                      WAVE_CAPTURE_DATA_COLUMN_COUNT) + window_tick - 1U) /
                    window_tick));

    /* 尚无有效采集区间时，零列本身就是一个完整且可显示的查询结果。 */
    if(*valid_columns == 0U) return true;

    if(LA_Service_Query_Try_Begin(&view) != LA_SERVICE_OK) return false;

    for(channel_index = 0U;
        channel_index < WAVE_CAPTURE_DATA_CHANNEL_COUNT;
        channel_index++) {
        LA_Service_Range_Result_t range;
        uint32_t next_index;
        uint32_t remaining;
        uint16_t batch_count = 0U;
        uint16_t batch_offset = 0U;
        uint32_t column_index;
        const uint32_t bit_shift = channel_index * 2U;
        bool level;

        result = LA_Service_Query_View_Range(&view,
            (LA_Service_Channel_t)channel_index, s_tick, e_tick, &range);
        if(result != LA_SERVICE_OK) goto query_end;

        level = range.start_level != 0U;
        if(level) *initial_levels |= (uint8_t)(1U << channel_index);
        next_index = range.first_index;
        remaining = range.after_index - range.first_index;

        for(column_index = 0U; column_index < *valid_columns; column_index++) {
            uint32_t column_end = s_tick +
                (uint32_t)(((uint64_t)(column_index + 1U) * window_tick) /
                           WAVE_CAPTURE_DATA_COLUMN_COUNT);
            bool has_transition = false;

            /* e_tick为有效数据右边界，不能把未来列或右边界边沿提前画入。 */
            if(column_end > e_tick) column_end = e_tick;

            for(;;) {
                uint32_t edge_tick;

                if(batch_offset >= batch_count) {
                    uint16_t request_count;

                    if(remaining == 0U) break;
                    request_count = (remaining > WAVE_CAPTURE_EDGE_BATCH_COUNT) ?
                        WAVE_CAPTURE_EDGE_BATCH_COUNT : (uint16_t)remaining;
                    result = LA_Service_Query_View_Get_Data(
                        &view, (LA_Service_Channel_t)channel_index, next_index,
                        s_edge_batch, request_count, &batch_count);
                    if(result != LA_SERVICE_OK || batch_count == 0U) {
                        goto query_end;
                    }
                    next_index += batch_count;
                    remaining -= batch_count;
                    batch_offset = 0U;
                }
                edge_tick = s_edge_batch[batch_offset];
                if(edge_tick >= column_end) break;
                batch_offset++;
                level = !level;
                has_transition = true;
            }
            if(has_transition) {
                columns[column_index] |= (uint16_t)(
                    (level ? 0x02U : 0x01U) << bit_shift);
            }
        }
    }
    success = true;

query_end:
    if(LA_Service_Query_End(&view) != LA_SERVICE_OK) success = false;
    return success;
#endif
}
