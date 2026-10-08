/** @file layer1_waveform_demo.c @brief 旧版波形绘制演示。 */

#include "layer1_waveform_demo.h"

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#define WAVEFORM_WINDOW_MS            5000UL
/* Cast before multiplication: 5000 ms * 1 MHz exceeds 32-bit unsigned long. */
#define WAVEFORM_WINDOW_CNT           ((uint32_t)(((uint64_t)WAVEFORM_WINDOW_MS * WAVEFORM_TIMER_HZ) / 1000U))
#define WAVEFORM_WIDTH_PX             600U
#define WAVEFORM_FRAME_RATE           60U
#define WAVEFORM_CHANNEL_COUNT        6U
#define WAVEFORM_TIME_RULER_HEIGHT    36U
#define WAVEFORM_TIME_TICK_CNT        WAVEFORM_TIMER_HZ
#define WAVEFORM_TIME_TICK_HEIGHT     7U
#define WAVEFORM_TIME_TICK_SLOTS      ((WAVEFORM_WINDOW_CNT / WAVEFORM_TIME_TICK_CNT) + 1U)
#define WAVEFORM_CAPTURE_CAPACITY     128U
/* Initial point + two same-X points per edge + final point. */
#define WAVEFORM_POINT_CAPACITY       (WAVEFORM_CAPTURE_CAPACITY * 2U + 2U)

/*
 * Demonstration capture data, in CNT units. CNT == 0 starts at low level.
 * At 5 s / 600 px, 100 and 600 CNT both map to pixel column 0.
 */
static const uint32_t demo_capture_cnt_ch0[] = {
    100U, 600U,
    300000U, 900000U,
    1180000U, 1190000U,
    1800000U, 2500000U,
    3450000U, 4020000U, 4650000U,
    5250000U, 5700000U,
    6500000U, 7200000U,
    8100000U, 8800000U,
    9200000U, 9750000U,
};

static const uint32_t demo_capture_cnt_ch1[] = {
    50000U, 150000U, 250000U, 350000U, 450000U, 550000U,
    800000U, 1100000U, 1400000U, 1700000U, 2100000U, 2600000U,
    3100000U, 3900000U, 4300000U, 5100000U, 5900000U, 6800000U,
    7600000U, 8500000U, 9300000U, 9900000U,
};

static const uint32_t demo_capture_cnt_ch2[] = {
    120000U, 720000U, 980000U, 1260000U, 1900000U, 2050000U,
    2760000U, 3330000U, 3600000U, 4210000U, 4880000U, 5400000U,
    6150000U, 6600000U, 7400000U, 8050000U, 8700000U, 9400000U,
};

static const uint32_t demo_capture_cnt_ch3[] = {
    180000U, 220000U, 660000U, 700000U, 1140000U, 1180000U,
    1730000U, 1770000U, 2300000U, 2340000U, 2980000U, 3020000U,
    3870000U, 3910000U, 4650000U, 4690000U, 5500000U, 5540000U,
    6350000U, 6390000U, 7250000U, 7290000U, 8100000U, 8140000U,
    9000000U, 9040000U,
};

static const uint32_t demo_capture_cnt_ch4[] = {
    30000U, 300000U, 620000U, 880000U, 1250000U, 1560000U,
    2020000U, 2470000U, 2850000U, 3300000U, 3750000U, 4180000U,
    4720000U, 5150000U, 5680000U, 6100000U, 6720000U, 7150000U,
    7800000U, 8300000U, 8950000U, 9600000U,
};

static const uint32_t demo_capture_cnt_ch5[] = {
    90000U, 130000U, 490000U, 530000U, 970000U, 1010000U,
    1500000U, 1540000U, 2150000U, 2190000U, 2940000U, 2980000U,
    3740000U, 3780000U, 4560000U, 4600000U, 5410000U, 5450000U,
    6310000U, 6350000U, 7180000U, 7220000U, 8060000U, 8100000U,
    8920000U, 8960000U, 9750000U, 9790000U,
};

typedef struct {
    const uint32_t * capture_cnt;
    uint32_t capture_count;
    lv_obj_t * obj;
    lv_point_precise_t points[WAVEFORM_POINT_CAPACITY];
    uint32_t point_count;
    uint32_t color;
} waveform_channel_t;

