/**
 ******************************************************************************
 * @file    layer1_wave.h
 * @brief   六路数字波形显示、配置和采集数据管理模块的公共接口。
 * @details 初始化和配置接口由 main.c、page_device_config.c 调用；采集控制和
 *          刷新接口由 layer0_right_panel.c 调用；性能统计接口由 layer1_frame_test.c 调用。
 ******************************************************************************
 */

#ifndef WAVE_H
#define WAVE_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * 波形条带传输方式诊断开关：
 * 0：CPU memcpy 写入 SDRAM；1：DMA2D 异步写入 SDRAM。
 * 两种方式共用相同的26行横向条带、双缓冲和 VBlank 换帧流程。
 */
#ifndef WAVE_TRANSFER_USE_DMA2D
#define WAVE_TRANSFER_USE_DMA2D  0
#endif

/*
 * DMA2D完成一个512×26条带后，发送任务主动让出总线的时长。
 * LTDC一直以实时优先级从同一片SDRAM取像素；若连续发送所有通道条带，
 * 虽然每笔很小，仍会形成持续总线占用并在固定扫描行造成FIFO欠载。
 * 0表示关闭节流。DMA2D自身还会通过AMTCR在每个AXI突发之间让出总线，
 * 因此默认不再额外拉长同帧相邻条带之间的任务等待时间。
 */
#ifndef WAVE_DMA_INTER_JOB_DELAY_MS
#define WAVE_DMA_INTER_JOB_DELAY_MS  0U
#endif

/*
 * 启动阶段的 DMA2D 全屏填黑只用于带宽基准测试，并非波形功能所必需。
 * 当前先关闭它：控件层已确认正常，而花屏严格落在 512x26 波形条带位置；
 * 保留条带、双缓冲和 LTDC 换帧，仅把搬运介质切到 CPU，用于隔离 DMA2D
 * 访问 SDRAM 时的总线竞争或源缓冲读取问题。
 */
#ifndef WAVE_SDRAM_DMA_FILL_TEST_ENABLE
#define WAVE_SDRAM_DMA_FILL_TEST_ENABLE  0U
#endif

/*
 * 每个 LTDC 场周期最多触发一次波形准备；若上一帧尚未完成，
 * 二值信号量只保留最新时刻，不积压过期帧。
 */
#ifndef WAVE_REFRESH_FRAME_DIVIDER
#define WAVE_REFRESH_FRAME_DIVIDER  3U
#endif

/*
 * 固定 LTDC 波形层地址的总线竞争验证：
 * 0：DMA2D 仍持续写入绘制后缓冲，但不提交 present()，LTDC 始终扫描初始
 *    显示缓冲；用于隔离“DMA/FMC 竞争”与“帧缓冲地址交换”两个因素。
 * 1：整帧 DMA 完成后请求 VBlank 换帧，正常显示波形。
 * 本轮验证结束后改回 1U。
 */
#ifndef WAVE_LTDC_PRESENT_ENABLE
#define WAVE_LTDC_PRESENT_ENABLE  1U
#endif

/*============================== 显示区域 ==============================*/

#define WAVE_CHANNEL_COUNT              6U

#define WAVE_LEFT_X                     0
#define WAVE_LEFT_WIDTH                 100

#define WAVE_AREA_X                     100
#define WAVE_AREA_WIDTH                 500
#define WAVE_AREA_X_END                 (WAVE_AREA_X + WAVE_AREA_WIDTH - 1)

#define WAVE_TIME_RULER_HEIGHT          36

/*============================== 时间窗口 ==============================*/

/* 采集定时器按 1 MHz 计数。 */
#define WAVE_WINDOW_1S_CNT              1000000UL
#define WAVE_WINDOW_2S_CNT              2000000UL
#define WAVE_WINDOW_5S_CNT              5000000UL
#define WAVE_CAPTURE_DEFAULT_CNT        10000000UL

/*============================== 文本容量 ==============================*/

#define WAVE_PIN_TEXT_SIZE              4U
#define WAVE_NAME_TEXT_SIZE             16U
#define WAVE_DETAIL_TEXT_SIZE           16U

