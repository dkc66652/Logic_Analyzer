# GUI 分层与采集工程合并差异

> 2026-10-08，对照 `D:/bootloader+ota/2_Logic_Analyzer_Tim_Ch GPT-6.1 Sol Ultra` 和当前 GUI Keil 工程。本文区分已经移植的 LA 采集链路与仍待统一的平台功能。

## 当前代码归属

| 位置 | 当前职责 | 主要入口或接口 |
| --- | --- | --- |
| `Code/main.c` | 唯一 `main()`，按前置条件装配各层 | `main` |
| `Code/1_App/demo` | 自定义 CNT 转单通道列状态的接口示例 | `App_Wave_Cnt_Demo_Query_Channel` |
| `Code/2_Services/layer0` | 左侧通道、右侧选项和手势 | `layer0_*`、`side_panel_*` |
| `Code/2_Services/layer1` | LA 显示适配、动态波形、帧与 DMA 调度 | `wave_*`、`wave_capture_data_*` |
| `Code/2_Services/la` | 六路采集任务、查询、SDRAM 存储、MDMA 传输 | `LA_Service_*` |
| `Code/4_BSP/la` | TIM2/TIM5 六路输入捕获与 DMA1 | `BSP_LA_IC_*` |
| `Code/4_BSP/fmc` | LA 存储层使用的 SDRAM/MDMA 接口 | `BSP_FMC_SDRAM_*` |
| `Code/3_Devices` | GUI SDRAM 芯片、LCD 和触摸器件 | `Devices_Init` |
| `Code/5_Platform` | HAL 启动、MPU、DWT 和 RTOS hooks | `Platform_*` |

`layer1_waveform_legacy.c` 与 `layer1_waveform_demo.c` 是历史实现，Keil 工程不编译。
`app_wave_cnt_demo.c` 只演示同事如何提供 CNT、初始电平和 500 字节结果数组；真实
显示路径已经由 `layer1_capture_data.c` 直接调用 LA Service。

## 本轮已经统一

- 活动 scatter 文件为 `Code/0_Config/STM32H743II.sct`。SDRAM 分成 UI 8 MiB、协议 2 MiB、LA 22 MiB；LA 的静态数组位于 `.bss.LA_DATA`。
- GUI 的显示帧缓冲是 `.bss.UI_DATA` 数组，UART DMA 缓冲位于 `DMA_NOCACHE`，不会再用未声明裸地址冒充数组。
- FreeRTOS 内核 tick 均为 1 kHz SysTick，HAL tick 为 TIM6；Timer Service 已启用，TIM7 只给 LVGL 提供 1 ms tick。
- 已合并 TIM1、TIM2、TIM5 和 GUI 原 TIM7 的源代码，加入 DMA1 Stream0~5、MDMA、TIM2 中断。输入引脚沿用同事工程：PA5、PB10、PB11、PA0、PA2、PA3。
- 已移植 LA Service、Query、Storage、Transfer、BSP 输入捕获和 SDRAM/MDMA 兼容层。SDRAM 芯片只由 GUI Devices 层初始化一次，LA 层随后安装 MDMA 回调。
- 硬件固定采集六路。界面开关不改变捕获通道，只控制 Layer 1 是否查询和显示对应物理通道。
- 波形任务按开启通道顺序复用一个 500 字节数组，每路只生成一块 512×26 横向条带；最后一个开启通道 DMA 完成后才提交 LTDC 换帧。

## 仍需统一或实机确认

| 项目 | 当前状态 | 后续处理 |
| --- | --- | --- |
| CubeMX `.ioc` | `GUI.ioc` 仍只有原 GUI 的基础配置，采集生成源码已手工合并 | CubeMX 再生成前合并 TIM1/TIM2/TIM5、DMA1、MDMA、PA0/PA2/PA3/PA5/PB10/PB11 和 IRQ 优先级 |
| 主时钟 | 两边 CPU 480 MHz、AHB 240 MHz；GUI PLL1Q 与同事值不同 | 保留当前已验证的 CPU/APB 定时器时钟，最终按外设时钟需求统一 `.ioc` 与生成源码 |
| MPU/Cache | GUI 由 `platform_memory.c` 管理 SDRAM 和 DMA non-cache 区 | 不直接再编译同事 `sys_config.c`；实机确认 MDMA、DMA1、DMA2D 与 LTDC 的一致性和总线竞争 |
| FreeRTOS 版本 | GUI V11.3.1，同事 V11.1.0 | 当前保留 GUI 内核，实测 LA 任务、波形任务和 Timer Service 的堆栈余量 |
| 日志 | GUI USART1 DMA2 Stream7；同事 Trace 轮询 USART1 | 保留唯一初始化和输出仲裁者，后续若移植 Trace 必须接入现有日志层 |
| Fault/RTOS hooks | GUI 已有异常入口与 hooks；同事另有持久化异常模块 | 选择唯一入口，再决定是否移植 SRAM4 异常记录，避免重复符号 |
| FatFs/SDMMC/压缩/协议 | 尚未移植 | 与 SDMMC1 引脚、FatFs、缓存策略和内存段一起单独移植 |
| 目标板验证 | 当前只完成编译和链接布局验证 | 测试六路边沿、停止截止 CNT、运行中查询、DMA 中断优先级、动态帧率及 FMC 带宽 |

## 动态显示调用关系

```text
开始按钮
  -> wave_start_capture()
  -> wave_capture_data_begin()
  -> LA_Service_Start(1 MHz, duration_us)
  -> LA内部任务处理TIM/DMA/MDMA并写LA_DATA

LTDC帧通知
  -> 波形准备任务锁存now_cnt
  -> 仅遍历界面开启通道
  -> wave_capture_data_request_channel()
  -> Query View批量读取该通道边沿
  -> 500字节列状态
  -> 512×26 RGB565横向条带
  -> DMA任务按通道顺序发送
  -> 最后一路完成后present
```

## 验证记录

Keil Arm Compiler 6.24 已对 `MDK-ARM/GUI.uvprojx` 完整构建，日志为
`MDK-ARM/la_migration_build.log`，结果 0 错误、0 警告。`GUI.map` 显示：

- `s_task_columns`：500 字节；
- `s_wave_dma_buffers`：53,248 字节，即两块 `512 × 26 × 2`；
- `la_storage_data`：`0xC0A00000`，22 MiB，位于 `RW_SDRAM_LA (UNINIT)`。

Keil 构建不会验证目标板引脚电气状态、输入边沿、DMA 实时性或显示总线带宽。
