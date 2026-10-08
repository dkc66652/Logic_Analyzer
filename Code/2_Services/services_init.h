/**
 * @file services_init.h
 * @brief 当前 GUI 服务的启动装配入口。
 */
#ifndef SERVICES_INIT_H
#define SERVICES_INIT_H

#include <stdbool.h>

/**
 * @brief 创建日志、触摸和波形服务任务，并按诊断开关创建帧统计任务。
 * @return true 表示所需任务均创建成功；false 表示任一创建步骤失败。
 * @note 调度器启动前调用；各任务的硬件前置条件由 Code/main.c 保证。
 */
bool Services_Init(void);

#endif /* SERVICES_INIT_H */
