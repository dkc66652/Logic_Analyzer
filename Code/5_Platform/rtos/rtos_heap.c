/**
 * @file rtos_heap.c
 * @brief 在 DTCM 中定义 FreeRTOS heap_4 使用的唯一堆数组。
 */

#include "FreeRTOS.h"
#include "memory_placement.h"
#include <stdint.h>

/* 由 .sct 的 RW_DTCM 放置；堆容量取 GUI 当前 configTOTAL_HEAP_SIZE。 */
PLATFORM_DTCM_ZI
PLATFORM_CUSTOM_ALIGNMENT_ATTRIBUTE(portBYTE_ALIGNMENT)
uint8_t ucHeap[configTOTAL_HEAP_SIZE];
