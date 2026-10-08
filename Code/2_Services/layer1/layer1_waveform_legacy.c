/** @file layer1_waveform_legacy.c @brief 旧版波形绘制实现。 */

#include "layer1_waveform_legacy.h"

#define wave_edge_count 128U
#define WAVEFORM_POINT_CAPACITY (wave_x_length * 2U) //一个显示区内最大可能的像素点个数
#define WAVEFORM_TIME_RULER_HEIGHT 36U
#define WAVEFORM_TIME_LABEL_SLOTS  12U

typedef struct{
	uint32_t  edge_cnt[wave_edge_count];
	bool full;		//缓冲区是否满
	
	bool level_valid;     /* 初始 false：未得到同步电平，不画 */
	bool level;		//初始电平
	uint32_t level_cnt;	//初始电平采集到的cnt，也是第一次显示的cnt
	
}wave_edge_t;

typedef struct{
	wave_edge_t capture;
	
	lv_obj_t * obj;
	
	lv_point_precise_t points[WAVEFORM_POINT_CAPACITY];	//
	uint32_t point_count;						//记录点数，绘制波形时后面的数据是无效的
	lv_point_precise_t start_point;	/* 当前窗口波形的起始点，不占 points[] 容量 */
	lv_point_precise_t end_point;	/* 当前窗口波形的结束点，不占 points[] 容量 */
	bool draw_valid;				/* 本次窗口是否具备可绘制的初始电平 */
	
	int16_t top;                     /* 本行起始 Y */
  int16_t height;                  /* 本行高度 */
  int16_t high_offset;             /* 高电平相对本行顶部的 Y */
  int16_t low_offset;              /* 低电平相对本行顶部的 Y */
	
	uint32_t color;                  /* 波形颜色，0xRRGGBB */
	
	uint16_t view_first_edge;  /* 当前窗口内第一条边沿的下标 */
	bool     view_start_level; /* 当前窗口左边界、第一条边沿之前的电平 */
	
}wave_channel_t;

static wave_channel_t s_channels[6] =
{
    { .capture = {
          .edge_cnt = { 50000U, 300000U, 900000U, 1180000U, 1190000U,
                        1800000U, 2500000U, 3450000U, 4020000U, 4650000U,
                        5250000U, 5700000U, 6500000U, 7200000U, 8100000U,
                        8800000U, 9200000U, 9750000U },
          .level_valid = true, .level = false, .level_cnt = 100U },
      .top =  36, .height = 74, .high_offset = 24, .low_offset = 49, .color = 0x37D67A },
    { .capture = {
          .edge_cnt = { 50000U, 150000U, 250000U, 350000U, 450000U, 550000U,
                        800000U, 1100000U, 1400000U, 1700000U, 2100000U,
                        2600000U, 3100000U, 3900000U, 4300000U, 5100000U,
                        5900000U, 6800000U, 7600000U, 8500000U, 9300000U,
                        9900000U },
          .level_valid = true, .level = false, .level_cnt = 200U },
      .top = 110, .height = 74, .high_offset = 24, .low_offset = 49, .color = 0x4BA3FF },
    { .capture = {
          .edge_cnt = { 120000U, 720000U, 980000U, 1260000U, 1900000U,
                        2050000U, 2760000U, 3330000U, 3600000U, 4210000U,
                        4880000U, 5400000U, 6150000U, 6600000U, 7400000U,
                        8050000U, 8700000U, 9400000U },
          .level_valid = true, .level = false, .level_cnt = 300U },
      .top = 184, .height = 74, .high_offset = 24, .low_offset = 49, .color = 0xFFB84D },
    { .capture = {
          .edge_cnt = { 180000U, 220000U, 660000U, 700000U, 1140000U,
                        1180000U, 1730000U, 1770000U, 2300000U, 2340000U,
                        2980000U, 3020000U, 3870000U, 3910000U, 4650000U,
                        4690000U, 5500000U, 5540000U, 6350000U, 6390000U,
                        7250000U, 7290000U, 8100000U, 8140000U, 9000000U,
                        9040000U },
          .level_valid = true, .level = false, .level_cnt = 400U },
      .top = 258, .height = 74, .high_offset = 24, .low_offset = 49, .color = 0xE36BFF },
    { .capture = {
          .edge_cnt = { 30000U, 300000U, 620000U, 880000U, 1250000U,
                        1560000U, 2020000U, 2470000U, 2850000U, 3300000U,
                        3750000U, 4180000U, 4720000U, 5150000U, 5680000U,
                        6100000U, 6720000U, 7150000U, 7800000U, 8300000U,
                        8950000U, 9600000U },
          .level_valid = true, .level = false, .level_cnt = 500U },
      .top = 332, .height = 74, .high_offset = 24, .low_offset = 49, .color = 0x62E8D5 },
    { .capture = {
          .edge_cnt = { 90000U, 130000U, 490000U, 530000U, 970000U,
                        1010000U, 1500000U, 1540000U, 2150000U, 2190000U,
                        2940000U, 2980000U, 3740000U, 3780000U, 4560000U,
                        4600000U, 5410000U, 5450000U, 6310000U, 6350000U,
                        7180000U, 7220000U, 8060000U, 8100000U, 8920000U,
                        8960000U, 9750000U, 9790000U },
          .level_valid = true, .level = false, .level_cnt = 600U },
      .top = 406, .height = 74, .high_offset = 24, .low_offset = 49, .color = 0xFF7A90 },
};

