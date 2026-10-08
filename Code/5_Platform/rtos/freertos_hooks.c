/**
  ******************************************************************************
  * @file    freertos_hooks.c
  * @brief   FreeRTOS 内核要求的钩子函数实现（平台层，Code/5_Platform）
  *
  *  这些函数由内核在异常情况下调用，属于"内核 ↔ 平台"的适配代码，
  *  所以放在平台层而不是应用层。
  *
  *  与 FreeRTOSConfig.h 的对应关系：
  *    configCHECK_FOR_STACK_OVERFLOW = 2  ->  vApplicationStackOverflowHook
  *    configUSE_MALLOC_FAILED_HOOK   = 1  ->  vApplicationMallocFailedHook
  ******************************************************************************
  */

#include "FreeRTOS.h"
#include "task.h"

/**
  * @brief  任务栈溢出时进入这里
  * @param  xTask      溢出的任务句柄
  * @param  pcTaskName 溢出的任务名
  *
  * 调试方法：在本函数第一行打断点，看 pcTaskName 就知道是哪个任务的栈给小了，
  * 再用 uxTaskGetStackHighWaterMark() 确认需要加多少。
  */
void vApplicationStackOverflowHook( TaskHandle_t xTask, char * pcTaskName )
{
    ( void ) xTask;
    ( void ) pcTaskName;        /* 断点停在这里时，把 pcTaskName 加入 Watch 窗口 */

    taskDISABLE_INTERRUPTS();
    for( ;; )
    {
        /* 停在此处，禁止继续运行以免破坏更多数据 */
    }
}

/**
  * @brief  堆内存分配失败时进入这里
  *
  * 常见原因：configTOTAL_HEAP_SIZE 给小了，或者有任务只申请不释放（内存泄漏）。
  * 排查手段：降低任务创建频率，观察 xPortGetFreeHeapSize() 是否持续下降。
  */
void vApplicationMallocFailedHook( void )
{
    taskDISABLE_INTERRUPTS();
    for( ;; )
    {
    }
}
