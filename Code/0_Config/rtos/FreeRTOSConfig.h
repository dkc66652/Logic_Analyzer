/** @file FreeRTOSConfig.h @brief FreeRTOSConfig。 */

/*
 * FreeRTOSConfig.h - FreeRTOS Kernel V11.3.1 Cortex-M7 工程配置
 * 用途：列出并分类 Cortex-M port 常用及通用的 FreeRTOS Kernel 配置项，
 * 说明每个配置项的用途、取值含义、单位和依赖关系。
 */

/*******************************************************************************
 * 适用芯片：STM32H743
 ******************************************************************************/

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include "platform_dwt.h"
#include "platform_config.h"

/*====================== 0. Cortex-M 内核异常路由（必须核对） ==================*/

/* 适用于使用 vPortSVCHandler/xPortPendSVHandler/xPortSysTickHandler 名称的经典 Cortex-M port。 */
#define vPortSVCHandler                       SVC_Handler /* FreeRTOS SVC 处理函数与启动文件异常入口之间的名称映射。 */
#define xPortPendSVHandler                     PendSV_Handler /* FreeRTOS PendSV 处理函数与启动文件异常入口之间的名称映射。 */
#define xPortSysTickHandler                    SysTick_Handler /* FreeRTOS Tick 处理函数与启动文件 SysTick 入口之间的名称映射。 */

/* 若 SysTick 还需执行芯片 HAL 时基处理，应注释 xPortSysTickHandler 映射，
 * 由项目自己的 SysTick_Handler() 调用 HAL 时基函数和 xPortSysTickHandler()。 */

/*========================= 1. 硬件与时基（必须核对） =========================*/

#define configCPU_CLOCK_HZ                      ( CPU_CYCLES_HZ ) /* 驱动内核 Tick 定时器的时钟频率，单位为 Hz。 */
/* DWT 的 CYCCNT 与 CPU 内核同频；CPU 测试的周期换算统一使用此配置。 */
#define configDWT_CPU_TEST_CLOCK_HZ             configCPU_CLOCK_HZ
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 ) /* 内核 Tick 频率，单位为 Hz。 */
/* #define configSYSTICK_CLOCK_HZ               ( configCPU_CLOCK_HZ ) */ /* Cortex-M SysTick 输入时钟频率，单位为 Hz；省略时等于 CPU 时钟。 */
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS /* TickType_t 位宽，可取 TICK_TYPE_WIDTH_16/32/64_BITS。 */
#define configINITIAL_TICK_COUNT                ( ( TickType_t ) 0 ) /* 调度器启动时的初始 Tick 值。 */

/* Cortex-M 中断优先级示例：数字越小，硬件优先级越高。以下假设实现了 4 个优先级位。 */
#ifndef __NVIC_PRIO_BITS
    #define __NVIC_PRIO_BITS                    4U /* NVIC 实现的中断优先级位数；通常由 CMSIS 芯片头文件定义。 */
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15 /* 可供库使用的最低硬件中断优先级。 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY     5 /* 可调用 FromISR API 的最高逻辑优先级；不得设为 0。 */
#define configKERNEL_INTERRUPT_PRIORITY          ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - __NVIC_PRIO_BITS ) ) /* PendSV/SysTick 使用的最低优先级。 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY     ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - __NVIC_PRIO_BITS ) ) /* FreeRTOS 临界区屏蔽边界。 */
#define configMAX_API_CALL_INTERRUPT_PRIORITY    configMAX_SYSCALL_INTERRUPT_PRIORITY /* 某些 port 使用的同义名称。 */
#define configCHECK_HANDLER_INSTALLATION         0 /* 异常入口检查开关：1 验证 SVC 和 PendSV 向量是否直接指向 port 处理函数。 */

/*============================== 2. 调度器 ==================================*/

