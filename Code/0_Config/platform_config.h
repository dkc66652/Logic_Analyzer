/**
 * @file platform_config.h
 * @brief 与采集工程共享的 CPU 时钟契约。
 * @note 本值必须与 Core/Src/main.c 的 PLL1P/SYSCLK 实际结果一致；
 *       LTDC、I2C 等外设时钟仍按 GUI.ioc 单独配置。
 */
#ifndef PLATFORM_CONFIG_H
#define PLATFORM_CONFIG_H

#define CPU_CYCLES_HZ 480000000UL

/*
 * 采集移植隔离开关：0时完全不初始化DMA1、MDMA、TIM1/TIM2/TIM5，
 * 不创建LA Service任务；GUI波形显示继续使用CNT Demo数据源。
 */
#define PLATFORM_LA_CAPTURE_ENABLE 1U
#endif /* PLATFORM_CONFIG_H */
