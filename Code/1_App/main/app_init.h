/**
 * @file app_init.h
 * @brief 应用 GUI 任务的创建入口。
 */
#ifndef APP_INIT_H
#define APP_INIT_H

#include <stdbool.h>

/**
 * @brief 初始化应用状态、创建唯一的 LVGL 任务并绑定触摸服务通知。
 * @return true 表示状态和任务均准备成功；false 表示状态或任务创建失败。
 * @note 在调度器启动前调用一次；显示、输入、波形控件在 GUI 任务开始后创建。
 */
bool App_Init(void);

#endif /* APP_INIT_H */