#define configUSE_PREEMPTION                     1 /* 调度模式：1 为抢占式调度，0 为协作式调度。 */
#define configUSE_TIME_SLICING                   1 /* 同优先级时间片轮转开关：1 在 Tick 时轮转就绪任务。 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1 /* port 优化任务选择开关：1 使用硬件相关的最高优先级查找。 */
#define configMAX_PRIORITIES                     32 /* 可用任务优先级数量，有效范围为 0 到 configMAX_PRIORITIES - 1。 */
#define configMINIMAL_STACK_SIZE                 ( ( configSTACK_DEPTH_TYPE ) 128 ) /* Idle 任务栈深度，单位是 StackType_t，不一定是字节。 */
#define configMAX_TASK_NAME_LEN                  16 /* 任务名最大字符数，包含结尾 '\0'。 */
#define configIDLE_SHOULD_YIELD                  1 /* Idle 让位开关：1 主动让位给同为优先级 0 的应用任务。 */
#define configUSE_TASK_PREEMPTION_DISABLE        0 /* 逐任务抢占控制开关：1 提供任务级抢占禁用功能。 */
#define configNUMBER_OF_CORES                    1 /* FreeRTOS 调度器管理的 CPU 核心数量。 */
#define configRUN_MULTIPLE_PRIORITIES            0 /* SMP 多优先级并行开关：1 允许不同优先级任务在不同核心同时运行。 */
#define configUSE_CORE_AFFINITY                  0 /* SMP 核心亲和性开关：1 提供任务核心掩码 API。 */
#define configTASK_DEFAULT_CORE_AFFINITY         tskNO_AFFINITY /* 未显式指定时的默认核心亲和掩码。 */
#define configUSE_PASSIVE_IDLE_HOOK              0 /* SMP 被动 Idle Hook 开关：1 调用 configPASSIVE_IDLE_HOOK。 */

/*=========================== 3. 任务与内核对象 ==============================*/

#define configUSE_TASK_NOTIFICATIONS             1 /* 直接任务通知开关：1 编译任务通知 API。 */
#define configTASK_NOTIFICATION_ARRAY_ENTRIES    1 /* 每个任务的通知槽数量。 */
#define configUSE_MUTEXES                        1 /* 互斥锁开关：1 编译互斥锁和优先级继承功能。 */
#define configUSE_RECURSIVE_MUTEXES              1 /* LVGL FreeRTOS OS 适配使用递归互斥锁。 */
#define configUSE_COUNTING_SEMAPHORES            1 /* LVGL FreeRTOS 同步对象使用计数信号量。 */
#define configUSE_QUEUE_SETS                     0 /* 队列集合开关：1 编译队列集合 API。 */
#define configQUEUE_REGISTRY_SIZE                0 /* 调试器可登记的队列和信号量数量。 */
#define configUSE_EVENT_GROUPS                   1 /* App 状态管理使用事件组发布模式和功能变化。 */
#define configUSE_STREAM_BUFFERS                 1 /* 流及消息缓冲区开关：1 编译 stream_buffer.c 和相关 API。 */
#define configUSE_SB_COMPLETED_CALLBACK           0 /* Buffer 完成回调开关：1 为每个 Stream/Message Buffer 保存回调。 */
#define configUSE_APPLICATION_TASK_TAG           0 /* 应用任务标签开关：1 在 TCB 中保存标签并编译相关 API。 */
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS  0 /* 每个任务保留的线程局部存储指针数量。 */
#define configUSE_POSIX_ERRNO                    0 /* POSIX errno 开关：1 为每个任务保存 FreeRTOS_errno。 */
#define configUSE_NEWLIB_REENTRANT               0 /* newlib 重入支持开关：1 为每个任务分配 _reent 结构。 */
#define configUSE_C_RUNTIME_TLS_SUPPORT          0 /* C 运行库 TLS 开关：1 要求定义 TLS 类型及初始化、切换、释放宏。 */
/* #define configTLS_BLOCK_TYPE                  YourTlsBlockType */ /* C 运行库 TLS 块类型。 */
/* #define configINIT_TLS_BLOCK( xTLSBlock )     yourTlsInit( xTLSBlock ) */ /* 创建任务时初始化 TLS。 */
/* #define configSET_TLS_BLOCK( xTLSBlock )      yourTlsSet( xTLSBlock ) */ /* 任务切换时设置活动任务的 TLS 块。 */
/* #define configDEINIT_TLS_BLOCK( xTLSBlock )   yourTlsDeinit( xTLSBlock ) */ /* 删除任务时释放 TLS。 */
#define configSTACK_DEPTH_TYPE                   uint32_t /* 任务栈深度参数类型。 */
#define configMESSAGE_BUFFER_LENGTH_TYPE         size_t /* Message Buffer 内保存消息长度所用类型。 */
#define configUSE_MINI_LIST_ITEM                 1 /* MiniListItem 布局选择：1 使用较小结构，0 与 ListItem 使用相同结构。 */
#define configRECORD_STACK_HIGH_ADDRESS          0 /* 栈高地址记录开关：1 在 TCB 中保存任务栈高地址。 */
#define configENABLE_BACKWARD_COMPATIBILITY      0 /* 旧 API 名称兼容开关：1 提供历史名称映射。 */

