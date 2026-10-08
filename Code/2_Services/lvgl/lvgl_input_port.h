/**
 ******************************************************************************
 * @file    lvgl_input_port.h
 * @brief   LVGL 输入设备 Port 的公共接口。
 ******************************************************************************
 */

#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/** 手势可在触控过程中连续上报；DRAG_MOVE/PINCH_MOVE 使用相邻采样点的增量。 */
typedef enum
{
    LV_PORT_GESTURE_SWIPE_LEFT,
    LV_PORT_GESTURE_SWIPE_RIGHT,
    LV_PORT_GESTURE_PINCH_IN,
    LV_PORT_GESTURE_PINCH_OUT,
    LV_PORT_GESTURE_DRAG_MOVE,
    LV_PORT_GESTURE_PINCH_MOVE
} lv_port_gesture_t;

typedef struct
{
    lv_point_t start;       /**< 单指滑动起点或双指中心起点。 */
    lv_point_t end;         /**< 单指滑动终点或双指中心终点。 */
    int32_t distance_delta; /**< 双指间距变化（放大为正，缩小为负）。 */
} lv_port_gesture_data_t;

typedef void (*lv_port_gesture_cb_t)(lv_port_gesture_t gesture,
                                     const lv_port_gesture_data_t *data,
                                     void *user_data);

/*==============================================================================
 * 公共接口
 *============================================================================*/

/**
 * @brief  初始化触摸控制器并注册 LVGL pointer 输入设备。
 * @note   调用位置：Core/Src/main.c 的 lvgl_task()。
 * @note   调用顺序：lv_init()、lv_port_disp_init()、lv_port_indev_init()。
 * @usage  lv_port_indev_init();
 */
void lv_port_indev_init(void);

/**
 * @brief  查询触摸控制器和 LVGL 输入对象是否已经准备好。
 * @return true 表示初始化成功，false 表示控制器未识别或对象创建失败。
 * @note   当前工程没有调用者；如需根据触摸可用性调整界面，应在初始化后调用。
 * @usage  if(lv_port_indev_is_ready()) { ... }
 */
bool lv_port_indev_is_ready(void);

/**
 * @brief 注册触控手势回调。
 * @param callback 回调在 LVGL 任务上下文中执行；传入 NULL 可取消注册。
 * @param user_data 原样传给回调的用户上下文。
 * @note 单指横向拖动和双指缩放在按下期间连续上报；松手时仍会对未连续上报的
 *       手势补发最终结果。
 */
void lv_port_indev_set_gesture_callback(lv_port_gesture_cb_t callback,
                                        void *user_data);

#endif /* LV_PORT_INDEV_H */
