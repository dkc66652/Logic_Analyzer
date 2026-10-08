/** @file layer0_wave_gesture.c @brief 波形区触摸交互实现。 */

#include "layer0_wave_gesture.h"

#include "layer1_wave.h"

/**
 * @brief touch_gesture_handle_wave：触摸手势到波形窗口的转换。
 */
void touch_gesture_handle_wave(lv_port_gesture_t gesture,
                               const lv_port_gesture_data_t *data,
                               void *user_data)
{
    const int32_t wave_right = WAVE_AREA_X + WAVE_AREA_WIDTH;

    (void)user_data;
    if ((data == NULL) ||
        (data->start.x < WAVE_AREA_X) || (data->start.x >= wave_right) ||
        (data->end.x < WAVE_AREA_X) || (data->end.x >= wave_right))
    {
        return;
    }

    if ((gesture == LV_PORT_GESTURE_SWIPE_LEFT) ||
        (gesture == LV_PORT_GESTURE_SWIPE_RIGHT) ||
        (gesture == LV_PORT_GESTURE_DRAG_MOVE))
    {
        wave_pan_view_pixels(data->end.x - data->start.x);
    }
    else if ((gesture == LV_PORT_GESTURE_PINCH_OUT) ||
             (gesture == LV_PORT_GESTURE_PINCH_IN) ||
             (gesture == LV_PORT_GESTURE_PINCH_MOVE))
    {
        const bool zoom_in = (gesture == LV_PORT_GESTURE_PINCH_OUT) ||
                             ((gesture == LV_PORT_GESTURE_PINCH_MOVE) &&
                              (data->distance_delta > 0));

        wave_zoom_view_at(data->end.x, zoom_in);
    }
}
