/**
 ******************************************************************************
 * @file    layer0_device_options.c
 * @brief   创建设备配置页，显示通道开关、采样频率和采集时间。
 * @details 页面通过 wave 模块读取和修改六路通道配置；所有控件均由 LVGL
 *          任务创建和处理，不额外建立 FreeRTOS 任务。
 ******************************************************************************
 */

#include "layer0_right_pages.h"
#include "resource_font.h"
#include "log_service.h"
#include "layer1_wave.h"

#define DEVICE_PAGE_WIDTH        138
#define DEVICE_TITLE_COLOR       0x2F80EDU
#define DEVICE_TEXT_COLOR        0xE6EDF7U
#define DEVICE_TEXT_MUTED_COLOR  0x91A0B2U
#define DEVICE_SEPARATOR_COLOR   0x2B3440U
#define DEVICE_VALUE_BG_COLOR    0x202833U
#define DEVICE_CHANNEL_SIZE       34

/* UTF-8 字节转义避免 Keil 的源文件代码页改变中文字节。 */
#define TEXT_DEVICE_CONFIG  "\xE8\xAE\xBE\xE5\xA4\x87\xE9\x85\x8D\xE7\xBD\xAE"
#define TEXT_CHANNEL_SELECT "\xE9\x80\x9A\xE9\x81\x93\xE9\x80\x89\xE6\x8B\xA9"
#define TEXT_CAPTURE_PARAM  "\xE9\x87\x87\xE9\x9B\x86\xE5\x8F\x82\xE6\x95\xB0"
#define TEXT_SAMPLE_RATE    "\xE9\x87\x87\xE6\xA0\xB7\xE9\xA2\x91\xE7\x8E\x87"
#define TEXT_SAMPLE_TIME    "\xE9\x87\x87\xE6\xA0\xB7\xE6\x97\xB6\xE9\x97\xB4"

typedef struct
{
    const char * name;                    /* 按钮显示的物理通道编号 */
    uint32_t color;                       /* 从 wave 配置读取并缓存的通道颜色 */
    uint32_t channel_index;               /* wave 模块使用的物理通道下标 */
    lv_obj_t * button;                    /* 当前页面中的通道选择按钮 */
    lv_obj_t * label;                     /* 当前按钮内部的编号标签 */
} device_channel_t;

static device_channel_t s_channels[] = {
    { "1", 0U, 0U, NULL, NULL },
    { "2", 0U, 1U, NULL, NULL },
    { "3", 0U, 2U, NULL, NULL },
    { "4", 0U, 3U, NULL, NULL },
    { "5", 0U, 4U, NULL, NULL },
    { "6", 0U, 5U, NULL, NULL }
};

/** 创建一行普通文字，并返回标签对象供调用者继续设置。 */
static lv_obj_t * device_config_label(lv_obj_t * parent, const char * text,
                                      int32_t x, int32_t y, uint32_t color);

/** 创建一路可点击的通道选择按钮，并同步 wave 模块中的当前选择状态。 */
static void device_config_channel_button(lv_obj_t * parent, uint32_t index,
                                          int32_t x, int32_t y);

/** 根据 enabled 切换一路按钮的填充、边框和文字颜色。 */
static void device_config_channel_set_visual(device_channel_t * channel,
                                             bool enabled);

/** 响应通道按钮点击，保存选择并重新创建 0~599 的静态通道界面。 */
static void device_config_channel_event_cb(lv_event_t * event);

/** 在指定纵坐标创建一条横向分隔线。 */
static void device_config_separator(lv_obj_t * parent, int32_t y);

/** 创建一行参数名称和右侧只读参数值。 */
static void device_config_value_row(lv_obj_t * parent, const char * name,
                                    const char * value, int32_t y);

/** 把 wave 中 1 MHz CNT 表示的采集时长格式化成秒或毫秒。 */
static void device_config_format_capture_time(char * buffer,
                                              uint32_t buffer_size);

/** 创建页面根对象，切换页面时该根对象由 side_panel 统一删除。 */
static lv_obj_t * page_device_config_display_task(lv_obj_t * page_parent);

/*==============================================================================
 * 公共函数：供 side_panel 模块调用
 *============================================================================*/

/**
 * @brief 创建“设备配置”页的全部控件。
 *
 * 六个通道按钮直接读取 wave 模块配置，因此切换到其它页面再返回时，选择
 * 状态仍然与左侧图标和波形轨道保持一致。
 */