static waveform_channel_t s_channels[WAVEFORM_CHANNEL_COUNT] = {
    {demo_capture_cnt_ch0, sizeof(demo_capture_cnt_ch0) / sizeof(demo_capture_cnt_ch0[0]), NULL, {{0}}, 0U, 0x37D67AU},
    {demo_capture_cnt_ch1, sizeof(demo_capture_cnt_ch1) / sizeof(demo_capture_cnt_ch1[0]), NULL, {{0}}, 0U, 0x4BA3FFU},
    {demo_capture_cnt_ch2, sizeof(demo_capture_cnt_ch2) / sizeof(demo_capture_cnt_ch2[0]), NULL, {{0}}, 0U, 0xFFB84DU},
    {demo_capture_cnt_ch3, sizeof(demo_capture_cnt_ch3) / sizeof(demo_capture_cnt_ch3[0]), NULL, {{0}}, 0U, 0xE36BFFU},
    {demo_capture_cnt_ch4, sizeof(demo_capture_cnt_ch4) / sizeof(demo_capture_cnt_ch4[0]), NULL, {{0}}, 0U, 0x62E8D5U},
    {demo_capture_cnt_ch5, sizeof(demo_capture_cnt_ch5) / sizeof(demo_capture_cnt_ch5[0]), NULL, {{0}}, 0U, 0xFF7A90U},
};

static uint32_t s_fractional_ms;
static TickType_t s_demo_start_tick;
static volatile uint32_t s_render_frame_count;
static volatile uint32_t s_last_render_cnt;
static lv_obj_t * s_time_ruler;
static uint32_t s_time_ruler_now_cnt;
static lv_point_precise_t s_time_ruler_tick_points[WAVEFORM_TIME_TICK_SLOTS][2];
static char s_time_ruler_labels[WAVEFORM_TIME_TICK_SLOTS][12];

/**
 * @brief waveform_now_cnt_from_time：旧版波形绘制演示。
 */
static uint32_t waveform_now_cnt_from_time(void)
{
    /* The simulated CNT follows real FreeRTOS time, not LVGL tick or rendered frames. */
    const TickType_t elapsed_ticks = xTaskGetTickCount() - s_demo_start_tick;
    const uint32_t elapsed_ms = (uint32_t)(((uint64_t)elapsed_ticks * 1000U)
                                           / configTICK_RATE_HZ);
    const uint64_t elapsed_cnt = (uint64_t)elapsed_ms * WAVEFORM_TIMER_HZ;

    return (uint32_t)((elapsed_cnt / 1000U) % WAVEFORM_RECORD_CNT);
}

/**
 * @brief waveform_cnt_to_pixel：旧版波形绘制演示。
 */
static uint32_t waveform_cnt_to_pixel(uint32_t cnt, uint32_t window_start_cnt)
{
    return (uint32_t)(((uint64_t)(cnt - window_start_cnt) * WAVEFORM_WIDTH_PX)
                      / WAVEFORM_WINDOW_CNT);
}

/**
 * @brief waveform_level_to_y：旧版波形绘制演示。
 */
static int32_t waveform_level_to_y(bool is_high, const lv_area_t * coords)
{
    const int32_t height = lv_area_get_height(coords);

    return coords->y1 + (is_high ? height / 3 : (height * 2) / 3);
}

/**
 * @brief waveform_time_ruler_draw_event_cb：旧版波形绘制演示。
 */
