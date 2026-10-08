/**
 ******************************************************************************
 * @file    layer0_right_icons.c
 * @brief   绘制右侧导航栏使用的文件、配置、协议和版本矢量图标。
 * @details 对外提供图标对象创建接口；具体线条绘制由私有 LVGL 事件回调完成。
 ******************************************************************************
 */

#include "layer0_right_icons.h"

#include <stdint.h>

#define APP_NAV_ICON_SIZE        34
#define APP_NAV_ICON_COLOR        0x8EA0BAU
#define APP_NAV_ICON_ACTIVE_COLOR 0xF4F8FFU

static void app_nav_icon_draw_cb(lv_event_t * e);
static void icon_line(lv_layer_t * layer, lv_draw_line_dsc_t * dsc,
                      int32_t x1, int32_t y1, int32_t x2, int32_t y2);

/*==============================================================================
 * 公共函数：供其他 App 模块调用
 *============================================================================*/

lv_obj_t * app_nav_icon_create(lv_obj_t * parent, app_nav_icon_t icon)
{
    lv_obj_t * obj = lv_obj_create(parent);

    if(obj == NULL) return NULL;

    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, APP_NAV_ICON_SIZE, APP_NAV_ICON_SIZE);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_event_bubble(obj, true);
    lv_obj_add_event_cb(obj, app_nav_icon_draw_cb, LV_EVENT_DRAW_MAIN,
                        (void *)(uintptr_t)icon);
    return obj;
}

/*==============================================================================
 * 私有函数：仅供本文件内部调用
 *============================================================================*/

static void app_nav_icon_draw_cb(lv_event_t * e)
{
    static const int8_t gear_ring[][2] = {
        { 17,  7 }, { 24, 10 }, { 27, 17 }, { 24, 24 },
        { 17, 27 }, { 10, 24 }, {  7, 17 }, { 10, 10 }
    };
    const app_nav_icon_t icon = (app_nav_icon_t)(uintptr_t)lv_event_get_user_data(e);
    const lv_obj_t * obj = lv_event_get_current_target(e);
    lv_draw_line_dsc_t dsc;
    lv_area_t c;
    uint32_t i;

    lv_obj_get_content_coords(obj, &c);
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(lv_obj_is_checked(lv_obj_get_parent(obj))
                             ? APP_NAV_ICON_ACTIVE_COLOR : APP_NAV_ICON_COLOR);
    dsc.width = 3U;
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 1U;
    dsc.round_end = 1U;

    if(icon == APP_NAV_ICON_OPEN_FILE) {
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 4, c.y1 + 11, c.x1 + 14, c.y1 + 11);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 14, c.y1 + 11, c.x1 + 17, c.y1 + 15);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 17, c.y1 + 15, c.x1 + 30, c.y1 + 15);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 4, c.y1 + 11, c.x1 + 4, c.y1 + 27);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 4, c.y1 + 27, c.x1 + 27, c.y1 + 27);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 27, c.y1 + 27, c.x1 + 30, c.y1 + 15);
    }
    else if(icon == APP_NAV_ICON_DEVICE_CONFIG) {
        for(i = 0U; i < 8U; ++i) {
            const uint32_t next = (i + 1U) % 8U;
            icon_line(lv_event_get_layer(e), &dsc, c.x1 + gear_ring[i][0], c.y1 + gear_ring[i][1],
                      c.x1 + gear_ring[next][0], c.y1 + gear_ring[next][1]);
        }
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 17, c.y1 + 3, c.x1 + 17, c.y1 + 7);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 31, c.y1 + 17, c.x1 + 27, c.y1 + 17);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 17, c.y1 + 31, c.x1 + 17, c.y1 + 27);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 3, c.y1 + 17, c.x1 + 7, c.y1 + 17);
        dsc.width = 2U;
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 14, c.y1 + 17, c.x1 + 20, c.y1 + 17);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 17, c.y1 + 14, c.x1 + 17, c.y1 + 20);
    }
    else if(icon == APP_NAV_ICON_PROTOCOL_DECODE) {
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 10, c.y1 + 6, c.x1 + 5, c.y1 + 6);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 5, c.y1 + 6, c.x1 + 5, c.y1 + 28);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 5, c.y1 + 28, c.x1 + 10, c.y1 + 28);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 24, c.y1 + 6, c.x1 + 29, c.y1 + 6);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 29, c.y1 + 6, c.x1 + 29, c.y1 + 28);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 29, c.y1 + 28, c.x1 + 24, c.y1 + 28);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 10, c.y1 + 22, c.x1 + 14, c.y1 + 22);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 14, c.y1 + 22, c.x1 + 14, c.y1 + 12);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 14, c.y1 + 12, c.x1 + 20, c.y1 + 12);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 20, c.y1 + 12, c.x1 + 20, c.y1 + 22);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 20, c.y1 + 22, c.x1 + 24, c.y1 + 22);
    }
    else {
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 17, c.y1 + 5, c.x1 + 17, c.y1 + 21);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 17, c.y1 + 21, c.x1 + 11, c.y1 + 15);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 17, c.y1 + 21, c.x1 + 23, c.y1 + 15);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 6, c.y1 + 24, c.x1 + 6, c.y1 + 29);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 6, c.y1 + 29, c.x1 + 28, c.y1 + 29);
        icon_line(lv_event_get_layer(e), &dsc, c.x1 + 28, c.y1 + 29, c.x1 + 28, c.y1 + 24);
    }
}

/**
 * @brief icon_line：右侧导航图标绘制。
 */
static void icon_line(lv_layer_t * layer, lv_draw_line_dsc_t * dsc,
                      int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    dsc->p1.x = x1; dsc->p1.y = y1;
    dsc->p2.x = x2; dsc->p2.y = y2;
    lv_draw_line(layer, dsc);
}