void gui_page_device_config_create(lv_obj_t * page_parent)
{
    lv_obj_t * page;
    uint32_t i;
    char capture_time_text[16];

    page = page_device_config_display_task(page_parent);
    if(page == NULL) {
        (void)log_printf("ui: device config root allocation failed\r\n");
        return;
    }

    (void)log_printf("ui: device config root=%p size=%ldx%ld\r\n", (void *)page,
                     (long)lv_obj_get_width(page), (long)lv_obj_get_height(page));

    /* 页面根对象继承中文字体；所有后续控件均可直接显示 UTF-8 中文。 */
    lv_obj_set_style_text_font(page, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    (void)device_config_label(page, TEXT_DEVICE_CONFIG, 8, 14, DEVICE_TITLE_COLOR);
    (void)device_config_label(page, TEXT_CHANNEL_SELECT, 8, 46, DEVICE_TEXT_COLOR);

    /* 六路通道固定为 2 行 × 3 列，不显示 All / Clear 等操作按钮。 */
    for(i = 0U; i < (sizeof(s_channels) / sizeof(s_channels[0])); ++i) {
        const int32_t column = (int32_t)(i % 3U);
        const int32_t row = (int32_t)(i / 3U);
        device_config_channel_button(page, i,
                                     10 + column * 42,
                                     72 + row * 40);
    }

    device_config_separator(page, 158);
    (void)device_config_label(page, TEXT_CAPTURE_PARAM, 8, 172, DEVICE_TEXT_COLOR);
    device_config_value_row(page, TEXT_SAMPLE_RATE, "20 MHz", 202);
    device_config_format_capture_time(capture_time_text,
                                      sizeof(capture_time_text));
    device_config_value_row(page, TEXT_SAMPLE_TIME, capture_time_text, 238);
    (void)log_printf("ui: device config widgets created\r\n");
}

/** @brief 创建无滚动的设备配置页根对象。 */
static lv_obj_t * page_device_config_display_task(lv_obj_t * page_parent)
{
    lv_obj_t * page;

    if(page_parent == NULL) {
        return NULL;
    }

    page = lv_obj_create(page_parent);
    if(page == NULL) {
        return NULL;
    }

    lv_obj_remove_style_all(page);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, lv_obj_get_width(page_parent), lv_obj_get_height(page_parent));
    lv_obj_set_style_bg_color(page, lv_color_hex(0x000000U), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(page, false);
    return page;
}

/** @brief 创建并定位一个使用应用中文字体的单行标签。 */
static lv_obj_t * device_config_label(lv_obj_t * parent, const char * text,
                                      int32_t x, int32_t y, uint32_t color)
{
    lv_obj_t * label = lv_label_create(parent);

    if(label == NULL) {
        (void)log_printf("ui: device config label allocation failed\r\n");
        return NULL;
    }

    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, DEVICE_PAGE_WIDTH - x - 4);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    return label;
}

/**
 * @brief 创建一个物理通道选择按钮。
 *
 * 按钮本身不使用 LVGL 的自动 CHECKABLE 状态，最终显示状态始终以 wave
 * 模块保存的配置为准，避免按钮状态与实际波形配置不一致。
 */
static void device_config_channel_button(lv_obj_t * parent, uint32_t index,
                                          int32_t x, int32_t y)
{
    lv_obj_t * button = lv_button_create(parent);
    lv_obj_t * label;
    wave_channel_config_t config;

    if(button == NULL || index >= (sizeof(s_channels) / sizeof(s_channels[0]))) {
        (void)log_printf("ui: channel button %lu allocation failed\r\n",
                         (unsigned long)index);
        return;
    }

    if(wave_get_channel_config(s_channels[index].channel_index, &config)) {
        s_channels[index].color = config.color;
    }
    else {
        /* 初始化次序异常时使用中性灰，按钮仍然保持可见。 */
        s_channels[index].color = DEVICE_TEXT_MUTED_COLOR;
        config.enabled = false;
    }

    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, DEVICE_CHANNEL_SIZE, DEVICE_CHANNEL_SIZE);
    lv_obj_set_style_radius(button, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(s_channels[index].color), LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(s_channels[index].color), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);

    label = lv_label_create(button);
    if(label != NULL) {
        lv_label_set_text(label, s_channels[index].name);
        lv_obj_set_style_text_font(label, APP_FONT_CN_14_PTR, LV_PART_MAIN);
        lv_obj_center(label);
    }

    s_channels[index].button = button;
    s_channels[index].label = label;
    lv_obj_add_event_cb(button, device_config_channel_event_cb,
                        LV_EVENT_CLICKED, &s_channels[index]);

    device_config_channel_set_visual(&s_channels[index], config.enabled);
}

