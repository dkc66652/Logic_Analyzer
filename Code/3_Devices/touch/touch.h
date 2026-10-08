/**
 ******************************************************************************
 * @file    touch.h
 * @brief   FT5206/FT5426 和 GT9xxx 触摸设备的统一屏幕坐标接口。
 * @details 对外返回已经完成方向转换和边界裁剪的坐标，上层不需要区分控制器。
 ******************************************************************************
 */
#ifndef __TOUCH_DEVICE_H
#define __TOUCH_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    TOUCH_STATUS_OK = 0,
    TOUCH_STATUS_INVALID_ARGUMENT,
    TOUCH_STATUS_NOT_FOUND,
    TOUCH_STATUS_UNSUPPORTED_CONTROLLER
} touch_status_t;

typedef enum
{
    TOUCH_CONTROLLER_NONE = 0,
    TOUCH_CONTROLLER_FT5206,
    TOUCH_CONTROLLER_GT9XXX
} touch_controller_t;

typedef struct
{
    uint16_t x;
    uint16_t y;
} touch_point_t;

/** The FT52xx family supports up to five simultaneous contacts. */
#define TOUCH_MAX_POINTS 5U

/** 触摸坐标需要映射到的有效显示区域。 */
typedef struct
{
    uint16_t width;  /**< 显示区域宽度，输出 x 范围为 0 到 width-1。 */
    uint16_t height; /**< 显示区域高度，输出 y 范围为 0 到 height-1。 */
} touch_config_t;

/**
 * @brief  初始化并识别触摸控制器，同时保存显示区域尺寸。
 * @param  config 显示区域配置，初始化后由设备层用于坐标转换和裁剪。
 * @return 触摸设备初始化状态。
 * @note   由 lvgl_input_port.c 在默认显示器创建后调用一次。
 * @usage  status = touch_init(&config);
 */
touch_status_t touch_init(const touch_config_t *config);

/** @brief 查询触摸设备是否已经初始化成功。 */
bool touch_is_ready(void);

/** @brief 获取当前识别到的触摸控制器型号字符串。 */
const char *touch_get_controller_id(void);

/** @brief 获取当前识别到的触摸控制器类型。 */
touch_controller_t touch_get_controller(void);

/**
 * @brief  读取第一个触点的屏幕坐标和按下状态。
 * @param  point 输出已完成方向转换和边界裁剪的屏幕坐标。
 * @return true 表示当前存在有效触摸，false 表示当前处于释放状态。
 * @note   由 lvgl_input_port.c 的输入读取回调周期调用。
 */
bool touch_read_point(touch_point_t *point);

/**
 * @brief Read all currently reported contacts.
 * @param points Output array of mapped screen coordinates; can be NULL when only
 *               the number of contacts is needed.
 * @param max_points Number of elements available in @p points.
 * @return Number of valid contacts copied to @p points.
 * @note The count is limited to TOUCH_MAX_POINTS.  The first contact is kept
 *       compatible with touch_read_point().
 */
uint8_t touch_read_points(touch_point_t *points, uint8_t max_points);

#endif /* __TOUCH_DEVICE_H */
