# FreeRTOS 日志队列与 USART1 DMA 发送

本文记录本工程调试日志的当前实现、运行逻辑和后续维护要点。

## 目标

调试日志不能因为 `HAL_UART_Transmit()` 的逐字节轮询而长时间占用 CPU。日志是低优先级功能；GUI/LVGL 的帧率和响应优先。

当前方案将串口在线路上的发送工作交给 DMA：日志任务在 DMA 发送期间进入 FreeRTOS 的 Blocked 状态，CPU 可运行 LVGL 或其他就绪任务。

## 结构

```text
业务任务
  │ log_printf() / log_write()
  ▼
FreeRTOS Queue（8 条，每条最多 96 B）
  │ xQueueReceive()
  ▼
log_task（优先级 1，UART 的唯一发送者）
  │ HAL_UART_Transmit_DMA()
  ▼
DMA2 Stream 7 ───────────────► USART1 TX（PA9）
                                      │
                                      ▼
                              USART1 TC 硬件中断
                                      │
                                      ▼
                         HAL_UART_TxCpltCallback()
                                      │
                                      ▼
                   vTaskNotifyGiveFromISR(log_task)
```

LVGL 任务优先级为 2，高于日志任务。因此完成中断退出后，若 LVGL 仍处于就绪态，调度器会优先继续执行 LVGL。

## 关键文件

| 文件 | 职责 |
| --- | --- |
| `Code/2-Services/log.c` | 日志队列、日志任务、UART 完成/错误回调、任务通知 |
| `Code/2-Services/log.h` | 日志消息格式和公开日志 API |
| `Code/4-Bsp/uart.c` | USART1、DMA2 Stream 7 初始化、DMA 启动和等待 |
| `Code/4-Bsp/uart.h` | USART1/DMA 配置和 UART API |
| `Core/Src/stm32h7xx_it.c` | DMA2 Stream 7、USART1 的中断入口 |
| `Core/Inc/stm32h7xx_it.h` | 中断函数声明 |

## 正常发送时序

1. 业务任务调用 `log_printf()` 或 `log_write()`。
2. `log_write()` 将日志内容复制进 FreeRTOS Queue；`xQueueSend(..., 0)` 不等待。队列满时该条新日志返回失败，不会覆盖旧日志。
3. `log_task` 用 `xQueueReceive(..., portMAX_DELAY)` 等待队列。队列为空时，日志任务不占用 CPU。
4. 日志任务取到一条消息，调用 `uart_write(msg.data, msg.len)`。
5. `HAL_UART_Transmit_DMA()` 启动 DMA；此时只完成硬件配置，不代表串口已经发完。
6. `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` 让当前日志任务进入 Blocked 状态。此时 `msg` 仍保留在日志任务栈中，不能被下一条消息覆盖。
7. DMA 搬运完成后，HAL 打开 USART 的 TC 中断；最后一个停止位发送完成时进入 `USART1_IRQHandler()`。
8. HAL 调用 `HAL_UART_TxCpltCallback()`，回调通过 `vTaskNotifyGiveFromISR()` 唤醒日志任务。
9. `uart_write()` 返回，日志任务才会接收并发送下一条消息。

在 115200 baud、8N1 下，96 B 日志的线路发送时间约为 8.3 ms。DMA 不会提高这一物理发送速率，但会释放这段时间的 CPU。

## 为什么不会覆盖 DMA 正在读取的数据

DMA 的源地址是 `log_task()` 栈上的 `msg.data`。

```text
取出日志 A → 启动 A 的 DMA → log_task 阻塞
                                  │
                              A 的缓冲区保持有效
                                  │
串口完成 → 通知 log_task → 取出日志 B
```

日志任务在通知到达前不会返回循环顶部，因此不会再次调用 `xQueueReceive()` 覆盖 `msg`。这保证 DMA 源缓冲区有效。

`ulTaskNotifyTake()` 与 `vTaskNotifyGiveFromISR()` 的通知操作由 FreeRTOS 原子地处理：

- 若完成中断先到，通知计数被保存；随后 `ulTaskNotifyTake()` 立即返回。
- 若日志任务先进入阻塞，中断会将它从 Blocked 移到 Ready。

