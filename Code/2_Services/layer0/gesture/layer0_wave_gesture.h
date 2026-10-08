/** @file layer0_wave_gesture.h @brief 波形区触摸手势到显示操作的转换。 */
#ifndef TOUCH_GESTURE_H
#define TOUCH_GESTURE_H

#include "lvgl_input_port.h"

/** @brief 处理波形区的左右拖动和双指缩放手势。 */
void touch_gesture_handle_wave(lv_port_gesture_t gesture,
                               const lv_port_gesture_data_t *data,
                               void *user_data);

#endif /* TOUCH_GESTURE_H */
