/**
 ******************************************************************************
 * @file    layer0_protocol_options.c
 * @brief   协议解码入口页及Layer0协议设置弹窗。
 * @details 右侧600~739区域仅显示UART、I2C、SPI入口。点击入口后，在
 *          Layer0创建覆盖100~599波形区的设置窗；关闭窗口会删除该覆盖对象，
 *          使Layer1中的原波形立即重新可见。解析器尚未接入时，确定按钮只
 *          保存本次界面设置的触发点，不修改采集或波形数据。
 ******************************************************************************
 */

#include "layer0_right_pages.h"

#include "layer1_wave.h"
#include "log_service.h"
#include "resource_font.h"

#include <stdint.h>

#define PROTOCOL_PAGE_TITLE_COLOR       0x2F80EDU
#define PROTOCOL_TEXT_COLOR             0xE6EDF7U
#define PROTOCOL_MUTED_COLOR            0xA9B9D0U
#define PROTOCOL_PANEL_COLOR            0x1D1D1DU
#define PROTOCOL_HEADER_COLOR           0x202020U
#define PROTOCOL_INPUT_COLOR            0x181818U
#define PROTOCOL_LINE_COLOR             0x3A3A40U
#define PROTOCOL_ACCENT_COLOR           0x2F80EDU
#define PROTOCOL_PAGE_WIDTH             138
#define PROTOCOL_DIALOG_PADDING          14
#define PROTOCOL_DIALOG_HEADER_HEIGHT    50
#define PROTOCOL_DIALOG_FOOTER_HEIGHT    54
#define PROTOCOL_DIALOG_VALUE_WIDTH     172
#define PROTOCOL_DIALOG_ROW_HEIGHT       39

typedef enum
{
    PROTOCOL_KIND_UART = 0,
    PROTOCOL_KIND_I2C,
    PROTOCOL_KIND_SPI,
    PROTOCOL_KIND_COUNT
} protocol_kind_t;

typedef struct
{
    lv_obj_t *clear_root;       /* 先于对话框显示的Layer0不透明黑色清屏层 */
    lv_obj_t *root;             /* 覆盖波形区的Layer0对话框根对象 */
    lv_obj_t *enabled_switch;   /* 当前协议的启用开关 */
    lv_timer_t *open_timer;     /* 清屏帧完成后创建设置窗的一次性定时器 */
    protocol_kind_t kind;       /* 当前正在编辑的协议 */
} protocol_dialog_t;

static protocol_dialog_t s_dialog;

static void protocol_page_button_event_cb(lv_event_t *event);
static void protocol_dialog_close_event_cb(lv_event_t *event);
static void protocol_dialog_confirm_event_cb(lv_event_t *event);
static void protocol_dialog_open(protocol_kind_t kind);
static void protocol_dialog_prepare_clear(protocol_kind_t kind);
static void protocol_dialog_open_timer_cb(lv_timer_t *timer);
static void protocol_dialog_close(void);
static void protocol_dialog_build_uart(lv_obj_t *parent, int32_t first_y);
static void protocol_dialog_build_i2c(lv_obj_t *parent, int32_t first_y);
static void protocol_dialog_build_spi(lv_obj_t *parent, int32_t first_y);

/**
 * @brief 创建协议解码右侧入口页面。
 * @param page_parent 右侧600~739页面区的父对象。
 * @note 入口页仅提供当前产品定义的UART、I2C和SPI三个协议。
 */
