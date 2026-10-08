/** @file app_wave_cnt_demo.h @brief 自定义CNT到六通道共享列状态的示例接口。 */
#ifndef APP_WAVE_CNT_DEMO_H
#define APP_WAVE_CNT_DEMO_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 把示例中的六路升序CNT边沿一次转换为共享屏幕列状态。
 * @param s_tick 当前显示窗口左端CNT，属于查询范围。
 * @param e_tick 当前有效数据右端CNT，不属于查询范围。
 * @param window_tick 完整显示窗口代表的CNT宽度。
 * @param columns 调用方提供的uint16_t列数组；每通道占2 bit，00无翻转、
 *                01翻转后低、10翻转后高，bit0~bit11依次对应六个通道。
 * @param column_count columns元素数；GUI正常传入500，不能大于500。
 * @param valid_columns 返回有效数据覆盖的列数。
 * @param initial_levels 返回s_tick处六路电平位图，bit0~bit5对应通道0~5。
 * @return 参数有效且转换完成时返回true。
 */
bool App_Wave_Cnt_Demo_Query_Window(uint32_t s_tick,
                                    uint32_t e_tick,
                                    uint32_t window_tick,
                                    uint16_t *columns,
                                    uint32_t column_count,
                                    uint32_t *valid_columns,
                                    uint8_t *initial_levels);

#endif
