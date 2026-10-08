/**
 * @file la_ic.c
 * @brief 逻辑分析仪采集驱动实现
 * @note 基于TIM1同步触发TIM2/TIM5，使用六路输入捕获DMA记录边沿时间戳
 */

/* ================================================== */

#include "la/la_ic.h"
#include "la/la_ic_internal.h"
#include "memory/memory_placement.h"
#include "stm32h7xx_hal.h"
#include "dma.h"
#include "tim.h"

/* ========================= 配置 ========================= */

#define BSP_LA_IC_START_RETRY_COUNT      3U  /* 启动窗口边沿碰撞最大重试次数 */
#define BSP_LA_IC_DMA_HALF_COUNT         (BSP_LA_IC_DMA_BUFFER_COUNT / 2U) /* DMA半缓冲区记录数 */
#define BSP_LA_IC_DMA_STOP_TIMEOUT_MS    10U /* 停稳等待上限，任务上下文TIM6时基 */

#define BSP_LA_IC_CAPTURE_CCER_MASK      (TIM_CCER_CC1E | TIM_CCER_CC3E | TIM_CCER_CC4E)
#define BSP_LA_IC_CAPTURE_DMA_MASK       (TIM_DIER_CC1DE | TIM_DIER_CC3DE | TIM_DIER_CC4DE)
#define BSP_LA_IC_CAPTURE_FLAG_MASK      (TIM_SR_CC1IF | TIM_SR_CC3IF | TIM_SR_CC4IF | \
                                          TIM_SR_CC1OF | TIM_SR_CC3OF | TIM_SR_CC4OF)
#define BSP_LA_IC_DMA_ERROR_FLAG_MASK    (DMA_FLAG_TEIF0_4 | DMA_FLAG_DMEIF0_4 | DMA_FLAG_FEIF0_4)

#if ((BSP_LA_IC_DMA_BUFFER_COUNT % 2U) != 0U)
    #error "BSP_LA_IC_DMA_BUFFER_COUNT must be even"
#endif

/* ================================================== */

typedef enum
{
    BSP_LA_IC_STATE_RESET = 0,
    BSP_LA_IC_STATE_IDLE,
    BSP_LA_IC_STATE_STARTING,
    BSP_LA_IC_STATE_RUNNING,
    BSP_LA_IC_STATE_STOPPED,
    BSP_LA_IC_STATE_FINALIZED
} BSP_LA_IC_State_t;

typedef struct
{
    uint32_t gpioa_idr; /* GPIOA输入数据寄存器快照 */
    uint32_t gpiob_idr; /* GPIOB输入数据寄存器快照 */
} BSP_LA_IC_GPIO_Snapshot_t;

typedef struct
{
    BSP_LA_IC_Channel_t channel; /* 输入捕获通道 */
    DMA_HandleTypeDef *dma;      /* 通道对应的DMA句柄 */
    volatile uint32_t *ccr;      /* 通道对应的捕获寄存器 */
    uint32_t *buffer;            /* 通道对应的DMA循环缓冲区 */
} BSP_LA_IC_Channel_Config_t;

/* ================================================== */

extern DMA_HandleTypeDef hdma_tim2_ch1;
extern DMA_HandleTypeDef hdma_tim2_ch3;
extern DMA_HandleTypeDef hdma_tim2_ch4;
extern DMA_HandleTypeDef hdma_tim5_ch1;
extern DMA_HandleTypeDef hdma_tim5_ch3;
extern DMA_HandleTypeDef hdma_tim5_ch4;

PLATFORM_DEFINE_DMA_NOCACHE_ARRAY(
    bsp_la_ic_dma_buffer,
    uint32_t,
    BSP_LA_IC_CHANNEL_COUNT * BSP_LA_IC_DMA_BUFFER_COUNT);

#define BSP_LA_IC_DMA_BUFFER(channel) \
    (&bsp_la_ic_dma_buffer[(channel) * BSP_LA_IC_DMA_BUFFER_COUNT])

static const BSP_LA_IC_Channel_Config_t bsp_la_ic_channels[BSP_LA_IC_CHANNEL_COUNT] = {
    {
        .channel = BSP_LA_IC_CHANNEL_TIM2_CH1,
        .dma = &hdma_tim2_ch1,
        .ccr = &TIM2->CCR1,
        .buffer = BSP_LA_IC_DMA_BUFFER(BSP_LA_IC_CHANNEL_TIM2_CH1),
    },
    {
        .channel = BSP_LA_IC_CHANNEL_TIM2_CH3,
        .dma = &hdma_tim2_ch3,
        .ccr = &TIM2->CCR3,
        .buffer = BSP_LA_IC_DMA_BUFFER(BSP_LA_IC_CHANNEL_TIM2_CH3),
    },
    {
        .channel = BSP_LA_IC_CHANNEL_TIM2_CH4,
        .dma = &hdma_tim2_ch4,
        .ccr = &TIM2->CCR4,
        .buffer = BSP_LA_IC_DMA_BUFFER(BSP_LA_IC_CHANNEL_TIM2_CH4),
    },
    {
        .channel = BSP_LA_IC_CHANNEL_TIM5_CH1,
        .dma = &hdma_tim5_ch1,
        .ccr = &TIM5->CCR1,
        .buffer = BSP_LA_IC_DMA_BUFFER(BSP_LA_IC_CHANNEL_TIM5_CH1),
    },
    {
        .channel = BSP_LA_IC_CHANNEL_TIM5_CH3,
        .dma = &hdma_tim5_ch3,
        .ccr = &TIM5->CCR3,
        .buffer = BSP_LA_IC_DMA_BUFFER(BSP_LA_IC_CHANNEL_TIM5_CH3),
    },
    {
        .channel = BSP_LA_IC_CHANNEL_TIM5_CH4,
        .dma = &hdma_tim5_ch4,
        .ccr = &TIM5->CCR4,
        .buffer = BSP_LA_IC_DMA_BUFFER(BSP_LA_IC_CHANNEL_TIM5_CH4),
    },
};