/**
 * @brief 一路物理通道对应的界面配置。
 *
 * 本结构只保存物理通道自身的属性，不保存屏幕坐标。显示位置由 layer1_wave.c
 * 根据已启用通道的顺序自动计算，外部代码不需要维护两套位置数据。
 */
typedef struct
{
    bool enabled;                       /* 是否创建左侧信息和波形轨道 */
    uint32_t color;                     /* 徽标和波形共用的 0xRRGGBB 颜色 */

    char pin_text[WAVE_PIN_TEXT_SIZE];  /* 左侧徽标文字，例如 D1 */
    char name_text[WAVE_NAME_TEXT_SIZE];/* 左侧名称，例如 Channel 1 */

    bool detail_visible;                /* 是否显示协议名/别名 */
    char detail_text[WAVE_DETAIL_TEXT_SIZE]; /* 协议名/别名，例如 SCL */
    bool separator_visible;             /* 是否显示底部下划线 */
} wave_channel_config_t;

typedef struct
{
    uint32_t cnt_start;
    uint32_t cnt_end;
} wave_cnt_range_t;

/*============================== 波形流水线任务 ==============================*/

/** 创建波形准备、DMA发送任务及其队列和信号量；由 Services_Init() 调用。 */
bool wave_display_task_create(void);

/**
 * 在 LTDC Layer 1 初始化完成后启动帧中断通知。
 * 此后帧中断只锁存tick并通知准备任务，DMA任务完成整帧搬运后提交VBlank换帧。
 */
void wave_display_task_start(void);

/** 由 GUI 任务处理波形准备任务产生的时间尺失效请求。 */
void wave_process_gui_updates(void);

/*============================== 生命周期 ==============================*/

/**
 * @brief 初始化模块状态并生成六路默认配置。
 *
 * 本函数不创建 LVGL 控件。调用者可以先创建其它页面，再调用
 * wave_apply_configuration() 创建 0~599 区域，便于控制对象的层级顺序。
 */
bool wave_init(lv_obj_t * parent);

/**
 * @brief 删除本模块创建的全部 LVGL 对象并清空内部状态。
 *
 * 页面被关闭或需要彻底重新初始化模块时调用；调用后可再次执行 wave_init()。
 */
void wave_deinit(void);

/*============================== 通道配置 ==============================*/

/**
 * @brief 读取一路当前配置。
 * @param channel_index 物理通道下标，范围 0~5。
 * @param config        接收配置副本的地址。
 * @return true 表示读取成功，false 表示参数或模块状态无效。
 *
 * 本函数只复制数据，不返回内部指针，调用者可以安全修改取得的副本。
 */
bool wave_get_channel_config(uint32_t channel_index,
                             wave_channel_config_t * config);

/**
 * @brief 修改一路配置。
 *
 * 只保存数据，不立即创建或移动控件。采集运行期间拒绝修改。
 * @param channel_index 物理通道下标，范围 0~5。
 * @param config        要保存的新配置。
 * @return true 表示配置已保存，false 表示配置非法或正在采集。
 */
bool wave_set_channel_config(uint32_t channel_index,
                             const wave_channel_config_t * config);

/**
 * @brief 单独设置一路通道是否参与采集和显示。
 *
 * 本函数只保存选择状态，不立即创建控件；随后调用
 * wave_apply_configuration()，启用通道会按物理通道号从小到大排列。
 * @param channel_index 物理通道下标，范围 0~5，分别对应界面通道 1~6。
 * @param enabled       true 表示使用该通道，false 表示不使用该通道。
 * @return true 表示选择状态已保存，false 表示模块未初始化或正在采集。
 */
bool wave_set_channel_enabled(uint32_t channel_index, bool enabled);

/**
 * @brief 按当前六路配置重建通道界面。
 *
 * 只为 enabled 通道创建左侧信息和波形轨道。启用通道按物理通道号从小
 * 到大依次放入顶部固定槽位，未使用的尾部槽位保持空白。采集期间拒绝重建。
 * @return true 表示全部对象创建成功，false 表示状态无效或创建失败。
 */
bool wave_apply_configuration(void);

/*============================== 采集控制 ==============================*/

