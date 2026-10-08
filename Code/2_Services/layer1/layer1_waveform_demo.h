/** @file layer1_waveform_demo.h @brief 旧版波形绘制演示。 */

#ifndef WAVEFORM_DEMO_H
#define WAVEFORM_DEMO_H

#include <stdint.h>
#include "lvgl.h"

#define WAVEFORM_TIMER_HZ       1000000UL
#define WAVEFORM_RECORD_CNT     (10UL * WAVEFORM_TIMER_HZ)

typedef struct {
    uint32_t frame_count;
    uint32_t render_cnt;
} waveform_render_stats_t;

/* Create a 60 FPS, 5 s, six-channel sliding-window waveform demonstration. */
void waveform_demo_create_once(lv_obj_t * parent);

/* Get one coherent snapshot of the completed waveform refresh statistics. */
void waveform_demo_get_render_stats(waveform_render_stats_t * stats);

#endif /* WAVEFORM_DEMO_H */