static volatile BSP_LA_IC_State_t bsp_la_ic_state = BSP_LA_IC_STATE_RESET; /* 输入捕获硬件状态 */
static volatile uint8_t bsp_la_ic_dma_error = 0U;                           /* 启动阶段DMA错误标志 */
static BSP_LA_IC_Event_Callback_t bsp_la_ic_event_callback = NULL;          /* 输入捕获事件回调 */
static void *bsp_la_ic_event_context = NULL;                                /* 输入捕获事件回调上下文 */
static uint32_t bsp_la_ic_deadline_tick = 0U;                               /* 采集截止计数值 */
static uint32_t bsp_la_ic_cutoff_tick = 0U;                                 /* 实际停止计数值 */
static uint32_t bsp_la_ic_overcapture_mask = 0U;                            /* 输入捕获溢出通道位图 */
static uint32_t bsp_la_ic_final_valid_mask = 0U;                            /* 停稳前保存的半区有效位图 */

/* ================================================== */

static void BSP_LA_IC_DMA_Half_Complete_Callback(DMA_HandleTypeDef *dma);
static void BSP_LA_IC_DMA_Complete_Callback(DMA_HandleTypeDef *dma);
static void BSP_LA_IC_DMA_Error_Callback(DMA_HandleTypeDef *dma);

/* ================================================== */

/* 私有内联函数，专门用于保存并关闭中断 */
static inline void disable_irq(uint32_t *const primask)
{
    *primask = __get_PRIMASK();
    __disable_irq();
}

/* 私有内联函数，专门用于恢复进入临界区前的中断状态 */
static inline void enable_irq(const uint32_t primask)
{
    __set_PRIMASK(primask);
}

/**
 * @brief  获取TIM2和TIM5的内核时钟
 * @note   当前工程TIMPRE未使能，TIM2和TIM5共同使用APB1定时器时钟
 * @param  无
 * @retval 定时器内核时钟，返回0表示当前时钟配置不受支持
 */
static uint32_t BSP_LA_IC_Get_TIM_Clock(void)
{
    uint32_t timer_clock;
    uint32_t apb1_prescaler;

    if ((RCC->CFGR & RCC_CFGR_TIMPRE) != 0U)
    {
        return 0U;
    }

    timer_clock = HAL_RCC_GetPCLK1Freq();
    apb1_prescaler = RCC->D2CFGR & RCC_D2CFGR_D2PPRE1;

    if (apb1_prescaler != RCC_D2CFGR_D2PPRE1_DIV1)
    {
        timer_clock *= 2U;
    }

    return timer_clock;
}

/**
 * @brief  根据DMA句柄取得输入捕获通道配置
 * @note   无
 * @param  dma DMA句柄
 * @retval 输入捕获通道配置，未找到时返回NULL
 */
static const BSP_LA_IC_Channel_Config_t *BSP_LA_IC_Get_Channel_Config(DMA_HandleTypeDef *dma)
{
    for (uint8_t i = 0U; i < BSP_LA_IC_CHANNEL_COUNT; i++)
    {
        if (bsp_la_ic_channels[i].dma == dma)
        {
            return &bsp_la_ic_channels[i];
        }
    }

    return NULL;
}

/**
 * @brief  上报输入捕获硬件事件
 * @note   该函数由中断上下文调用，服务层回调不得执行阻塞操作
 * @param  type 事件类型
 * @param  channel 输入捕获通道
 * @param  data DMA半区首地址
 * @param  count DMA半区记录数
 * @retval 无
 */
static void BSP_LA_IC_Report_Event(BSP_LA_IC_Event_Type_t type,
                                   BSP_LA_IC_Channel_t channel,
                                   const uint32_t *data,
                                   uint16_t count)
{
    BSP_LA_IC_Event_t event;

    if (bsp_la_ic_event_callback == NULL)
    {
        return;
    }

    event.type = type;
    event.channel = channel;
    event.data = data;
    event.count = count;

    bsp_la_ic_event_callback(&event, bsp_la_ic_event_context);
}

/**
 * @brief  读取输入捕获溢出通道位图
 * @note   bit0..5依次对应通道枚举0..5
 * @param  无
 * @retval 输入捕获溢出通道位图
 */
static uint32_t BSP_LA_IC_Read_Overcapture(void)
{
    uint32_t overcapture_mask = 0U;

    if ((TIM2->SR & TIM_SR_CC1OF) != 0U)
    {
        overcapture_mask |= 1UL << BSP_LA_IC_CHANNEL_TIM2_CH1;
    }
    if ((TIM2->SR & TIM_SR_CC3OF) != 0U)
    {
        overcapture_mask |= 1UL << BSP_LA_IC_CHANNEL_TIM2_CH3;
    }
    if ((TIM2->SR & TIM_SR_CC4OF) != 0U)
    {
        overcapture_mask |= 1UL << BSP_LA_IC_CHANNEL_TIM2_CH4;
    }
    if ((TIM5->SR & TIM_SR_CC1OF) != 0U)
    {
        overcapture_mask |= 1UL << BSP_LA_IC_CHANNEL_TIM5_CH1;
    }
    if ((TIM5->SR & TIM_SR_CC3OF) != 0U)
    {
        overcapture_mask |= 1UL << BSP_LA_IC_CHANNEL_TIM5_CH3;
    }
    if ((TIM5->SR & TIM_SR_CC4OF) != 0U)
    {
        overcapture_mask |= 1UL << BSP_LA_IC_CHANNEL_TIM5_CH4;
    }

    return overcapture_mask;
}

/**
 * @brief 一次读取并归一化当前DMA流的HT/TC/TE/DME/FE标志
 * @note 仅供本BSP六路DMA1 Stream使用，不支持BDMA。
 *       StreamIndex位移由HAL私有DMA_CalcBaseAndBitshift初始化，与HAL IRQ一致；
 *       宏参数使用简单局部变量，避免FLAG_INDEX三元式嵌套展开造成代码膨胀。
 */
