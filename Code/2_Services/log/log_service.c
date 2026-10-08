/**
 * @file log_service.c
 * @brief 通过 FreeRTOS 队列把日志交给 USART1 DMA 发送任务。
 * @note 本服务持有消息队列和 UART HAL 完成/错误回调。
 */

#include "log_service.h"
#include <uart.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include <queue.h>
#include <task.h>

static QueueHandle_t s_queue;
static void log_task(void* param);

static TaskHandle_t s_log_task_handle = NULL;





/** @brief 创建日志消息队列和串口发送任务。
 *  @return true 表示两者均创建成功；false 表示资源不足。
 *  @note 在调度器启动前调用一次。 */
bool log_task_creat(void)
{
	s_queue = xQueueCreate(LOG_QUENE_LEN,sizeof(log_msg_queue_t));
	if(s_queue == NULL)
	{
		return false;
	}
	if(xTaskCreate(log_task,"log",LOG_TASK_STACK,NULL,LOG_TASK_PRIO,&s_log_task_handle) != pdPASS)
	{
		vQueueDelete(s_queue);
		s_queue = NULL;
		return false;
	}
	
	return true;
}


/** @brief 把原始日志字节复制到异步队列。
 *  @param data 待发送字节缓冲区；返回前已复制。
 *  @param len 字节数，范围为 1 到 LOG_MSG_MAX。
 *  @return true 表示已入队；false 表示参数无效、未初始化或队列已满。
 *  @note 零等待入队；成功不代表 UART 已完成发送。 */
bool log_write(const void* data,uint16_t len)
{
	log_msg_queue_t msg;
	if(s_queue == NULL || data == NULL || len == 0U || len > LOG_MSG_MAX)
	{
		return false;
	}
	msg.len = len;
	memcpy(msg.data,data,len);
	return xQueueSend(s_queue,&msg,0) == pdTRUE;
}


/** @brief 格式化并尝试把日志加入发送队列。
 *  @param fmt printf 风格格式串，后续实参须与其匹配。
 *  @return true 表示已入队；false 表示无输出或队列不可用。
 *  @note 单条消息最多保留 LOG_MSG_MAX-1 字节，超出部分截断。 */
bool log_printf(const char *fmt, ...)
{
    char tmp[LOG_MSG_MAX];
    va_list ap;
    int     n;

    va_start(ap, fmt);                        
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap); 
    va_end(ap);                               

    if (n <= 0)
    {
        return false;
    }
    if (n >= (int)sizeof(tmp))                
    {
        n = (int)sizeof(tmp) - 1;
    }

    return log_write(tmp, (uint16_t)n);
}


/** @brief 阻塞读取日志队列，并通过板级 UART 接口发送。
 *  @param param FreeRTOS 任务参数，当前未使用。 */
static void log_task(void* param)
{
	log_msg_queue_t msg;
	(void)param;
	
	for(;;)
	{
		if(xQueueReceive(s_queue,&msg,portMAX_DELAY) == pdTRUE)
		{
			uart_write(msg.data,msg.len);
		}
	}
}

/** @brief 在 UART 完成或错误中断中通知日志任务。
 *  @note 仅可从 ISR 调用，使用 FreeRTOS FromISR API。 */
static void log_uart_notify_from_isr(void)
{
    BaseType_t higher_task_woken = pdFALSE;

    if (s_log_task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(s_log_task_handle, &higher_task_woken);
        portYIELD_FROM_ISR(higher_task_woken);
    }
}

/** @brief 处理本工程 USART1 的 HAL 发送完成回调。
 *  @param huart HAL 提供的 UART 句柄，其他 UART 不通知日志任务。 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &g_uart_handle)
    {
        log_uart_notify_from_isr();
    }
}

/** @brief 处理本工程 USART1 的 HAL 错误回调。
 *  @param huart HAL 提供的 UART 句柄，其他 UART 不通知日志任务。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &g_uart_handle)
    {
        log_uart_notify_from_isr();
    }
}