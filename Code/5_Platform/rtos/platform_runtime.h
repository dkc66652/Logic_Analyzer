/**
 * @file platform_runtime.h
 * @brief FreeRTOS 平台周期维护接口。
 */

#ifndef PLATFORM_RUNTIME_H
#define PLATFORM_RUNTIME_H

#include <stdbool.h>

/**
 * @brief 创建并启动每秒维护 DWT 64 位累计值的软件定时器。
 * @return true 表示定时器启动命令已进入 Timer Service 队列；false 表示创建或入队失败。
 * @note 在平台时钟及 DWT 初始化后、调度器启动前调用；重复调用不会创建第二个定时器。
 *       回调只维护时间计数，不调用阻塞接口。
 */
bool Platform_Runtime_Init(void);

#endif /* PLATFORM_RUNTIME_H */