/** @brief 用实心表示已启用，用透明底和彩色边框表示未启用。 */
static void device_config_channel_set_visual(device_channel_t * channel,
                                             bool enabled)
{
    if(channel == NULL || channel->button == NULL) {
        return;
    }

    lv_obj_set_style_bg_opa(channel->button,
                            enabled ? LV_OPA_COVER : LV_OPA_TRANSP,
                            LV_PART_MAIN);
    if(channel->label != NULL) {
        lv_obj_set_style_text_color(channel->label,
                                    lv_color_hex(enabled ? 0xFFFFFFU
                                                         : channel->color),
                                    LV_PART_MAIN);
    }
}

/*==============================================================================
 * 私有函数：仅供本文件内部调用
 *============================================================================*/

/** @brief 切换一路通道并让 wave 模块立即刷新静态布局。 */
static void device_config_channel_event_cb(lv_event_t * event)
{
    device_channel_t * channel = lv_event_get_user_data(event);
    wave_channel_config_t config;
    bool new_enabled;

    if(channel == NULL ||
       !wave_get_channel_config(channel->channel_index, &config)) {
        return;
    }

    new_enabled = !config.enabled;
    if(!wave_set_channel_enabled(channel->channel_index, new_enabled)) {
        /* 例如最后一路通道不能关闭时，不重建界面，按钮保持原状态。 */
        device_config_channel_set_visual(channel, config.enabled);
        return;
    }

    if(!wave_apply_configuration()) {
        /* 恢复旧选择，保证按钮、配置与当前界面不会长期处于不同状态。 */
        (void)wave_set_channel_enabled(channel->channel_index, config.enabled);
        (void)wave_apply_configuration();
        device_config_channel_set_visual(channel, config.enabled);
        (void)log_printf("ui: channel %lu selection failed\r\n",
                         (unsigned long)(channel->channel_index + 1U));
        return;
    }

    device_config_channel_set_visual(channel, new_enabled);
}

/** @brief 创建一条不接收触摸事件的单像素横向分隔线。 */
static void device_config_separator(lv_obj_t * parent, int32_t y)
{
    lv_obj_t * line = lv_obj_create(parent);

    if(line == NULL) {
        (void)log_printf("ui: device config separator allocation failed\r\n");
        return;
    }

    lv_obj_remove_style_all(line);
    lv_obj_set_pos(line, 8, y);
    lv_obj_set_size(line, DEVICE_PAGE_WIDTH - 16, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(DEVICE_SEPARATOR_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(line, false);
}

/** @brief 创建参数名称及其右侧的只读数值框。 */
static void device_config_value_row(lv_obj_t * parent, const char * name,
                                    const char * value, int32_t y)
{
    lv_obj_t * value_box = lv_obj_create(parent);
    lv_obj_t * value_label;

    (void)device_config_label(parent, name, 8, y + 6, DEVICE_TEXT_MUTED_COLOR);
    if(value_box == NULL) {
        (void)log_printf("ui: device config value box allocation failed\r\n");
        return;
    }

    lv_obj_set_pos(value_box, 76, y);
    lv_obj_set_size(value_box, 56, 28);
    lv_obj_set_style_radius(value_box, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(value_box, lv_color_hex(DEVICE_VALUE_BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(value_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(value_box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(value_box, lv_color_hex(DEVICE_SEPARATOR_COLOR), LV_PART_MAIN);
    lv_obj_set_style_pad_all(value_box, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(value_box, false);

    value_label = lv_label_create(value_box);
    if(value_label != NULL) {
        lv_label_set_text(value_label, value);
        lv_obj_set_style_text_color(value_label, lv_color_hex(DEVICE_TEXT_COLOR), LV_PART_MAIN);
        lv_obj_set_style_text_font(value_label, APP_FONT_CN_14_PTR, LV_PART_MAIN);
        lv_obj_center(value_label);
    }
}

/** @brief 优先以整秒显示，否则向上取整为毫秒，避免显示小数。 */
static void device_config_format_capture_time(char * buffer,
                                              uint32_t buffer_size)
{
    const uint32_t duration_cnt = wave_get_capture_duration();

    if(buffer == NULL || buffer_size == 0U) {
        return;
    }

    if(duration_cnt != 0U && (duration_cnt % WAVE_WINDOW_1S_CNT) == 0U) {
        (void)lv_snprintf(buffer, buffer_size, "%lu s",
                          (unsigned long)(duration_cnt / WAVE_WINDOW_1S_CNT));
    }
    else {
        (void)lv_snprintf(buffer, buffer_size, "%lu ms",
                          (unsigned long)((duration_cnt + 999U) / 1000U));
    }
}