/**
 * @file layer0_left_channels.h
 * @brief 左侧通道名控件的创建接口。
 * @details 本模块只创建和设置 x=0~99 的 LVGL 控件；波形数据与采集状态由
 *          layer1_wave 持有。调用方负责传入已经计算好的行位置和文本配置。
 */
#ifndef LAYER0_LEFT_CHANNELS_H
#define LAYER0_LEFT_CHANNELS_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    const char *pin_text;       /**< 彩色徽标中的物理通道名，如 D1。 */
    const char *name_text;      /**< 通道标题；创建期间 LVGL 会复制文本。 */
    const char *detail_text;    /**< 协议别名或其他说明。 */
    uint32_t color;             /**< 徽标颜色，0xRRGGBB。 */
    bool detail_visible;        /**< 是否显示说明文本。 */
    bool separator_visible;     /**< 是否显示标题下方的分隔线。 */
} layer0_left_channel_config_t;

typedef struct
{
    lv_obj_t *side_row;         /**< 一路左侧信息的父容器。 */
    lv_obj_t *badge;            /**< 彩色物理通道徽标。 */
    lv_obj_t *name_label;       /**< 通道名称。 */
    lv_obj_t *detail_label;     /**< 协议别名或说明。 */
    lv_obj_t *separator;        /**< 分隔线。 */
} layer0_left_channel_row_t;

/**
 * @brief 在左侧信息根容器中创建并设置一路通道控件。
 * @param parent 已创建的左侧根容器；本函数不持有其所有权。
 * @param top 行顶部，相对于 parent 的 Y 坐标。
 * @param height 行高，必须能容纳徽标及说明文字。
 * @param config 文本和可见性配置；调用期间有效即可，LVGL 会复制标签文字。
 * @param row 返回控件句柄，供调用方在清空父容器后复位；不得跨 LVGL 删除使用。
 * @return true 表示全部控件创建并设置成功；false 表示参数或对象创建失败。
 * @note 仅可在 GUI/LVGL 任务上下文调用；创建失败时调用方应清空父容器。
 */
bool layer0_left_channel_create(lv_obj_t *parent,
                                int16_t top,
                                int16_t height,
                                const layer0_left_channel_config_t *config,
                                layer0_left_channel_row_t *row);

#endif /* LAYER0_LEFT_CHANNELS_H */