static uint32_t BSP_LA_IC_Read_DMA_Flags(DMA_HandleTypeDef *dma)
{
    uint32_t shift = dma->StreamIndex & 0x1FU;
    uint32_t mask = (DMA_FLAG_HTIF0_4 | DMA_FLAG_TCIF0_4 |
                     BSP_LA_IC_DMA_ERROR_FLAG_MASK) << shift;

    return __HAL_DMA_GET_FLAG(dma, mask) >> shift;
}

/**
 * @brief  关闭输入捕获、DMA请求和定时器计数
 * @note   只关闭硬件事件源，不等待DMA停止
 * @param  无
 * @retval 无
 */
static void BSP_LA_IC_Disable_Event_Source(void)
{
    TIM2->CCER &= ~(BSP_LA_IC_CAPTURE_CCER_MASK | TIM_CCER_CC2E);
    TIM5->CCER &= ~BSP_LA_IC_CAPTURE_CCER_MASK;

    TIM2->DIER &= ~(BSP_LA_IC_CAPTURE_DMA_MASK | TIM_DIER_CC2IE);
    TIM5->DIER &= ~BSP_LA_IC_CAPTURE_DMA_MASK;

    CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);
    CLEAR_BIT(TIM5->CR1, TIM_CR1_CEN);

    __DSB();
}

/**
 * @brief  快速停止输入捕获硬件
 * @note   调用者必须保证状态检查和状态修改不被并发打断
 * @param  cutoff_tick 统一采集截止计数值
 * @retval 无
 */
static void BSP_LA_IC_Fast_Stop(uint32_t cutoff_tick)
{
    bsp_la_ic_overcapture_mask |= BSP_LA_IC_Read_Overcapture();
    BSP_LA_IC_Disable_Event_Source();
    /* 最后一笔捕获可能在第一次读SR与CCER停源之间产生CCxOF。 */
    bsp_la_ic_overcapture_mask |= BSP_LA_IC_Read_Overcapture();
    /* 保留未处理HT/TC；STOPPED后HAL回调被忽略，不能让IRQ先清掉证据。 */
    for (uint8_t i = 0U; i < BSP_LA_IC_CHANNEL_COUNT; i++)
    {
        __HAL_DMA_DISABLE_IT(bsp_la_ic_channels[i].dma,
                             DMA_IT_TC | DMA_IT_HT | DMA_IT_TE | DMA_IT_DME);
        __HAL_DMA_DISABLE_IT(bsp_la_ic_channels[i].dma, DMA_IT_FE);
    }
    __DSB();
    bsp_la_ic_cutoff_tick = cutoff_tick;
    bsp_la_ic_state = BSP_LA_IC_STATE_STOPPED;
}

/**
 * @brief 关闭六路DMA并等待DMA停止
 * @note 只能在任务上下文调用；在HAL Abort清标志前保存停稳的位置和有效性
 */
static BSP_LA_IC_Result_t BSP_LA_IC_Stop_DMA(BSP_LA_IC_Final_Result_t *result)
{
    uint16_t counter_before[BSP_LA_IC_CHANNEL_COUNT];
    uint8_t tc_pending[BSP_LA_IC_CHANNEL_COUNT];
    uint8_t ht_pending[BSP_LA_IC_CHANNEL_COUNT];
    BSP_LA_IC_Result_t stop_result = BSP_LA_IC_OK;
    uint32_t primask;
    uint32_t start_tick;

    if (result != NULL)
    {
        bsp_la_ic_final_valid_mask = 0U;
        result->completed_half_mask = 0U;
        if (bsp_la_ic_dma_error != 0U)
        {
            stop_result = BSP_LA_IC_ERROR_DMA;
        }
    }

    /* 禁止回调清除TC，同时请求所有Stream停止；不在关中断期间等待。 */
    disable_irq(&primask);
    for (uint8_t i = 0U; i < BSP_LA_IC_CHANNEL_COUNT; i++)
    {
        DMA_HandleTypeDef *dma = bsp_la_ic_channels[i].dma;
        uint32_t flags;

        __HAL_DMA_DISABLE_IT(dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE | DMA_IT_DME);
        __HAL_DMA_DISABLE_IT(dma, DMA_IT_FE);
        flags = BSP_LA_IC_Read_DMA_Flags(dma);
        if ((result != NULL) &&
            (((flags & BSP_LA_IC_DMA_ERROR_FLAG_MASK) != 0U) ||
             (dma->State == HAL_DMA_STATE_ERROR) ||
             (dma->ErrorCode != HAL_DMA_ERROR_NONE)))
        {
            /* IRQ尚未消费的故障不能在Abort清标志后变成正常采集。 */
            stop_result = BSP_LA_IC_ERROR_DMA;
        }
        tc_pending[i] = ((flags & DMA_FLAG_TCIF0_4) != 0U) ? 1U : 0U;
        ht_pending[i] = ((flags & DMA_FLAG_HTIF0_4) != 0U) ? 1U : 0U;
        counter_before[i] = (uint16_t)__HAL_DMA_GET_COUNTER(dma);
        __HAL_DMA_DISABLE(dma);
    }
    __DSB();
    enable_irq(primask);

    start_tick = HAL_GetTick();
    for (uint8_t i = 0U; i < BSP_LA_IC_CHANNEL_COUNT; i++)
    {
        DMA_HandleTypeDef *dma = bsp_la_ic_channels[i].dma;
        DMA_Stream_TypeDef *stream = (DMA_Stream_TypeDef *)dma->Instance;
        uint32_t flags;

        while ((stream->CR & DMA_SxCR_EN) != 0U)
        {
            if ((HAL_GetTick() - start_tick) >= BSP_LA_IC_DMA_STOP_TIMEOUT_MS)
            {
                stop_result = BSP_LA_IC_ERROR_DMA;
                break;
            }
        }
        if ((stream->CR & DMA_SxCR_EN) != 0U)
        {
            continue;
        }
        __DSB();
        flags = BSP_LA_IC_Read_DMA_Flags(dma);

        if ((result != NULL) &&
            (((flags & BSP_LA_IC_DMA_ERROR_FLAG_MASK) != 0U) ||
             (dma->State == HAL_DMA_STATE_ERROR) ||
             (dma->ErrorCode != HAL_DMA_ERROR_NONE) ||
             (bsp_la_ic_dma_error != 0U)))
        {
            /* 最后一笔在途请求也可能失败；继续清理所有Stream，但拒绝发布。 */
            stop_result = BSP_LA_IC_ERROR_DMA;
        }

        if (result != NULL)
        {
            uint32_t remaining = __HAL_DMA_GET_COUNTER(dma);
            uint16_t write_index;
            uint8_t ht_after = ((flags & DMA_FLAG_HTIF0_4) != 0U) ? 1U : 0U;
            uint8_t completed;

            if (remaining > BSP_LA_IC_DMA_BUFFER_COUNT)
            {
                stop_result = BSP_LA_IC_ERROR_DMA;
                remaining = BSP_LA_IC_DMA_BUFFER_COUNT;
            }
            write_index = (uint16_t)(BSP_LA_IC_DMA_BUFFER_COUNT - remaining);
            completed = BSP_LA_IC_Frozen_Completed_Halves(counter_before[i],
                               (uint16_t)remaining, ht_pending[i], tc_pending[i],
                               ht_after);
            result->completed_half_mask |= (uint32_t)completed << (i * 2U);
            /* 自然跨边界的最后一笔请求必须参与有效性判定。 */
            ht_pending[i] = ((completed & 1U) != 0U) ? 1U : 0U;
            tc_pending[i] = ((completed & 2U) != 0U) ? 1U : 0U;
            for (uint8_t half = 0U; half < 2U; half++)
            {
                if (BSP_LA_IC_Half_Is_Valid((uint16_t)remaining,
                                           ht_pending[i], tc_pending[i],
                                           half, 1U) != 0U)
                {
                    bsp_la_ic_final_valid_mask |= 1UL << ((i * 2U) + half);
                }
            }
            /* Stream禁用可能产生TC，因此只使用禁用前的TC和跨回绕计数。 */
            if ((write_index == 0U) &&
                ((tc_pending[i] != 0U) ||
                 (counter_before[i] < BSP_LA_IC_DMA_BUFFER_COUNT)))
            {
                write_index = BSP_LA_IC_DMA_BUFFER_COUNT;
            }
            result->buffer[i] = bsp_la_ic_channels[i].buffer;
            result->write_index[i] = write_index;
        }
        if ((dma->State == HAL_DMA_STATE_BUSY) && (HAL_DMA_Abort(dma) != HAL_OK))
        {
            stop_result = BSP_LA_IC_ERROR_DMA;
        }
        /* Reset/启动回滚允许恢复已停稳的错误句柄；Finalize不得隐藏失败。 */
        if ((result == NULL) && (dma->State == HAL_DMA_STATE_ERROR))
        {
            if (HAL_DMA_Init(dma) != HAL_OK)
            {
                stop_result = BSP_LA_IC_ERROR_DMA;
            }
        }
        if (dma->State != HAL_DMA_STATE_READY)
        {
            stop_result = BSP_LA_IC_ERROR_DMA;
        }
        if ((stream->CR & DMA_SxCR_EN) != 0U)
        {
            stop_result = BSP_LA_IC_ERROR_DMA;
        }
    }
    return stop_result;
}

