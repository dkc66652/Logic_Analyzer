/**
 ******************************************************************************
 * @file    lvgl_input_port.c
 * @brief   LVGL 输入设备适配，将板载电容触摸设备注册为 pointer 输入设备。
 *
 * @details 创建 LVGL pointer 输入对象，并消费 touch_service 任务发布的
 *          坐标快照；本文件不直接访问 I2C 触摸设备。
 ******************************************************************************
 */

#include "lvgl_input_port.h"
#include "touch.h"
#include "touch_service.h"
#include "log_service.h"

#define GESTURE_SWIPE_THRESHOLD  40
#define GESTURE_PINCH_THRESHOLD  40
#define GESTURE_DRAG_START_THRESHOLD  8
#define GESTURE_PINCH_STEP_THRESHOLD  24

/*==============================================================================
 * 私有函数前置声明
 *============================================================================*/
static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data);
static void gesture_process(const touch_point_t *points, uint8_t count);
static int32_t gesture_distance(const touch_point_t *first,
                                const touch_point_t *second);

/*==============================================================================
 * 私有数据
 *============================================================================*/
static bool s_ready;                 /**< 触摸控制器和 LVGL 输入对象均初始化成功。 */
static lv_point_t s_last_point;      /**< 最近一次有效坐标，释放时继续报告该坐标。 */
static lv_indev_t *s_indev;          /**< 注册到 LVGL 的 pointer 输入设备对象。 */
static lv_port_gesture_cb_t s_gesture_cb;
static void *s_gesture_user_data;
static bool s_gesture_active;
static bool s_gesture_had_multi_touch;
static bool s_gesture_has_pinch;
static bool s_gesture_has_drag;
static bool s_gesture_has_pinch_move;
static uint32_t s_last_sample_sequence = UINT32_MAX;
static touch_point_t s_gesture_start;
static touch_point_t s_gesture_last;
static touch_point_t s_pinch_start_center;
static touch_point_t s_pinch_last_center;
static int32_t s_pinch_start_distance;
static int32_t s_pinch_last_distance;
static int32_t s_pinch_report_distance;

/*==============================================================================
 * 公共函数：供其他模块调用
 *============================================================================*/

/**
 * @brief  将已经初始化的触摸服务注册为 LVGL pointer 输入设备。
 * @note   由 GUI 任务在 lv_init() 和显示 Port 初始化后调用一次。
 */
void lv_port_indev_init(void)
{
    lv_display_t *display = lv_display_get_default();

    if(display == NULL) {
        s_ready = false;
        (void)log_printf("touch: default display not created\r\n");
        return;
    }

    s_ready = touch_is_ready() && touch_service_is_ready();
    s_last_point.x = 0;
    s_last_point.y = 0;
    s_indev = NULL;

    if(!s_ready) {
        (void)log_printf("touch: controller not detected\r\n");
        return;
    }

    s_indev = lv_indev_create();
    if(s_indev == NULL) {
        s_ready = false;
        (void)log_printf("touch: LVGL indev allocation failed\r\n");
        return;
    }

    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, indev_read_cb);
    (void)log_printf("touch: %s pointer input enabled\r\n",
                     touch_get_controller_id());
}

/**
 * @brief  查询触摸输入 Port 是否已经成功初始化。
 * @return true 表示触摸控制器可用且 LVGL 输入对象创建成功，否则返回 false。
 */
bool lv_port_indev_is_ready(void)
{
    return s_ready;
}

/**
 * @brief lv_port_indev_set_gesture_callback：LVGL 输入端口适配。
 */
void lv_port_indev_set_gesture_callback(lv_port_gesture_cb_t callback,
                                        void *user_data)
{
    s_gesture_cb = callback;
    s_gesture_user_data = user_data;
}

/*==============================================================================
 * 私有函数：仅供本文件内部调用
 *============================================================================*/

/**
 * @brief LVGL 输入读取回调，返回当前触摸坐标及按下/释放状态。
 * @note  由 LVGL 输入设备定时器调用，应用代码不应直接调用。
 */
static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    touch_service_sample_t sample = {0};

    (void)indev;
    data->continue_reading = false;
    data->point = s_last_point;

    if (!s_ready || !touch_service_get_latest(&sample)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (sample.sequence != s_last_sample_sequence) {
        gesture_process(sample.points, sample.count);
        s_last_sample_sequence = sample.sequence;
    }
    if(sample.count == 0U) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    s_last_point.x = (int32_t)sample.points[0].x;
    s_last_point.y = (int32_t)sample.points[0].y;
    data->point = s_last_point;
    data->state = LV_INDEV_STATE_PRESSED;
}

