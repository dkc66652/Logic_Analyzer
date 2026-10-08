/**
 ******************************************************************************
 * @file    touch.c
 * @brief   FT5206/FT5426 和 GT9xxx 触摸设备识别、读取及坐标统一。
 * @details 通过 BSP 层访问控制器寄存器，将不同控制器的原始坐标统一转换为
 *          左上角为原点的有效屏幕坐标，再提供给上层输入适配模块。
 ******************************************************************************
 */
#include "touch.h"
#include "bsp_i2c_touch.h"
#include <stddef.h>
#include <string.h>

#define GT9XXX_WRITE_ADDRESS        0x28U   /**< GT9xxx的8位I2C写地址。 */
#define GT9XXX_READ_ADDRESS         0x29U   /**< GT9xxx的8位I2C读地址。 */
#define GT9XXX_CONTROL_REGISTER     0x8040U /**< GT9xxx实时命令寄存器。 */
#define GT9XXX_PRODUCT_ID_REGISTER  0x8140U /**< GT9xxx产品ID首地址。 */
#define GT9XXX_STATUS_REGISTER      0x814EU /**< GT9xxx坐标状态和触点数寄存器。 */
#define GT9XXX_POINT1_REGISTER      0x8150U /**< GT9xxx第一个触点坐标首地址。 */
#define FT5206_WRITE_ADDRESS        0x70U   /**< FT5206的8位I2C写地址。 */
#define FT5206_READ_ADDRESS         0x71U   /**< FT5206的8位I2C读地址。 */
#define FT5206_TOUCH_COUNT_REGISTER 0x02U   /**< FT5206当前触点数量寄存器。 */
#define FT5206_POINT1_REGISTER      0x03U   /**< FT5206第一个触点数据首地址。 */
#define FT5206_LIBRARY_REGISTER     0xA1U   /**< FT5206固件库版本高字节寄存器。 */
#define FT5206_MODE_REGISTER        0xA4U   /**< FT5206中断上报模式寄存器。 */
#define FT5206_THRESHOLD_REGISTER   0x80U   /**< FT5206有效触摸检测阈值寄存器。 */
#define FT5206_PERIOD_REGISTER      0x88U   /**< FT5206工作态扫描周期寄存器。 */
#define FT5206_POINT_SIZE           6U      /**< FT5206单个触点数据长度。 */
#define GT9XXX_POINT_SIZE           8U      /**< GT9xxx单个触点数据长度。 */

static bool s_ready;                    /**< 触摸设备是否初始化成功。 */
static bool s_pressed;                  /**< 当前是否保持按下状态。 */
static uint8_t s_last_count;             /**< 最近一次有效触点数量。 */
static touch_controller_t s_controller; /**< 当前识别到的控制器类型。 */
static char s_controller_id[5];         /**< 控制器型号字符串。 */
static touch_point_t s_last_point;      /**< 最近一次转换后的有效屏幕坐标。 */
static touch_point_t s_last_points[TOUCH_MAX_POINTS]; /**< 最近一次全部有效触点。 */
static uint16_t s_display_width;        /**< 触摸坐标对应的显示区域宽度。 */
static uint16_t s_display_height;       /**< 触摸坐标对应的显示区域高度。 */

/** @brief 将控制器原始坐标转换并裁剪为有效屏幕坐标。 */
static void touch_map_to_display(const touch_point_t *raw, touch_point_t *point);

/** @brief 读取GT9xxx的16位地址寄存器。 */
static bool gt9xxx_read(uint16_t reg, uint8_t *data, uint8_t length)
{
    return bsp_i2c_touch_read16(GT9XXX_WRITE_ADDRESS, GT9XXX_READ_ADDRESS,
                                reg, data, length);
}

/** @brief 写入GT9xxx的16位地址寄存器。 */
static bool gt9xxx_write(uint16_t reg, const uint8_t *data, uint8_t length)
{
    return bsp_i2c_touch_write16(GT9XXX_WRITE_ADDRESS, reg, data, length);
}

/** @brief 读取FT5206的8位地址寄存器。 */
static bool ft5206_read(uint8_t reg, uint8_t *data, uint8_t length)
{
    return bsp_i2c_touch_read8(FT5206_WRITE_ADDRESS, FT5206_READ_ADDRESS,
                               reg, data, length);
}

/** @brief 向FT5206寄存器写入一个字节。 */
static bool ft5206_write(uint8_t reg, uint8_t value)
{
    return bsp_i2c_touch_write8(FT5206_WRITE_ADDRESS, reg, &value, 1U);
}

