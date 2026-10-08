/**
 * @file la_transfer_internal.h
 * @brief 逻辑分析仪采集数据存储搬运适配私有接口
 * @note 仅由采集服务调用，隐藏FMC、MDMA及传输回调细节
 */

#ifndef LA_TRANSFER_INTERNAL_H
#define LA_TRANSFER_INTERNAL_H

/* ================================================== */

#include "la/la_service.h"
#include "FreeRTOS.h"
#include "task.h"

/* ================================================== */

/* 校验源缓冲区并原子提交，调用时已持有storage_mutex；count允许为0。 */
typedef LA_Service_Result_t (*LA_Transfer_Commit_Callback_t)(
        uint8_t channel, uint16_t count, void *context);

/**
 * @brief 初始化存储搬运适配
 * @param task_handle 唯一搬运任务，须与Append调用任务一致且保持有效
 */
void LA_Transfer_Init(TaskHandle_t task_handle);

/**
 * @brief 复位搬运状态
 * @note 仅在采集尚未开始或已完全收尾、没有传输在途时调用
 */
void LA_Transfer_Reset(void);
/* Reset前确认没有MDMA继续写入旧存储；失败时不得清空或重用数据区。 */
LA_Service_Result_t LA_Transfer_Abort(void);

/**
 * @brief 追加截止计数值之前的采集数据
 * @note 调用者须持有存储互斥锁，保持源缓冲有效直到返回。
 *       内部预留空间、等待异步搬运完成后提交；等待使用当前任务通知，
 *       允许采集事件同时唤醒，累计等待不超过100ms，超时会终止MDMA。
 * @param channel 通道编号
 * @param data DMA时间戳源地址
 * @param count 时间戳数量
 * @param cutoff_tick 截止计数值，等于该值的记录不保存
 * @retval LA_SERVICE_OK-成功，其他值-存储或搬运失败
 */
LA_Service_Result_t LA_Transfer_Append(uint8_t channel,
                                      const uint32_t *data,
                                      uint16_t count,
                                      uint32_t cutoff_tick);

/**
 * @brief 搬运完成后由采集服务校验并提交DMA半区
 * @note commit必须在临界区中完成源有效性校验、存储提交和半区释放。
 *       即使没有截止值之前的数据，也调用commit释放已消费半区。
 */
LA_Service_Result_t LA_Transfer_Append_Checked(
        uint8_t channel,
        const uint32_t *data,
        uint16_t count,
        uint32_t cutoff_tick,
        LA_Transfer_Commit_Callback_t commit,
        void *context);

/* ================================================== */

#endif /* LA_TRANSFER_INTERNAL_H */