/* 默认显示 5 s；后续由 wave_set_window_size() 修改。 */
static wave_view_t s_view =
{
    .wave_window_size = WAVE_WINDOW_5S_CNT,
};

/* 防止 wave_init() 被重复调用后创建两组重叠的子对象。 */
static bool s_initialized = false;

/* 每次 wave_refresh() 成功准备完六路数据后加一。 */
static volatile uint32_t s_refresh_count = 0U;
static lv_obj_t * s_time_ruler;
/* 标签文本必须在绘制任务完成前保持有效，不能使用回调中的栈变量。 */
static char s_time_ruler_labels[WAVEFORM_TIME_LABEL_SLOTS][12];

/*==============================================================================
 * 内部函数声明
 *
 * 这些函数只在 layer1_waveform_legacy.c 内部使用。函数体由后续显示逻辑实现，
 * 当前先集中列出，使模块入口、坐标转换、数据准备和实际绘制的职责清晰。
 *============================================================================*/

/** 创建一路波形对应的 LVGL 对象并应用通道布局。 */
static void wave_create_channel_obj(lv_obj_t * parent,
                                    wave_channel_t * channel);

/** 根据显示窗口画顶部时间刻度。 */
static void wave_time_ruler_draw_event_cb(lv_event_t * e);

/** 根据 1 s / 2 s / 5 s 窗口选择可读的时间刻度间隔。 */
static uint32_t wave_get_time_tick_cnt(uint32_t window_cnt);

/** 将 CNT 格式化为秒标签；子秒窗口显示一位小数。 */
static void wave_format_time_label(char * buffer,
                                   uint32_t buffer_size,
                                   uint32_t cnt,
                                   uint32_t tick_cnt);

/** 根据当前窗口更新显示左、右 CNT 边界。 */
static void wave_update_view(uint32_t now_cnt);

/** 将窗口内的 CNT 映射为波形区屏幕 X 坐标，结果范围为 100~599。 */
static uint32_t wave_cnt_to_pixel(uint32_t cnt,
                                  const wave_view_t * view);

/** 根据通道的高、低电平布局参数取得屏幕 Y 坐标。 */
static int32_t wave_level_to_y(const wave_channel_t * channel,
                               bool level);

/** 新 X 坐标追加一对边沿点；相同 X 坐标只更新该列的最终电平。 */
static bool wave_append_edge_pair(wave_channel_t * channel,
                                  int32_t x,
                                  bool level_before,
                                  bool level_after);

/** 将一路边沿数据转换为当前窗口内可绘制的点对。 */
static void wave_prepare_channel(wave_channel_t * channel,
                                 const wave_view_t * view);