/** @brief 复位、配置并识别FT5206/FT5426控制器。 */
static bool ft5206_init(void)
{
    uint8_t version[2];
    bsp_i2c_touch_reset(20U, 50U);
    if (!ft5206_write(0x00U, 0x00U) ||
        !ft5206_write(FT5206_MODE_REGISTER, 0x00U) ||
        !ft5206_write(FT5206_THRESHOLD_REGISTER, 22U) ||
        !ft5206_write(FT5206_PERIOD_REGISTER, 12U) ||
        !ft5206_read(FT5206_LIBRARY_REGISTER, version, sizeof(version))) return false;

    if (!((version[0] == 0x30U && version[1] == 0x03U) ||
          version[1] == 0x01U || version[1] == 0x02U ||
          (version[0] == 0x00U && version[1] == 0x00U))) return false;
    (void)memcpy(s_controller_id, "FT52", 5U);
    return true;
}

/** @brief 复位、识别并启动GT9xxx控制器。 */
static touch_status_t gt9xxx_init(void)
{
    uint8_t control;
    bsp_i2c_touch_reset(10U, 100U);
    if (!gt9xxx_read(GT9XXX_PRODUCT_ID_REGISTER, (uint8_t *)s_controller_id, 4U))
        return TOUCH_STATUS_NOT_FOUND;
    if (strcmp(s_controller_id, "911") != 0 && strcmp(s_controller_id, "9147") != 0 &&
        strcmp(s_controller_id, "1158") != 0 && strcmp(s_controller_id, "9271") != 0)
        return TOUCH_STATUS_UNSUPPORTED_CONTROLLER;

    control = 0x02U;
    if (!gt9xxx_write(GT9XXX_CONTROL_REGISTER, &control, 1U)) return TOUCH_STATUS_NOT_FOUND;
    bsp_i2c_touch_delay_ms(10U);
    control = 0x00U;
    if (!gt9xxx_write(GT9XXX_CONTROL_REGISTER, &control, 1U)) return TOUCH_STATUS_NOT_FOUND;
    return TOUCH_STATUS_OK;
}

/** @brief 初始化触摸设备并自动探测控制器类型。 */
touch_status_t touch_init(const touch_config_t *config)
{
    touch_status_t status;

    s_ready = false;
    s_pressed = false;
    s_controller = TOUCH_CONTROLLER_NONE;
    if(config == NULL || config->width == 0U || config->height == 0U) {
        return TOUCH_STATUS_INVALID_ARGUMENT;
    }

    s_display_width = config->width;
    s_display_height = config->height;
    memset(s_controller_id, 0, sizeof(s_controller_id));
    memset(&s_last_point, 0, sizeof(s_last_point));
    memset(s_last_points, 0, sizeof(s_last_points));
    s_last_count = 0U;
    bsp_i2c_touch_init();

    if (ft5206_init())
    {
        s_controller = TOUCH_CONTROLLER_FT5206;
        s_ready = true;
        return TOUCH_STATUS_OK;
    }
    memset(s_controller_id, 0, sizeof(s_controller_id));
    status = gt9xxx_init();
    if (status == TOUCH_STATUS_OK)
    {
        s_controller = TOUCH_CONTROLLER_GT9XXX;
        s_ready = true;
    }
    return status;
}

/** @brief 查询触摸设备是否就绪。 */
bool touch_is_ready(void) { return s_ready; }
/** @brief 获取控制器型号字符串。 */
const char *touch_get_controller_id(void) { return s_controller_id; }
/** @brief 获取当前控制器类型。 */
touch_controller_t touch_get_controller(void) { return s_controller; }

