/**
 * @file app_wave_cnt_demo.c
 * @brief 演示调用方提供CNT窗口和500元素六通道共享数组的转换算法。
 * @details 本文件不注册为真实数据源，仅供同事理解显示侧接口和列边界规则。
 */
#include "app_wave_cnt_demo.h"
#include <stddef.h>
#include <string.h>

typedef struct
{
    const uint32_t *edges;
    uint32_t count;
    bool initial_level;
} app_wave_cnt_demo_channel_t;

static const uint32_t s_ch1[] = {700U, 4200U, 8700U, 15000U, 31000U, 80000U};
static const uint32_t s_ch2[] = {1200U, 5200U, 9200U, 13200U, 17200U, 21200U};
static const uint32_t s_ch3[] = {3000U, 13000U, 23000U, 50000U, 90000U};
static const uint32_t s_ch4[] = {2000U, 2500U, 18000U, 18500U, 64000U};
static const uint32_t s_ch5[] = {10000U, 60000U, 110000U, 160000U};
static const uint32_t s_ch6[] = {500U, 1500U, 3500U, 7500U, 15500U};

#define DEMO_COUNT(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))
static const app_wave_cnt_demo_channel_t s_channels[6] = {
    {s_ch1, DEMO_COUNT(s_ch1), false}, {s_ch2, DEMO_COUNT(s_ch2), true},
    {s_ch3, DEMO_COUNT(s_ch3), false}, {s_ch4, DEMO_COUNT(s_ch4), true},
    {s_ch5, DEMO_COUNT(s_ch5), false}, {s_ch6, DEMO_COUNT(s_ch6), true}
};

/** 返回第一条大于等于tick的边沿下标。 */
static uint32_t app_wave_cnt_demo_lower_bound(
    const app_wave_cnt_demo_channel_t *channel, uint32_t tick)
{
    uint32_t first = 0U;
    uint32_t count = channel->count;
    while(count != 0U) {
        const uint32_t step = count / 2U;
        const uint32_t index = first + step;
        if(channel->edges[index] < tick) {
            first = index + 1U;
            count -= step + 1U;
        }
        else count = step;
    }
    return first;
}

/**
 * @brief 一次调用把六路升序CNT边沿转换到共享uint16_t列数组。
 * @details 每路独立计算初始电平和列末电平，再写入该通道对应的2 bit。
 */
bool App_Wave_Cnt_Demo_Query_Window(uint32_t s_tick,
                                    uint32_t e_tick,
                                    uint32_t window_tick,
                                    uint16_t *columns,
                                    uint32_t column_count,
                                    uint32_t *valid_columns,
                                    uint8_t *initial_levels)
{
    uint32_t channel_index;

    if(columns == NULL || valid_columns == NULL || initial_levels == NULL ||
       column_count == 0U || column_count > 500U || window_tick == 0U ||
       e_tick < s_tick) return false;

    *initial_levels = 0U;
    *valid_columns = (e_tick == s_tick) ? 0U :
        (((e_tick - s_tick) >= window_tick) ? column_count :
         (uint32_t)((((uint64_t)(e_tick - s_tick) * column_count) +
                     window_tick - 1U) / window_tick));
    memset(columns, 0, column_count * sizeof(columns[0]));

    for(channel_index = 0U; channel_index < 6U; channel_index++) {
        const app_wave_cnt_demo_channel_t *channel = &s_channels[channel_index];
        uint32_t edge_index = app_wave_cnt_demo_lower_bound(channel, s_tick);
        uint32_t column_index;
        bool level = channel->initial_level ^ ((edge_index & 1U) != 0U);

        if(level) *initial_levels |= (uint8_t)(1U << channel_index);
        for(column_index = 0U; column_index < *valid_columns; column_index++) {
            uint32_t column_end = s_tick +
                (uint32_t)(((uint64_t)(column_index + 1U) * window_tick) /
                           column_count);
            bool changed = false;

            if(column_end > e_tick) column_end = e_tick;
            while(edge_index < channel->count &&
                  channel->edges[edge_index] < column_end) {
                level = !level;
                changed = true;
                edge_index++;
            }
            if(changed) {
                columns[column_index] |= (uint16_t)(
                    (level ? 0x02U : 0x01U) << (channel_index * 2U));
            }
        }
    }
    return true;
}
