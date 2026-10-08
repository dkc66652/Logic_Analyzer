/**
 ******************************************************************************
 * @file    layer1_wave.c
 * @brief   六路数字波形的配置、采集数据管理和 Layer 1 像素绘制。
 * @details 帧中断只锁存时刻并释放信号量；准备任务一次查询六路共享列状态，
 *          再为每路已开启通道生成26行RGB565条带并按顺序搬运到SDRAM。
 *          LVGL只绘制静态UI和时间标尺。
 ******************************************************************************
 */

#include "layer1_wave.h"
#include "layer0_left_channels.h"
#include "layer1_capture_data.h"
#include "bsp_dma2d.h"
#include "bsp_ltdc_lcd.h"
#include "layer1_frame_test.h"
#include "FreeRTOS.h"
#include "platform_dwt.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include <string.h>

/*============================== 模块常量 ==============================*/

#define WAVE_TIME_LABEL_SLOTS           12U
#define WAVE_TIME_TICK_BASE_CNT         (WAVE_WINDOW_1S_CNT / 10U)
#define WAVE_TIME_TICK_MIN_COUNT        5U
#define WAVE_SIMULATED_CHANNEL_INDEX     0U
#define WAVE_MIN_WINDOW_CNT               5000U
#define WAVE_PREPARE_TASK_STACK            768U
#define WAVE_PREPARE_TASK_PRIORITY           1U
#define WAVE_DMA_TASK_STACK                384U
#define WAVE_DMA_TASK_PRIORITY               2U
/* 两块固定容量的片内RGB565条带：准备任务与DMA发送任务交替持有。 */
#define WAVE_DMA_BUFFER_COUNT                2U
#define WAVE_DMA_TILE_LINES                 26U
#define WAVE_DMA_NO_BUFFER                0xFFU
#define WAVE_DMA_CACHE_LINE_BYTES            32U
#define WAVE_DMA_TILE_STRIDE_PIXELS          512U
#define WAVE_DMA_BUFFER_CAPACITY_PIXELS \
    (WAVE_DMA_TILE_STRIDE_PIXELS * WAVE_DMA_TILE_LINES)

#define WAVE_BACKGROUND_COLOR           0x0A0F14U
#define WAVE_TIME_TICK_COLOR            0x7D8B99U
#define WAVE_TIME_TEXT_COLOR            0xB9C5D0U


/*============================== 内部类型 ==============================*/

typedef enum
{
    WAVE_STATE_UNINITIALIZED = 0,       /* 尚未调用 wave_init() */
    WAVE_STATE_CONFIGURED,              /* 已初始化，允许修改和提交配置 */
    WAVE_STATE_CAPTURING                /* 正在采集，只允许写数据和刷新波形 */
} wave_state_t;

typedef struct
{
    uint8_t display_slot;                  /* 当前占用的显示槽位，0 表示最上面一行 */
    int16_t top;                           /* 当前显示行顶部，相对整个屏幕 */
    int16_t height;                        /* 当前显示行高度 */
    int16_t high_offset;                   /* 高电平 Y，相对当前行顶部 */
    int16_t low_offset;                    /* 低电平 Y，相对当前行顶部 */
} wave_channel_layout_t;

typedef layer0_left_channel_row_t wave_channel_render_t;

typedef struct
{
    uint8_t source_id;                     /* 固定物理通道编号，不随显示位置改变 */
    wave_channel_config_t config;          /* 不随每帧变化的界面配置 */
    wave_channel_layout_t layout;          /* 由启用顺序计算出的内部显示位置 */
    wave_channel_render_t render;          /* 左侧 LVGL 对象句柄和本帧坐标缓存 */
} wave_channel_t;

typedef struct
{
    uint32_t start_cnt;                    /* 当前可见时间窗口左边界的相对 CNT */
    uint32_t now_cnt;                      /* 当前可见时间窗口右边界的相对 CNT */
    uint32_t window_size;                  /* 时间窗口宽度，单位为 CNT */
} wave_view_t;

typedef struct
{
    lv_obj_t * parent;                     /* 本模块所有界面对象的外部父对象 */
    lv_obj_t * left_root;                  /* x=0~99 的左侧静态信息根容器 */
    lv_obj_t * wave_root;                  /* x=100~599 的 Layer 0 背景与时间标尺根容器 */
    lv_obj_t * time_ruler;                 /* 波形区顶部时间刻度对象 */
    int32_t display_height;                /* 当前显示器纵向分辨率 */

    wave_channel_t channels[WAVE_CHANNEL_COUNT]; /* 六路物理通道的全部状态 */
    wave_view_t view;                      /* 当前整帧显示的时间范围 */
    wave_state_t state;                    /* 模块当前所处的生命周期状态 */
    bool configuration_applied;            /* 是否已按配置创建通道控件 */
    uint32_t capture_timer_start_cnt;       /* 点击开始采集时的原始定时器 CNT */
    uint32_t capture_duration_cnt;          /* 一轮采集计划持续的 CNT */
    uint32_t captured_end_cnt;               /* 当前轮已经采集到的最后 CNT */
    uint32_t live_window_size;              /* 采集过程中使用的实时滑动窗口宽度 */
    uint32_t last_live_range_end_cnt;       /* 上次读取实时增量范围的右边界 */
    uint32_t pending_view_start_cnt;        /* 手势请求的目标窗口左端 */
    uint32_t pending_window_size;           /* 手势请求的目标窗口宽度 */
    bool follow_live;                        /* true 时采集中窗口自动跟随最新 CNT */
    bool view_dirty;                         /* 手势有新目标窗口，等待后缓冲可写 */
    bool render_requested;                  /* 下一次帧信号到来时重建 Layer 1 */
    uint8_t clear_frames_remaining;          /* 配置改变后依次清理两块SDRAM缓冲 */

    /* 绘制任务执行完前，标签字符串必须一直有效。 */
    char time_labels[WAVE_TIME_LABEL_SLOTS][16];
} wave_context_t;

typedef struct
{
    bool enabled;
    uint8_t source_id;
    uint32_t color;
    int16_t high_y;
    int16_t low_y;
} wave_frame_channel_t;

typedef struct
{
    wave_view_t view;
    wave_frame_channel_t channels[WAVE_CHANNEL_COUNT];
    bool clear_background;
} wave_frame_snapshot_t;

typedef enum
{
    WAVE_DMA_JOB_COPY = 0,              /* 从片内乒乓缓冲搬运一个通道条带 */
    WAVE_DMA_JOB_FILL                   /* R2M模式直接清空整个波形后缓冲 */
} wave_dma_job_type_t;

typedef struct
{
    wave_dma_job_type_t type;
    uint8_t buffer_index;               /* COPY任务占用的乒乓缓冲编号 */
    uint16_t width;
    uint16_t height;
    uint16_t destination_stride;        /* 目标SDRAM相邻两行的像素步长 */
    const uint16_t *source;             /* COPY子任务在片内条带中的起始地址 */
    uint16_t *destination;
    uint32_t measure_pixels;            /* 非0时记录本次DMA测试吞吐率 */
    bool first_in_frame;
    bool last_in_frame;
    bool release_buffer;                /* 本子任务完成后才归还源条带缓冲 */
    bool content_valid;                 /* false时完成搬运但不提交本帧 */
} wave_dma_job_t;

/*============================== 内部函数声明 ==============================*/

/** 根据物理通道下标生成一路可直接使用的默认配置。 */
static void wave_init_default_config(wave_channel_t * channel,
                                     uint32_t channel_index);

/** 把一路已启用通道放入指定的固定显示槽位，并计算行内高低电平位置。 */
static void wave_assign_display_slot(wave_channel_t * channel,
                                     uint32_t display_slot);

/** 创建左侧静态根容器和右侧波形根容器。 */
static bool wave_create_roots(lv_obj_t * parent, int32_t display_height);

/** 清空一路已经失效的 LVGL 对象句柄。 */
static void wave_reset_render_handles(wave_channel_t * channel);

/** 在 wave_root 顶部创建时间标尺对象并注册自绘回调。 */
static bool wave_create_time_ruler(void);

/** 为一路已启用通道创建左侧信息行。波形轨道由 LTDC Layer 1 直接显示。 */
static bool wave_create_channel_widgets(wave_channel_t * channel);

/** 根据当前相对 CNT 计算时间窗口的左右边界。 */
static void wave_update_view(uint32_t relative_now_cnt);

/** 将一个相对 CNT 映射为屏幕 x=100~599 内的横坐标。 */
static int32_t wave_cnt_to_x(uint32_t cnt, const wave_view_t * view);

/** 将一个候选窗口左端限制在当前已采集数据的合法范围内。 */
static uint32_t wave_clamp_view_start(uint32_t requested_start_cnt,
                                      uint32_t window_cnt,
                                      uint32_t available_end_cnt);

/** 取得相邻的 1/2/5 缩放档位；窗口放大为变小，缩小为变大。 */
static uint32_t wave_get_zoom_window(uint32_t current_window_cnt,
                                     uint32_t max_window_cnt,
                                     bool zoom_in);

/** LVGL 时间标尺自绘回调，绘制刻度线及秒数标签。 */
static void wave_draw_time_ruler_cb(lv_event_t * event);

/** 将 0xRRGGBB 颜色转换为 LTDC Layer 1 使用的 RGB565 像素。 */
static uint16_t wave_color_to_rgb565(uint32_t color);

