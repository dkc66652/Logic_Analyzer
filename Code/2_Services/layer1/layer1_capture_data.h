/**
 ******************************************************************************
 * @file    layer1_capture_data.h
 * @brief   同事逻辑分析仪采集服务到波形显示任务的查询适配接口。
 ******************************************************************************
 */
#ifndef WAVE_CAPTURE_DATA_H
#define WAVE_CAPTURE_DATA_H

#include <stdbool.h>
#include <stdint.h>

#define WAVE_CAPTURE_DATA_CHANNEL_COUNT  6U
#define WAVE_CAPTURE_DATA_COLUMN_COUNT   500U

/*
 * 波形显示链路诊断开关：
 * 1：采集时基和500列数据全部来自App_Wave_Cnt_Demo，不启动LA硬件采集；
 * 0：使用同事LA Service的真实六通道采集和固定视图查询。
 */
#ifndef WAVE_CAPTURE_DATA_USE_CNT_DEMO
#define WAVE_CAPTURE_DATA_USE_CNT_DEMO  0U
#endif

/** 初始化显示侧采集适配状态；LA Service必须已经初始化。 */
void wave_capture_data_init(void);

/**
 * @brief 启动同事LA服务的六路硬件采集。
 * @param duration_cnt 采集时长；当前时间戳为1 MHz，所以1 CNT等于1 us。
 * @return true表示服务接受启动请求。
 */
bool wave_capture_data_begin(uint32_t duration_cnt);

/** 停止硬件采集并保留已采集数据。 */
bool wave_capture_data_stop(void);

/** 返回当前采集CNT；终态返回实际截止CNT。 */
uint32_t wave_capture_data_get_now_cnt(void);

/** 帧中断锁存值只决定刷新时机，实际CNT在波形任务中从LA服务取得。 */
uint32_t wave_capture_data_get_cnt_at_tick(uint32_t rtos_tick);

/**
 * @brief 在一个固定查询视图内把六路采集边沿转换为500列显示状态。
 * @param s_tick 当前显示窗口左边界CNT，属于查询范围。
 * @param e_tick 当前有效波形的右边界CNT；实时显示时就是当前now_tick。
 * @param window_tick 完整500像素显示窗口代表的CNT宽度。
 * @param columns 调用方提供的500元素uint16_t数组。每个通道占2 bit：
 *                00表示本列无翻转；01表示有翻转且列末为低；
 *                10表示有翻转且列末为高；11保留不用。
 * @param valid_columns 返回columns中从下标0开始的有效列数，范围0~500。
 *                      后续列未覆盖有效时间，显示侧必须保持黑色。
 * @param initial_levels 返回s_tick处六路初始电平位图，bit0~bit5对应通道0~5。
 * @return true表示六路查询和转换全部完成；false表示参数或查询失败。
 * @note 本接口一次调用只建立一个Query View，并在该固定视图内依次读取六路。
 *       时间列按[s_tick, e_tick)处理，恰好等于e_tick的边沿留给下一窗口。
 */
bool wave_capture_data_request_window(uint32_t s_tick,
                                      uint32_t e_tick,
                                      uint32_t window_tick,
                                      uint16_t *columns,
                                      uint32_t *valid_columns,
                                      uint8_t *initial_levels);

#endif /* WAVE_CAPTURE_DATA_H */