因此不存在“中断恰好发生，任务既没收到通知又继续发送下一条”的窗口。

## STM32H7：DMA 缓冲区必须固定到指定 SRAM

DMA 缓冲区不能依赖默认链接位置：工程代码或任务布局变化后，地址可能被分配到 DTCM（`0x20000000`）。DMA1/DMA2 无法访问 DTCM，会产生 DMA 传输错误。

DMA 缓冲区使用同事工程已定义的 `.bss.DMA_NOCACHE` 段，位于 DMA 可访问的 SRAM1 前 32 KiB，且 MPU 配置为不可缓存。链接后检查 `GUI.map`：

```text
s_uart_dma_tx_buffer  0x30000000
Execution Region RW_DMA_NOCACHE
```

## 中断与 FreeRTOS 的关系

DMA2 Stream 7 和 USART1 是 NVIC 硬件中断，不是 FreeRTOS 任务。

```text
DMA2_Stream7_IRQHandler() → HAL_DMA_IRQHandler()
USART1_IRQHandler()       → HAL_UART_IRQHandler()
```

`HAL_UART_TxCpltCallback()` 在 USART1 中断上下文中执行。它调用 `vTaskNotifyGiveFromISR()`，因此 DMA 和 USART1 的 NVIC 优先级设为 5。

FreeRTOS 配置为：

```c
configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5
```

在 Cortex-M 中数值越小，硬件优先级越高。优先级 5 到 15 的 ISR 可以调用 FreeRTOS 的 `FromISR` API；优先级 0 到 4 不可以。

任务优先级和 NVIC 中断优先级是两套不同体系，不能直接比较。例如 LVGL 的任务优先级 2 不代表它比 NVIC 优先级 5 低或高。

## 当前调试策略：无限等待并保留现场

`uart_write()` 当前使用：

```c
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
```

这是调试策略：如果 DMA/USART 的完成中断链路没有到达，只有 `log_task` 会保持 Blocked。GUI、其他任务和硬件中断仍可继续运行。

这样做的好处是 DMA/UART 寄存器现场不会被自动中止，便于在调试器中检查：

- `DMA2_Stream7->CR`、`DMA2_Stream7->NDTR`
- DMA 状态标志
- `USART1->ISR`、`USART1->CR1`、`USART1->CR3`
- `g_uart_handle.gState`、`g_uart_handle.ErrorCode`

此模式的代价是：完成通知永远不到时，日志任务不再消费队列；队列最终填满，后续日志会被丢弃。

稳定后的产品策略可以改成：有限等待超时 → 保存故障计数/寄存器快照 → `HAL_UART_DMAStop()` → 重新初始化或恢复发送。`HAL_UART_DMAStop()` 是异常恢复用的强制中止，不是正常发送完成所必需的操作。

## D-Cache 注意事项

当前工程没有启用 D-Cache。`uart_clean_tx_dcache()` 会先检查 D-Cache 是否已经开启；未开启时直接返回，几乎没有开销。

后续若启用 D-Cache，DMA TX 前必须保证 CPU 写入的日志已经写回实际 SRAM：

```text
CPU 写日志 → D-Cache 中是新内容，SRAM 可能仍是旧内容
DMA 直接读 SRAM → 可能发送旧数据
```

因此启动 DMA 前会对对应范围执行 `SCB_CleanDCache_by_Addr()`。

通用规则：

- DMA TX（DMA 读取内存）：启动前 Clean D-Cache。
- DMA RX（DMA 写入内存）：CPU 读取前 Invalidate D-Cache。
- 高频 DMA 缓冲区也可通过 MPU 放进 non-cacheable 区域；代价是 CPU 访问该区域会变慢。

## 使用限制

- `uart_write()` 仅允许由 `log_task` 调用，不能从 ISR 或其他任务直接调用。
- 不要让其他任务直接调用 `HAL_UART_Transmit_DMA()` 操作 USART1。
- ISR 中不要调用 `log_printf()`；它使用 `vsnprintf()` 和普通 `xQueueSend()`，不是 ISR API。
- 发送失败时，当前 `log_task` 未统计失败次数。后续可增加发送成功、队列满、UART 错误、DMA 超时等计数器，并在调试器 Watch 窗口查看。