/** 结束采集并将 0~end_cnt 重建为停止状态的完整总览。 */
static bool wave_finish_capture(uint32_t end_cnt);

/** 根据当前窗口选择主刻度间隔，使主刻度间隔数保持在 5~10 个。 */
static uint32_t wave_get_time_tick_cnt(uint32_t window_cnt);

/** 把相对 CNT 格式化为时间标尺使用的秒数字符串。 */
static void wave_format_time_label(char * buffer,
                                   uint32_t buffer_size,
                                   uint32_t cnt,
                                   uint32_t tick_cnt);

/** LTDC 帧回调：锁存时刻并通过二值信号量唤醒波形任务。 */
static void wave_frame_start_isr(void *context);

/** 波形准备任务；读取采集数据并交替生成两个片内RGB565条带。 */
static void wave_prepare_task(void *parameter);

/** DMA发送任务；串行搬运就绪条带，最后一个条带完成后提交换帧。 */
static void wave_dma_send_task(void *parameter);

/** DMA2D中断回调；仅保存结果并释放完成信号量。 */
static void wave_dma_complete_isr(void *context, bool success);

/** 使用CPU逐行复制条带；仅替代搬运介质，不改变后续换帧流程。 */
static bool wave_copy_job_by_cpu(const wave_dma_job_t *job);

/** 未采集时使用DMA2D整块写黑，用于测量R2M填充开销。 */
static bool wave_sdram_dma_fill_test_is_active(void);
static bool wave_run_sdram_dma_fill_test(void);

/** 根据帧中断时刻生成一帧；返回 true 表示已经提交 VBlank 换帧。 */
static bool wave_render_frame_at_tick(uint32_t frame_tick);

/** 复制当前显示窗口和绘制配置，避免写 SDRAM 时继续访问共享状态。 */
static bool wave_make_frame_snapshot(uint32_t frame_tick,
                                     wave_frame_snapshot_t *snapshot);

/** 一次查询六路列状态，再把已开启通道以26行横向条带送入DMA队列。 */
static bool wave_queue_render_snapshot(uint16_t *pixels,
                                       const wave_frame_snapshot_t *snapshot);

/** 把一路500列状态绘制到与该通道相交的26行RGB565条带。 */
static void wave_render_channel_tile(uint16_t *tile_pixels,
                                     uint32_t tile_y,
                                     uint32_t tile_height,
                                     uint32_t tile_width,
                                     const wave_frame_channel_t *channel,
                                     const uint16_t *columns,
                                     uint32_t valid_columns,
                                     uint8_t initial_levels);

/*============================== 模块状态 ==============================*/

static wave_context_t s_wave;
static uint16_t s_task_columns[WAVE_CAPTURE_DATA_COLUMN_COUNT];
/*
 * 两块512×26像素横向条带供准备任务与DMA任务乒乓使用。共享uint16_t
 * 列数组的每个元素同时携带六路2 bit状态，只在每帧查询一次。
 */
__attribute__((aligned(WAVE_DMA_CACHE_LINE_BYTES)))
static uint16_t s_wave_dma_buffers[WAVE_DMA_BUFFER_COUNT]
                                  [WAVE_DMA_BUFFER_CAPACITY_PIXELS];
static SemaphoreHandle_t s_wave_frame_semaphore;
static SemaphoreHandle_t s_wave_dma_done_semaphore;
static SemaphoreHandle_t s_wave_frame_done_semaphore;
static QueueHandle_t s_wave_free_buffer_queue;
static QueueHandle_t s_wave_ready_job_queue;
static TaskHandle_t s_wave_prepare_task_handle;
static TaskHandle_t s_wave_dma_task_handle;
static volatile uint32_t s_latest_frame_tick;
static volatile uint32_t s_wave_frame_divider_count;
static volatile bool s_gui_update_pending;
static volatile bool s_dma_transfer_success;
static volatile bool s_dma_frame_success;
static bool s_frame_callback_started;

/* 旧的两帧固定样本保留作数据格式参考；动态模拟采集不再编译或使用它。 */
#if 0
static const uint16_t s_static_waveforms[WAVE_STATIC_FRAME_COUNT][WAVE_PACKED_COLUMN_COUNT] =
{
    {
        0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0010, 0X0020, 0X0020, 
        0X0020, 0X0020, 0X0010, 0X0010, 0X0020, 0X0010, 0X0020, 0X0020, 
        0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 0X0020, 0X0020, 0X0020, 
        0X0020, 0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 
        0X0010, 0X0010, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 
        0X0020, 0X0020, 0X0010, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0010, 0X0020, 0X0020, 0X0020, 0X0020, 0X0010, 0X0020, 0X0020, 
        0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 
        0X0010, 0X0010, 0X0010, 0X0020, 0X0010, 0X0020, 0X0010, 0X0010, 
        0X0010, 0X0020, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0010, 
        0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 
        0X0020, 0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0020, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0010, 0X0010, 0X0020, 0X0020, 0X0010, 0X0010, 
        0X0010, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 
        0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 
        0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0010, 
        0X0020, 0X0020, 0X0010, 0X0010, 0X0020, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 
        0X0020, 0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 
        0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 
        0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 
        0X0010, 0X0010, 0X0010, 0X0020, 0X0010, 0X0020, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0010, 0X0020, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 
        0X0020, 0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0010, 0X0020, 0X0010, 
        0X0010, 0X0020, 0X0010, 0X0010, 0X0020, 0X0010, 0X0020, 0X0010, 
        0X0010, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 
        0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 
        0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 
        0X0020, 0X0020, 0X0010, 0X0020, 0X0020, 0X0010, 0X0010, 0X0020, 
        0X0010, 0X0010, 0X0020, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 
        0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 0X0020, 0X0020, 0X0020, 
        0X0020, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 
        0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 
        0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 0X0020, 
        0X0020, 0X0010, 0X0020, 0X0010, 0X0010, 0X0020, 0X0010, 0X0010, 
        0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 
        0X0020, 0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0010, 0X0010, 0X0010, 0X0020, 0X0010, 0X0010, 0X0020, 
        0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0020, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 
        0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 
        0X0010, 0X0010, 0X0010, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 0X0020, 
        0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0020, 0X0020, 0X0020, 
        0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0010, 0X0020, 0X0020, 0X0020, 0X0010, 0X0010, 
        0X0010, 0X0020, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 0X0010, 
        0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0020, 0X0010, 
        0X0010, 0X0010, 0X0010, 0X0020
    },
    {
        0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 
        0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 
        0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 
        0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 
        0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 
        0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 
        0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 
        0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 0X0020, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0010, 0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0020, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0020, 0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 0X0010, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0020, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 
        0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 
        0X0000, 0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0020, 0X0000, 
        0X0000, 0X0000, 0X0010, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 
        0X0000, 0X0000, 0X0000, 0X0020
    }
};
#endif

static const uint32_t s_default_colors[WAVE_CHANNEL_COUNT] =
{
    0x37D67AU, 0x4BA3FFU, 0xFFB84DU,
    0xE36BFFU, 0x62E8D5U, 0xFF7A90U
};

/*============================== 对外函数 ==============================*/

bool wave_display_task_create(void)
{
    uint8_t buffer_index;

    if(s_wave_prepare_task_handle != NULL && s_wave_dma_task_handle != NULL) {
        return true;
    }

    s_wave_frame_semaphore = xSemaphoreCreateBinary();
    s_wave_dma_done_semaphore = xSemaphoreCreateBinary();
    s_wave_frame_done_semaphore = xSemaphoreCreateBinary();
    s_wave_free_buffer_queue = xQueueCreate(WAVE_DMA_BUFFER_COUNT,
                                            sizeof(buffer_index));
    s_wave_ready_job_queue = xQueueCreate(WAVE_DMA_BUFFER_COUNT,
                                          sizeof(wave_dma_job_t));
    if(s_wave_frame_semaphore == NULL ||
       s_wave_dma_done_semaphore == NULL ||
       s_wave_frame_done_semaphore == NULL ||
       s_wave_free_buffer_queue == NULL ||
       s_wave_ready_job_queue == NULL) {
        goto create_failed;
    }

    /* 空闲队列携带缓冲编号，避免两个任务同时取得同一块乒乓缓冲。 */
    for(buffer_index = 0U; buffer_index < WAVE_DMA_BUFFER_COUNT; buffer_index++) {
        if(xQueueSend(s_wave_free_buffer_queue, &buffer_index, 0U) != pdPASS) {
            goto create_failed;
        }
    }

    if(xTaskCreate(wave_dma_send_task,
                   "wave_dma",
                   WAVE_DMA_TASK_STACK,
                   NULL,
                   WAVE_DMA_TASK_PRIORITY,
                   &s_wave_dma_task_handle) != pdPASS) {
        goto create_failed;
    }

    if(xTaskCreate(wave_prepare_task,
                   "wave_prep",
                   WAVE_PREPARE_TASK_STACK,
                   NULL,
                   WAVE_PREPARE_TASK_PRIORITY,
                   &s_wave_prepare_task_handle) != pdPASS) {
        goto create_failed;
    }

    return true;

create_failed:
    if(s_wave_prepare_task_handle != NULL) {
        vTaskDelete(s_wave_prepare_task_handle);
        s_wave_prepare_task_handle = NULL;
    }
    if(s_wave_dma_task_handle != NULL) {
        vTaskDelete(s_wave_dma_task_handle);
        s_wave_dma_task_handle = NULL;
    }
    if(s_wave_ready_job_queue != NULL) {
        vQueueDelete(s_wave_ready_job_queue);
        s_wave_ready_job_queue = NULL;
    }
    if(s_wave_free_buffer_queue != NULL) {
        vQueueDelete(s_wave_free_buffer_queue);
        s_wave_free_buffer_queue = NULL;
    }
    if(s_wave_frame_done_semaphore != NULL) {
        vSemaphoreDelete(s_wave_frame_done_semaphore);
        s_wave_frame_done_semaphore = NULL;
    }
    if(s_wave_dma_done_semaphore != NULL) {
        vSemaphoreDelete(s_wave_dma_done_semaphore);
        s_wave_dma_done_semaphore = NULL;
    }
    if(s_wave_frame_semaphore != NULL) {
        vSemaphoreDelete(s_wave_frame_semaphore);
        s_wave_frame_semaphore = NULL;
    }
    return false;
}