/*============================== 4. 内存管理 =================================*/

#define configSUPPORT_STATIC_ALLOCATION          0 /* 静态分配 API 开关：1 编译 xTaskCreateStatic 等接口。 */
#define configSUPPORT_DYNAMIC_ALLOCATION         1 /* 动态分配 API 开关：1 编译 xTaskCreate 等接口。 */
#define configTOTAL_HEAP_SIZE                    ( 48U * 1024U ) /* 采集任务关闭时恢复GUI原有容量，供显示、触摸、日志、队列及LVGL同步对象使用。 */
#define configAPPLICATION_ALLOCATED_HEAP         1 /* 与采集工程一致：由平台层在 DTCM 定义唯一的 ucHeap。 */
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP 0 /* 栈分配器选择：1 使用 pvPortMallocStack/vPortFreeStack。 */
#define configHEAP_CLEAR_MEMORY_ON_FREE          0 /* 释放清零开关：1 在 vPortFree 时清零被释放内存。 */
#define configENABLE_HEAP_PROTECTOR              0 /* 堆保护开关：1 在 heap_4/heap_5 中启用边界保护和指针混淆。 */
#define configKERNEL_PROVIDED_STATIC_MEMORY      0 /* 静态系统任务内存来源：1 由内核提供，0 由应用回调提供。 */

/*============================== 5. 软件定时器 ===============================*/

#define configUSE_TIMERS                         1 /* 与采集工程一致：启用 Timer Service，维护平台 DWT 时间。 */
#define configTIMER_TASK_PRIORITY                ( configMAX_PRIORITIES - 1 ) /* Timer Service 任务优先级。 */
#define configTIMER_QUEUE_LENGTH                 5 /* Timer Service 命令队列可容纳的命令数量。 */
#define configTIMER_TASK_STACK_DEPTH             configMINIMAL_STACK_SIZE /* Timer Service 任务栈深度，单位为 StackType_t。 */
#define configTIMER_SERVICE_TASK_CORE_AFFINITY   tskNO_AFFINITY /* SMP 下 Timer Service 任务允许运行的核心。 */

/*=========================== 6. Hook 与错误检测 =============================*/

#define configUSE_IDLE_HOOK                      0 /* Idle Hook 开关：1 要求应用实现 vApplicationIdleHook。 */
#define configUSE_TICK_HOOK                      0 /* Tick Hook 开关：1 要求应用实现 vApplicationTickHook。 */
#define configUSE_MALLOC_FAILED_HOOK             1 /* 分配失败 Hook 开关：1 调用 vApplicationMallocFailedHook。 */
#define configUSE_DAEMON_TASK_STARTUP_HOOK       0 /* Daemon 启动 Hook 开关：1 调用 vApplicationDaemonTaskStartupHook。 */
#define configCHECK_FOR_STACK_OVERFLOW           2 /* 栈溢出检查级别：0 不检查，1 检查栈指针，2 额外检查填充值。 */
#define configASSERT( x ) do { if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ) {} } } while( 0 ) /* 内核断言动作；参数为假时执行该宏。 */

/*============================= 7. 低功耗 Tickless ============================*/