static void waveform_time_ruler_draw_event_cb(lv_event_t * e)
{
    const uint32_t window_start_cnt = (s_time_ruler_now_cnt > WAVEFORM_WINDOW_CNT)
                                    ? (s_time_ruler_now_cnt - WAVEFORM_WINDOW_CNT) : 0U;
    /* The first integral second still visible in the sliding window. */
    const uint32_t first_tick_cnt = ((window_start_cnt + WAVEFORM_TIME_TICK_CNT - 1U)
                                     / WAVEFORM_TIME_TICK_CNT) * WAVEFORM_TIME_TICK_CNT;
    const uint32_t last_tick_cnt = (s_time_ruler_now_cnt / WAVEFORM_TIME_TICK_CNT)
                                 * WAVEFORM_TIME_TICK_CNT;
    lv_draw_line_dsc_t tick_dsc;
    lv_draw_label_dsc_t label_dsc;
    lv_area_t coords;
    uint32_t tick_cnt;
    uint32_t tick_index = 0U;

    if(first_tick_cnt > last_tick_cnt) {
        return;
    }

    lv_obj_get_content_coords(lv_event_get_current_target(e), &coords);

    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = lv_color_hex(0x7D8B99U);
    tick_dsc.width = 1;
    tick_dsc.opa = LV_OPA_COVER;

    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_hex(0xB9C5D0U);
    label_dsc.opa = LV_OPA_COVER;
    label_dsc.align = LV_TEXT_ALIGN_CENTER;

    for(tick_cnt = first_tick_cnt;
        tick_cnt <= last_tick_cnt && tick_index < WAVEFORM_TIME_TICK_SLOTS;
        tick_cnt += WAVEFORM_TIME_TICK_CNT, tick_index++) {
        const uint32_t pixel = waveform_cnt_to_pixel(tick_cnt, window_start_cnt);
        const int32_t x = (pixel >= WAVEFORM_WIDTH_PX)
                        ? coords.x2 : (coords.x1 + (int32_t)pixel);
        lv_area_t label_coords;

        s_time_ruler_tick_points[tick_index][0].x = x;
        s_time_ruler_tick_points[tick_index][0].y = coords.y1;
        s_time_ruler_tick_points[tick_index][1].x = x;
        s_time_ruler_tick_points[tick_index][1].y = coords.y1 + WAVEFORM_TIME_TICK_HEIGHT - 1;
        tick_dsc.points = s_time_ruler_tick_points[tick_index];
        tick_dsc.point_cnt = 2;
        lv_draw_line(lv_event_get_layer(e), &tick_dsc);

        /* One persistent string per task: LVGL draws after this callback returns. */
        (void)lv_snprintf(s_time_ruler_labels[tick_index], sizeof(s_time_ruler_labels[tick_index]), "%lus",
                          (unsigned long)(tick_cnt / WAVEFORM_TIME_TICK_CNT));
        label_dsc.text = s_time_ruler_labels[tick_index];
        if(x == coords.x1) {
            label_dsc.align = LV_TEXT_ALIGN_LEFT;
            label_coords.x1 = x;
            label_coords.x2 = x + 36;
        }
        else if(x == coords.x2) {
            label_dsc.align = LV_TEXT_ALIGN_RIGHT;
            label_coords.x1 = x - 36;
            label_coords.x2 = x;
        }
        else {
            label_dsc.align = LV_TEXT_ALIGN_CENTER;
            label_coords.x1 = x - 18;
            label_coords.x2 = x + 18;
        }
        label_coords.y1 = coords.y1 + WAVEFORM_TIME_TICK_HEIGHT + 2;
        label_coords.y2 = coords.y2;
        lv_draw_label(lv_event_get_layer(e), &label_dsc, &label_coords);

    }
}

/**
 * @brief waveform_rebuild_points：旧版波形绘制演示。
 */
static void waveform_rebuild_points(waveform_channel_t * channel, uint32_t now_cnt)
{
    const uint32_t window_start_cnt = (now_cnt > WAVEFORM_WINDOW_CNT)
                                    ? (now_cnt - WAVEFORM_WINDOW_CNT) : 0U;
    const uint32_t right_pixel = (now_cnt >= WAVEFORM_WINDOW_CNT)
                               ? WAVEFORM_WIDTH_PX
                               : waveform_cnt_to_pixel(now_cnt, 0U);
    lv_area_t coords;
    bool is_high = false;
    uint32_t i;

    lv_obj_get_content_coords(channel->obj, &coords);
    channel->point_count = 0U;

    /* Find the real signal level at the left edge of the 5 s window. */
    for(i = 0U; i < channel->capture_count; i++) {
        if(channel->capture_cnt[i] >= window_start_cnt) {
            break;
        }
        is_high = !is_high;
    }

    /* Start at the left boundary, then add only actual edge transitions. */
    channel->points[channel->point_count].x = coords.x1;
    channel->points[channel->point_count].y = waveform_level_to_y(is_high, &coords);
    channel->point_count++;

    while(i < channel->capture_count && channel->capture_cnt[i] <= now_cnt) {
        const uint32_t pixel = waveform_cnt_to_pixel(channel->capture_cnt[i], window_start_cnt);

        /*
         * Keep X unchanged across an edge: first retain the old level at this
         * CNT, then append the new level at the same X. This makes a vertical
         * logic-analyzer edge instead of a diagonal line to the next level.
         */
        channel->points[channel->point_count].x = coords.x1 + (int32_t)pixel;
        channel->points[channel->point_count].y = waveform_level_to_y(is_high, &coords);
        channel->point_count++;

        is_high = !is_high;
        channel->points[channel->point_count].x = coords.x1 + (int32_t)pixel;
        channel->points[channel->point_count].y = waveform_level_to_y(is_high, &coords);
        channel->point_count++;
        i++;
    }

    /* Extend the last level to the current right edge. */
    channel->points[channel->point_count].x = coords.x1 + (int32_t)right_pixel;
    channel->points[channel->point_count].y = waveform_level_to_y(is_high, &coords);
    channel->point_count++;
}

/**
 * @brief waveform_draw_event_cb：旧版波形绘制演示。
 */