/**
 * @brief wave_display_task_start：Layer 1 波形配置、绘制和帧提交。
 */
void wave_display_task_start(void)
{
    if(s_wave_frame_semaphore == NULL || s_frame_callback_started) {
        return;
    }

    s_frame_callback_started = true;
    s_wave_frame_divider_count = 0U;
    bsp_ltdc_lcd_set_frame_callback(wave_frame_start_isr, NULL);

    /* 不必等待下一帧才显示初始化结果；首次绘制也走同一个任务入口。 */
    s_latest_frame_tick = (uint32_t)xTaskGetTickCount();
    (void)xSemaphoreGive(s_wave_frame_semaphore);
}

/**
 * @brief wave_process_gui_updates：Layer 1 波形配置、绘制和帧提交。
 */
void wave_process_gui_updates(void)
{
    bool invalidate;

    taskENTER_CRITICAL();
    invalidate = s_gui_update_pending;
    s_gui_update_pending = false;
    taskEXIT_CRITICAL();

    if(invalidate && s_wave.time_ruler != NULL) {
        lv_obj_invalidate(s_wave.time_ruler);
    }
}

/**
 * @brief wave_frame_start_isr：Layer 1 波形配置、绘制和帧提交。
 */
static void wave_frame_start_isr(void *context)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)context;
    if(s_wave_frame_semaphore == NULL) {
        return;
    }

