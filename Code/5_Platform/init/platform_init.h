/**
 * @file platform_init.h
 * @brief STM32H743 基础启动接口。
 */
#ifndef PLATFORM_INIT_H
#define PLATFORM_INIT_H

#include <stdbool.h>

/**
 * @brief 配置启动 MPU、HAL、系统时钟和 DWT 软件时间源。
 * @return true 表示系统主频符合 CPU_CYCLES_HZ 且 DWT 可用；false 表示初始化失败。
 * @note 由 Code/main.c 在任何板级外设初始化之前调用一次。
 */
bool Platform_Init(void);

#endif /* PLATFORM_INIT_H */