void gui_page_protocol_decode_create(lv_obj_t *page_parent)
{
    static const char * const names[PROTOCOL_KIND_COUNT] = { "UART", "I2C", "SPI" };
    static const int32_t button_x[PROTOCOL_KIND_COUNT] = { 8, 57, 98 };
    static const int32_t button_width[PROTOCOL_KIND_COUNT] = { 43, 35, 34 };
    lv_obj_t *page;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    uint32_t index;

    if(page_parent == NULL) return;

    page = lv_obj_create(page_parent);
    if(page == NULL) return;
    lv_obj_remove_style_all(page);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, lv_obj_get_width(page_parent), lv_obj_get_height(page_parent));
    lv_obj_set_style_bg_color(page, lv_color_hex(PROTOCOL_PANEL_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(page, false);
    lv_obj_set_style_text_font(page, APP_FONT_CN_14_PTR, LV_PART_MAIN);

    title = lv_label_create(page);
    if(title != NULL) {
        lv_label_set_text(title, "协议解码");
        lv_obj_set_pos(title, 8, 16);
        lv_obj_set_style_text_font(title, APP_FONT_CN_14_PTR, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(PROTOCOL_PAGE_TITLE_COLOR), LV_PART_MAIN);
    }
    subtitle = lv_label_create(page);
    if(subtitle != NULL) {
        lv_label_set_text(subtitle, "协议");
        lv_obj_set_pos(subtitle, 8, 53);
        lv_obj_set_style_text_font(subtitle, APP_FONT_CN_14_PTR, LV_PART_MAIN);
        lv_obj_set_style_text_color(subtitle, lv_color_hex(PROTOCOL_TEXT_COLOR), LV_PART_MAIN);
    }

    for(index = 0U; index < PROTOCOL_KIND_COUNT; index++) {
        lv_obj_t *button = lv_button_create(page);
        lv_obj_t *label;
        const int32_t width = button_width[index];
        const int32_t x = button_x[index];

        if(button == NULL) continue;
        lv_obj_set_pos(button, x, 83);
        lv_obj_set_size(button, width, 29);
        lv_obj_set_style_radius(button, 15, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, lv_color_hex(PROTOCOL_MUTED_COLOR), LV_PART_MAIN);
        lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(button, protocol_page_button_event_cb,
                            LV_EVENT_CLICKED, (void *)(uintptr_t)index);

        label = lv_label_create(button);
        if(label != NULL) {
            lv_label_set_text(label, names[index]);
            lv_obj_set_style_text_font(label, APP_FONT_CN_14_PTR, LV_PART_MAIN);
            lv_obj_set_style_text_color(label, lv_color_hex(PROTOCOL_MUTED_COLOR), LV_PART_MAIN);
            lv_obj_center(label);
        }
    }
}

/** @brief 响应右侧协议入口按钮，打开覆盖波形区的设置窗口。 */
static void protocol_page_button_event_cb(lv_event_t *event)
{
    protocol_dialog_prepare_clear((protocol_kind_t)(uintptr_t)lv_event_get_user_data(event));
}

/** @brief 创建下拉选择控件并设置统一的深色外观。 */
static lv_obj_t *protocol_dialog_dropdown(lv_obj_t *parent, int32_t y,
                                          const char *options, uint32_t selected)
{
    lv_obj_t *dropdown = lv_dropdown_create(parent);

    if(dropdown == NULL) return NULL;
    lv_obj_set_pos(dropdown, WAVE_AREA_WIDTH - PROTOCOL_DIALOG_PADDING -
                   PROTOCOL_DIALOG_VALUE_WIDTH, y);
    lv_obj_set_size(dropdown, PROTOCOL_DIALOG_VALUE_WIDTH, 31);
    lv_dropdown_set_options_static(dropdown, options);
    lv_dropdown_set_selected(dropdown, selected);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(PROTOCOL_INPUT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, lv_color_hex(PROTOCOL_TEXT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_text_font(dropdown, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    return dropdown;
}

/** @brief 创建一行字段标签和右侧下拉选择控件。 */
static void protocol_dialog_row(lv_obj_t *parent, const char *name, int32_t y,
                                const char *options, uint32_t selected)
{
    lv_obj_t *label = lv_label_create(parent);

    if(label != NULL) {
        lv_label_set_text(label, name);
        lv_obj_set_pos(label, PROTOCOL_DIALOG_PADDING, y + 6);
        lv_obj_set_width(label, WAVE_AREA_WIDTH - PROTOCOL_DIALOG_VALUE_WIDTH - 42);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(label, APP_FONT_CN_14_PTR, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(PROTOCOL_TEXT_COLOR), LV_PART_MAIN);
    }
    (void)protocol_dialog_dropdown(parent, y, options, selected);
}

/** @brief 创建UART设置字段。 */
static void protocol_dialog_build_uart(lv_obj_t *parent, int32_t first_y)
{
    protocol_dialog_row(parent, "RX (UART receive line)", first_y + 0 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "D1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6", 0U);
    protocol_dialog_row(parent, "TX (UART transmit line)", first_y + 1 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "D1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6", 1U);
    protocol_dialog_row(parent, "Baud rate (波特率)", first_y + 2 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "9600\n19200\n38400\n57600\n115200\n1000000", 4U);
    protocol_dialog_row(parent, "Data bits (数据位数)", first_y + 3 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "5\n6\n7\n8\n9", 3U);
    protocol_dialog_row(parent, "Parity (校验位)", first_y + 4 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "none\neven\nodd", 0U);
    protocol_dialog_row(parent, "Stop bits (停止位)", first_y + 5 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "1.0\n1.5\n2.0", 0U);
    protocol_dialog_row(parent, "Bit order (位序)", first_y + 6 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "lsb-first\nmsb-first", 0U);
    protocol_dialog_row(parent, "Data format (数据格式)", first_y + 7 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "hex\ndec\nbin\nascii", 0U);
}

/** @brief 创建I2C设置字段。 */
static void protocol_dialog_build_i2c(lv_obj_t *parent, int32_t first_y)
{
    protocol_dialog_row(parent, "SCL (Serial clock line) *", first_y + 0 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6", 0U);
    protocol_dialog_row(parent, "SDA (Serial data line) *", first_y + 1 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6", 0U);
    protocol_dialog_row(parent, "Slave address format", first_y + 2 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "shifted\nunshifted", 0U);
    protocol_dialog_row(parent, "Display packets (数据格式)", first_y + 3 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "hex\ndec\nbin", 0U);
    protocol_dialog_row(parent, "Show data point (数据点显示)", first_y + 4 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "yes\nno", 0U);
}

/** @brief 创建SPI设置字段。 */
static void protocol_dialog_build_spi(lv_obj_t *parent, int32_t first_y)
{
    protocol_dialog_row(parent, "CLK (Clock串行时钟) *", first_y + 0 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6", 0U);
    protocol_dialog_row(parent, "MISO (Master in, slave out)", first_y + 1 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6", 0U);
    protocol_dialog_row(parent, "MOSI (Master out, slave in)", first_y + 2 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6", 0U);
    protocol_dialog_row(parent, "CS# (Chip-select片选信号)", first_y + 3 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6", 0U);
    protocol_dialog_row(parent, "CS# polarity (片选极性)", first_y + 4 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "active-low\nactive-high", 0U);
    protocol_dialog_row(parent, "Clock polarity (时钟极性)", first_y + 5 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "0\n1", 0U);
    protocol_dialog_row(parent, "Clock phase (时钟相位)", first_y + 6 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "0\n1", 0U);
    protocol_dialog_row(parent, "Bit order (位序)", first_y + 7 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "msb-first\nlsb-first", 0U);
    protocol_dialog_row(parent, "Word size (字长)", first_y + 8 * PROTOCOL_DIALOG_ROW_HEIGHT,
                        "4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16", 4U);
}

/**
 * @brief 先用Layer0黑色块清除波形区，再延后一帧创建协议窗口。
 * @details 清屏层与设置窗分两次LVGL调度创建。这样点击协议时，旧波形和
 *          Layer0控件会先从100~599区域消失；设置窗不会与旧画面混合出现。
 */
static void protocol_dialog_prepare_clear(protocol_kind_t kind)
{
    lv_display_t *display;
    int32_t height;

    if(kind >= PROTOCOL_KIND_COUNT || lv_display_get_default() == NULL) return;
    protocol_dialog_close();

    display = lv_display_get_default();
    height = lv_display_get_vertical_resolution(display);
    if(height <= 0) return;

    s_dialog.clear_root = lv_obj_create(lv_screen_active());
    if(s_dialog.clear_root == NULL) return;
    lv_obj_remove_style_all(s_dialog.clear_root);
    lv_obj_set_pos(s_dialog.clear_root, WAVE_AREA_X, 0);
    lv_obj_set_size(s_dialog.clear_root, WAVE_AREA_WIDTH, height);
    lv_obj_set_style_bg_color(s_dialog.clear_root, lv_color_hex(0x000000U), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dialog.clear_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(s_dialog.clear_root, false);
    lv_obj_move_foreground(s_dialog.clear_root);
    lv_obj_invalidate(s_dialog.clear_root);

    /* 给下一次LVGL刷新留出一个完整的黑色清屏帧。 */
    s_dialog.open_timer = lv_timer_create(protocol_dialog_open_timer_cb, 20U,
                                          (void *)(uintptr_t)kind);
    if(s_dialog.open_timer != NULL) lv_timer_set_repeat_count(s_dialog.open_timer, 1);
}

/** @brief 清屏帧显示后，创建对应协议的设置窗口。 */
static void protocol_dialog_open_timer_cb(lv_timer_t *timer)
{
    const protocol_kind_t kind = (protocol_kind_t)(uintptr_t)lv_timer_get_user_data(timer);

    s_dialog.open_timer = NULL;
    lv_timer_delete(timer);
    protocol_dialog_open(kind);
}

/**
 * @brief 打开一个协议设置弹窗。
 * @note 根对象只覆盖100~599波形区域。关闭时删除根对象，Layer0失效区域会重绘
 *       为原背景，Layer1的上一帧波形无需重算即可重新显示。
 */
static void protocol_dialog_open(protocol_kind_t kind)
{
    static const char * const titles[PROTOCOL_KIND_COUNT] = {
        "协议设置(UART)", "协议设置(I2C)", "协议设置(SPI)"
    };
    lv_obj_t *header;
    lv_obj_t *title;
    lv_obj_t *close_button;
    lv_obj_t *close_label;
    lv_obj_t *separator;
    lv_obj_t *footer;
    lv_obj_t *cancel_button;
    lv_obj_t *confirm_button;
    lv_obj_t *label;
    lv_display_t *display;
    int32_t height;
    int32_t first_y;

    if(kind >= PROTOCOL_KIND_COUNT || lv_display_get_default() == NULL) return;
    if(s_dialog.root != NULL) {
        lv_obj_delete_async(s_dialog.root);
        s_dialog.root = NULL;
        s_dialog.enabled_switch = NULL;
    }

    display = lv_display_get_default();
    height = lv_display_get_vertical_resolution(display);
    if(height <= PROTOCOL_DIALOG_HEADER_HEIGHT + PROTOCOL_DIALOG_FOOTER_HEIGHT) return;

    s_dialog.root = lv_obj_create(lv_screen_active());
    if(s_dialog.root == NULL) return;
    s_dialog.kind = kind;
    lv_obj_remove_style_all(s_dialog.root);
    lv_obj_set_pos(s_dialog.root, WAVE_AREA_X, 0);
    lv_obj_set_size(s_dialog.root, WAVE_AREA_WIDTH, height);
    lv_obj_set_style_bg_color(s_dialog.root, lv_color_hex(PROTOCOL_PANEL_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dialog.root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(s_dialog.root, false);
    lv_obj_set_style_text_font(s_dialog.root, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    lv_obj_move_foreground(s_dialog.root);

    header = lv_obj_create(s_dialog.root);
    if(header == NULL) { protocol_dialog_close(); return; }
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, WAVE_AREA_WIDTH, PROTOCOL_DIALOG_HEADER_HEIGHT);
    lv_obj_set_style_bg_color(header, lv_color_hex(PROTOCOL_HEADER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(header, false);

    title = lv_label_create(header);
    if(title != NULL) {
        lv_label_set_text(title, titles[kind]);
        lv_obj_set_pos(title, PROTOCOL_DIALOG_PADDING, 14);
        lv_obj_set_style_text_font(title, APP_FONT_CN_14_PTR, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(PROTOCOL_TEXT_COLOR), LV_PART_MAIN);
    }

    s_dialog.enabled_switch = lv_switch_create(header);
    if(s_dialog.enabled_switch != NULL) {
        lv_obj_set_pos(s_dialog.enabled_switch, 167, 12);
        lv_obj_set_size(s_dialog.enabled_switch, 48, 27);
        lv_obj_add_state(s_dialog.enabled_switch, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(s_dialog.enabled_switch, lv_color_hex(PROTOCOL_ACCENT_COLOR),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
    }

    close_button = lv_button_create(header);
    if(close_button != NULL) {
        lv_obj_set_pos(close_button, WAVE_AREA_WIDTH - 47, 5);
        lv_obj_set_size(close_button, 42, 40);
        lv_obj_set_style_bg_opa(close_button, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(close_button, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(close_button, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(close_button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(close_button, protocol_dialog_close_event_cb, LV_EVENT_CLICKED, NULL);
        close_label = lv_label_create(close_button);
        if(close_label != NULL) {
            lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_color(close_label, lv_color_hex(PROTOCOL_MUTED_COLOR), LV_PART_MAIN);
            lv_obj_center(close_label);
        }
    }

    first_y = PROTOCOL_DIALOG_HEADER_HEIGHT + 16;
    if(kind == PROTOCOL_KIND_UART) protocol_dialog_build_uart(s_dialog.root, first_y);
    else if(kind == PROTOCOL_KIND_I2C) protocol_dialog_build_i2c(s_dialog.root, first_y);
    else protocol_dialog_build_spi(s_dialog.root, first_y);

    separator = lv_obj_create(s_dialog.root);
    if(separator != NULL) {
        lv_obj_remove_style_all(separator);
        lv_obj_set_pos(separator, 0, height - PROTOCOL_DIALOG_FOOTER_HEIGHT);
        lv_obj_set_size(separator, WAVE_AREA_WIDTH, 1);
        lv_obj_set_style_bg_color(separator, lv_color_hex(PROTOCOL_LINE_COLOR), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, LV_PART_MAIN);
    }
    footer = lv_obj_create(s_dialog.root);
    if(footer == NULL) return;
    lv_obj_remove_style_all(footer);
    lv_obj_set_pos(footer, 0, height - PROTOCOL_DIALOG_FOOTER_HEIGHT + 1);
    lv_obj_set_size(footer, WAVE_AREA_WIDTH, PROTOCOL_DIALOG_FOOTER_HEIGHT - 1);
    lv_obj_set_style_bg_color(footer, lv_color_hex(PROTOCOL_HEADER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(footer, false);

    cancel_button = lv_button_create(footer);
    if(cancel_button != NULL) {
        lv_obj_set_pos(cancel_button, WAVE_AREA_WIDTH - 139, 8);
        lv_obj_set_size(cancel_button, 62, 37);
        lv_obj_set_style_bg_opa(cancel_button, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(cancel_button, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(cancel_button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(cancel_button, protocol_dialog_close_event_cb, LV_EVENT_CLICKED, NULL);
        label = lv_label_create(cancel_button);
        if(label != NULL) { lv_label_set_text(label, "取消"); lv_obj_center(label); }
    }
    confirm_button = lv_button_create(footer);
    if(confirm_button != NULL) {
        lv_obj_set_pos(confirm_button, WAVE_AREA_WIDTH - 70, 8);
        lv_obj_set_size(confirm_button, 56, 37);
        lv_obj_set_style_bg_color(confirm_button, lv_color_hex(PROTOCOL_ACCENT_COLOR), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(confirm_button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(confirm_button, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(confirm_button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(confirm_button, protocol_dialog_confirm_event_cb, LV_EVENT_CLICKED, NULL);
        label = lv_label_create(confirm_button);
        if(label != NULL) { lv_label_set_text(label, "确定"); lv_obj_center(label); }
    }
}

/** @brief 关闭或取消：移除Layer0覆盖窗口，恢复之前的Layer1波形。 */
static void protocol_dialog_close_event_cb(lv_event_t *event)
{
    (void)event;
    protocol_dialog_close();
}

/** @brief 确认当前设置；解析器API接入后在这里提交本协议参数。 */
static void protocol_dialog_confirm_event_cb(lv_event_t *event)
{
    (void)event;
    (void)log_printf("ui: protocol config confirmed kind=%u enabled=%u\r\n",
                     (unsigned int)s_dialog.kind,
                     (unsigned int)(s_dialog.enabled_switch != NULL &&
                     lv_obj_has_state(s_dialog.enabled_switch, LV_STATE_CHECKED)));
    protocol_dialog_close();
}

/** @brief 删除覆盖对象并失效活动屏幕，确保Layer0不保留旧窗口像素。 */
static void protocol_dialog_close(void)
{
    if(s_dialog.open_timer != NULL) {
        lv_timer_delete(s_dialog.open_timer);
        s_dialog.open_timer = NULL;
    }
    if(s_dialog.root != NULL) {
        /* 关闭按钮是根对象的子对象；异步删除避免在当前点击事件栈中释放祖先。 */
        lv_obj_delete_async(s_dialog.root);
        s_dialog.root = NULL;
        s_dialog.enabled_switch = NULL;
    }
    if(s_dialog.clear_root != NULL) {
        lv_obj_delete_async(s_dialog.clear_root);
        s_dialog.clear_root = NULL;
    }
    lv_obj_invalidate(lv_screen_active());
}
