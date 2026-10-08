/**
 ******************************************************************************
 * @file    layer0_right_panel.h
 * @brief   右侧页面区、导航栏和采集控制模块的公共接口。
 ******************************************************************************
 */

#ifndef SIDE_PANEL_H
#define SIDE_PANEL_H

#include "lvgl.h"

/**
 * @brief  创建 x=740~799 的采集控制和导航栏，以及 x=600~739 页面区。
 * @param  parent 界面根对象，当前由 GUI 任务传入活动屏幕。
 * @note   由 App_GUI_Task() 在 wave_init() 成功后调用一次。
 * @usage  side_panel_create(lv_screen_active());
 */
void side_panel_create(lv_obj_t * parent);

/**
 * @brief 更新采集进度和按钮状态，并在到达采集时长时结束本轮采集。
 * @return true 表示本轮仍有界面状态被处理；波形绘制由独立任务完成。
 * @note   由 GUI 任务循环调用，本函数不写波形 SDRAM，也不强制 LVGL 刷新。
 */
bool side_panel_capture_process(void);

#endif /* SIDE_PANEL_H */
