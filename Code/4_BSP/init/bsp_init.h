/** @file bsp_init.h @brief BSP 层统一初始化入口。 */
#ifndef BSP_INIT_H
#define BSP_INIT_H

#include <stdbool.h>

/** @brief 初始化 GPIO、TIM7、DMA2D 和调试串口。
 *  @return true 表示共同板级资源准备成功，否则返回 false。
 *  @note 调度器启动前调用一次。 */
bool BSP_Init(void);

/** @brief 启动 TIM7 的 1 ms 中断，作为 LVGL 的 tick 来源。
 *  @return true 表示 HAL 已成功启动计数器中断，否则返回 false。
 *  @note 必须在 BSP_Init() 后调用；由 GUI 任务在 lv_init() 后调用。 */
bool BSP_Lvgl_Tick_Start(void);

#endif /* BSP_INIT_H */