/** @brief 读取所有有效触点，并输出转换后的屏幕坐标。 */
uint8_t touch_read_points(touch_point_t *points, uint8_t max_points)
{
    uint8_t status;
    uint8_t count;
    uint8_t index;
    uint8_t valid_count;
    uint8_t point_size;
    uint8_t data[TOUCH_MAX_POINTS * GT9XXX_POINT_SIZE];
    touch_point_t raw;

    if (!s_ready) return 0U;
    if (!bsp_i2c_touch_interrupt_active())
    {
        if (points != NULL && max_points > 0U && s_pressed)
        {
            const uint8_t copy_count = s_last_count < max_points ? s_last_count : max_points;
            (void)memcpy(points, s_last_points, (size_t)copy_count * sizeof(points[0]));
        }
        return s_pressed ? s_last_count : 0U;
    }

    if (s_controller == TOUCH_CONTROLLER_FT5206)
    {
        if (!ft5206_read(FT5206_TOUCH_COUNT_REGISTER, &status, 1U) ||
            (status & 0x0FU) > 5U)
        {
            if (points != NULL && max_points > 0U && s_pressed)
            {
                const uint8_t copy_count = s_last_count < max_points ? s_last_count : max_points;
                (void)memcpy(points, s_last_points, (size_t)copy_count * sizeof(points[0]));
            }
            return s_pressed ? s_last_count : 0U;
        }
        count = status & 0x0FU;
        point_size = FT5206_POINT_SIZE;
        if (count == 0U ||
            !ft5206_read(FT5206_POINT1_REGISTER, data, (uint8_t)(count * point_size)))
        {
            s_pressed = false;
            s_last_count = 0U;
            return 0U;
        }
    }
    else if (s_controller == TOUCH_CONTROLLER_GT9XXX)
    {
        if (!gt9xxx_read(GT9XXX_STATUS_REGISTER, &status, 1U) ||
            (status & 0x80U) == 0U)
        {
            if (points != NULL && max_points > 0U && s_pressed)
            {
                const uint8_t copy_count = s_last_count < max_points ? s_last_count : max_points;
                (void)memcpy(points, s_last_points, (size_t)copy_count * sizeof(points[0]));
            }
            return s_pressed ? s_last_count : 0U;
        }
        count = status & 0x0FU;
        if (count == 0U || count > 10U)
        {
            status = 0U;
            (void)gt9xxx_write(GT9XXX_STATUS_REGISTER, &status, 1U);
            s_pressed = false;
            s_last_count = 0U;
            return 0U;
        }
        if (count > TOUCH_MAX_POINTS) count = TOUCH_MAX_POINTS;
        point_size = GT9XXX_POINT_SIZE;
        if (!gt9xxx_read(GT9XXX_POINT1_REGISTER, data, (uint8_t)(count * point_size)))
        {
            if (points != NULL && max_points > 0U && s_pressed)
            {
                const uint8_t copy_count = s_last_count < max_points ? s_last_count : max_points;
                (void)memcpy(points, s_last_points, (size_t)copy_count * sizeof(points[0]));
            }
            return s_pressed ? s_last_count : 0U;
        }
        status = 0U;
        (void)gt9xxx_write(GT9XXX_STATUS_REGISTER, &status, 1U);
    }
    else return 0U;

    valid_count = 0U;
    for (index = 0U; index < count; index++)
    {
        const uint8_t *entry = &data[index * point_size];

        if (s_controller == TOUCH_CONTROLLER_FT5206)
        {
            if ((entry[0] & 0xC0U) == 0x40U) continue;
            raw.x = (uint16_t)(((uint16_t)(entry[0] & 0x0FU) << 8U) | entry[1]);
            raw.y = (uint16_t)(((uint16_t)(entry[2] & 0x0FU) << 8U) | entry[3]);
        }
        else
        {
            raw.x = (uint16_t)(((uint16_t)entry[1] << 8U) | entry[0]);
            raw.y = (uint16_t)(((uint16_t)entry[3] << 8U) | entry[2]);
        }

        touch_map_to_display(&raw, &s_last_points[valid_count]);
        if (points != NULL && valid_count < max_points) points[valid_count] = s_last_points[valid_count];
        valid_count++;
    }

    count = valid_count;
    if(count == 0U)
    {
        s_pressed = false;
        s_last_count = 0U;
        return 0U;
    }
    s_last_point = s_last_points[0];
    s_last_count = count;
    s_pressed = true;
    return count;
}

/** @brief 读取第一个有效触点，并输出转换后的屏幕坐标。 */
bool touch_read_point(touch_point_t *point)
{
    return point != NULL && touch_read_points(point, 1U) > 0U;
}

/**
 * @brief 统一不同控制器的坐标方向，并限制坐标不超过显示区域。
 * @note  GT9xxx 坐标方向与当前 LCD 一致；FT5206/FT5426 需要交换 X、Y。
 */
static void touch_map_to_display(const touch_point_t *raw, touch_point_t *point)
{
    uint16_t x;
    uint16_t y;

    if(s_controller == TOUCH_CONTROLLER_GT9XXX) {
        x = raw->x;
        y = raw->y;
    }
    else {
        x = raw->y;
        y = raw->x;
    }

    point->x = (x < s_display_width) ? x : (uint16_t)(s_display_width - 1U);
    point->y = (y < s_display_height) ? y : (uint16_t)(s_display_height - 1U);
}