/** 使用统一的线宽、颜色和透明度绘制一段线。 */
static void wave_draw_segment(lv_layer_t * layer,
                              const lv_point_precise_t * p1,
                              const lv_point_precise_t * p2,
                              uint32_t color);

/** LVGL 重绘一路波形对象时的绘制回调。 */
static void wave_draw_event_cb(lv_event_t * e);

/*==============================================================================
 * 对外函数实现
 *============================================================================*/

/**
 * @brief 创建六个通道对象并完成一次显示模块初始化。
 *
 * @param parent 六个波形对象的共同父对象，通常传入 lv_screen_active()。
 *
 * 每个通道对象只占 x=100~599 的波形区域；左侧 x=0~99 保留给上层。
 * 本函数只应成功执行一次，重复调用会直接返回。
 */
void wave_init(lv_obj_t * parent)
{
    uint32_t channel_index;

    if(parent == NULL || s_initialized) {
        return;
    }

    /* 父对象作为页面底色；各通道对象会在重绘时覆盖自己的区域。 */
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0A0F14U), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);

    s_view.wave_start_cnt = 0U;
    s_view.wave_now_cnt = 0U;
    s_refresh_count = 0U;

    /* 顶部标尺与波形共用 x=100~599，左侧信息区不受影响。 */
    s_time_ruler = lv_obj_create(parent);
    lv_obj_remove_style_all(s_time_ruler);
    lv_obj_set_pos(s_time_ruler, wave_x_start, 0);
    lv_obj_set_size(s_time_ruler, wave_x_length, WAVEFORM_TIME_RULER_HEIGHT);
    lv_obj_set_style_bg_color(s_time_ruler, lv_color_hex(0x0A0F14U), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_time_ruler, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(s_time_ruler, false);
    lv_obj_add_event_cb(s_time_ruler, wave_time_ruler_draw_event_cb,
                        LV_EVENT_DRAW_MAIN, NULL);

    for(channel_index = 0U; channel_index < 6U; channel_index++) {
        wave_channel_t * channel = &s_channels[channel_index];

        channel->point_count = 0U;
        channel->draw_valid = false;
        channel->view_first_edge = 0U;
        channel->view_start_level = channel->capture.level;
        wave_create_channel_obj(parent, channel);
    }

    s_initialized = true;
}

/**
 * @brief 修改后续刷新的时间窗口长度。
 *
 * @param window_cnt 新窗口长度，单位为采样定时器 CNT；0 为无效值。
 *
 * 本函数只更新配置。调用者随后调用 wave_refresh()，新窗口才会重建并显示。
 */
void wave_set_window_size(uint32_t window_cnt)
{
    if(window_cnt == 0U) {
        return;
    }

    s_view.wave_window_size = window_cnt;
}

/**
 * @brief 用当前 CNT 重建六路波形，并请求 LVGL 在下一次调度时重绘。
 *
 * @param now_cnt 当前显示窗口的右边界。
 *
 * 此处只准备点坐标并使对象失效；实际线段绘制发生在
 * wave_draw_event_cb() 收到 LV_EVENT_DRAW_MAIN 时。
 */
void wave_refresh(uint32_t now_cnt)
{
    uint32_t channel_index;

    if(!s_initialized) {
        return;
    }

    wave_update_view(now_cnt);

    lv_obj_invalidate(s_time_ruler);

    for(channel_index = 0U; channel_index < 6U; channel_index++) {
        wave_channel_t * channel = &s_channels[channel_index];

        wave_prepare_channel(channel, &s_view);
        lv_obj_invalidate(channel->obj);
    }

    s_refresh_count++;
}

/**
 * @brief wave_get_refresh_count：旧版波形绘制实现。
 */
uint32_t wave_get_refresh_count(void)
{
    return s_refresh_count;
}

/*==============================================================================
 * 内部函数实现
 *============================================================================*/

