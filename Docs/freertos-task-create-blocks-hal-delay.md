# FreeRTOS 任务创建后 `HAL_Delay()` 卡死问题记录

## 现象

在 STM32H743 工程中，执行以下初始化顺序时，程序会卡在 SDRAM 初始化阶段，屏幕也无法显示：

```c
uart_init();
log_task_creat();
sdram_init();
```

`log_task_creat()` 本身能够成功创建消息队列和日志任务，但随后 `sdram_init()` 中的 `HAL_Delay(1U)` 一直无法返回。调试时还会发现 TIM6 中断不再进入，导致现象很像 SDRAM、FMC 或 LTDC 出了问题。

把 SDRAM 初始化移到日志任务创建之前，程序又能正常运行：

```c
uart_init();
sdram_init();
log_task_creat();
vTaskStartScheduler();
```

## 相关代码

```c
bool log_task_creat(void)
{
    s_queue = xQueueCreate(LOG_QUENE_LEN, sizeof(log_msg_queue_t));
    if (s_queue == NULL)
    {
        return false;
    }

    if (xTaskCreate(log_task,
                    "log",
                    LOG_TASK_STACK,
                    NULL,
                    LOG_TASK_PRIO,
                    NULL) != pdPASS)
    {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return false;
    }

    return true;
}
```

问题并不是日志任务已经开始运行。此时调度器尚未启动，真正触发问题的是调度器启动前调用了 `xTaskCreate()`。

## 根因

该工程使用 FreeRTOS Cortex-M7 端口。`xTaskCreate()` 在操作内核任务链表时会进入和退出 FreeRTOS 临界区。

在调度器启动之前，FreeRTOS 端口的临界区嵌套变量仍处于特殊的初始状态。退出这次临界区时，端口不会像正常任务上下文那样把 Cortex-M 的 `BASEPRI` 恢复为 `0`。

因此，`xTaskCreate()` 返回后可能出现：

```text
BASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY
```

本工程配置的 FreeRTOS 系统调用中断优先级边界为 5。在 4 位 NVIC 优先级配置下，`BASEPRI` 通常会表现为 `0x50`。

`BASEPRI` 非零会屏蔽优先级数值大于或等于该边界的中断。如果作为 HAL 时基的 TIM6 中断优先级也处于被屏蔽范围，TIM6 中断就不能执行：

```text
TIM6 中断停止
    -> HAL_IncTick() 不再执行
    -> HAL_GetTick() 不再递增
    -> HAL_Delay() 的等待条件永远不成立
    -> 程序看起来卡死
```

所以 SDRAM 和 LTDC 只是后续最先暴露问题的模块，并不是根因。

## 为什么换一下初始化位置就正常了

当 SDRAM 初始化发生在第一次 `xTaskCreate()` 之前时，TIM6 仍可正常产生 HAL tick，`HAL_Delay()` 能够返回，因此 SDRAM 初始化成功。

调用 `vTaskStartScheduler()` 后，FreeRTOS 会通过启动第一个任务的上下文切换进入正常调度状态，临界区和 `BASEPRI` 的管理也恢复正常。因此问题主要出现在下面这个窗口：

```text
第一次调用 xTaskCreate()
          |
          |  不应再执行依赖中断或 HAL_Delay 的硬件初始化
          v
调用 vTaskStartScheduler()
```

## 推荐初始化顺序

原则是：先完成所有依赖中断和 `HAL_Delay()` 的硬件初始化，再创建 RTOS 对象和任务；创建完成后立即启动调度器。

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();

    /* 先完成板级硬件初始化。 */
    if (!uart_init())
    {
        Error_Handler();
    }

    if (!sdram_init() || !sdram_selftest())
    {
        Error_Handler();
    }

    if (ltdc_init() != LTDC_STATUS_OK)
    {
        Error_Handler();
    }

    /* 再创建 FreeRTOS 对象和任务。 */
    if (!log_task_creat())
    {
        Error_Handler();
    }

    if (xTaskCreate(draw_task,
                    "draw",
                    512U,
                    NULL,
                    2U,
                    NULL) != pdPASS)
    {
        Error_Handler();
    }

    /* 创建任务后立即启动调度器。 */
    vTaskStartScheduler();

    /* 正常情况下不会运行到这里。 */
    Error_Handler();
}
```

调度器启动后，普通任务中的延时优先使用：

```c
vTaskDelay(pdMS_TO_TICKS(delay_ms));
```

不要在任务中长期使用 `HAL_Delay()` 做周期调度，因为它是忙等待，不能替代 RTOS 的阻塞延时。

## Keil 调试验证方法

可以用下面的方法快速确认是否为同一个问题：

1. 在第一次 `xTaskCreate()` 调用前后查看 Cortex-M 寄存器 `BASEPRI`。
2. 正常情况下，调用前应为 `0`；调用后如果变为 `0x50` 或其他非零值，说明中断优先级屏蔽已经生效。
3. 在 `TIM6_DAC_IRQHandler()` 或 `HAL_TIM_PeriodElapsedCallback()` 设置断点。
4. 单步执行 `HAL_Delay()`，观察 `HAL_GetTick()` 是否持续递增。
5. 检查第一次 `xTaskCreate()` 与 `vTaskStartScheduler()` 之间，是否还存在依赖中断的初始化或延时。

还应同时检查 `PRIMASK`。但本问题通常是 `BASEPRI` 导致的，而不是全局中断开关 `PRIMASK`。

## 容易误用的临时处理

### `__enable_irq()` 无法解决

`__enable_irq()` 清除的是 `PRIMASK`，而本问题由 `BASEPRI` 屏蔽中断，因此调用它通常没有效果。

### 不要手动执行 `__set_BASEPRI(0)`

强行清零可能暂时让 TIM6 恢复，但会破坏 FreeRTOS 对临界区和中断屏蔽状态的管理，留下更难定位的并发问题。

### 不要先修改 SDRAM 时序或任务栈

增大任务栈、调整 SDRAM 时序、修改帧缓冲地址都不能解决 tick 中断被屏蔽的问题。应先确认 `BASEPRI` 和 TIM6 是否正常。

### 不建议靠提高 TIM6 中断优先级规避

理论上可把 TIM6 设置为比 `BASEPRI` 屏蔽边界更高的紧急优先级，但这会让 HAL tick 和 FreeRTOS 的中断优先级规则更复杂。若高优先级 ISR 调用了不允许的 RTOS API，还会产生新的错误。

优先采用正确的初始化顺序。

## 排查清单

- 检查队列创建和任务创建的返回值。
- 将 `xQueueCreate()` 与 `xTaskCreate()` 分开单步，确认真正触发点。
- 查看 `BASEPRI`、`PRIMASK` 和 TIM6 中断优先级。
- 确认 `HAL_GetTick()` 是否递增。
- 确认 TIM6 中断服务函数是否实际进入。
- 确认第一次创建任务后没有再调用依赖中断的硬件初始化。
- 创建完任务后尽快调用 `vTaskStartScheduler()`。
- 调度器启动后使用 `vTaskDelay()` 进行任务延时。

## 结论

本次“创建日志任务后 SDRAM 初始化失败”的本质是：

> 在 FreeRTOS 调度器启动前创建任务，使 `BASEPRI` 保持在中断屏蔽阈值；TIM6 的 HAL tick 中断因此无法运行，最终让 `HAL_Delay()` 永久等待。

以后应统一遵循“硬件初始化完成 -> 创建队列和任务 -> 立即启动调度器”的顺序，避免在任务已创建但调度器尚未启动的阶段执行依赖中断的代码。
