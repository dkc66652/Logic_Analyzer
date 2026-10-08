/**
 * @file la_transfer_internal.h
 * @brief 逻辑分析仪采集数据存储搬运适配私有接口
 * @note 仅由采集服务调用，隐藏CPU到SDRAM的同步复制细节
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
 * @param task_handle 保留的服务任务句柄参数；CPU复制不使用任务通知
 */
void LA_Transfer_Init(TaskHandle_t task_handle);

/**
 * @brief 复位搬运状态
 * @note 仅在采集尚未开始或已完全收尾、没有传输在途时调用
 */
void LA_Transfer_Reset(void);
/* CPU复制同步完成，Abort确认没有异步写入旧存储。 */
LA_Service_Result_t LA_Transfer_Abort(void);

/**
 * @brief 追加截止计数值之前的采集数据
 * @note 调用者须持有存储互斥锁，保持源缓冲有效直到返回。
 *       内部预留空间、同步CPU复制、校验后提交；复制期间采集ISR仍可标记
 *       半区污染，提交回调会拒绝被DMA复用过的源数据。
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
