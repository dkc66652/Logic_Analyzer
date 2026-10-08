# 触控手势

触摸 Port 现在识别以下手势，并且仅在整次触摸完全抬起时回调一次：

| 手势   | 事件值                           | 触发条件                        |
| ---- | ----------------------------- | --------------------------- |
| 左滑   | `LV_PORT_GESTURE_SWIPE_LEFT`  | 单指横向位移不小于 40 px，且横向位移大于纵向位移 |
| 右滑   | `LV_PORT_GESTURE_SWIPE_RIGHT` | 单指横向位移不小于 40 px，且横向位移大于纵向位移 |
| 双指缩小 | `LV_PORT_GESTURE_PINCH_IN`    | 两指间距减少不小于 40 px             |
| 双指放大 | `LV_PORT_GESTURE_PINCH_OUT`   | 两指间距增加不小于 40 px             |

在应用初始化阶段（`lv_port_indev_init()` 之后）注册回调：

```c
#include "lv_port_indev.h"

static void app_gesture_cb(lv_port_gesture_t gesture,
                           const lv_port_gesture_data_t *data,
                           void *user_data)
{
    (void)user_data;

    switch(gesture)
    {
    case LV_PORT_GESTURE_SWIPE_LEFT:
        /* 切换到下一页 */
        break;
    case LV_PORT_GESTURE_SWIPE_RIGHT:
        /* 切换到上一页 */
        break;
    case LV_PORT_GESTURE_PINCH_IN:
        /* 缩小波形或图片；data->distance_delta 为负 */
        break;
    case LV_PORT_GESTURE_PINCH_OUT:
        /* 放大波形或图片；data->distance_delta 为正 */
        break;
    }
}

/* 在 lv_port_indev_init() 之后调用。 */
lv_port_indev_set_gesture_callback(app_gesture_cb, NULL);
```

`data->start`、`data->end` 是单指起止坐标；双指手势时它们分别是两指初始和最终中心点，`distance_delta` 是两指距离的变化量。触控底层同时保留 `touch_read_point()`，现有 LVGL 单指点击不需要修改。