static void waveform_draw_event_cb(lv_event_t * e)
{
    waveform_channel_t * channel = lv_event_get_user_data(e);
    lv_draw_line_dsc_t line_dsc;

    if(channel->point_count < 2U) {
        return;
    }

    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(channel->color);
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.points = channel->points;
    line_dsc.point_cnt = (int32_t)channel->point_count;
    lv_draw_line(lv_event_get_layer(e), &line_dsc);
}

/**
 * @brief waveform_refresh：旧版波形绘制演示。
 */
static void waveform_refresh(uint32_t now_cnt)
{
    uint32_t channel_index;

    s_time_ruler_now_cnt = now_cnt;
    lv_obj_invalidate(s_time_ruler);

    for(channel_index = 0U; channel_index < WAVEFORM_CHANNEL_COUNT; channel_index++) {
        waveform_rebuild_points(&s_channels[channel_index], now_cnt);
        lv_obj_invalidate(s_channels[channel_index].obj);
    }
}

/**
 * @brief waveform_frame_timer_cb：旧版波形绘制演示。
 */
static void waveform_frame_timer_cb(lv_timer_t * timer)
{
    uint32_t period_ms = 1000U / WAVEFORM_FRAME_RATE;
    const uint32_t now_cnt = waveform_now_cnt_from_time();

    waveform_refresh(now_cnt);
    s_last_render_cnt = now_cnt;
    s_render_frame_count++;

    /* 16, 17, 17 ms repeating cadence: 60 frames/s on average. */
    s_fractional_ms += 1000U % WAVEFORM_FRAME_RATE;
    if(s_fractional_ms >= WAVEFORM_FRAME_RATE) {
        s_fractional_ms -= WAVEFORM_FRAME_RATE;
        period_ms++;
    }
    lv_timer_set_period(timer, period_ms);
}

/**
 * @brief waveform_demo_get_render_stats：旧版波形绘制演示。
 */
void waveform_demo_get_render_stats(waveform_render_stats_t * stats)
{
    if(stats == NULL) {
        return;
    }

    taskENTER_CRITICAL();
    stats->frame_count = s_render_frame_count;
    stats->render_cnt = s_last_render_cnt;
    taskEXIT_CRITICAL();
}

/**
 * @brief waveform_demo_create_once：旧版波形绘制演示。
 */
void waveform_demo_create_once(lv_obj_t * parent)
{
    static bool created;
    const int32_t screen_height = lv_display_get_vertical_resolution(lv_display_get_default());
    const int32_t waveform_height = screen_height - WAVEFORM_TIME_RULER_HEIGHT;
    const int32_t row_height = waveform_height / WAVEFORM_CHANNEL_COUNT;
    uint32_t channel_index;

    if(created) {
        return;
    }
    created = true;

    /* The ruler has only short tick marks; the six waveform objects have no grid or Chart work. */
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0A0F14), LV_PART_MAIN);

    s_time_ruler = lv_obj_create(parent);
    lv_obj_remove_style_all(s_time_ruler);
    lv_obj_set_pos(s_time_ruler, 0, 0);
    lv_obj_set_size(s_time_ruler, WAVEFORM_WIDTH_PX, WAVEFORM_TIME_RULER_HEIGHT);
    lv_obj_set_style_bg_color(s_time_ruler, lv_color_hex(0x0A0F14), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_time_ruler, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(s_time_ruler, false);
    lv_obj_add_event_cb(s_time_ruler, waveform_time_ruler_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);

    for(channel_index = 0U; channel_index < WAVEFORM_CHANNEL_COUNT; channel_index++) {
        waveform_channel_t * channel = &s_channels[channel_index];
        const int32_t y = WAVEFORM_TIME_RULER_HEIGHT + (int32_t)channel_index * row_height;
        const int32_t height = (channel_index == (WAVEFORM_CHANNEL_COUNT - 1U))
                             ? (screen_height - y) : row_height;

        channel->obj = lv_obj_create(parent);
        lv_obj_remove_style_all(channel->obj);
        lv_obj_set_pos(channel->obj, 0, y);
        lv_obj_set_size(channel->obj, WAVEFORM_WIDTH_PX, height);
        lv_obj_set_style_bg_color(channel->obj, lv_color_hex(0x0A0F14), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(channel->obj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_remove_flag(channel->obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(channel->obj, waveform_draw_event_cb, LV_EVENT_DRAW_MAIN, channel);
    }

    s_demo_start_tick = xTaskGetTickCount();
    waveform_refresh(0U);
    lv_timer_create(waveform_frame_timer_cb, 1000U / WAVEFORM_FRAME_RATE, NULL);
}