#define configUSE_TICKLESS_IDLE                  0 /* Tickless Idle 模式：0 关闭，1 使用 port 实现，2 使用应用实现。 */
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP    2 /* 进入 Tickless Idle 所需的最小预计空闲 Tick 数。 */
/* #define configPRE_SLEEP_PROCESSING( x )       do { ( void ) ( x ); } while( 0 ) */ /* 睡眠前回调，可修改预计睡眠 Tick。 */
/* #define configPOST_SLEEP_PROCESSING( x )      do { ( void ) ( x ); } while( 0 ) */ /* 唤醒后回调。 */
/* #define configPRE_SUPPRESS_TICKS_AND_SLEEP_PROCESSING( x ) */ /* port 抑制 Tick 前的可选回调。 */

/*============================= 8. 运行统计与跟踪 =============================*/

#define configGENERATE_RUN_TIME_STATS            1 /* 使用 DWT 64 位周期计数统计各任务 CPU 占用率。 */
#define configUSE_TRACE_FACILITY                 1 /* 跟踪设施开关：1 在内核对象中加入跟踪所需数据。 */
#define configUSE_STATS_FORMATTING_FUNCTIONS     0 /* 文本统计 API 开关：1 编译 vTaskList 等格式化函数。 */
#define configSTATS_BUFFER_MAX_LENGTH            4096 /* 旧版统计格式化 API 假定的输出缓冲区最大长度。 */
#define configRUN_TIME_COUNTER_TYPE              uint64_t /* 运行时间统计计数器类型。 */
/* DWT 在 main() 中、调度器启动前初始化；此处仅满足 FreeRTOS 配置接口。 */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() do { } while( 0 )
#define portGET_RUN_TIME_COUNTER_VALUE()         platform_dwt_cycles64()
#define configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES 0 /* 链表完整性字节开关：1 在链表结构中加入校验值。 */
#define configPRINTF( X )                        do { } while( 0 ) /* 内核诊断输出适配；X 是括号包裹的 printf 参数。 */

/*============================== 9. 协程（旧功能） ============================*/

#define configUSE_CO_ROUTINES                    0 /* 协程开关：1 编译 croutine.c 和协程 API。 */
#define configMAX_CO_ROUTINE_PRIORITIES          2 /* 协程优先级数量，供协程调度器使用。 */

/*============================== 10. MPU / 安全 ===============================*/

#define configINCLUDE_APPLICATION_DEFINED_PRIVILEGED_FUNCTIONS 0 /* 自定义特权函数开关：1 引入应用提供的 privileged_functions.h。 */
#define configTOTAL_MPU_REGIONS                  8 /* MPU 硬件区域数，常见为 8 或 16。 */
#define configTEX_S_C_B_FLASH                    0x07UL /* Cortex-M MPU 的 Flash TEX/S/C/B 属性。 */
#define configTEX_S_C_B_SRAM                     0x07UL /* Cortex-M MPU 的 SRAM TEX/S/C/B 属性。 */
#define configENFORCE_SYSTEM_CALLS_FROM_KERNEL_ONLY 1 /* MPU v2 权限提升策略：1 限制系统调用只能从内核代码发起。 */
#define configALLOW_UNPRIVILEGED_CRITICAL_SECTIONS 0 /* 非特权临界区开关：1 允许非特权任务屏蔽中断。 */
#define configUSE_MPU_WRAPPERS_V1                0 /* MPU wrapper 版本：0 使用 v2，1 使用 v1。 */
#define configPROTECTED_KERNEL_OBJECT_POOL_SIZE  32 /* MPU v2 可保护的内核对象总数。 */
#define configSYSTEM_CALL_STACK_SIZE             128 /* MPU v2 每个任务的系统调用栈深度，单位为字。 */
#define configENABLE_ACCESS_CONTROL_LIST         0 /* 内核对象 ACL 开关：1 启用 MPU wrapper v2 访问控制列表。 */