/**
 * @brief 校验DMA源半区未被复用
 * @note 调用者必须屏蔽DMA中断，避免状态/标志与位置读数跨越回调处理。
 */
BSP_LA_IC_Result_t BSP_LA_IC_Validate_DMA_Half(
        BSP_LA_IC_Channel_t channel, uint8_t half)
{
    DMA_HandleTypeDef *dma;
    uint16_t remaining;
    uint8_t ht_pending;
    uint8_t tc_pending;
    uint32_t flags;

    if ((channel >= BSP_LA_IC_CHANNEL_NONE) || (half > 1U))
    {
        return BSP_LA_IC_ERROR_INVALID_PARAMETER;
    }
    if (bsp_la_ic_state == BSP_LA_IC_STATE_FINALIZED)
    {
        return ((bsp_la_ic_final_valid_mask &
                 (1UL << (((uint32_t)channel * 2U) + half))) != 0U) ?
               BSP_LA_IC_OK : BSP_LA_IC_ERROR_DMA;
    }
    if ((bsp_la_ic_state != BSP_LA_IC_STATE_RUNNING) &&
        (bsp_la_ic_state != BSP_LA_IC_STATE_STOPPED))
    {
        return BSP_LA_IC_ERROR_STATE;
    }
    dma = bsp_la_ic_channels[channel].dma;
    remaining = (uint16_t)__HAL_DMA_GET_COUNTER(dma);
    flags = BSP_LA_IC_Read_DMA_Flags(dma);
    ht_pending = ((flags & DMA_FLAG_HTIF0_4) != 0U) ? 1U : 0U;
    tc_pending = ((flags & DMA_FLAG_TCIF0_4) != 0U) ? 1U : 0U;
    /* 标志与位置之间可能有硬件进展，保留较新的位置与已观察到的复用标志。 */
    remaining = (uint16_t)__HAL_DMA_GET_COUNTER(dma);
    return (BSP_LA_IC_Half_Is_Valid(remaining, ht_pending, tc_pending, half,
                     (bsp_la_ic_state == BSP_LA_IC_STATE_STOPPED) ? 1U : 0U) != 0U) ?
           BSP_LA_IC_OK : BSP_LA_IC_ERROR_DMA;
}

/**
 * @brief  清除定时器捕获和比较标志
 * @note   无
 * @param  无
 * @retval 无
 */
static void BSP_LA_IC_Clear_TIM_Flag(void)
{
    __HAL_TIM_CLEAR_FLAG(&htim2,
                         TIM_FLAG_UPDATE |
                         TIM_FLAG_TRIGGER |
                         TIM_FLAG_CC2 |
                         BSP_LA_IC_CAPTURE_FLAG_MASK);
    __HAL_TIM_CLEAR_FLAG(&htim5,
                         TIM_FLAG_UPDATE |
                         TIM_FLAG_TRIGGER |
                         BSP_LA_IC_CAPTURE_FLAG_MASK);
}