/** @brief Accumulate touch input and report horizontal drag/pinch increments while pressed. */
static void gesture_process(const touch_point_t *points, uint8_t count)
{
    lv_port_gesture_data_t data;
    int32_t dx;
    int32_t dy;

    if(count > 0U && !s_gesture_active)
    {
        s_gesture_active = true;
        s_gesture_had_multi_touch = false;
        s_gesture_has_pinch = false;
        s_gesture_has_drag = false;
        s_gesture_has_pinch_move = false;
        s_gesture_start = points[0];
        s_gesture_last = points[0];
    }

    if(count == 1U && s_gesture_active && !s_gesture_had_multi_touch)
    {
        const int32_t total_dx = (int32_t)points[0].x - (int32_t)s_gesture_start.x;
        const int32_t total_dy = (int32_t)points[0].y - (int32_t)s_gesture_start.y;
        const int32_t abs_dx = total_dx >= 0 ? total_dx : -total_dx;
        const int32_t abs_dy = total_dy >= 0 ? total_dy : -total_dy;

        if((s_gesture_has_drag || abs_dx >= GESTURE_DRAG_START_THRESHOLD) &&
           abs_dx > abs_dy) {
            data.start.x = (int32_t)(s_gesture_has_drag ? s_gesture_last.x :
                                     s_gesture_start.x);
            data.start.y = (int32_t)(s_gesture_has_drag ? s_gesture_last.y :
                                     s_gesture_start.y);
            data.end.x = (int32_t)points[0].x;
            data.end.y = (int32_t)points[0].y;
            data.distance_delta = 0;
            if(data.start.x != data.end.x && s_gesture_cb != NULL) {
                s_gesture_cb(LV_PORT_GESTURE_DRAG_MOVE, &data,
                             s_gesture_user_data);
                s_gesture_has_drag = true;
            }
        }
        s_gesture_last = points[0];
    }
    else if(count >= 2U && s_gesture_active)
    {
        const int32_t distance = gesture_distance(&points[0], &points[1]);
        touch_point_t center;

        center.x = (uint16_t)(((uint32_t)points[0].x + points[1].x) / 2U);
        center.y = (uint16_t)(((uint32_t)points[0].y + points[1].y) / 2U);
        s_gesture_had_multi_touch = true;
        if(!s_gesture_has_pinch)
        {
            s_gesture_has_pinch = true;
            s_pinch_start_center = center;
            s_pinch_last_center = center;
            s_pinch_start_distance = distance;
            s_pinch_last_distance = distance;
            s_pinch_report_distance = distance;
        }
        else {
            const int32_t distance_delta = distance - s_pinch_report_distance;
            const int32_t abs_delta = distance_delta >= 0 ? distance_delta : -distance_delta;

            if(abs_delta >= GESTURE_PINCH_STEP_THRESHOLD) {
                data.start.x = (int32_t)s_pinch_last_center.x;
                data.start.y = (int32_t)s_pinch_last_center.y;
                data.end.x = (int32_t)center.x;
                data.end.y = (int32_t)center.y;
                data.distance_delta = distance_delta;
                if(s_gesture_cb != NULL) {
                    s_gesture_cb(LV_PORT_GESTURE_PINCH_MOVE, &data,
                                 s_gesture_user_data);
                }
                s_gesture_has_pinch_move = true;
                s_pinch_report_distance = distance;
            }
        }
        s_pinch_last_center = center;
        s_pinch_last_distance = distance;
        s_gesture_last = points[0];
    }

    if(count != 0U || !s_gesture_active) return;

    data.start.x = (int32_t)s_gesture_start.x;
    data.start.y = (int32_t)s_gesture_start.y;
    data.end.x = (int32_t)s_gesture_last.x;
    data.end.y = (int32_t)s_gesture_last.y;
    data.distance_delta = 0;
    dx = data.end.x - data.start.x;
    dy = data.end.y - data.start.y;

    if(s_gesture_has_pinch && !s_gesture_has_pinch_move)
    {
        const int32_t distance_delta = s_pinch_last_distance - s_pinch_start_distance;

        if(distance_delta >= GESTURE_PINCH_THRESHOLD ||
           distance_delta <= -GESTURE_PINCH_THRESHOLD)
        {
            data.start.x = (int32_t)s_pinch_start_center.x;
            data.start.y = (int32_t)s_pinch_start_center.y;
            data.end.x = (int32_t)s_pinch_last_center.x;
            data.end.y = (int32_t)s_pinch_last_center.y;
            data.distance_delta = distance_delta;
            if(s_gesture_cb != NULL) {
                s_gesture_cb(distance_delta > 0 ? LV_PORT_GESTURE_PINCH_OUT :
                             LV_PORT_GESTURE_PINCH_IN, &data,
                             s_gesture_user_data);
            }
        }
    }
    else if(!s_gesture_had_multi_touch && !s_gesture_has_drag &&
            ((dx >= GESTURE_SWIPE_THRESHOLD || dx <= -GESTURE_SWIPE_THRESHOLD) &&
             (dx >= 0 ? dx : -dx) > (dy >= 0 ? dy : -dy)))
    {
        if(s_gesture_cb != NULL) {
            s_gesture_cb(dx > 0 ? LV_PORT_GESTURE_SWIPE_RIGHT :
                         LV_PORT_GESTURE_SWIPE_LEFT, &data,
                         s_gesture_user_data);
        }
    }

    s_gesture_active = false;
}

/**
 * @brief gesture_distance：LVGL 输入端口适配。
 */
static int32_t gesture_distance(const touch_point_t *first,
                                const touch_point_t *second)
{
    const int32_t dx = (int32_t)first->x - second->x;
    const int32_t dy = (int32_t)first->y - second->y;
    uint32_t value = (uint32_t)(dx * dx + dy * dy);
    uint32_t bit = 1UL << 30;
    uint32_t root = 0U;

    while(bit > value) bit >>= 2U;
    while(bit != 0U)
    {
        if(value >= root + bit)
        {
            value -= root + bit;
            root = (root >> 1U) + bit;
        }
        else root >>= 1U;
        bit >>= 2U;
    }
    return (int32_t)root;
}