/* ARMv8-M 专用；普通 Cortex-M0/M3/M4/M7 port 不读取这些配置。 */
#define configENABLE_TRUSTZONE                   0 /* ARMv8-M TrustZone 调用支持开关：1 启用。 */
#define configRUN_FREERTOS_SECURE_ONLY           0 /* ARMv8-M 安全域模式：1 让 FreeRTOS 仅运行于安全侧。 */
#define configENABLE_MPU                         0 /* ARMv8-M MPU 上下文支持开关：1 启用。 */
#define configENABLE_FPU                         0 /* ARMv8-M FPU 上下文支持开关：1 启用。 */
#define configENABLE_MVE                         0 /* Cortex-M55/M85 MVE 上下文支持开关：1 启用。 */
#define secureconfigMAX_SECURE_CONTEXTS          5 /* 最多可进入安全侧的非安全任务数量。 */

/*============================== 11. API 包含选项 ==============================*/

#define INCLUDE_vTaskPrioritySet                 0 /* vTaskPrioritySet API 编译开关：1 包含该函数。 */
#define INCLUDE_uxTaskPriorityGet                0 /* uxTaskPriorityGet API 编译开关：1 包含该函数。 */
#define INCLUDE_vTaskDelete                      1 /* vTaskDelete API 编译开关：1 包含该函数。 */
#define INCLUDE_vTaskSuspend                     0 /* vTaskSuspend/vTaskResume API 编译开关：1 包含这些函数。 */
#define INCLUDE_xTaskResumeFromISR               0 /* xTaskResumeFromISR API 编译开关：1 包含该函数。 */
#define INCLUDE_xTaskDelayUntil                  1 /* xTaskDelayUntil API 编译开关：1 包含该函数。 */
#define INCLUDE_vTaskDelay                       1 /* vTaskDelay API 编译开关：1 包含该函数。 */
#define INCLUDE_xTaskGetSchedulerState           1 /* xTaskGetSchedulerState API 编译开关：1 包含该函数。 */
#define INCLUDE_xTaskGetCurrentTaskHandle        1 /* xTaskGetCurrentTaskHandle API 编译开关：1 包含该函数。 */
#define INCLUDE_uxTaskGetStackHighWaterMark      0 /* uxTaskGetStackHighWaterMark API 编译开关：1 包含该函数。 */
#define INCLUDE_uxTaskGetStackHighWaterMark2     0 /* uxTaskGetStackHighWaterMark2 API 编译开关：1 包含该函数。 */
#define INCLUDE_xTaskGetIdleTaskHandle           0 /* xTaskGetIdleTaskHandle API 编译开关：1 包含该函数。 */
#define INCLUDE_eTaskGetState                    0 /* eTaskGetState API 编译开关：1 包含该函数。 */
#define INCLUDE_xEventGroupSetBitFromISR         0 /* xEventGroupSetBitsFromISR API 编译开关：1 包含该函数。 */
#define INCLUDE_xTimerPendFunctionCall           0 /* xTimerPendFunctionCall API 编译开关：1 包含该函数。 */
#define INCLUDE_xTaskAbortDelay                  0 /* xTaskAbortDelay API 编译开关：1 包含该函数。 */
#define INCLUDE_xTaskGetHandle                   0 /* xTaskGetHandle API 编译开关：1 包含该函数。 */
#define INCLUDE_xQueueGetMutexHolder             0 /* xQueueGetMutexHolder API 编译开关：1 包含该函数。 */
#define INCLUDE_xSemaphoreGetMutexHolder         0 /* xSemaphoreGetMutexHolder API 编译开关：1 包含该函数。 */

/*=========================== 12. 高级/特殊集成开关 ===========================*/

#define configINCLUDE_FREERTOS_TASK_C_ADDITIONS_H 0 /* tasks.c 扩展头开关：1 包含 freertos_tasks_c_additions.h。 */
#define configUSE_ALTERNATIVE_API                0 /* 已弃用替代队列 API 的兼容开关；有效配置值为 0。 */
/* #define configCONTROL_INFINITE_LOOP() TestShouldContinue() */ /* 内核无限循环继续条件的应用覆盖接口。 */
#define configPRECONDITION( x )                  configASSERT( x ) /* 内核前置条件检查动作。 */
#define configUSE_PICOLIBC_TLS                   0 /* picolibc TLS 开关：1 为每个任务维护运行库 TLS。 */

#endif /* FREERTOS_CONFIG_H */
