/**
 * @file log_service.h
 * @brief GUI 异步日志服务的公共接口。
 * @details 调用方提交消息，日志服务复制后由独立任务发送。
 */
#ifndef LOG_SERVICE_H
#define LOG_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

#define LOG_MSG_MAX 96U       /* 单条日志最大字节数。 */
#define LOG_QUENE_LEN 16U     /* 队列槽数；保留原宏名以兼容调用方。 */
#define LOG_TASK_STACK 512U   /* 任务栈深度，单位为 FreeRTOS stack words。 */
#define LOG_TASK_PRIO 4U      /* 日志任务优先级。 */

typedef struct {
    uint16_t len;              /**< 实际字节数。 */
    uint8_t data[LOG_MSG_MAX]; /**< 入队时复制的字节。 */
} log_msg_queue_t;

/** @brief 创建日志队列和发送任务。
 *  @return true 表示成功；false 表示资源不足。 */
bool log_task_creat(void);

/** @brief 零等待提交原始字节。
 *  @param data 原始缓冲区，函数内复制。
 *  @param len 字节数，范围为 1..LOG_MSG_MAX。
 *  @return true 表示已入队，false 表示未入队。 */
bool log_write(const void* data,uint16_t len);

/** @brief 格式化并零等待提交一条日志。
 *  @param fmt printf 风格格式串，后续实参须与格式匹配。
 *  @return true 表示已入队；false 表示无输出或队列不可用。
 *  @note 最多保留 LOG_MSG_MAX-1 字节，超长会截断。 */
bool log_printf(const char* fmt,...);

#endif /* LOG_SERVICE_H */