/**
 * @brief 重置显示窗口并启动六路采集数据源，下一帧由波形任务重建画面。
 * @param timer_start_cnt 采集定时器开始时刻；后续刷新会自动换算为相对时间。
 * @return true 表示进入采集状态，false 表示界面配置尚未提交或状态无效。
 */
bool wave_start_capture(uint32_t timer_start_cnt);

/**
 * @brief 正常完成本轮采集，并用完整采集时长重新生成最后一帧总览。
 *
 * 例如实时窗口为 5 s、采集时长为 10 s，完成时会把最后一帧改成 0~10 s，
 * 显示全部已采集边沿并退出采集状态；下一轮开始时恢复 5 s 实时窗口。
 * @return true 表示总览帧已经准备好，false 表示当前未处于采集状态。
 */
bool wave_complete_capture(void);

/**
 * @brief 暂停当前采集，保留当前 CNT 之前的数据并重建 0~暂停 CNT 的完整总览。
 *
 * 暂停后的时间标尺、x 坐标和波形都使用同一显示窗口；下一次开始采集仍会
 * 新建一轮采集，不会从暂停位置继续。
 */
bool wave_pause_capture(void);

/**
 * @brief 退出采集状态并保留最后画面。
 *
 * 停止后可以修改通道配置并重新调用 wave_apply_configuration()。
 */
void wave_stop_capture(void);

/**
 * @brief 用户中途停止本轮采集，并请求波形任务清空波形显示区。
 *
 * 与 wave_stop_capture() 不同，本函数不保留最后一帧。下一次开始采集会
 * 从 CNT=0 和空缓冲区重新开始。
 */
void wave_cancel_capture(void);

/** @return true 表示当前处于采集状态，false 表示未采集。 */
bool wave_is_capturing(void);

/**
 * @brief 设置一轮采集计划持续的时间。
 * @param duration_cnt 采集时长，单位为 1 MHz 定时器 CNT；0 会被拒绝。
 * @return true 表示配置成功，false 表示正在采集或参数无效。
 */
bool wave_set_capture_duration(uint32_t duration_cnt);

/** @return 当前配置的一轮采集时长，单位为 1 MHz 定时器 CNT。 */
uint32_t wave_get_capture_duration(void);

/**
 * @brief 修改采集过程中的实时滑动窗口长度。
 * @param window_cnt 实时窗口长度，单位为采集定时器 CNT；0 会被忽略。
 *
 * 完成采集后的总览窗口不修改这项配置；下一轮开始仍恢复这里设置的值。
 */
void wave_set_window_size(uint32_t window_cnt);

/**
 * @brief 请求将当前窗口按手势水平位移，并在波形任务中节流提交最新位置。
 *
 * @param delta_x_pixels 手势终点减起点的 X 像素差；正值表示手指向右滑动，
 *                       因而查看更早的数据。函数不立即绘制，也不累积旧请求。
 */
void wave_pan_view_pixels(int32_t delta_x_pixels);

/**
 * @brief 以波形区内的指定 X 坐标为锚点缩放时间窗口。
 *
 * @param anchor_x_pixels 屏幕 X 坐标；应在 WAVE_AREA_X~WAVE_AREA_X_END 内。
 * @param zoom_in         true 放大（窗口变小），false 缩小（窗口变大）。
 * @note 窗口最小为 5 ms，最大为本轮已采集的完整 CNT 范围；函数只提交目标，
 *       实际绘制仍由波形任务统一节流处理。
 */
void wave_zoom_view_at(int32_t anchor_x_pixels, bool zoom_in);

/** @brief 取消历史浏览并恢复采集窗口自动跟随最新 CNT。 */
void wave_return_to_live(void);

/*============================== CNT 范围 ==============================*/

/** 获取从上次调用到当前时刻新增的数据范围；适合实时补右侧波形。 */
bool wave_get_live_range(wave_cnt_range_t * range);

/** 获取当前完整显示窗口范围；适合停止后平移、缩放和整帧重建。 */
bool wave_get_view_range(wave_cnt_range_t * range);

/*============================== 显示统计 ==============================*/

/** @return LTDC Layer 1 已完成的实际垂直同步换帧次数。 */
uint32_t wave_get_refresh_count(void);

#endif /* WAVE_H */
