/**
 ******************************************************************************
 * @file    layer0_right_panel.c
 * @brief   创建右侧页面区、导航工具栏和波形采集控制界面。
 * @details 负责四个功能页面的切换，以及模拟采集的开始、取消、进度更新和
 *          波形刷新调度；具体页面显示由 Services/layer0_right_pages.c 创建。
 ******************************************************************************
 */

#include "layer0_right_panel.h"

#include "layer0_right_icons.h"
#include "layer0_right_pages.h"




#include "layer1_wave.h"
#include "log_service.h"
#include <stdint.h>

#define SIDE_PAGE_SEPARATOR_X   600
#define SIDE_PAGE_X             602
#define SIDE_PAGE_WIDTH         138
#define SIDE_TOOLBAR_X          740
#define SIDE_TOOLBAR_WIDTH       60
#define SIDE_TOOLBAR_BUTTON      48
#define SIDE_TOOLBAR_BUTTON_X    5
#define SIDE_TOOLBAR_FIRST_Y     20
#define SIDE_TOOLBAR_GAP         16
#define SIDE_TOOLBAR_NAV_FIRST_Y (SIDE_TOOLBAR_FIRST_Y + SIDE_TOOLBAR_BUTTON + SIDE_TOOLBAR_GAP)
#define SIDE_CAPTURE_ARC_SIZE     40

#define COLOR_SEPARATOR          0x2B3440U
#define COLOR_SCREEN_BG          0x0A0F14U
#define COLOR_TOOLBAR_BG        0x171D25U
#define COLOR_ACCENT            0x2F80EDU

typedef enum
{
    SIDE_PAGE_OPEN_FILE = 0,              /* 文件打开页面 */
    SIDE_PAGE_DEVICE_CONFIG,              /* 设备与通道配置页面 */
    SIDE_PAGE_PROTOCOL_DECODE,             /* 协议解码页面 */
    SIDE_PAGE_VERSION_UPDATE,              /* 固件版本和升级页面 */
    SIDE_PAGE_COUNT                        /* 页面数量，仅用于数组长度和检查 */
} side_page_t;

typedef enum
{
    SIDE_CAPTURE_IDLE = 0,                /* 尚未开始或本轮采集已经完成 */
    SIDE_CAPTURE_RUNNING                  /* 波形和圆环都随时间推进 */
} side_capture_state_t;

typedef struct
{
    lv_obj_t * page_host;                  /* x=602~739 的当前页面父对象 */
    lv_obj_t * buttons[SIDE_PAGE_COUNT];   /* 四个页面导航按钮 */
    side_page_t active_page;               /* 当前正在显示的页面编号 */

    lv_obj_t * capture_button;             /* 工具栏最上方的开始/暂停按钮 */
    lv_obj_t * capture_arc;                /* 围绕按钮符号的整数百分比圆环 */
    lv_obj_t * capture_symbol;             /* 播放或暂停符号标签 */
    side_capture_state_t capture_state;     /* 当前采集控制状态 */
    uint32_t capture_duration_cnt;          /* 本轮计划时长，单位为 1 MHz CNT */
    uint32_t capture_duration_ms;           /* 本轮计划时长，单位为毫秒 */
    uint32_t last_frame_tick;               /* 上一帧准备完成时读取的 LVGL tick */
    uint32_t elapsed_ms;                    /* 排除暂停时间后累计的有效毫秒 */
    uint8_t progress_percent;               /* 当前已经显示的整数进度百分比 */
    bool refresh_pending;                   /* 停止清屏后是否需要立即强制刷新 */
} side_panel_state_t;

static side_panel_state_t s_panel;

/** 响应四个页面导航按钮的点击事件。 */
static void side_panel_nav_event_cb(lv_event_t * e);

/** 清理旧页面并创建指定页面，同时刷新四个导航按钮的选中状态。 */
static void side_panel_show_page(side_page_t page);

/** 在工具栏最上方创建开始/暂停按钮、进度圆环和刷新定时器。 */
static bool side_panel_create_capture_control(lv_obj_t * toolbar);

/** 响应开始/暂停按钮，运行中点击会冻结当前采集结果。 */
static void side_panel_capture_event_cb(lv_event_t * e);

/** 开始一轮新的模拟采集并清零进度。 */
static bool side_panel_capture_start(void);

/** 暂停当前模拟采集，并把已采集的全部数据缩放到完整波形窗口。 */
static void side_panel_capture_pause(void);

/** 根据运行状态把中央符号切换为播放或暂停。 */
static void side_panel_capture_update_symbol(void);

/*==============================================================================
 * 公共函数：供 LVGL 主任务调用
 *============================================================================*/

/**
 * @brief 创建 600~799 的右侧页面区和工具栏。
 *
 * 工具栏第一格是采集控制，原有四个页面按钮整体向下移动一格。
 */
