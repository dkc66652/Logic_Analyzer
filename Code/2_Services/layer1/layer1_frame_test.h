/**
 ******************************************************************************
 * @file    layer1_frame_test.h
 * @brief   LVGL handler、显示 flush 和波形刷新诊断接口。
 * @details App 层负责记录 handler；lvgl_display_port.c 记录 flush；
 *          可选的低优先级诊断任务可周期读取数据；正常版本不创建该任务。
 ******************************************************************************
 */

#ifndef __FRAME_TEST_H
#define __FRAME_TEST_H

#include <stdbool.h>
#include <stdint.h>

#define FRAME_TEST_TASK_STACK    512U
#define FRAME_TEST_TASK_PRIO     1U

/*
 * 1：启动低优先级 LTDC 诊断任务，每 5 秒输出一次 FIFO underrun、TE 和
 *    换帧状态；用于当前 SDRAM/LTDC 压力测试。
 * 0：不创建诊断任务，恢复无周期诊断日志的正式运行模式。
 */
#define FRAME_TEST_DIAGNOSTIC_ENABLE  1U

/*
 * 1：未采集且没有保留波形时，wave_prep 任务在每次 LTDC 帧通知后，请求
 *    DMA2D 以 R2M 模式把 Layer 1 后缓冲填成黑色，用于测量DMA填充的CPU
 *    占用、实际搬运时间和最高刷新率。
 * 0：关闭该临时基准，恢复正常的空闲等待行为。
 */
#define FRAME_TEST_WAVE_SDRAM_DMA_FILL_ENABLE  0

/**
 * @brief  创建低优先级性能统计任务。
 * @return true 表示任务创建成功，否则返回 false。
 * @note   仅用于临时诊断；正常版本的 Services_Init() 不创建此任务。
 * @usage  (void)frame_test_task_create();
 */
bool frame_test_task_create(void);

/** @brief FreeRTOS 性能统计任务入口，仅由 frame_test_task_create() 使用。 */
void frame_test_task(void *param);

/*
 * 性能计数由 LVGL 任务和显示 flush 回调写入，由低优先级的本测试任务
 * 每 5 秒读取一次。DWT 周期计数只用于测量短时间差值，32 位回绕不影响
 * 单次 lv_timer_handler() 的耗时计算。
 */
typedef struct
{
	uint64_t lvgl_handler_cycles;
	uint32_t lvgl_handler_calls;
	uint64_t rendered_handler_cycles;
	uint32_t rendered_handler_calls;
	uint64_t flush_pixels;
	uint32_t flush_calls;
} frame_test_render_stats_t;

typedef struct
{
	uint64_t cycles;
	uint64_t pixels;
	uint32_t frames;
} frame_test_sdram_write_stats_t;

/** @brief 获取累计 flush 次数；由 frame_test_task() 计算刷新频率。 */
uint32_t frame_test_get_flush_count(void);

/** @brief 记录一次 lv_timer_handler() 的周期数；由 main.c 调用。 */
void frame_test_record_lvgl_handler(uint32_t cycles, bool rendered);

/** @brief 记录一次实际显示 flush 的像素数；由 lvgl_display_port.c 调用。 */
void frame_test_record_flush(uint32_t pixels);

/** 记录一次 DMA2D 向波形 SDRAM 后缓冲执行整块填充的完成时间。 */
void frame_test_record_sdram_write(uint32_t cycles, uint32_t pixels);

/** @brief 取得并清零渲染统计；由 frame_test_task() 每 5 秒调用。 */
void frame_test_get_and_reset_render_stats(frame_test_render_stats_t *stats);

/** 取得并清零 DMA2D 写波形 SDRAM 的基准数据。 */
void frame_test_get_and_reset_sdram_write_stats(frame_test_sdram_write_stats_t *stats);

#endif /* __FRAME_TEST_H */