/**
 * @brief  设置两个采集定时器的计时参数并复位
 * @note   TIM2和TIM5保持Trigger Slave模式，复位后等待TIM1_TRGO
 * @param  prescaler 定时器预分频值
 * @param  deadline_tick 采集截止计数值
 * @retval 0-成功
 *         1-失败
 */
static uint8_t BSP_LA_IC_Reset_TIM(uint32_t prescaler, uint32_t deadline_tick)
{
    BSP_LA_IC_Disable_Event_Source();

    __HAL_TIM_SET_PRESCALER(&htim2, prescaler);
    __HAL_TIM_SET_PRESCALER(&htim5, prescaler);
    __HAL_TIM_SET_AUTORELOAD(&htim2, UINT32_MAX);
    __HAL_TIM_SET_AUTORELOAD(&htim5, UINT32_MAX);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, deadline_tick);

    if (HAL_TIM_GenerateEvent(&htim2, TIM_EVENTSOURCE_UPDATE) != HAL_OK)
    {
        return 1U;
    }
    if (HAL_TIM_GenerateEvent(&htim5, TIM_EVENTSOURCE_UPDATE) != HAL_OK)
    {
        return 1U;
    }

    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_SET_COUNTER(&htim5, 0U);
    BSP_LA_IC_Clear_TIM_Flag();

    return 0U;
}

/**
 * @brief  配置并启动六路DMA
 * @note   该函数只启动DMA Stream，不使能TIM输入捕获通道
 * @param  无
 * @retval 0-成功
 *         1-失败
 */
static uint8_t BSP_LA_IC_Start_DMA(void)
{
    for (uint8_t i = 0U; i < BSP_LA_IC_CHANNEL_COUNT; i++)
    {
        DMA_HandleTypeDef *dma = bsp_la_ic_channels[i].dma;

        dma->XferHalfCpltCallback = BSP_LA_IC_DMA_Half_Complete_Callback;
        dma->XferCpltCallback = BSP_LA_IC_DMA_Complete_Callback;
        dma->XferErrorCallback = BSP_LA_IC_DMA_Error_Callback;

        if (HAL_DMA_Start_IT(dma,
                             (uint32_t)bsp_la_ic_channels[i].ccr,
                             (uint32_t)bsp_la_ic_channels[i].buffer,
                             BSP_LA_IC_DMA_BUFFER_COUNT) != HAL_OK)
        {
            if (BSP_LA_IC_Stop_DMA(NULL) != BSP_LA_IC_OK)
            {
                bsp_la_ic_state = BSP_LA_IC_STATE_STOPPED;
            }
            return 1U;
        }
    }

    return 0U;
}

/**
 * @brief  读取GPIOA和GPIOB输入寄存器原始快照
 * @note   只记录原始寄存器，不在采样时转换通道电平
 * @param  无
 * @retval GPIO输入寄存器快照
 */
static BSP_LA_IC_GPIO_Snapshot_t BSP_LA_IC_Take_GPIO_Snapshot(void)
{
    BSP_LA_IC_GPIO_Snapshot_t snapshot;

    snapshot.gpioa_idr = GPIOA->IDR;
    snapshot.gpiob_idr = GPIOB->IDR;

    return snapshot;
}

/**
 * @brief  将GPIO寄存器快照转换为六路初始电平
 * @note   bit0..5依次对应通道枚举0..5
 * @param  snapshot GPIO输入寄存器快照
 * @retval 六路初始电平位图
 */
static uint8_t BSP_LA_IC_Convert_Initial_Level(const BSP_LA_IC_GPIO_Snapshot_t *snapshot)
{
    uint8_t levels = 0U;

    if ((snapshot->gpioa_idr & GPIO_PIN_5) != 0U)
    {
        levels |= 1U << BSP_LA_IC_CHANNEL_TIM2_CH1;
    }
    if ((snapshot->gpiob_idr & GPIO_PIN_10) != 0U)
    {
        levels |= 1U << BSP_LA_IC_CHANNEL_TIM2_CH3;
    }
    if ((snapshot->gpiob_idr & GPIO_PIN_11) != 0U)
    {
        levels |= 1U << BSP_LA_IC_CHANNEL_TIM2_CH4;
    }
    if ((snapshot->gpioa_idr & GPIO_PIN_0) != 0U)
    {
        levels |= 1U << BSP_LA_IC_CHANNEL_TIM5_CH1;
    }
    if ((snapshot->gpioa_idr & GPIO_PIN_2) != 0U)
    {
        levels |= 1U << BSP_LA_IC_CHANNEL_TIM5_CH3;
    }
    if ((snapshot->gpioa_idr & GPIO_PIN_3) != 0U)
    {
        levels |= 1U << BSP_LA_IC_CHANNEL_TIM5_CH4;
    }

    return levels;
}

/**
 * @brief  检查同步启动窗口内是否发生输入边沿或DMA错误
 * @note   在读取DMA计数器前后各读取一次TIM状态，缩小检查竞态窗口
 * @param  无
 * @retval 0-没有启动碰撞
 *         1-发生启动碰撞
 */