#if WAVE_REFRESH_FRAME_DIVIDER < 1U
#error "WAVE_REFRESH_FRAME_DIVIDER must be at least 1"
#endif
    /* LTDC继续逐帧扫描，仅降低波形准备和DMA2D的启动频率。 */
    s_wave_frame_divider_count++;
    if(s_wave_frame_divider_count < WAVE_REFRESH_FRAME_DIVIDER) {
        return;
    }
    s_wave_frame_divider_count = 0U;

    /* 覆盖最新帧时刻；二值信号量已满时不会累计过期帧。 */
    s_latest_frame_tick = (uint32_t)xTaskGetTickCountFromISR();
    (void)xSemaphoreGiveFromISR(s_wave_frame_semaphore,
                                &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * @brief wave_prepare_task：Layer 1 波形配置、绘制和帧提交。
 */
static void wave_prepare_task(void *parameter)
{
    (void)parameter;

    for(;;) {
        uint32_t frame_tick;

        (void)xSemaphoreTake(s_wave_frame_semaphore, portMAX_DELAY);
        frame_tick = s_latest_frame_tick;

#if FRAME_TEST_WAVE_SDRAM_DMA_FILL_ENABLE
        if(wave_sdram_dma_fill_test_is_active()) {
            (void)wave_run_sdram_dma_fill_test();
            continue;
        }
#endif

        if(wave_render_frame_at_tick(frame_tick)) {
            taskENTER_CRITICAL();
            s_gui_update_pending = true;
            taskEXIT_CRITICAL();
        }
    }
}

/**
 * @brief wave_dma_send_task：Layer 1 波形配置、绘制和帧提交。
 */
static void wave_dma_send_task(void *parameter)
{
    wave_dma_job_t job;
    bool frame_success = false;

    (void)parameter;

    for(;;) {
        bool transfer_started;
        bool transfer_success = false;
        uint32_t cycle_start = 0U;

        (void)xQueueReceive(s_wave_ready_job_queue, &job, portMAX_DELAY);
        if(job.first_in_frame) {
            frame_success = true;
        }
        frame_success = frame_success && job.content_valid;

        if(job.measure_pixels != 0U) {
            cycle_start = platform_dwt_cycles();
        }

        s_dma_transfer_success = false;
        if(job.type == WAVE_DMA_JOB_FILL) {
            transfer_started = bsp_dma2d_fill_rgb565_async(
                job.destination, job.width, job.height,
                job.destination_stride, 0x0000U,
                wave_dma_complete_isr, NULL);
        }
        else {
#if WAVE_TRANSFER_USE_DMA2D
            transfer_started = bsp_dma2d_copy_rgb565_async(
                job.source, job.destination,
                job.width, job.height, job.destination_stride,
                wave_dma_complete_isr, NULL);
#else
            transfer_started = wave_copy_job_by_cpu(&job);
            transfer_success = transfer_started;
#endif
        }

#if WAVE_TRANSFER_USE_DMA2D
        if(transfer_started &&
           xSemaphoreTake(s_wave_dma_done_semaphore, portMAX_DELAY) == pdTRUE) {
            transfer_success = s_dma_transfer_success;
        }
#else
        /* R2M测试仍使用DMA2D；正常COPY任务已经由同步memcpy完成。 */
        if(job.type == WAVE_DMA_JOB_FILL && transfer_started &&
           xSemaphoreTake(s_wave_dma_done_semaphore, portMAX_DELAY) == pdTRUE) {
            transfer_success = s_dma_transfer_success;
        }
#endif

        if(job.measure_pixels != 0U && transfer_success) {
            frame_test_record_sdram_write(platform_dwt_elapsed(cycle_start),
                                          job.measure_pixels);
        }

        /* DMA读完后才归还源缓冲，准备任务此时才能安全覆盖它。 */
        if(job.buffer_index != WAVE_DMA_NO_BUFFER && job.release_buffer) {
            (void)xQueueSend(s_wave_free_buffer_queue,
                             &job.buffer_index, portMAX_DELAY);
        }

        frame_success = frame_success && transfer_success;
        if(job.last_in_frame) {
#if WAVE_LTDC_PRESENT_ENABLE
            if(frame_success) {
                frame_success = bsp_ltdc_lcd_wave_layer_present();
            }
#endif
            s_dma_frame_success = frame_success;
            (void)xSemaphoreGive(s_wave_frame_done_semaphore);
        }
#if WAVE_DMA_INTER_JOB_DELAY_MS > 0U
        else {
            /*
             * DMA2D完成中断只负责唤醒本任务。下一笔不在ISR中立即启动，
             * 而是在任务上下文让出一个RTOS节拍，让LTDC优先补足其FIFO。
             * 条带任务仍属于同一帧，最后一笔完成后才请求VBlank换帧。
             */
            vTaskDelay(pdMS_TO_TICKS(WAVE_DMA_INTER_JOB_DELAY_MS));
        }
#endif
    }
}

/**
 * @brief wave_copy_job_by_cpu：Layer 1 波形配置、绘制和帧提交。
 */
static bool wave_copy_job_by_cpu(const wave_dma_job_t *job)
{
    const uint16_t *source;
    uint32_t row;

    if(job == NULL || job->type != WAVE_DMA_JOB_COPY ||
       job->buffer_index >= WAVE_DMA_BUFFER_COUNT ||
       job->source == NULL || job->destination == NULL ||
       job->width == 0U || job->height == 0U ||
       job->destination_stride < job->width) {
        return false;
    }

    source = job->source;
    for(row = 0U; row < job->height; row++) {
        memcpy(&job->destination[row * job->destination_stride],
               &source[row * job->width],
               (size_t)job->width * sizeof(source[0]));
    }

    /* 确保所有CPU写入在提交LTDC垂直消隐换帧之前完成。 */
    __DSB();
    return true;
}

/**
 * @brief wave_dma_complete_isr：Layer 1 波形配置、绘制和帧提交。
 */
static void wave_dma_complete_isr(void *context, bool success)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)context;
    s_dma_transfer_success = success;
    if(s_wave_dma_done_semaphore != NULL) {
        (void)xSemaphoreGiveFromISR(s_wave_dma_done_semaphore,
                                    &higher_priority_task_woken);
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * @brief wave_sdram_dma_fill_test_is_active：Layer 1 波形配置、绘制和帧提交。
 */
static bool wave_sdram_dma_fill_test_is_active(void)
{
#if !WAVE_SDRAM_DMA_FILL_TEST_ENABLE
    return false;
#else
    bool active;

    taskENTER_CRITICAL();
    active = (s_wave.state == WAVE_STATE_CONFIGURED) &&
             s_wave.configuration_applied &&
             (s_wave.captured_end_cnt == 0U);
    taskEXIT_CRITICAL();
    return active;
#endif
}

/**
 * @brief wave_run_sdram_dma_fill_test：Layer 1 波形配置、绘制和帧提交。
 */
static bool wave_run_sdram_dma_fill_test(void)
{
    wave_dma_job_t job = {0};
    uint16_t *pixels = bsp_ltdc_lcd_wave_layer_get_back_buffer();
    uint32_t pixel_count;

    if(pixels == NULL) {
        return false;
    }

    pixel_count = (uint32_t)bsp_ltdc_lcd_wave_layer_get_width() *
                  bsp_ltdc_lcd_wave_layer_get_height();
    job.type = WAVE_DMA_JOB_FILL;
    job.buffer_index = WAVE_DMA_NO_BUFFER;
    job.width = bsp_ltdc_lcd_wave_layer_get_width();
    job.height = bsp_ltdc_lcd_wave_layer_get_height();
    job.destination_stride = bsp_ltdc_lcd_wave_layer_get_stride();
    job.destination = pixels;
    job.measure_pixels = pixel_count;
    job.first_in_frame = true;
    job.last_in_frame = true;
    job.content_valid = true;

    if(xQueueSend(s_wave_ready_job_queue, &job, portMAX_DELAY) != pdPASS ||
       xSemaphoreTake(s_wave_frame_done_semaphore, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    return s_dma_frame_success;
}

/**
 * @brief wave_make_frame_snapshot：Layer 1 波形配置、绘制和帧提交。
 */
static bool wave_make_frame_snapshot(uint32_t frame_tick,
                                     wave_frame_snapshot_t *snapshot)
{
    uint32_t available_end_cnt;
    uint32_t channel_index;
    bool view_changed = false;
    bool should_render;

    if(snapshot == NULL) {
        return false;
    }

    available_end_cnt = wave_capture_data_get_cnt_at_tick(frame_tick);

    taskENTER_CRITICAL();
    if(s_wave.state == WAVE_STATE_UNINITIALIZED ||
       !s_wave.configuration_applied) {
        taskEXIT_CRITICAL();
        return false;
    }

    if(s_wave.state == WAVE_STATE_CAPTURING) {
        s_wave.captured_end_cnt = available_end_cnt;
    }

    if(s_wave.view_dirty) {
        s_wave.pending_view_start_cnt = wave_clamp_view_start(
            s_wave.pending_view_start_cnt,
            s_wave.pending_window_size,
            available_end_cnt);
        s_wave.view.start_cnt = s_wave.pending_view_start_cnt;
        s_wave.view.window_size = s_wave.pending_window_size;
        s_wave.view.now_cnt = s_wave.view.start_cnt + s_wave.view.window_size;
        s_wave.view_dirty = false;
        view_changed = true;
    }
    else if(s_wave.state == WAVE_STATE_CAPTURING && s_wave.follow_live) {
        wave_update_view(available_end_cnt);
        view_changed = true;
    }

    should_render = s_wave.render_requested || view_changed ||
                    s_wave.clear_frames_remaining != 0U;
    if(!should_render) {
        taskEXIT_CRITICAL();
        return false;
    }

    s_wave.render_requested = false;
    snapshot->view = s_wave.view;
    snapshot->clear_background = s_wave.clear_frames_remaining != 0U;
    for(channel_index = 0U; channel_index < WAVE_CHANNEL_COUNT; channel_index++) {
        const wave_channel_t *channel = &s_wave.channels[channel_index];
        wave_frame_channel_t *destination = &snapshot->channels[channel_index];

        destination->enabled = channel->config.enabled;
        destination->source_id = channel->source_id;
        destination->color = channel->config.color;
        destination->high_y = (int16_t)(channel->layout.top +
                                         channel->layout.high_offset);
        destination->low_y = (int16_t)(channel->layout.top +
                                        channel->layout.low_offset);
    }
    taskEXIT_CRITICAL();
    return true;
}

/**
 * @brief wave_render_frame_at_tick：Layer 1 波形配置、绘制和帧提交。
 */
static bool wave_render_frame_at_tick(uint32_t frame_tick)
{
    wave_frame_snapshot_t snapshot;
    uint16_t *pixels;

    /* 换帧尚未完成时不准备数据，也不触碰正在等待锁存的后缓冲。 */
    pixels = bsp_ltdc_lcd_wave_layer_get_back_buffer();
    if(pixels == NULL || !wave_make_frame_snapshot(frame_tick, &snapshot)) {
        return false;
    }

    if(!wave_queue_render_snapshot(pixels, &snapshot)) {
        taskENTER_CRITICAL();
        s_wave.render_requested = true;
        taskEXIT_CRITICAL();
        return false;
    }

    if(snapshot.clear_background) {
        taskENTER_CRITICAL();
        if(s_wave.clear_frames_remaining != 0U) {
            s_wave.clear_frames_remaining--;
        }
        taskEXIT_CRITICAL();
    }

    return true;
}

/**
 * @brief wave_queue_render_snapshot：Layer 1 波形配置、绘制和帧提交。
 */
static bool wave_queue_render_snapshot(uint16_t *pixels,
                                       const wave_frame_snapshot_t *snapshot)
{
    const uint32_t width = bsp_ltdc_lcd_wave_layer_get_width();
    const uint32_t height = bsp_ltdc_lcd_wave_layer_get_height();
    const uint32_t stride = bsp_ltdc_lcd_wave_layer_get_stride();
    wave_dma_job_t job = {0};
    uint32_t channel_index;
    uint32_t last_enabled = WAVE_CHANNEL_COUNT;
    uint32_t valid_columns = 0U;
    uint8_t initial_levels = 0U;
    bool content_valid = true;
    bool first_channel_job = true;

    if(pixels == NULL || snapshot == NULL || width == 0U ||
       width > WAVE_AREA_WIDTH || height == 0U ||
       stride != WAVE_DMA_TILE_STRIDE_PIXELS) {
        return false;
    }

    for(channel_index = 0U; channel_index < WAVE_CHANNEL_COUNT; channel_index++) {
        if(snapshot->channels[channel_index].enabled) last_enabled = channel_index;
    }
    if(last_enabled == WAVE_CHANNEL_COUNT) return false;

    /* 固定一次采集快照，六路通道都从相同的s_tick/e_tick窗口取得。 */
    if(!wave_capture_data_request_window(snapshot->view.start_cnt,
                                         snapshot->view.now_cnt,
                                         snapshot->view.window_size,
                                         s_task_columns,
                                         &valid_columns,
                                         &initial_levels)) {
        return false;
    }
    if(valid_columns > WAVE_AREA_WIDTH) valid_columns = WAVE_AREA_WIDTH;

    /*
     * 通道布局改变时清空当前后缓冲。这里不能再用一次444行的DMA2D填充：
     * 即使普通波形按26行切片，该整块任务仍会连续占用SDRAM总线，足以让
     * 同时扫描Layer 0和Layer 1的LTDC发生FIFO欠载。后缓冲此刻不被LTDC
     * 扫描，CPU清零安全且仅在修改通道配置时发生。
     */
    if(snapshot->clear_background) {
        memset(pixels, 0, stride * height * sizeof(pixels[0]));
        __DSB();
    }

    for(channel_index = 0U; channel_index <= last_enabled; channel_index++) {
        const wave_frame_channel_t *channel = &snapshot->channels[channel_index];
        uint32_t tile_y;
        uint32_t tile_height;
        uint8_t buffer_index;
        uint16_t *tile_buffer;

        if(!channel->enabled) continue;

        /*
         * 每路只发送高、低电平之间的一块横向条带。800x480布局中两条电平线
         * 相差25像素，所以包含首尾两行后恰好为26行。
         */
        if(channel->high_y <= channel->low_y) {
            tile_y = (uint32_t)(channel->high_y - WAVE_TIME_RULER_HEIGHT);
            tile_height = (uint32_t)(channel->low_y - channel->high_y) + 1U;
        }
        else {
            tile_y = (uint32_t)(channel->low_y - WAVE_TIME_RULER_HEIGHT);
            tile_height = (uint32_t)(channel->high_y - channel->low_y) + 1U;
        }
        if(tile_height != WAVE_DMA_TILE_LINES ||
           tile_y >= height || tile_y + tile_height > height) {
            content_valid = false;
            tile_y = 0U;
            tile_height = (height < WAVE_DMA_TILE_LINES) ?
                height : WAVE_DMA_TILE_LINES;
        }

        if(xQueueReceive(s_wave_free_buffer_queue,
                         &buffer_index, portMAX_DELAY) != pdPASS) {
            return false;
        }
        tile_buffer = s_wave_dma_buffers[buffer_index];
        memset(tile_buffer, 0,
               stride * tile_height * sizeof(tile_buffer[0]));
        if(content_valid) {
            wave_render_channel_tile(tile_buffer, tile_y, tile_height, stride,
                                     channel, s_task_columns,
                                     valid_columns, initial_levels);
        }

        job.type = WAVE_DMA_JOB_COPY;
        job.buffer_index = buffer_index;
        job.width = (uint16_t)stride;
        job.height = (uint16_t)tile_height;
        job.destination_stride = (uint16_t)stride;
        job.source = tile_buffer;
        job.destination = &pixels[tile_y * stride];
        job.first_in_frame = first_channel_job;
        job.last_in_frame = channel_index == last_enabled;
        job.release_buffer = true;
        job.content_valid = content_valid;
        if(xQueueSend(s_wave_ready_job_queue, &job,
                      portMAX_DELAY) != pdPASS) return false;
        first_channel_job = false;
    }

    /* 最后一个开启通道的26行DMA完成并提交VBlank后，才准备下一帧。 */
    if(xSemaphoreTake(s_wave_frame_done_semaphore, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    return s_dma_frame_success && content_valid;
}

/**
 * @brief 把一路500列状态绘制到当前26行横向RGB565条带。
 */
static void wave_render_channel_tile(uint16_t *tile_pixels,
                                     uint32_t tile_y,
                                     uint32_t tile_height,
                                     uint32_t tile_width,
                                     const wave_frame_channel_t *channel,
                                     const uint16_t *columns,
                                     uint32_t valid_columns,
                                     uint8_t initial_levels)
{
    const int32_t high_y = channel->high_y - WAVE_TIME_RULER_HEIGHT;
    const int32_t low_y = channel->low_y - WAVE_TIME_RULER_HEIGHT;
    const int32_t tile_bottom = (int32_t)(tile_y + tile_height - 1U);
    const uint16_t color = wave_color_to_rgb565(channel->color);
    const uint32_t bit_shift = (uint32_t)channel->source_id * 2U;
    bool level = ((initial_levels >> channel->source_id) & 0x01U) != 0U;
    uint32_t x;

    for(x = 0U; x < valid_columns; x++) {
        const uint8_t code = (uint8_t)((columns[x] >> bit_shift) & 0x03U);
        const int32_t level_y = level ? high_y : low_y;

        if(level_y >= (int32_t)tile_y && level_y <= tile_bottom) {
            tile_pixels[(uint32_t)(level_y - (int32_t)tile_y) * tile_width + x] = color;
        }
        if(code == 0x01U || code == 0x02U) {
            const bool after = code == 0x02U;
            int32_t y1;
            int32_t y2;
            int32_t y;

            if(after == level) {
                y1 = high_y < low_y ? high_y : low_y;
                y2 = high_y > low_y ? high_y : low_y;
            }
            else {
                const int32_t after_y = after ? high_y : low_y;
                y1 = level_y < after_y ? level_y : after_y;
                y2 = level_y > after_y ? level_y : after_y;
            }
            if(y1 < (int32_t)tile_y) y1 = (int32_t)tile_y;
            if(y2 > tile_bottom) y2 = tile_bottom;
            for(y = y1; y <= y2; y++) {
                tile_pixels[(uint32_t)(y - (int32_t)tile_y) * tile_width + x] = color;
            }
            level = after;
        }
    }
}

/**
 * @brief 初始化 wave 模块的固定资源。
 *
 * 只保存父对象和显示高度，并为六路物理通道生成默认配置。根容器和具体
 * 通道控件延迟到 wave_apply_configuration() 创建，方便主任务安排层级。
 */
bool wave_init(lv_obj_t * parent)
{
    lv_display_t * display;
    int32_t display_width;
    int32_t display_height;
    uint32_t channel_index;

    if(parent == NULL || s_wave.state != WAVE_STATE_UNINITIALIZED) {
        return false;
    }

    display = lv_obj_get_display(parent);
    if(display == NULL) {
        return false;
    }

    display_height = lv_display_get_vertical_resolution(display);
    display_width = lv_display_get_horizontal_resolution(display);
    if(display_width < (WAVE_AREA_X + WAVE_AREA_WIDTH) ||
       display_height <= WAVE_TIME_RULER_HEIGHT) {
        return false;
    }

    memset(&s_wave, 0, sizeof(s_wave));
    wave_capture_data_init();
    s_wave.parent = parent;
    s_wave.display_height = display_height;
    s_wave.view.window_size = WAVE_WINDOW_5S_CNT;
    s_wave.live_window_size = WAVE_WINDOW_5S_CNT;
    s_wave.capture_duration_cnt = WAVE_CAPTURE_DEFAULT_CNT;

    /*
     * Layer 0 继续由 LVGL 的小 RAM 绘制缓冲驱动；Layer 1 只覆盖标尺下方
     * 的 500 像素波形区，拥有两块 SDRAM RGB565 缓冲并在 VSYNC 时交换。
     */
    if(!bsp_ltdc_lcd_wave_layer_init(WAVE_AREA_X,
                                     WAVE_TIME_RULER_HEIGHT,
                                     WAVE_AREA_WIDTH,
                                     (uint16_t)(display_height - WAVE_TIME_RULER_HEIGHT),
                                     0U)) {
        memset(&s_wave, 0, sizeof(s_wave));
        return false;
    }

    for(channel_index = 0U; channel_index < WAVE_CHANNEL_COUNT; channel_index++) {
        wave_init_default_config(&s_wave.channels[channel_index], channel_index);
    }

    s_wave.state = WAVE_STATE_CONFIGURED;
    return true;
}

/**
 * @brief 释放 wave 模块创建的界面对象并恢复未初始化状态。
 *
 * 先删除波形根容器，再删除左侧根容器；其所有子对象由 LVGL 自动删除。
 */
void wave_deinit(void)
{
    if(s_wave.wave_root != NULL) {
        lv_obj_delete(s_wave.wave_root);
    }
    if(s_wave.left_root != NULL) {
        lv_obj_delete(s_wave.left_root);
    }

    memset(&s_wave, 0, sizeof(s_wave));
}

/**
 * @brief 取得一路通道配置的副本。
 *
 * 返回副本而不是内部指针，防止外部绕过状态检查直接改写模块数据。
 */
bool wave_get_channel_config(uint32_t channel_index,
                             wave_channel_config_t * config)
{
    if(config == NULL || channel_index >= WAVE_CHANNEL_COUNT ||
       s_wave.state == WAVE_STATE_UNINITIALIZED) {
        return false;
    }

    *config = s_wave.channels[channel_index].config;
    return true;
}

/**
 * @brief 保存一路新的通道配置。
 *
 * 本函数只保存物理通道属性，不立即操作 LVGL 对象；显示坐标属于模块内部
 * 数据，由 wave_apply_configuration() 根据启用顺序统一重新计算。
 */
bool wave_set_channel_config(uint32_t channel_index,
                             const wave_channel_config_t * config)
{
    wave_channel_config_t * destination;

    if(config == NULL || channel_index >= WAVE_CHANNEL_COUNT ||
       s_wave.state != WAVE_STATE_CONFIGURED) {
        return false;
    }

    destination = &s_wave.channels[channel_index].config;
    *destination = *config;

    /* 无论调用者怎样构造配置，都保证内部字符串有结尾。 */
    destination->pin_text[WAVE_PIN_TEXT_SIZE - 1U] = '\0';
    destination->name_text[WAVE_NAME_TEXT_SIZE - 1U] = '\0';
    destination->detail_text[WAVE_DETAIL_TEXT_SIZE - 1U] = '\0';
    return true;
}

/**
 * @brief 保存一路通道的启用状态。
 *
 * 该接口供“通道选择”按钮使用，只修改配置，不直接操作 LVGL 对象。
 */
bool wave_set_channel_enabled(uint32_t channel_index, bool enabled)
{
    uint32_t enabled_count = 0U;
    uint32_t index;

    if(channel_index >= WAVE_CHANNEL_COUNT ||
       s_wave.state != WAVE_STATE_CONFIGURED) {
        return false;
    }

    if(!enabled && s_wave.channels[channel_index].config.enabled) {
        for(index = 0U; index < WAVE_CHANNEL_COUNT; index++) {
            if(s_wave.channels[index].config.enabled) {
                enabled_count++;
            }
        }

        /* 波形区至少保留一路，避免删除最后一路后失去可用显示轨道。 */
        if(enabled_count <= 1U) {
            return false;
        }
    }

    s_wave.channels[channel_index].config.enabled = enabled;
    return true;
}

/**
 * @brief 根据已保存配置一次性重建 0~599 区域。
 *
 * 删除旧通道控件并重建时间标尺，只为 enabled 通道创建左侧信息行和波形
 * 轨道。已采集的 CNT 数据和当前显示窗口保持不变，最后由已有数据重画
 * 剩余通道，不能先提交空白 Layer 1。
 */
bool wave_apply_configuration(void)
{
    uint32_t channel_index;
    uint32_t display_slot = 0U;
    bool had_configuration;

    if(s_wave.state != WAVE_STATE_CONFIGURED || s_wave.parent == NULL) {
        return false;
    }

    /* 第一次提交配置时才创建根容器，使主任务可以先创建右侧静态页面。 */
    if(s_wave.left_root == NULL || s_wave.wave_root == NULL) {
        if(!wave_create_roots(s_wave.parent, s_wave.display_height)) {
            return false;
        }
    }

    /* 配置只在停止状态提交；只清理 LVGL 控件，绝不清空已申请的采集数据。 */
    had_configuration = s_wave.configuration_applied;
    s_wave.configuration_applied = false;
    lv_obj_clean(s_wave.left_root);
    lv_obj_clean(s_wave.wave_root);
    s_wave.time_ruler = NULL;

    for(channel_index = 0U; channel_index < WAVE_CHANNEL_COUNT; channel_index++) {
        wave_reset_render_handles(&s_wave.channels[channel_index]);
    }

    if(!wave_create_time_ruler()) {
        return false;
    }

    for(channel_index = 0U; channel_index < WAVE_CHANNEL_COUNT; channel_index++) {
        wave_channel_t * channel = &s_wave.channels[channel_index];

        if(channel->config.enabled) {
            wave_assign_display_slot(channel, display_slot);
            display_slot++;

            if(!wave_create_channel_widgets(channel)) {
                return false;
            }
        }
    }

    s_wave.configuration_applied = true;
    if(had_configuration) {
        /* 两个后续帧分别清理当前前、后波形缓冲，防止关闭通道后残留。 */
        s_wave.clear_frames_remaining = 2U;
    }

    /* Layer 1 由独立波形任务在下一次帧信号到来时重建。 */
    s_wave.render_requested = true;

    /* 一次性清除旧控件残留；后续正常刷新只失效波形对象。 */
    lv_obj_invalidate(s_wave.left_root);
    lv_obj_invalidate(s_wave.wave_root);
    return true;
}

/**
 * @brief 开始一轮新的波形采集和显示。
 *
 * 保存定时器起点，重置六路本帧坐标并启动大容量 CNT 数据源。
 * 左侧通道信息不变，因此无需重新创建或刷新。
 */
bool wave_start_capture(uint32_t timer_start_cnt)
{
    if(s_wave.state != WAVE_STATE_CONFIGURED ||
       !s_wave.configuration_applied) {
        return false;
    }

    s_wave.capture_timer_start_cnt = timer_start_cnt;
    s_wave.view.start_cnt = 0U;
    s_wave.view.now_cnt = 0U;
    s_wave.view.window_size = s_wave.live_window_size;
    s_wave.captured_end_cnt = 0U;
    s_wave.pending_view_start_cnt = 0U;
    s_wave.pending_window_size = s_wave.live_window_size;
    s_wave.follow_live = true;
    s_wave.view_dirty = false;
    if(s_wave.time_ruler != NULL) {
        lv_obj_invalidate(s_wave.time_ruler);
    }

    if(!wave_capture_data_begin(s_wave.capture_duration_cnt)) {
        return false;
    }
    s_wave.last_live_range_end_cnt = 0U;
    s_wave.state = WAVE_STATE_CAPTURING;
    /* 新一轮第一帧会用各通道完整26行条带覆盖上一轮对应波形。 */
    s_wave.render_requested = true;

    /* 开始新一轮采集时只请求波形重建，左侧配置无需重画。 */
    lv_obj_invalidate(s_wave.wave_root);
    return true;
}

/**
 * @brief 用实际已采集时长准备最后一帧总览并正常结束采集。
 *
 * 先把窗口宽度临时切换成实际采集 CNT，再用采集终点刷新。这样
 * wave_update_view() 得到 start_cnt=0，所有边沿会重新映射到 100~599。
 */
bool wave_complete_capture(void)
{
    if(s_wave.state != WAVE_STATE_CAPTURING ||
       s_wave.capture_duration_cnt == 0U) {
        return false;
    }

    return wave_finish_capture(wave_capture_data_get_now_cnt());
}

/**
 * @brief wave_pause_capture：Layer 1 波形配置、绘制和帧提交。
 */
bool wave_pause_capture(void)
{
    uint32_t pause_cnt;

    if(s_wave.state != WAVE_STATE_CAPTURING) {
        return false;
    }

    pause_cnt = wave_capture_data_get_now_cnt();
    return wave_finish_capture(pause_cnt);
}

/**
 * @brief wave_finish_capture：Layer 1 波形配置、绘制和帧提交。
 */
static bool wave_finish_capture(uint32_t end_cnt)
{
    if(s_wave.state != WAVE_STATE_CAPTURING) {
        return false;
    }

    if(end_cnt > s_wave.capture_duration_cnt) {
        end_cnt = s_wave.capture_duration_cnt;
    }

    /* stop() 会锁存当前 CNT，之后范围申请只暴露这一时刻之前的数据。 */
    wave_capture_data_stop();
    s_wave.captured_end_cnt = end_cnt;
    s_wave.view.start_cnt = 0U;
    s_wave.view.now_cnt = end_cnt;
    s_wave.view.window_size = (end_cnt == 0U) ? s_wave.live_window_size : end_cnt;
    s_wave.pending_view_start_cnt = 0U;
    s_wave.pending_window_size = s_wave.view.window_size;
    s_wave.follow_live = false;
    s_wave.view_dirty = false;
    s_wave.last_live_range_end_cnt = end_cnt;

    if(s_wave.time_ruler != NULL) {
        lv_obj_invalidate(s_wave.time_ruler);
    }
    s_wave.render_requested = true;
    s_wave.state = WAVE_STATE_CONFIGURED;
    return true;
}

/** @brief 停止接收刷新请求，结束本轮采集并保留最后一帧。 */
void wave_stop_capture(void)
{
    if(s_wave.state == WAVE_STATE_CAPTURING) {
        wave_capture_data_stop();
        s_wave.state = WAVE_STATE_CONFIGURED;
    }
}

/**
 * @brief 中途取消本轮采集并把波形区恢复为空白背景。
 *
 * 停止数据源并请求波形任务清空 Layer 1；左侧通道选择和通道信息保持不变。
 */
void wave_cancel_capture(void)
{
    if(s_wave.state != WAVE_STATE_CAPTURING) {
        return;
    }

    s_wave.view.start_cnt = 0U;
    s_wave.view.now_cnt = 0U;
    s_wave.view.window_size = s_wave.live_window_size;
    s_wave.captured_end_cnt = 0U;
    s_wave.pending_view_start_cnt = 0U;
    s_wave.pending_window_size = s_wave.live_window_size;
    s_wave.follow_live = false;
    s_wave.view_dirty = false;
    wave_capture_data_stop();
    s_wave.state = WAVE_STATE_CONFIGURED;

    s_wave.render_requested = true;

    if(s_wave.wave_root != NULL) {
        lv_obj_invalidate(s_wave.wave_root);
    }
}

/** @brief 查询模块是否正在采集，供按键逻辑和配置页面判断当前状态。 */
bool wave_is_capturing(void)
{
    return s_wave.state == WAVE_STATE_CAPTURING;
}

/** @brief 保存一轮采集时长；采集期间不允许修改。 */
bool wave_set_capture_duration(uint32_t duration_cnt)
{
    if(duration_cnt == 0U || s_wave.state != WAVE_STATE_CONFIGURED) {
        return false;
    }

    s_wave.capture_duration_cnt = duration_cnt;
    return true;
}

/** @brief 返回一轮采集时长，单位为 1 MHz 定时器 CNT。 */
uint32_t wave_get_capture_duration(void)
{
    return s_wave.capture_duration_cnt;
}

/** @brief 设置采集中的实时窗口，并在非采集状态立即应用该窗口。 */
void wave_set_window_size(uint32_t window_cnt)
{
    if(window_cnt != 0U) {
        s_wave.live_window_size = window_cnt;
        if(s_wave.state != WAVE_STATE_CAPTURING) {
            s_wave.view.window_size = window_cnt;
            s_wave.render_requested = true;
        }
    }
}

/**
 * @brief wave_pan_view_pixels：Layer 1 波形配置、绘制和帧提交。
 */
void wave_pan_view_pixels(int32_t delta_x_pixels)
{
    uint32_t available_end_cnt;
    uint32_t base_start_cnt;
    uint32_t base_window_cnt;
    int64_t delta_cnt;
    int64_t requested_start_cnt;

    if(s_wave.state == WAVE_STATE_UNINITIALIZED ||
       !s_wave.configuration_applied ||
       s_wave.view.window_size == 0U || delta_x_pixels == 0) {
        return;
    }

    available_end_cnt = (s_wave.state == WAVE_STATE_CAPTURING)
                      ? wave_capture_data_get_now_cnt()
                      : s_wave.captured_end_cnt;
    base_window_cnt = s_wave.view_dirty ? s_wave.pending_window_size
                                        : s_wave.view.window_size;
    if(available_end_cnt <= base_window_cnt) {
        return;
    }

    delta_cnt = ((int64_t)delta_x_pixels * base_window_cnt) /
                WAVE_AREA_WIDTH;
    base_start_cnt = s_wave.view_dirty ? s_wave.pending_view_start_cnt
                                       : s_wave.view.start_cnt;
    requested_start_cnt = (int64_t)base_start_cnt - delta_cnt;
    if(requested_start_cnt < 0) {
        requested_start_cnt = 0;
    }

    s_wave.pending_view_start_cnt = wave_clamp_view_start(
        (uint32_t)requested_start_cnt, base_window_cnt, available_end_cnt);
    if(s_wave.pending_view_start_cnt == base_start_cnt) {
        return;
    }

    /* 用户开始浏览历史，采集刷新不能再把窗口拉回最新位置。 */
    s_wave.follow_live = false;
    s_wave.view_dirty = true;
    s_wave.render_requested = true;
}

/**
 * @brief wave_zoom_view_at：Layer 1 波形配置、绘制和帧提交。
 */
void wave_zoom_view_at(int32_t anchor_x_pixels, bool zoom_in)
{
    uint32_t available_end_cnt;
    uint32_t base_start_cnt;
    uint32_t base_window_cnt;
    uint32_t new_window_cnt;
    uint32_t anchor_offset_pixels;
    uint32_t anchor_cnt;
    int64_t requested_start_cnt;

    if(s_wave.state == WAVE_STATE_UNINITIALIZED ||
       !s_wave.configuration_applied) {
        return;
    }

    available_end_cnt = (s_wave.state == WAVE_STATE_CAPTURING)
                      ? wave_capture_data_get_now_cnt()
                      : s_wave.captured_end_cnt;
    if(available_end_cnt == 0U) {
        return;
    }

    base_start_cnt = s_wave.view_dirty ? s_wave.pending_view_start_cnt
                                       : s_wave.view.start_cnt;
    base_window_cnt = s_wave.view_dirty ? s_wave.pending_window_size
                                        : s_wave.view.window_size;
    new_window_cnt = wave_get_zoom_window(base_window_cnt,
                                          available_end_cnt, zoom_in);
    if(new_window_cnt == base_window_cnt) {
        return;
    }

    if(anchor_x_pixels <= WAVE_AREA_X) {
        anchor_offset_pixels = 0U;
    }
    else if(anchor_x_pixels >= WAVE_AREA_X_END) {
        anchor_offset_pixels = WAVE_AREA_WIDTH;
    }
    else {
        anchor_offset_pixels = (uint32_t)(anchor_x_pixels - WAVE_AREA_X);
    }

    anchor_cnt = base_start_cnt + (uint32_t)
        (((uint64_t)base_window_cnt * anchor_offset_pixels) / WAVE_AREA_WIDTH);
    requested_start_cnt = (int64_t)anchor_cnt - (int64_t)
        (((uint64_t)new_window_cnt * anchor_offset_pixels) / WAVE_AREA_WIDTH);
    if(requested_start_cnt < 0) {
        requested_start_cnt = 0;
    }

    s_wave.pending_window_size = new_window_cnt;
    s_wave.pending_view_start_cnt = wave_clamp_view_start(
        (uint32_t)requested_start_cnt, new_window_cnt, available_end_cnt);
    s_wave.follow_live = false;
    s_wave.view_dirty = true;
    s_wave.render_requested = true;
}

/**
 * @brief wave_return_to_live：Layer 1 波形配置、绘制和帧提交。
 */
void wave_return_to_live(void)
{
    if(s_wave.state != WAVE_STATE_CAPTURING) {
        return;
    }

    s_wave.follow_live = true;
    s_wave.view_dirty = false;
    s_wave.render_requested = true;
}

/**
 * @brief wave_get_live_range：Layer 1 波形配置、绘制和帧提交。
 */
bool wave_get_live_range(wave_cnt_range_t * range)
{
    const uint32_t now_cnt = wave_capture_data_get_now_cnt();

    if(range == NULL || s_wave.state != WAVE_STATE_CAPTURING) {
        return false;
    }

    range->cnt_start = s_wave.last_live_range_end_cnt;
    range->cnt_end = now_cnt;
    s_wave.last_live_range_end_cnt = now_cnt;
    return true;
}

/**
 * @brief wave_get_view_range：Layer 1 波形配置、绘制和帧提交。
 */
bool wave_get_view_range(wave_cnt_range_t * range)
{
    if(range == NULL || s_wave.state == WAVE_STATE_UNINITIALIZED) {
        return false;
    }

    range->cnt_start = s_wave.view.start_cnt;
    range->cnt_end = s_wave.view.now_cnt;
    return true;
}

/** @brief 返回 LTDC Layer 1 已完成的实际垂直同步换帧次数。 */
uint32_t wave_get_refresh_count(void)
{
    return bsp_ltdc_lcd_wave_layer_get_present_count();
}

/*============================== 初始化和配置 ==============================*/

/**
 * @brief 生成一路默认配置并清空其采集、渲染状态。
 *
 * 默认启用六路通道，并填写通道颜色、徽标和名称。显示位置不在这里生成，
 * 因为隐藏某一路以后，其后的已启用通道需要自动向上补位。
 */
static void wave_init_default_config(wave_channel_t * channel,
                                     uint32_t channel_index)
{
    wave_channel_config_t * config = &channel->config;

    memset(channel, 0, sizeof(*channel));

    channel->source_id = (uint8_t)channel_index;
    config->enabled = true;
    config->color = s_default_colors[channel_index];

    (void)lv_snprintf(config->pin_text, sizeof(config->pin_text),
                      "D%lu", (unsigned long)(channel_index + 1U));
    (void)lv_snprintf(config->name_text, sizeof(config->name_text),
                      "Channel %lu", (unsigned long)(channel_index + 1U));
    config->detail_visible = false;
    config->detail_text[0] = '\0';
    config->separator_visible = true;
}

/**
 * @brief 根据显示槽位计算一路通道的固定行位置。
 *
 * 标尺下方始终按六个槽位划分。即使只选中四路，也只占用前四个固定槽位，
 * 不会把四路拉伸铺满屏幕；最后一个槽位吸收整数除法产生的高度余数。
 * 在 800x480 屏幕上得到旧版相同的 top=36/110/184/258/332/406、
 * height=74、high_offset=24、low_offset=49。
 */
static void wave_assign_display_slot(wave_channel_t * channel,
                                     uint32_t display_slot)
{
    const int32_t usable_height =
        s_wave.display_height - WAVE_TIME_RULER_HEIGHT;
    const int32_t row_height = usable_height / (int32_t)WAVE_CHANNEL_COUNT;
    wave_channel_layout_t * layout = &channel->layout;

    layout->display_slot = (uint8_t)display_slot;
    layout->top = WAVE_TIME_RULER_HEIGHT +
                  (int16_t)((int32_t)display_slot * row_height);
    layout->height = (display_slot == (WAVE_CHANNEL_COUNT - 1U))
                   ? (int16_t)(s_wave.display_height - layout->top)
                   : (int16_t)row_height;
    layout->high_offset = layout->height / 3;
    layout->low_offset = (int16_t)((layout->height * 2) / 3);
}

/**
 * @brief 创建两个长期存在的区域根容器。
 *
 * left_root 管理静态通道信息；wave_root 管理 Layer 0 背景和时间标尺。真正
 * 的动态波形由覆盖其下方区域的 LTDC Layer 1 显示。
 */
static bool wave_create_roots(lv_obj_t * parent, int32_t display_height)
{
    s_wave.left_root = lv_obj_create(parent);
    if(s_wave.left_root == NULL) {
        return false;
    }

    s_wave.wave_root = lv_obj_create(parent);
    if(s_wave.wave_root == NULL) {
        lv_obj_delete(s_wave.left_root);
        s_wave.left_root = NULL;
        return false;
    }

    /* 左侧静态区先创建，波形根容器后创建并位于更高层。两者不重叠。 */
    lv_obj_remove_style_all(s_wave.left_root);
    lv_obj_set_pos(s_wave.left_root, WAVE_LEFT_X, 0);
    lv_obj_set_size(s_wave.left_root, WAVE_LEFT_WIDTH, display_height);
    lv_obj_set_style_bg_color(s_wave.left_root,
                              lv_color_hex(WAVE_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_wave.left_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(s_wave.left_root, false);

    lv_obj_remove_style_all(s_wave.wave_root);
    lv_obj_set_pos(s_wave.wave_root, WAVE_AREA_X, 0);
    lv_obj_set_size(s_wave.wave_root, WAVE_AREA_WIDTH, display_height);
    lv_obj_set_style_bg_color(s_wave.wave_root,
                              lv_color_hex(WAVE_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_wave.wave_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(s_wave.wave_root, false);
    return true;
}

/**
 * @brief 在 lv_obj_clean() 后清空一路旧的对象句柄和坐标状态。
 *
 * 这些对象已经被 LVGL 删除，保留旧地址会形成悬空指针。
 */
static void wave_reset_render_handles(wave_channel_t * channel)
{
    memset(&channel->render, 0, sizeof(channel->render));
}

/** @brief 创建覆盖 x=100~599、y=0~35 的时间标尺自绘对象。 */
static bool wave_create_time_ruler(void)
{
    s_wave.time_ruler = lv_obj_create(s_wave.wave_root);
    if(s_wave.time_ruler == NULL) {
        return false;
    }

    lv_obj_remove_style_all(s_wave.time_ruler);
    lv_obj_set_pos(s_wave.time_ruler, 0, 0);
    lv_obj_set_size(s_wave.time_ruler,
                    WAVE_AREA_WIDTH, WAVE_TIME_RULER_HEIGHT);
    lv_obj_set_style_bg_color(s_wave.time_ruler,
                              lv_color_hex(WAVE_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_wave.time_ruler, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(s_wave.time_ruler, false);
    lv_obj_add_event_cb(s_wave.time_ruler, wave_draw_time_ruler_cb,
                        LV_EVENT_DRAW_MAIN, NULL);
    return true;
}

/**
 * @brief 为一路已启用通道创建完整界面对象。
 *
 * 波形轨道属于 LTDC Layer 1，不创建 LVGL 波形对象；这里仅创建 Layer 0 的
 * 左侧信息行。这样实时刷新不会进入 LVGL 的逐线段绘制路径。
 */
static bool wave_create_channel_widgets(wave_channel_t * channel)
{
    layer0_left_channel_config_t config = {
        .pin_text = channel->config.pin_text,
        .name_text = channel->config.name_text,
        .detail_text = channel->config.detail_text,
        .color = channel->config.color,
        .detail_visible = channel->config.detail_visible,
        .separator_visible = channel->config.separator_visible
    };

    return layer0_left_channel_create(s_wave.left_root,
                                      channel->layout.top,
                                      channel->layout.height,
                                      &config,
                                      &channel->render);
}

/*============================== 数据准备 ==============================*/

static uint32_t wave_clamp_view_start(uint32_t requested_start_cnt,
                                      uint32_t window_cnt,
                                      uint32_t available_end_cnt)
{
    uint32_t max_start_cnt;

    if(available_end_cnt <= window_cnt) {
        return 0U;
    }

    max_start_cnt = available_end_cnt - window_cnt;
    return (requested_start_cnt > max_start_cnt) ? max_start_cnt : requested_start_cnt;
}

/**
 * @brief wave_get_zoom_window：Layer 1 波形配置、绘制和帧提交。
 */
static uint32_t wave_get_zoom_window(uint32_t current_window_cnt,
                                     uint32_t max_window_cnt,
                                     bool zoom_in)
{
    static const uint32_t zoom_windows[] =
    {
        WAVE_MIN_WINDOW_CNT,
        10000U, 20000U, 50000U,
        100000U, 200000U, 500000U,
        WAVE_WINDOW_1S_CNT, WAVE_WINDOW_2S_CNT, WAVE_WINDOW_5S_CNT,
        10000000U, 20000000U, 50000000U,
        100000000U, 200000000U, 500000000U, 1000000000U
    };
    uint32_t index;

    if(max_window_cnt < WAVE_MIN_WINDOW_CNT) {
        return max_window_cnt;
    }

    if(zoom_in) {
        for(index = sizeof(zoom_windows) / sizeof(zoom_windows[0]);
            index > 0U;
            index--) {
            const uint32_t candidate_cnt = zoom_windows[index - 1U];

            if(candidate_cnt < current_window_cnt) {
                return (candidate_cnt > max_window_cnt) ?
                       max_window_cnt : candidate_cnt;
            }
        }
        return WAVE_MIN_WINDOW_CNT;
    }

    for(index = 0U; index < (sizeof(zoom_windows) / sizeof(zoom_windows[0])); index++) {
        if(zoom_windows[index] > current_window_cnt) {
            return (zoom_windows[index] > max_window_cnt) ?
                   max_window_cnt : zoom_windows[index];
        }
    }

    return max_window_cnt;
}

/**
 * @brief 更新当前可见时间窗口。
 *
 * 采集时间未超过窗口宽度时左边界保持 0；超过后窗口随 now_cnt 向右滑动。
 */
static void wave_update_view(uint32_t relative_now_cnt)
{
    s_wave.view.now_cnt = relative_now_cnt;
    s_wave.view.start_cnt = (relative_now_cnt > s_wave.view.window_size)
                          ? relative_now_cnt - s_wave.view.window_size
                          : 0U;
}

/**
 * @brief 把时间值线性映射到波形区横坐标。
 *
 * 使用 64 位中间值避免乘以 500 时溢出，并把结果限制在 100~599。
 */
static int32_t wave_cnt_to_x(uint32_t cnt, const wave_view_t * view)
{
    uint64_t pixel;

    if(view->window_size == 0U || cnt <= view->start_cnt) {
        return WAVE_AREA_X;
    }

    pixel = ((uint64_t)(cnt - view->start_cnt) * WAVE_AREA_WIDTH) /
            view->window_size;
    if(pixel >= WAVE_AREA_WIDTH) {
        return WAVE_AREA_X_END;
    }

    return WAVE_AREA_X + (int32_t)pixel;
}

/*============================== Layer 1 像素绘制 ==============================*/

static uint16_t wave_color_to_rgb565(uint32_t color)
{
    return (uint16_t)(((color & 0x00F80000UL) >> 8U) |
                      ((color & 0x0000FC00UL) >> 5U) |
                      ((color & 0x000000F8UL) >> 3U));
}

/**
 * @brief 绘制当前时间窗口顶部的主刻度和时间文字。
 *
 * 标签字符串保存在 s_wave.time_labels 中，确保异步绘制任务执行时仍然有效。
 */
static void wave_draw_time_ruler_cb(lv_event_t * event)
{
    wave_view_t view;
    uint32_t tick_cnt;
    uint32_t first_tick_cnt;
    lv_draw_line_dsc_t tick_dsc;
    lv_draw_label_dsc_t label_dsc;
    lv_area_t coords;
    uint32_t current_tick_cnt;
    uint32_t label_index = 0U;

    taskENTER_CRITICAL();
    view = s_wave.view;
    taskEXIT_CRITICAL();

    tick_cnt = wave_get_time_tick_cnt(view.window_size);
    first_tick_cnt = ((view.start_cnt + tick_cnt - 1U) / tick_cnt) * tick_cnt;
    if(first_tick_cnt > view.now_cnt) {
        return;
    }

    lv_obj_get_content_coords(lv_event_get_current_target(event), &coords);

    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = lv_color_hex(WAVE_TIME_TICK_COLOR);
    tick_dsc.width = 1U;
    tick_dsc.opa = LV_OPA_COVER;

    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_hex(WAVE_TIME_TEXT_COLOR);
    label_dsc.opa = LV_OPA_COVER;
    label_dsc.flag = LV_TEXT_FLAG_EXPAND;

    for(current_tick_cnt = first_tick_cnt;
        current_tick_cnt <= view.now_cnt &&
        label_index < WAVE_TIME_LABEL_SLOTS;
        current_tick_cnt += tick_cnt, label_index++) {
        const int32_t x = wave_cnt_to_x(current_tick_cnt, &view);
        lv_area_t label_coords;
        lv_point_t label_size;

        tick_dsc.p1.x = x;
        tick_dsc.p1.y = coords.y1;
        tick_dsc.p2.x = x;
        tick_dsc.p2.y = coords.y1 + 6;
        lv_draw_line(lv_event_get_layer(event), &tick_dsc);

        wave_format_time_label(s_wave.time_labels[label_index],
                               sizeof(s_wave.time_labels[label_index]),
                               current_tick_cnt, tick_cnt);
        label_dsc.text = s_wave.time_labels[label_index];
        lv_text_get_size(&label_size, label_dsc.text, label_dsc.font,
                         label_dsc.letter_space, label_dsc.line_space,
                         LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
        label_dsc.align = LV_TEXT_ALIGN_LEFT;

        if(x == coords.x1) {
            label_coords.x1 = x;
            label_coords.x2 = x + label_size.x - 1;
        }
        else if(x == coords.x2) {
            label_coords.x1 = x - label_size.x + 1;
            label_coords.x2 = x;
        }
        else {
            label_coords.x1 = x - (label_size.x / 2);
            label_coords.x2 = label_coords.x1 + label_size.x - 1;
        }

        label_coords.y1 = coords.y1 + 9;
        label_coords.y2 = coords.y2;
        lv_draw_label(lv_event_get_layer(event), &label_dsc, &label_coords);
    }
}

/**
 * @brief 根据窗口宽度选择时间标尺主刻度间隔。
 *
 * 使用示波器常用的 1 / 2 / 5 × 10^n 间隔，并保证完整窗口至少有 5 个
 * 主刻度间隔。这样 5 s 窗口固定选择 1 s，不会出现 0.8、1.6 s 这类
 * 不直观的时间戳；1 s 窗口选择 0.2 s，2 s 窗口选择 0.2 s（10 格）。
 */
static uint32_t wave_get_time_tick_cnt(uint32_t window_cnt)
{
    static const uint8_t multipliers[] = { 1U, 2U, 5U };
    uint32_t best_tick_cnt = 1U;
    uint32_t decade_cnt = 1U;
    uint32_t multiplier_index;

    while(decade_cnt != 0U) {
        for(multiplier_index = 0U;
            multiplier_index < (sizeof(multipliers) / sizeof(multipliers[0]));
            multiplier_index++) {
            const uint64_t candidate_cnt =
                (uint64_t)decade_cnt * multipliers[multiplier_index];

            if(candidate_cnt > UINT32_MAX ||
               (candidate_cnt * WAVE_TIME_TICK_MIN_COUNT) > window_cnt) {
                return best_tick_cnt;
            }

            best_tick_cnt = (uint32_t)candidate_cnt;
        }

        if(decade_cnt > (UINT32_MAX / 10U)) {
            break;
        }
        decade_cnt *= 10U;
    }

    return best_tick_cnt;
}

/**
 * @brief 把 CNT 格式化成标尺文字。
 *
 * 小数位数由刻度精度决定；例如 0.2 s 使用 1 位、0.05 s 使用 2 位，
 * 避免缩放到较小时把不同刻度都显示为同一个标签。
 */
static void wave_format_time_label(char * buffer,
                                   uint32_t buffer_size,
                                   uint32_t cnt,
                                   uint32_t tick_cnt)
{
    const uint32_t seconds = cnt / WAVE_WINDOW_1S_CNT;
    const uint32_t fraction = cnt % WAVE_WINDOW_1S_CNT;

    if((tick_cnt % WAVE_WINDOW_1S_CNT) == 0U) {
        (void)lv_snprintf(buffer, buffer_size, "%lus",
                          (unsigned long)seconds);
    }
    else if((tick_cnt % 100000U) == 0U) {
        (void)lv_snprintf(buffer, buffer_size, "%lu.%lus",
                          (unsigned long)seconds,
                          (unsigned long)(fraction / 100000U));
    }
    else if((tick_cnt % 10000U) == 0U) {
        (void)lv_snprintf(buffer, buffer_size, "%lu.%02lus",
                          (unsigned long)seconds,
                          (unsigned long)(fraction / 10000U));
    }
    else if((tick_cnt % 1000U) == 0U) {
        (void)lv_snprintf(buffer, buffer_size, "%lu.%03lus",
                          (unsigned long)seconds,
                          (unsigned long)(fraction / 1000U));
    }
    else if((tick_cnt % 100U) == 0U) {
        (void)lv_snprintf(buffer, buffer_size, "%lu.%04lus",
                          (unsigned long)seconds,
                          (unsigned long)(fraction / 100U));
    }
    else if((tick_cnt % 10U) == 0U) {
        (void)lv_snprintf(buffer, buffer_size, "%lu.%05lus",
                          (unsigned long)seconds,
                          (unsigned long)(fraction / 10U));
    }
    else {
        (void)lv_snprintf(buffer, buffer_size, "%lu.%06lus",
                          (unsigned long)seconds, (unsigned long)fraction);
    }
}