static void wave_create_channel_obj(lv_obj_t * parent,
                                    wave_channel_t * channel)
{
    channel->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(channel->obj);

    /* 每路对象只覆盖自己的 500 px 波形区，不影响左侧信息区。 */
    lv_obj_set_pos(channel->obj, wave_x_start, channel->top);
    lv_obj_set_size(channel->obj, wave_x_length, channel->height);
    lv_obj_set_style_bg_color(channel->obj, lv_color_hex(0x0A0F14U), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(channel->obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(channel->obj, false);

    lv_obj_add_event_cb(channel->obj, wave_draw_event_cb,
                        LV_EVENT_DRAW_MAIN, channel);
}

/**
 * @brief wave_get_time_tick_cnt：旧版波形绘制实现。
 */
static uint32_t wave_get_time_tick_cnt(uint32_t window_cnt)
{
    if(window_cnt <= WAVE_WINDOW_1S_CNT) {
        return WAVE_WINDOW_1S_CNT / 10U;  /* 1 s: 0.0, 0.1, ..., 1.0 s */
    }

    if(window_cnt <= WAVE_WINDOW_2S_CNT) {
        return WAVE_WINDOW_2S_CNT / 10U;  /* 2 s: 0.0, 0.2, ..., 2.0 s */
    }

    return WAVE_WINDOW_1S_CNT;            /* 5 s: 0, 1, ..., 5 s */
}

/**
 * @brief wave_format_time_label：旧版波形绘制实现。
 */
static void wave_format_time_label(char * buffer,
                                   uint32_t buffer_size,
                                   uint32_t cnt,
                                   uint32_t tick_cnt)
{
    const uint32_t seconds = cnt / WAVE_WINDOW_1S_CNT;

    if(tick_cnt < WAVE_WINDOW_1S_CNT) {
        const uint32_t tenths = (cnt % WAVE_WINDOW_1S_CNT) /
                                (WAVE_WINDOW_1S_CNT / 10U);
        (void)lv_snprintf(buffer, buffer_size, "%lu.%lus",
                          (unsigned long)seconds, (unsigned long)tenths);
    }
    else {
        (void)lv_snprintf(buffer, buffer_size, "%lus", (unsigned long)seconds);
    }
}
//帧回调时调用，画时间刻度
/**
 * @brief wave_time_ruler_draw_event_cb：旧版波形绘制实现。
 */
static void wave_time_ruler_draw_event_cb(lv_event_t * e)
{
    const uint32_t tick_cnt = wave_get_time_tick_cnt(s_view.wave_window_size);
    const uint32_t first_tick_cnt = ((s_view.wave_start_cnt + tick_cnt - 1U) /
                                     tick_cnt) * tick_cnt;
    lv_draw_line_dsc_t tick_dsc;
    lv_draw_label_dsc_t label_dsc;
    lv_area_t coords;
    uint32_t current_tick_cnt;
    uint32_t label_index = 0U;

    if(first_tick_cnt > s_view.wave_now_cnt) {
        return;
    }

    lv_obj_get_content_coords(lv_event_get_current_target(e), &coords);

    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = lv_color_hex(0x7D8B99U);
    tick_dsc.width = 1U;
    tick_dsc.opa = LV_OPA_COVER;

    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_hex(0xB9C5D0U);
    label_dsc.opa = LV_OPA_COVER;
    label_dsc.align = LV_TEXT_ALIGN_CENTER;

    for(current_tick_cnt = first_tick_cnt;
        current_tick_cnt <= s_view.wave_now_cnt &&
        label_index < WAVEFORM_TIME_LABEL_SLOTS;
        current_tick_cnt += tick_cnt, label_index++) {
        const int32_t x = (int32_t)wave_cnt_to_pixel(current_tick_cnt, &s_view);
        lv_area_t label_coords;

        tick_dsc.p1.x = x;
        tick_dsc.p1.y = coords.y1;
        tick_dsc.p2.x = x;
        tick_dsc.p2.y = coords.y1 + 6;
        lv_draw_line(lv_event_get_layer(e), &tick_dsc);

        wave_format_time_label(s_time_ruler_labels[label_index],
                               sizeof(s_time_ruler_labels[label_index]),
                               current_tick_cnt, tick_cnt);
        label_dsc.text = s_time_ruler_labels[label_index];

        if(x == coords.x1) {
            label_dsc.align = LV_TEXT_ALIGN_LEFT;
            label_coords.x1 = x;
            label_coords.x2 = x + 32;
        }
        else if(x == coords.x2) {
            label_dsc.align = LV_TEXT_ALIGN_RIGHT;
            label_coords.x1 = x - 32;
            label_coords.x2 = x;
        }
        else {
            label_dsc.align = LV_TEXT_ALIGN_CENTER;
            label_coords.x1 = x - 16;
            label_coords.x2 = x + 16;
        }

        label_coords.y1 = coords.y1 + 9;
        label_coords.y2 = coords.y2;
        lv_draw_label(lv_event_get_layer(e), &label_dsc, &label_coords);
    }
}
//设置现在的波形左右边界的cnt，如果没到显示区的右边界说明刚开始显示，左边界就是0，超过就代表开始滑动显示
/**
 * @brief wave_update_view：旧版波形绘制实现。
 */
static void wave_update_view(uint32_t now_cnt)
{
    s_view.wave_now_cnt = now_cnt;

    if(now_cnt > s_view.wave_window_size) {
        s_view.wave_start_cnt = now_cnt - s_view.wave_window_size;
    }
    else {
        s_view.wave_start_cnt = 0U;
    }
}
//cnt转化为屏幕x坐标，限制坐标在100~599
/**
 * @brief wave_cnt_to_pixel：旧版波形绘制实现。
 */
static uint32_t wave_cnt_to_pixel(uint32_t cnt,
                                  const wave_view_t * view)
{
    uint64_t pixel;

    if(view->wave_window_size == 0U || cnt <= view->wave_start_cnt) {
        return wave_x_start;
    }

    pixel = ((uint64_t)(cnt - view->wave_start_cnt) * wave_x_length)
          / view->wave_window_size;

    /* 右边界 CNT 恰好映射到 500，显示坐标要限制在 100~599。 */
    if(pixel >= wave_x_length) {
        return wave_x_end;
    }

    return wave_x_start + (uint32_t)pixel;
}
//高低电平转换为像素y坐标
/**
 * @brief wave_level_to_y：旧版波形绘制实现。
 */
static int32_t wave_level_to_y(const wave_channel_t * channel,
                               bool level)
{
    return channel->top + (level ? channel->high_offset
                                 : channel->low_offset);
}
/*
 * 更新一个像素列对应的边沿点对。
 * 新像素列记录首次翻转前的坐标和当前翻转后的坐标；
 * 同一像素列多次翻转时，只保留最早的起始电平和最终结束电平。
 */
static bool wave_append_edge_pair(wave_channel_t * channel,
                                  int32_t x,
                                  bool level_before,
                                  bool level_after)
{
    /*
     * CNT 分别不同但映射到同一像素列时，不再增加点。
     * 第一条边沿保存该列开始时的电平；后续边沿只更新该列最终电平。
     */
    if(channel->point_count >= 2U &&
       channel->points[channel->point_count - 2U].x == x) {
        channel->points[channel->point_count - 1U].y =
            wave_level_to_y(channel, level_after);
        return true;
    }

    if((channel->point_count + 2U) > WAVEFORM_POINT_CAPACITY) {
        return false;
    }

    channel->points[channel->point_count].x = x;
    channel->points[channel->point_count].y =
        wave_level_to_y(channel, level_before);
    channel->point_count++;

    channel->points[channel->point_count].x = x;
    channel->points[channel->point_count].y =
        wave_level_to_y(channel, level_after);
    channel->point_count++;

    return true;
}
//读出上次记录的起始电平，遇到翻转时判断翻转前后的电平，更新到坐标缓存内
/**
 * @brief wave_prepare_channel：旧版波形绘制实现。
 */
static void wave_prepare_channel(wave_channel_t * channel,
                                 const wave_view_t * view)
{
    uint32_t visible_start_cnt;
    uint32_t edge_index;
    bool level;

    channel->point_count = 0U;
    channel->draw_valid = false;

    /* 没有同步到初始电平，或当前时间尚未到初始电平时刻：只显示背景。 */
    if(!channel->capture.level_valid ||
       view->wave_now_cnt < channel->capture.level_cnt) {
        return;
    }

    visible_start_cnt = view->wave_start_cnt;
    if(visible_start_cnt < channel->capture.level_cnt) {
        visible_start_cnt = channel->capture.level_cnt;
    }

    level = channel->capture.level;

    /*
     * 自定义边沿表要求按 CNT 升序填写；0U 表示后续元素未使用。
     * 先跨过窗口左边界之前的边沿，恢复左端真实电平。
     */
    for(edge_index = 0U; edge_index < wave_edge_count; edge_index++) {
        uint32_t edge_cnt = channel->capture.edge_cnt[edge_index];

        if(edge_cnt == 0U || edge_cnt >= visible_start_cnt) {
            break;
        }

        if(edge_cnt >= channel->capture.level_cnt) {
            level = !level;
        }
    }

    channel->start_point.x =
        (int32_t)wave_cnt_to_pixel(visible_start_cnt, view);
    channel->start_point.y = wave_level_to_y(channel, level);

    /* 同一像素列内的后续边沿只更新已有点对的最终电平。 */
    for(; edge_index < wave_edge_count; edge_index++) {
        uint32_t edge_cnt = channel->capture.edge_cnt[edge_index];
        int32_t edge_x;

        if(edge_cnt == 0U || edge_cnt > view->wave_now_cnt) {
            break;
        }

        edge_x = (int32_t)wave_cnt_to_pixel(edge_cnt, view);

        (void)wave_append_edge_pair(channel, edge_x, level, !level);
        level = !level;
    }

    channel->end_point.x =
        (int32_t)wave_cnt_to_pixel(view->wave_now_cnt, view);
    channel->end_point.y = wave_level_to_y(channel, level);
    channel->draw_valid = true;
}
//将两个点位连成一条线
/**
 * @brief wave_draw_segment：旧版波形绘制实现。
 */
static void wave_draw_segment(lv_layer_t * layer,
                              const lv_point_precise_t * p1,
                              const lv_point_precise_t * p2,
                              uint32_t color)
{
    lv_draw_line_dsc_t line_dsc;

    /*
     * 偶数次翻转的同列点对已由 wave_draw_event_cb() 转为活动竖线；
     * 到这里的相同点只是普通重复线段，不创建无意义的绘制任务。
     */
    if(p1->x == p2->x && p1->y == p2->y) {
        return;
    }

    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(color);
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.p1 = *p1;
    line_dsc.p2 = *p2;

    lv_draw_line(layer, &line_dsc);
}
//绘制波形，lvgl帧回调时立刻调用
/**
 * @brief wave_draw_event_cb：旧版波形绘制实现。
 */
static void wave_draw_event_cb(lv_event_t * e)
{
    wave_channel_t * channel = lv_event_get_user_data(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_point_precise_t last_point;
    lv_point_precise_t activity_top;
    lv_point_precise_t activity_bottom;
    uint32_t point_index;

    if(channel == NULL || !channel->draw_valid) {
        return;
    }

    last_point = channel->start_point;

    /* points[] 每两个元素组成一条竖直翻转边沿。 */
    for(point_index = 0U;
        (point_index + 1U) < channel->point_count;
        point_index += 2U) {
        wave_draw_segment(layer, &last_point,
                          &channel->points[point_index], channel->color);

        if(channel->points[point_index].y ==
           channel->points[point_index + 1U].y) {
            /*
             * 同一像素列内发生偶数次翻转：最终电平不变，
             * 但仍画一条完整竖线，标记此列存在边沿活动。
             */
            activity_top.x = channel->points[point_index].x;
            activity_top.y = wave_level_to_y(channel, true);
            activity_bottom.x = activity_top.x;
            activity_bottom.y = wave_level_to_y(channel, false);
            wave_draw_segment(layer, &activity_top, &activity_bottom,
                              channel->color);
        }
        else {
            wave_draw_segment(layer, &channel->points[point_index],
                              &channel->points[point_index + 1U],
                              channel->color);
        }

        last_point = channel->points[point_index + 1U];
    }

    wave_draw_segment(layer, &last_point, &channel->end_point,
                      channel->color);
}



