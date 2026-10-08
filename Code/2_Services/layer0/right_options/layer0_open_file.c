/**
 ******************************************************************************
 * @file    layer0_open_file.c
 * @brief   创建“打开文件”页面；当前仅实现页面容器和黑色背景。
 ******************************************************************************
 */

#include "layer0_right_pages.h"

static void page_open_file_display_task(lv_obj_t *page_parent);

/*==============================================================================
 * 公共函数：供 side_panel 模块调用
 *============================================================================*/

/** 创建“打开文件”页面；页面控件统一运行在现有 LVGL 任务中。 */
void gui_page_open_file_create(lv_obj_t *page_parent)
{
    page_open_file_display_task(page_parent);
}

/*==============================================================================
 * 私有函数：仅供本文件内部调用
 *============================================================================*/

/** 创建本页面的 LVGL 对象，不单独创建 FreeRTOS 任务。 */
static void page_open_file_display_task(lv_obj_t *page_parent)
{
    lv_obj_t * page;

    if(page_parent == NULL) {
        return;
    }

    page = lv_obj_create(page_parent);
    if(page == NULL) {
        return;
    }

    lv_obj_remove_style_all(page);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, lv_obj_get_width(page_parent), lv_obj_get_height(page_parent));
    lv_obj_set_style_bg_color(page, lv_color_hex(0x000000U), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(page, false);
}