/**
 * @file layer0_left_channels.c
 * @brief 左侧通道信息区的 LVGL 控件创建与样式设置。
 * @note 本模块只由 GUI 任务调用，不持有波形采集状态或显示缓冲区。
 */

#include "layer0_left_channels.h"

#define LAYER0_LEFT_WIDTH 100
#define LAYER0_BADGE_X 6
#define LAYER0_BADGE_SIZE 32
#define LAYER0_NAME_X 42
#define LAYER0_NAME_Y 7
#define LAYER0_DETAIL_X 42
#define LAYER0_DETAIL_Y 31
#define LAYER0_SEPARATOR_Y 55
#define LAYER0_SEPARATOR_RIGHT_MARGIN 6

static void layer0_left_channel_apply_style(const layer0_left_channel_config_t *config,
                                            layer0_left_channel_row_t *render,
                                            int16_t height);

/**
 * @brief 创建一路左侧通道信息行及其徽标、名称、说明和分隔线。
 * @param parent 左侧根容器，由调用方管理生命周期。
 * @param top 相对父容器顶部的 Y 坐标。
 * @param height 本行高度。
 * @param config 通道文本、颜色与可见性配置。
 * @param row 输出所有 LVGL 对象句柄；父容器清空后这些句柄失效。
 * @return 全部对象创建成功返回 true，否则返回 false。
 */
bool layer0_left_channel_create(lv_obj_t *parent,
                                int16_t top,
                                int16_t height,
                                const layer0_left_channel_config_t *config,
                                layer0_left_channel_row_t *row)
{
    if(parent == NULL || config == NULL || row == NULL || height <= 0) {
        return false;
    }

    row->side_row = lv_obj_create(parent);
    if(row->side_row == NULL) {
        return false;
    }

    lv_obj_remove_style_all(row->side_row);
    lv_obj_set_pos(row->side_row, 0, top);
    lv_obj_set_size(row->side_row, LAYER0_LEFT_WIDTH, height);
    lv_obj_set_scrollable(row->side_row, false);

    row->badge = lv_label_create(row->side_row);
    row->name_label = lv_label_create(row->side_row);
    row->detail_label = lv_label_create(row->side_row);
    row->separator = lv_obj_create(row->side_row);
    if(row->badge == NULL || row->name_label == NULL ||
       row->detail_label == NULL || row->separator == NULL) {
        return false;
    }

    layer0_left_channel_apply_style(config, row, height);
    return true;
}

/**
 * @brief 将通道配置映射到左侧徽标、名称、说明与分隔线的 LVGL 样式。
 * @param config 调用方传入的本行配置。
 * @param render 已完成创建的对象句柄。
 * @param height 用于计算徽标在本行中的垂直居中位置。
 */
static void layer0_left_channel_apply_style(const layer0_left_channel_config_t *config,
                                            layer0_left_channel_row_t *render,
                                            int16_t height)
{

    lv_label_set_text(render->badge, config->pin_text);
    lv_obj_set_pos(render->badge, LAYER0_BADGE_X,
                   (height - LAYER0_BADGE_SIZE) / 2);
    lv_obj_set_size(render->badge, LAYER0_BADGE_SIZE, LAYER0_BADGE_SIZE);
    lv_obj_set_style_bg_color(render->badge,
                              lv_color_hex(config->color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(render->badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(render->badge, 10, LV_PART_MAIN);
    lv_obj_set_style_text_color(render->badge,
                                lv_color_hex(0xFFFFFFU), LV_PART_MAIN);
    lv_obj_set_style_text_align(render->badge,
                                LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_top(render->badge, 9, LV_PART_MAIN);

    lv_label_set_text(render->name_label, config->name_text);
    lv_obj_set_pos(render->name_label, LAYER0_NAME_X, LAYER0_NAME_Y);
    lv_obj_set_width(render->name_label,
                     LAYER0_LEFT_WIDTH - LAYER0_NAME_X - 2);
    lv_label_set_long_mode(render->name_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(render->name_label,
                                lv_color_hex(0xE8EDF2U), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(render->name_label, -1, LV_PART_MAIN);

    lv_label_set_text(render->detail_label, config->detail_text);
    lv_obj_set_pos(render->detail_label, LAYER0_DETAIL_X, LAYER0_DETAIL_Y);
    lv_obj_set_width(render->detail_label,
                     LAYER0_LEFT_WIDTH - LAYER0_DETAIL_X - 2);
    lv_label_set_long_mode(render->detail_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(render->detail_label,
                                lv_color_hex(0x91A0B2U), LV_PART_MAIN);
    lv_obj_set_hidden(render->detail_label, !config->detail_visible);

    lv_obj_remove_style_all(render->separator);
    lv_obj_set_pos(render->separator, LAYER0_NAME_X, LAYER0_SEPARATOR_Y);
    lv_obj_set_size(render->separator,
                    LAYER0_LEFT_WIDTH - LAYER0_NAME_X - LAYER0_SEPARATOR_RIGHT_MARGIN,
                    1);
    lv_obj_set_style_bg_color(render->separator,
                              lv_color_hex(0x65717CU), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(render->separator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(render->separator, false);
    lv_obj_set_hidden(render->separator, !config->separator_visible);
}