static uint8_t BSP_LA_IC_Has_Start_Collision(void)
{
    uint32_t tim2_status_before;
    uint32_t tim5_status_before;
    uint32_t tim2_status_after;
    uint32_t tim5_status_after;

    if (bsp_la_ic_dma_error != 0U)
    {
        return 1U;
    }

    tim2_status_before = TIM2->SR;
    tim5_status_before = TIM5->SR;

    for (uint8_t i = 0U; i < BSP_LA_IC_CHANNEL_COUNT; i++)
    {
        if (__HAL_DMA_GET_COUNTER(bsp_la_ic_channels[i].dma) != BSP_LA_IC_DMA_BUFFER_COUNT)
        {
            return 1U;
        }
    }

    tim2_status_after = TIM2->SR;
    tim5_status_after = TIM5->SR;

    if (((tim2_status_before | tim2_status_after) & BSP_LA_IC_CAPTURE_FLAG_MASK) != 0U)
    {
        return 1U;
    }
    if (((tim5_status_before | tim5_status_after) & BSP_LA_IC_CAPTURE_FLAG_MASK) != 0U)
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief  计算定时器分频和采集截止计数值
 * @note   请求频率必须能够由TIM2/TIM5内核时钟精确分频
 * @param  timestamp_hz 请求的时间戳频率
 * @param  duration_us 请求的采集时长，单位us
 * @param  prescaler 返回定时器预分频值
 * @param  actual_hz 返回实际时间戳频率
 * @param  deadline_tick 返回采集截止计数值
 * @retval 0-成功
 *         1-参数错误
 */
static uint8_t BSP_LA_IC_Calculate_TIM(uint32_t timestamp_hz,
                                       uint32_t duration_us,
                                       uint32_t *prescaler,
                                       uint32_t *actual_hz,
                                       uint32_t *deadline_tick)
{
    uint32_t timer_clock;
    uint32_t divider;
    uint64_t ticks;

    if ((timestamp_hz == 0U) || (duration_us == 0U))
    {
        return 1U;
    }

    timer_clock = BSP_LA_IC_Get_TIM_Clock();
    if ((timer_clock == 0U) || ((timer_clock % timestamp_hz) != 0U))
    {
        return 1U;
    }

    divider = timer_clock / timestamp_hz;
    if ((divider == 0U) || (divider > 65536U))
    {
        return 1U;
    }

    *actual_hz = timer_clock / divider;
    ticks = ((uint64_t)(*actual_hz) * (uint64_t)duration_us) / 1000000ULL;

    if ((ticks == 0ULL) || (ticks > UINT32_MAX))
    {
        return 1U;
    }

    *prescaler = divider - 1U;
    *deadline_tick = (uint32_t)ticks;

    return 0U;
}

/* ================================================== */

/**
 * @brief  DMA前半区传输完成回调
 * @note   由HAL_DMA_IRQHandler在DMA中断上下文调用
 * @param  dma DMA句柄
 * @retval 无
 */
static void BSP_LA_IC_DMA_Half_Complete_Callback(DMA_HandleTypeDef *dma)
{
    const BSP_LA_IC_Channel_Config_t *channel_config;

    if (bsp_la_ic_state != BSP_LA_IC_STATE_RUNNING)
    {
        return;
    }

    channel_config = BSP_LA_IC_Get_Channel_Config(dma);
    if (channel_config != NULL)
    {
        BSP_LA_IC_Report_Event(BSP_LA_IC_EVENT_DMA_HALF,
                               channel_config->channel,
                               channel_config->buffer,
                               BSP_LA_IC_DMA_HALF_COUNT);
    }
}

/**
 * @brief  DMA后半区传输完成回调
 * @note   由HAL_DMA_IRQHandler在DMA中断上下文调用
 * @param  dma DMA句柄
 * @retval 无
 */
static void BSP_LA_IC_DMA_Complete_Callback(DMA_HandleTypeDef *dma)
{
    const BSP_LA_IC_Channel_Config_t *channel_config;

    if (bsp_la_ic_state != BSP_LA_IC_STATE_RUNNING)
    {
        return;
    }

    channel_config = BSP_LA_IC_Get_Channel_Config(dma);
    if (channel_config != NULL)
    {
        BSP_LA_IC_Report_Event(BSP_LA_IC_EVENT_DMA_FULL,
                               channel_config->channel,
                               channel_config->buffer + BSP_LA_IC_DMA_HALF_COUNT,
                               BSP_LA_IC_DMA_HALF_COUNT);
    }
}

/**
 * @brief  DMA传输错误回调
 * @note   启动阶段只记录错误，运行阶段立即停止输入捕获硬件并上报错误
 * @param  dma DMA句柄
 * @retval 无
 */
static void BSP_LA_IC_DMA_Error_Callback(DMA_HandleTypeDef *dma)
{
    const BSP_LA_IC_Channel_Config_t *channel_config;

    bsp_la_ic_dma_error = 1U;
    channel_config = BSP_LA_IC_Get_Channel_Config(dma);

    if (bsp_la_ic_state == BSP_LA_IC_STATE_RUNNING)
    {
        BSP_LA_IC_Fast_Stop(TIM2->CNT);

        BSP_LA_IC_Report_Event(BSP_LA_IC_EVENT_DMA_ERROR,
                               channel_config == NULL ? BSP_LA_IC_CHANNEL_NONE : channel_config->channel,
                               NULL,
                               0U);
    }
}

/* ================================================== */

/**
 * @brief  逻辑分析仪输入捕获驱动初始化
 * @note   必须在MX_DMA_Init之后调用
 * @param  无
 * @retval 无
 */
void BSP_LA_IC_Init(void)
{
    MX_TIM1_Init();
    MX_TIM2_Init();
    MX_TIM5_Init();

    BSP_LA_IC_Disable_Event_Source();
    if (BSP_LA_IC_Stop_DMA(NULL) != BSP_LA_IC_OK)
    {
        return;
    }
    BSP_LA_IC_Clear_TIM_Flag();

    bsp_la_ic_event_callback = NULL;
    bsp_la_ic_event_context = NULL;
    bsp_la_ic_deadline_tick = 0U;
    bsp_la_ic_cutoff_tick = 0U;
    bsp_la_ic_overcapture_mask = 0U;
    bsp_la_ic_dma_error = 0U;
    bsp_la_ic_state = BSP_LA_IC_STATE_IDLE;

}

/**
 * @brief  设置逻辑分析仪输入捕获事件回调
 * @note   回调在DMA或TIM中断上下文执行，不得进行阻塞操作
 * @param  callback 输入捕获事件回调，传入NULL表示关闭事件上报
 * @param  context 回调上下文
 * @retval 无
 */
void BSP_LA_IC_Set_Event_Callback(BSP_LA_IC_Event_Callback_t callback, void *context)
{
    uint32_t primask;

    disable_irq(&primask);
    bsp_la_ic_event_callback = callback;
    bsp_la_ic_event_context = context;
    enable_irq(primask);
}

/**
 * @brief  启动逻辑分析仪输入捕获
 * @note   TIM1产生一次Update TRGO，同时启动TIM2和TIM5
 * @param  timestamp_hz 请求的时间戳频率
 * @param  duration_us 请求的采集时长，单位us
 * @param  result 返回实际时间戳频率、截止计数值和六路初始电平
 * @retval BSP_LA_IC_OK-成功
 *         其他值-失败
 */
BSP_LA_IC_Result_t BSP_LA_IC_Start(uint32_t timestamp_hz,
                                   uint32_t duration_us,
                                   BSP_LA_IC_Start_Result_t *result)
{
    BSP_LA_IC_GPIO_Snapshot_t snapshot;
    uint32_t primask;
    uint32_t prescaler;
    uint32_t actual_hz;
    uint32_t deadline_tick;
    uint32_t tim2_ccer_armed;
    uint32_t tim5_ccer_armed;
    uint8_t collision;

    if (result == NULL)
    {
        return BSP_LA_IC_ERROR_INVALID_PARAMETER;
    }

    disable_irq(&primask);
    if (bsp_la_ic_state != BSP_LA_IC_STATE_IDLE)
    {
        enable_irq(primask);
        return BSP_LA_IC_ERROR_BUSY;
    }
    bsp_la_ic_state = BSP_LA_IC_STATE_STARTING;
    enable_irq(primask);

    if (BSP_LA_IC_Calculate_TIM(timestamp_hz,
                                duration_us,
                                &prescaler,
                                &actual_hz,
                                &deadline_tick) != 0U)
    {
        bsp_la_ic_state = BSP_LA_IC_STATE_IDLE;
        return BSP_LA_IC_ERROR_INVALID_PARAMETER;
    }

    bsp_la_ic_deadline_tick = deadline_tick;
    bsp_la_ic_cutoff_tick = 0U;
    bsp_la_ic_overcapture_mask = 0U;

    for (uint8_t retry = 0U; retry < BSP_LA_IC_START_RETRY_COUNT; retry++)
    {
        bsp_la_ic_dma_error = 0U;

        if (BSP_LA_IC_Reset_TIM(prescaler, deadline_tick) != 0U)
        {
            bsp_la_ic_state = BSP_LA_IC_STATE_IDLE;
            return BSP_LA_IC_ERROR_STATE;
        }

        if (BSP_LA_IC_Start_DMA() != 0U)
        {
            BSP_LA_IC_Disable_Event_Source();
            if (bsp_la_ic_state != BSP_LA_IC_STATE_STOPPED)
            {
                bsp_la_ic_state = BSP_LA_IC_STATE_IDLE;
            }
            return BSP_LA_IC_ERROR_DMA;
        }

        TIM2->DIER |= BSP_LA_IC_CAPTURE_DMA_MASK;
        TIM5->DIER |= BSP_LA_IC_CAPTURE_DMA_MASK;

        tim2_ccer_armed = TIM2->CCER | BSP_LA_IC_CAPTURE_CCER_MASK | TIM_CCER_CC2E;
        tim5_ccer_armed = TIM5->CCER | BSP_LA_IC_CAPTURE_CCER_MASK;

        disable_irq(&primask);

        BSP_LA_IC_Clear_TIM_Flag();
        TIM2->CCER = tim2_ccer_armed;
        TIM5->CCER = tim5_ccer_armed;
        __HAL_TIM_ENABLE_IT(&htim2, TIM_IT_CC2);

        __DSB();

        if (HAL_TIM_GenerateEvent(&htim1, TIM_EVENTSOURCE_UPDATE) != HAL_OK)
        {
            BSP_LA_IC_Disable_Event_Source();
            enable_irq(primask);
            bsp_la_ic_state = (BSP_LA_IC_Stop_DMA(NULL) == BSP_LA_IC_OK) ?
                              BSP_LA_IC_STATE_IDLE : BSP_LA_IC_STATE_STOPPED;
            return BSP_LA_IC_ERROR_STATE;
        }

        snapshot = BSP_LA_IC_Take_GPIO_Snapshot();
        collision = BSP_LA_IC_Has_Start_Collision();

        if (collision == 0U)
        {
            result->timestamp_hz = actual_hz;
            result->deadline_tick = deadline_tick;
            result->initial_levels = BSP_LA_IC_Convert_Initial_Level(&snapshot);
            bsp_la_ic_state = BSP_LA_IC_STATE_RUNNING;

            enable_irq(primask);
            return BSP_LA_IC_OK;
        }

        BSP_LA_IC_Disable_Event_Source();
        enable_irq(primask);
        if (BSP_LA_IC_Stop_DMA(NULL) != BSP_LA_IC_OK)
        {
            bsp_la_ic_state = BSP_LA_IC_STATE_STOPPED;
            return BSP_LA_IC_ERROR_DMA;
        }
    }

    (void)BSP_LA_IC_Reset_TIM(prescaler, deadline_tick);
    bsp_la_ic_state = BSP_LA_IC_STATE_IDLE;

    if (bsp_la_ic_dma_error != 0U)
    {
        return BSP_LA_IC_ERROR_DMA;
    }

    return BSP_LA_IC_ERROR_START_COLLISION;
}

/**
 * @brief  停止逻辑分析仪输入捕获
 * @note   只快速关闭硬件事件源，不等待DMA停止，可在任务或中断上下文调用
 * @param  cutoff_tick 返回实际停止计数值
 * @retval BSP_LA_IC_OK-成功
 *         BSP_LA_IC_ERROR_STATE-当前状态不能停止
 */
BSP_LA_IC_Result_t BSP_LA_IC_Stop(uint32_t *cutoff_tick)
{
    uint32_t primask;
    uint32_t current_tick;

    if (cutoff_tick == NULL)
    {
        return BSP_LA_IC_ERROR_INVALID_PARAMETER;
    }

    disable_irq(&primask);
    if (bsp_la_ic_state != BSP_LA_IC_STATE_RUNNING)
    {
        enable_irq(primask);
        return BSP_LA_IC_ERROR_STATE;
    }

    current_tick = TIM2->CNT;
    BSP_LA_IC_Fast_Stop(current_tick);
    *cutoff_tick = current_tick;

    enable_irq(primask);
    return BSP_LA_IC_OK;
}

/**
 * @brief 获取指定通道DMA实时数据快照
 * @note 只读取一次DMA位置，快照后产生的新记录不包含在本次结果中
 * @param channel 输入捕获通道
 * @param result 返回DMA缓冲区和写入位置
 * @retval BSP_LA_IC_OK-成功
 *         其他值-失败
 */
BSP_LA_IC_Result_t BSP_LA_IC_Get_Live(BSP_LA_IC_Channel_t channel,
                                      BSP_LA_IC_Live_Result_t *result)
{
    const BSP_LA_IC_Channel_Config_t *channel_config;
    uint16_t write_index;

    if ((channel >= BSP_LA_IC_CHANNEL_NONE) || (result == NULL))
    {
        return BSP_LA_IC_ERROR_INVALID_PARAMETER;
    }
    if (bsp_la_ic_state != BSP_LA_IC_STATE_RUNNING)
    {
        return BSP_LA_IC_ERROR_STATE;
    }

    channel_config = &bsp_la_ic_channels[channel];
    write_index = (uint16_t)(BSP_LA_IC_DMA_BUFFER_COUNT -
                             __HAL_DMA_GET_COUNTER(channel_config->dma));
    if ((write_index == 0U) &&
        ((BSP_LA_IC_Read_DMA_Flags(channel_config->dma) & DMA_FLAG_TCIF0_4) != 0U))
    {
        write_index = BSP_LA_IC_DMA_BUFFER_COUNT;
    }

    result->buffer = channel_config->buffer;
    result->write_index = write_index;

    return BSP_LA_IC_OK;
}

/**
 * @brief 获取逻辑分析仪当前计数值
 * @note 运行时返回TIM2 CNT，停止后返回统一截止计数值
 * @param current_tick 返回当前计数值
 * @retval BSP_LA_IC_OK-成功
 *         其他值-失败
 */
BSP_LA_IC_Result_t BSP_LA_IC_Get_Current_Tick(uint32_t *current_tick)
{
    uint32_t primask;

    if (current_tick == NULL)
    {
        return BSP_LA_IC_ERROR_INVALID_PARAMETER;
    }

    disable_irq(&primask);
    if (bsp_la_ic_state == BSP_LA_IC_STATE_RUNNING)
    {
        *current_tick = TIM2->CNT;
    }
    else if ((bsp_la_ic_state == BSP_LA_IC_STATE_STOPPED) ||
             (bsp_la_ic_state == BSP_LA_IC_STATE_FINALIZED))
    {
        *current_tick = bsp_la_ic_cutoff_tick;
    }
    else
    {
        enable_irq(primask);
        return BSP_LA_IC_ERROR_STATE;
    }
    enable_irq(primask);

    return BSP_LA_IC_OK;
}

/**
 * @brief  完成逻辑分析仪输入捕获硬件收尾
 * @note   关闭六路DMA并返回DMA缓冲区最终写入位置，只能在任务上下文调用
 * @param  result 返回DMA缓冲区、写入位置、截止计数值和捕获溢出位图
 * @retval BSP_LA_IC_OK-成功
 *         其他值-失败
 */
BSP_LA_IC_Result_t BSP_LA_IC_Finalize(BSP_LA_IC_Final_Result_t *result)
{
    if (result == NULL)
    {
        return BSP_LA_IC_ERROR_INVALID_PARAMETER;
    }
    if (bsp_la_ic_state != BSP_LA_IC_STATE_STOPPED)
    {
        return BSP_LA_IC_ERROR_STATE;
    }

    if (BSP_LA_IC_Stop_DMA(result) != BSP_LA_IC_OK)
    {
        return BSP_LA_IC_ERROR_DMA;
    }

    result->cutoff_tick = bsp_la_ic_cutoff_tick;
    result->overcapture_mask = bsp_la_ic_overcapture_mask;
    bsp_la_ic_state = BSP_LA_IC_STATE_FINALIZED;

    return BSP_LA_IC_OK;
}

/**
 * @brief  复位逻辑分析仪输入捕获驱动
 * @note   运行状态下不能复位，收尾结果使用完成后调用
 * @param  无
 * @retval BSP_LA_IC_OK-成功
 *         BSP_LA_IC_ERROR_BUSY-当前正在采集
 */
BSP_LA_IC_Result_t BSP_LA_IC_Reset(void)
{
    if ((bsp_la_ic_state == BSP_LA_IC_STATE_STARTING) ||
        (bsp_la_ic_state == BSP_LA_IC_STATE_RUNNING))
    {
        return BSP_LA_IC_ERROR_BUSY;
    }

    BSP_LA_IC_Disable_Event_Source();
    if (BSP_LA_IC_Stop_DMA(NULL) != BSP_LA_IC_OK)
    {
        return BSP_LA_IC_ERROR_DMA;
    }
    BSP_LA_IC_Clear_TIM_Flag();

    bsp_la_ic_deadline_tick = 0U;
    bsp_la_ic_cutoff_tick = 0U;
    bsp_la_ic_overcapture_mask = 0U;
    bsp_la_ic_dma_error = 0U;
    bsp_la_ic_state = BSP_LA_IC_STATE_IDLE;

    return BSP_LA_IC_OK;
}

/* ================================================== */

/**
 * @brief  TIM输出比较延时到达回调
 * @note   TIM2_CH2用于产生统一采集截止事件
 * @param  htim TIM句柄
 * @retval 无
 */
void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
    if ((htim->Instance == TIM2) &&
        (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) &&
        (bsp_la_ic_state == BSP_LA_IC_STATE_RUNNING))
    {
        BSP_LA_IC_Fast_Stop(bsp_la_ic_deadline_tick);
        BSP_LA_IC_Report_Event(BSP_LA_IC_EVENT_DEADLINE,
                               BSP_LA_IC_CHANNEL_NONE,
                               NULL,
                               0U);
    }
}
