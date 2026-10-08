/** @file layer1_waveform_legacy.h @brief 旧版波形绘制实现。 */

#ifndef WAVEFORM_H
#define WAVEFORM_H

#include "lvgl.h"
#include <stdint.h>

//显示区像素
#define wave_x_start 100U
#define wave_x_end	599U
#define wave_x_length	(wave_x_end - wave_x_start +1)

/* 采集定时器为 1 MHz；可直接传给 wave_set_window_size()。 */
#define WAVE_WINDOW_1S_CNT 1000000UL
#define WAVE_WINDOW_2S_CNT 2000000UL
#define WAVE_WINDOW_5S_CNT 5000000UL

typedef struct{
	uint32_t wave_start_cnt;	//显示左边界
	uint32_t wave_now_cnt;		//显示右边界
	uint32_t wave_window_size;	//显示区大小，1s、2s、5s
	bool view_end_level;   /* 上一次 wave_now_cnt 时刻的电平 */
}wave_view_t;

/** 初始化六路波形显示。 */
void wave_init(lv_obj_t * parent);

/** 设置滑动窗口长度，单位为 CNT。 */
void wave_set_window_size(uint32_t window_cnt);

/** 使用当前 CNT 刷新六路波形。 */
void wave_refresh(uint32_t now_cnt);

/** 获取累计的波形刷新帧数。 */
uint32_t wave_get_refresh_count(void);




#endif