void side_panel_create(lv_obj_t * parent)
{
    static const app_nav_icon_t icons[SIDE_PAGE_COUNT] = {
        APP_NAV_ICON_OPEN_FILE,
        APP_NAV_ICON_DEVICE_CONFIG,
        APP_NAV_ICON_PROTOCOL_DECODE,
        APP_NAV_ICON_VERSION_UPDATE
    };
    lv_obj_t * separator;
    lv_obj_t * toolbar;
    lv_display_t * display;
    int32_t display_height;
    uint32_t i;

    if(parent == NULL) {
        (void)log_printf("ui: side panel parent is null\r\n");
        return;
    }
    if(s_panel.page_host != NULL) {
        (void)log_printf("ui: side panel already exists\r\n");
        return;
    }

    /*
     * Do not derive this from the screen object's current coordinates.  Use
     * the registered LTDC display resolution so page creation remains valid
     * even while the screen object's layout is pending.
     */
    display = lv_obj_get_display(parent);
    if(display == NULL) {
        (void)log_printf("ui: side panel display is null\r\n");
        return;
    }
    display_height = lv_display_get_vertical_resolution(display);
    if(display_height <= 0) {
        (void)log_printf("ui: side panel invalid display height=%ld\r\n",
                         (long)display_height);
        return;
    }

    /* x=600 的竖线隔开波形区和后续页面区；x=600..739 暂时保持为空。 */
    separator = lv_obj_create(parent);
    lv_obj_remove_style_all(separator);
    lv_obj_set_pos(separator, SIDE_PAGE_SEPARATOR_X, 0);
    lv_obj_set_size(separator, 2, display_height);
    lv_obj_set_style_bg_color(separator, lv_color_hex(COLOR_SEPARATOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(separator, false);

    s_panel.page_host = lv_obj_create(parent);
    if(s_panel.page_host == NULL) {
        (void)log_printf("ui: page host allocation failed\r\n");
        return;
    }
    lv_obj_remove_style_all(s_panel.page_host);
    lv_obj_set_pos(s_panel.page_host, SIDE_PAGE_X, 0);
    lv_obj_set_size(s_panel.page_host, SIDE_PAGE_WIDTH, display_height);
    lv_obj_set_style_bg_color(s_panel.page_host, lv_color_hex(COLOR_SCREEN_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_panel.page_host, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(s_panel.page_host, false);

    toolbar = lv_obj_create(parent);
    if(toolbar == NULL) {
        (void)log_printf("ui: toolbar allocation failed\r\n");
        return;
    }
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_pos(toolbar, SIDE_TOOLBAR_X, 0);
    lv_obj_set_size(toolbar, SIDE_TOOLBAR_WIDTH, display_height);
    lv_obj_set_style_bg_color(toolbar, lv_color_hex(COLOR_TOOLBAR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(toolbar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(toolbar, false);

    if(!side_panel_create_capture_control(toolbar)) {
        (void)log_printf("ui: capture control allocation failed\r\n");
    }

    for(i = 0U; i < SIDE_PAGE_COUNT; ++i) {
        lv_obj_t * button = lv_button_create(toolbar);
        lv_obj_t * icon;

        if(button == NULL) {
            (void)log_printf("ui: nav button %lu allocation failed\r\n",
                             (unsigned long)i);
            continue;
        }

        s_panel.buttons[i] = button;
        lv_obj_set_pos(button, SIDE_TOOLBAR_BUTTON_X,
                       SIDE_TOOLBAR_NAV_FIRST_Y + (int32_t)i *
                       (SIDE_TOOLBAR_BUTTON + SIDE_TOOLBAR_GAP));
        lv_obj_set_size(button, SIDE_TOOLBAR_BUTTON, SIDE_TOOLBAR_BUTTON);
        lv_obj_set_style_radius(button, 12, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_ACCENT),
                                  LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
                                LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x25364FU),
                                  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
                                LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(button, side_panel_nav_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        icon = app_nav_icon_create(button, icons[i]);
        if(icon != NULL) {
            lv_obj_center(icon);
        }
    }

    /*
     * Size and position styles are applied by LVGL's deferred layout pass.
     * The initial page reads page_host's size, therefore materialize this
     * layout before side_panel_show_page() creates that page.
     */
    lv_obj_update_layout(parent);

    /* 首屏固定显示设备配置。 */
    side_panel_show_page(SIDE_PAGE_DEVICE_CONFIG);
}

/**
 * @brief 根据真实 tick 差更新采集进度和按钮状态。
 * @return true 表示本轮仍有界面状态被处理。
 *
 * @note  本函数只操作 LVGL 控件和应用状态。波形数据准备、SDRAM 写入以及
 *        换帧均由帧中断信号量唤醒的独立波形任务完成。
 */
bool side_panel_capture_process(void)
{
    bool refresh_pending = s_panel.refresh_pending;
    uint32_t now_tick;
    uint32_t frame_delta_ms;
    uint32_t progress;

    s_panel.refresh_pending = false;
    if(s_panel.capture_state != SIDE_CAPTURE_RUNNING ||
       s_panel.capture_duration_ms == 0U) {
        return refresh_pending;
    }

    /* 单次读取 tick，只用于采集进度与结束条件，不再驱动波形绘制。 */
    now_tick = lv_tick_get();
    frame_delta_ms = now_tick - s_panel.last_frame_tick;
    s_panel.last_frame_tick = now_tick;
    if(frame_delta_ms > (s_panel.capture_duration_ms - s_panel.elapsed_ms)) {
        s_panel.elapsed_ms = s_panel.capture_duration_ms;
    }
    else {
        s_panel.elapsed_ms += frame_delta_ms;
    }

    if(s_panel.elapsed_ms >= s_panel.capture_duration_ms) {
        /* 最后一帧改用完整采集时长，重新映射并显示全部模拟边沿。 */
        if(!wave_complete_capture()) {
            return false;
        }
    }
    progress = (uint32_t)(((uint64_t)s_panel.elapsed_ms * 100U) /
                          s_panel.capture_duration_ms);
    if(progress > 100U) {
        progress = 100U;
    }
    if(progress != s_panel.progress_percent) {
        s_panel.progress_percent = (uint8_t)progress;
        lv_arc_set_value(s_panel.capture_arc, (int32_t)progress);
    }

    if(s_panel.elapsed_ms >= s_panel.capture_duration_ms) {
        s_panel.capture_state = SIDE_CAPTURE_IDLE;
        side_panel_capture_update_symbol();
    }

    return true;
}

/*==============================================================================
 * 私有函数：仅供本文件内部调用
 *============================================================================*/

/** @brief 读取被点击按钮的页面编号并切换页面。 */
static void side_panel_nav_event_cb(lv_event_t * e)
{
    const side_page_t page = (side_page_t)(uintptr_t)lv_event_get_user_data(e);

    (void)log_printf("ui: nav click page=%lu\r\n", (unsigned long)page);
    side_panel_show_page(page);
}

/** @brief 切换页面并保证同一时刻只有一个导航按钮处于选中状态。 */
static void side_panel_show_page(side_page_t page)
{
    uint32_t i;

    if(s_panel.page_host == NULL || page >= SIDE_PAGE_COUNT) {
        (void)log_printf("ui: show page rejected host=%p page=%lu\r\n",
                         (void *)s_panel.page_host, (unsigned long)page);
        return;
    }

    (void)log_printf("ui: show page=%lu host=%ldx%ld\r\n", (unsigned long)page,
                     (long)lv_obj_get_width(s_panel.page_host),
                     (long)lv_obj_get_height(s_panel.page_host));

    s_panel.active_page = page;
    for(i = 0U; i < SIDE_PAGE_COUNT; ++i) {
        if(i == (uint32_t)page) {
            lv_obj_add_state(s_panel.buttons[i], LV_STATE_CHECKED);
        }
        else {
            lv_obj_clear_state(s_panel.buttons[i], LV_STATE_CHECKED);
        }
        lv_obj_invalidate(s_panel.buttons[i]);
    }

    /* 删除当前页面根对象，再调用目标页面文件中的显示任务重建界面。 */
    lv_obj_clean(s_panel.page_host);
    switch(page) {
        case SIDE_PAGE_OPEN_FILE:
            gui_page_open_file_create(s_panel.page_host);
            break;
        case SIDE_PAGE_DEVICE_CONFIG:
            (void)log_printf("ui: create device config page\r\n");
            gui_page_device_config_create(s_panel.page_host);
            break;
        case SIDE_PAGE_PROTOCOL_DECODE:
            gui_page_protocol_decode_create(s_panel.page_host);
            break;
        case SIDE_PAGE_VERSION_UPDATE:
        default:
            gui_page_version_update_create(s_panel.page_host);
            break;
    }

}

/** @brief 创建蓝色采集按钮、无数字圆环和内部播放符号。 */
static bool side_panel_create_capture_control(lv_obj_t * toolbar)
{
    s_panel.capture_button = lv_button_create(toolbar);
    if(s_panel.capture_button == NULL) {
        return false;
    }

    lv_obj_set_pos(s_panel.capture_button,
                   SIDE_TOOLBAR_BUTTON_X, SIDE_TOOLBAR_FIRST_Y);
    lv_obj_set_size(s_panel.capture_button,
                    SIDE_TOOLBAR_BUTTON, SIDE_TOOLBAR_BUTTON);
    lv_obj_set_style_radius(s_panel.capture_button, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_panel.capture_button,
                              lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_panel.capture_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel.capture_button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_panel.capture_button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel.capture_button, 0, LV_PART_MAIN);
    s_panel.capture_arc = lv_arc_create(s_panel.capture_button);
    if(s_panel.capture_arc == NULL) {
        lv_obj_delete(s_panel.capture_button);
        s_panel.capture_button = NULL;
        return false;
    }
    lv_obj_set_size(s_panel.capture_arc,
                    SIDE_CAPTURE_ARC_SIZE, SIDE_CAPTURE_ARC_SIZE);
    lv_obj_center(s_panel.capture_arc);
    lv_arc_set_range(s_panel.capture_arc, 0, 100);
    lv_arc_set_rotation(s_panel.capture_arc, 270);
    lv_arc_set_bg_angles(s_panel.capture_arc, 0, 360);
    lv_arc_set_value(s_panel.capture_arc, 0);
    lv_obj_remove_style(s_panel.capture_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(s_panel.capture_arc, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_panel.capture_arc,
                               lv_color_hex(0x7FB5FFU), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_panel.capture_arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_panel.capture_arc, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_panel.capture_arc,
                               lv_color_hex(0xFFFFFFU), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_panel.capture_arc, true, LV_PART_INDICATOR);
    lv_obj_set_clickable(s_panel.capture_arc, false);

    s_panel.capture_symbol = lv_label_create(s_panel.capture_button);
    if(s_panel.capture_symbol == NULL) {
        lv_obj_delete(s_panel.capture_button);
        s_panel.capture_button = NULL;
        s_panel.capture_arc = NULL;
        return false;
    }
    lv_obj_set_style_text_color(s_panel.capture_symbol,
                                lv_color_hex(0xFFFFFFU), LV_PART_MAIN);
    lv_label_set_text_static(s_panel.capture_symbol, LV_SYMBOL_PLAY);
    lv_obj_center(s_panel.capture_symbol);
    lv_obj_set_clickable(s_panel.capture_symbol, false);

    s_panel.capture_state = SIDE_CAPTURE_IDLE;
    s_panel.progress_percent = 0U;
    lv_obj_add_event_cb(s_panel.capture_button, side_panel_capture_event_cb,
                        LV_EVENT_CLICKED, NULL);
    return true;
}

/** @brief 点击运行中的按钮会暂停并保留数据，空闲时点击会从头开始。 */
static void side_panel_capture_event_cb(lv_event_t * e)
{
    (void)e;

    if(s_panel.capture_state == SIDE_CAPTURE_RUNNING) {
        side_panel_capture_pause();
    }
    else {
        (void)side_panel_capture_start();
    }
}

/** @brief 清空旧显示并启动大容量模拟采集数据源。 */
static bool side_panel_capture_start(void)
{
    s_panel.capture_duration_cnt = wave_get_capture_duration();
    if(s_panel.capture_duration_cnt == 0U ||
       !wave_start_capture(0U)) {
        wave_stop_capture();
        (void)log_printf("ui: capture data source start failed\r\n");
        return false;
    }

    /* 1 MHz 下 1000 CNT 等于 1 ms；向上取整保证非整毫秒时不提前结束。 */
    s_panel.capture_duration_ms =
        (s_panel.capture_duration_cnt + 999U) / 1000U;
    s_panel.elapsed_ms = 0U;
    s_panel.last_frame_tick = lv_tick_get();
    s_panel.progress_percent = 0U;
    s_panel.capture_state = SIDE_CAPTURE_RUNNING;
    lv_arc_set_value(s_panel.capture_arc, 0);
    side_panel_capture_update_symbol();
    return true;
}

/** @brief 暂停本轮采集，并把实际采集时间映射为完整显示窗口。 */
static void side_panel_capture_pause(void)
{
    wave_cnt_range_t view_range;

    if(!wave_pause_capture()) {
        return;
    }

    s_panel.capture_state = SIDE_CAPTURE_IDLE;
    if(wave_get_view_range(&view_range)) {
        s_panel.elapsed_ms = (view_range.cnt_end + 999U) / 1000U;
    }

    s_panel.progress_percent = (uint8_t)(((uint64_t)s_panel.elapsed_ms * 100U) /
                                         s_panel.capture_duration_ms);
    if(s_panel.progress_percent > 100U) {
        s_panel.progress_percent = 100U;
    }

    s_panel.refresh_pending = true;
    lv_arc_set_value(s_panel.capture_arc, s_panel.progress_percent);
    side_panel_capture_update_symbol();
}

/** @brief 运行时显示停止操作符号，未运行时显示播放符号。 */
static void side_panel_capture_update_symbol(void)
{
    if(s_panel.capture_symbol == NULL) {
        return;
    }

    lv_label_set_text_static(s_panel.capture_symbol,
        s_panel.capture_state == SIDE_CAPTURE_RUNNING
        ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_center(s_panel.capture_symbol);
}
