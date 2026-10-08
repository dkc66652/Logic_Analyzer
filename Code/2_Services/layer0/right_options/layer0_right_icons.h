/**
 ******************************************************************************
 * @file    layer0_right_icons.h
 * @brief   右侧导航栏矢量图标类型及创建接口。
 ******************************************************************************
 */

#ifndef APP_NAV_ICONS_H
#define APP_NAV_ICONS_H

#include "lvgl.h"

typedef enum
{
    APP_NAV_ICON_OPEN_FILE = 0,
    APP_NAV_ICON_DEVICE_CONFIG,
    APP_NAV_ICON_PROTOCOL_DECODE,
    APP_NAV_ICON_VERSION_UPDATE
} app_nav_icon_t;

/**
 * @brief  在指定父对象内创建一个导航栏矢量图标。
 * @param  parent 图标的父对象，通常为 layer0_right_panel.c 创建的导航按钮。
 * @param  icon 要绘制的图标类型。
 * @return 创建成功返回图标对象，内存不足时返回 NULL。
 * @note   调用位置：layer0_right_panel.c 的 side_panel_create()。
 * @usage  icon_obj = app_nav_icon_create(button, APP_NAV_ICON_DEVICE_CONFIG);
 */
lv_obj_t * app_nav_icon_create(lv_obj_t * parent, app_nav_icon_t icon);

#endif /* APP_NAV_ICONS_H */
