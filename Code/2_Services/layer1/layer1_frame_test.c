/**
 ******************************************************************************
 * @file    layer1_frame_test.c
 * @brief   采集显示统计，并周期输出LTDC错误及换帧诊断。
 * @details 使用 Cortex-M7 DWT 周期计数器采集耗时，由低优先级 FreeRTOS
 *          测试任务每 5 秒汇总一次；不参与正式界面功能。
 ******************************************************************************
 */

#include "layer1_frame_test.h"
#include "FreeRTOS.h"
#include "task.h"
#include "log_service.h"
#include "bsp_ltdc_lcd.h"
#include "bsp_dma2d.h"
#include <stdint.h>

#define FRAME_DELAY    pdMS_TO_TICKS(5000U)

static volatile uint32_t s_flush_count;
static frame_test_render_stats_t s_render_stats;
static frame_test_sdram_write_stats_t s_sdram_write_stats;

/**
 * @brief frame_test_get_flush_count：显示帧耗时与带宽诊断。
 */
uint32_t frame_test_get_flush_count(void)
{
	return s_flush_count;
}

/**
 * @brief frame_test_record_lvgl_handler：显示帧耗时与带宽诊断。
 */
void frame_test_record_lvgl_handler(uint32_t cycles, bool rendered)
{
	s_render_stats.lvgl_handler_cycles += cycles;
	s_render_stats.lvgl_handler_calls++;

	if(rendered) {
		s_render_stats.rendered_handler_cycles += cycles;
		s_render_stats.rendered_handler_calls++;
	}
}

/**
 * @brief frame_test_record_flush：显示帧耗时与带宽诊断。
 */
void frame_test_record_flush(uint32_t pixels)
{
	s_flush_count++;
	s_render_stats.flush_pixels += pixels;
	s_render_stats.flush_calls++;
}

/**
 * @brief frame_test_record_sdram_write：显示帧耗时与带宽诊断。
 */
void frame_test_record_sdram_write(uint32_t cycles, uint32_t pixels)
{
	taskENTER_CRITICAL();
	s_sdram_write_stats.cycles += cycles;
	s_sdram_write_stats.pixels += pixels;
	s_sdram_write_stats.frames++;
	taskEXIT_CRITICAL();
}

/**
 * @brief frame_test_get_and_reset_render_stats：显示帧耗时与带宽诊断。
 */
void frame_test_get_and_reset_render_stats(frame_test_render_stats_t *stats)
{
	if(stats == NULL) {
		return;
	}

    /* 写入端来自显示相关任务，临界区可保证统计快照不会丢样本。 */
	taskENTER_CRITICAL();
	*stats = s_render_stats;
	s_render_stats = (frame_test_render_stats_t){0};
	taskEXIT_CRITICAL();
}

/**
 * @brief frame_test_get_and_reset_sdram_write_stats：显示帧耗时与带宽诊断。
 */
void frame_test_get_and_reset_sdram_write_stats(frame_test_sdram_write_stats_t *stats)
{
	if(stats == NULL) {
		return;
	}

	taskENTER_CRITICAL();
	*stats = s_sdram_write_stats;
	s_sdram_write_stats = (frame_test_sdram_write_stats_t){0};
	taskEXIT_CRITICAL();
}

/**
 * @brief frame_test_task_create：显示帧耗时与带宽诊断。
 */
bool frame_test_task_create(void)
{
	return xTaskCreate(frame_test_task, "frame_test", FRAME_TEST_TASK_STACK,
					   NULL, FRAME_TEST_TASK_PRIO, NULL) == pdPASS;
}

/**
 * @brief frame_test_task：显示帧耗时与带宽诊断。
 */
void frame_test_task(void *param)
{
	uint32_t last_fifo_underrun_count;
	uint32_t last_transfer_error_count;
	TickType_t last_wake_time = xTaskGetTickCount();

	(void)param;
	last_fifo_underrun_count = bsp_ltdc_lcd_get_fifo_underrun_count();
	last_transfer_error_count = bsp_ltdc_lcd_get_transfer_error_count();
	/* 丢弃首屏创建产生的一次性全屏 flush，后续统计只反映稳态波形刷新。 */
	{
		frame_test_render_stats_t initial_stats;
		frame_test_get_and_reset_render_stats(&initial_stats);
	}
	
	for(;;)
	{
		frame_test_render_stats_t render_stats;
		frame_test_sdram_write_stats_t sdram_write_stats;
		bsp_ltdc_error_diagnostic_t diagnostic;

		xTaskDelayUntil(&last_wake_time, FRAME_DELAY);

		/* 继续清空统计快照，关闭性能日志后不让计数长期累积溢出。 */
		frame_test_get_and_reset_render_stats(&render_stats);
		frame_test_get_and_reset_sdram_write_stats(&sdram_write_stats);
		if(!bsp_ltdc_lcd_get_error_diagnostic(&diagnostic)) {
			continue;
		}

		/* 错误IRQ中只保存寄存器，日志由低优先级任务每5秒输出。 */
		(void)log_printf("ltdc err: fu=%lu(+%lu) te=%lu(+%lu) irq=%lu hal=%08lX dma=%08lX\r\n",
			(unsigned long)diagnostic.fifo_underrun_count,
			(unsigned long)(diagnostic.fifo_underrun_count - last_fifo_underrun_count),
			(unsigned long)diagnostic.transfer_error_count,
			(unsigned long)(diagnostic.transfer_error_count - last_transfer_error_count),
			(unsigned long)diagnostic.error_irq_count,
			(unsigned long)diagnostic.hal_error_code,
			(unsigned long)bsp_dma2d_get_error_code());
		(void)log_printf("ltdc sync: line=%lu rr=%lu present=%lu pend=%u isr=%02lX ier=%02lX\r\n",
			(unsigned long)diagnostic.line_event_count,
			(unsigned long)diagnostic.reload_event_count,
			(unsigned long)diagnostic.present_count,
			diagnostic.swap_pending ? 1U : 0U,
			(unsigned long)diagnostic.interrupt_flags,
			(unsigned long)diagnostic.interrupt_enable);
		if(diagnostic.fifo_underrun_count != last_fifo_underrun_count ||
		   diagnostic.transfer_error_count != last_transfer_error_count) {
			(void)log_printf("ltdc last: flags=%02lX pos=%08lX status=%02lX frame=%lu\r\n",
				(unsigned long)diagnostic.last_error_flags,
				(unsigned long)diagnostic.last_error_position,
				(unsigned long)diagnostic.last_error_display_status,
				(unsigned long)diagnostic.last_error_frame_count);
		}
		last_fifo_underrun_count = diagnostic.fifo_underrun_count;
		last_transfer_error_count = diagnostic.transfer_error_count;
	}
}
