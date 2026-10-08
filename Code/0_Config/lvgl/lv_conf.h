/**
 * @file lv_conf.h
 * @brief LVGL v9.6.0 在 STM32H743 + FreeRTOS + LTDC 工程中的配置。
 *
 * 硬件环境：STM32H743、LTDC、外部 SDRAM、RGB565。
 * 运行方式：FreeRTOS 创建独立 LVGL 任务，TIM7 每 1 ms 调用 lv_tick_inc(1)。
 * 刷新方式：CPU 将 LVGL 局部绘图缓冲复制到 LTDC 的 SDRAM 帧缓冲，暂未使用 DMA2D。
 *
 * 相对 LVGL v9.6.0 官方模板的原始修改：
 *   1. 启用配置内容（官方模板最外层 #if 0 改为有效配置）；
 *   2. LV_DEF_REFR_PERIOD 由 33 ms 改为 16 ms。
 *
 * 本文件只显式列出本工程需要关注的选项。未列出的配置项由
 * LVGL 的 lv_conf_internal.h 按 v9.6.0 官方默认值补齐。
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* clang-format off */

/*====================== 0. 配置文件说明与头文件保护 ======================*/

/*
 * lv_conf.h 位于 Code/0_Config。Keil 需要将该目录加入 Include Paths。
 * 建议同时定义 LV_CONF_INCLUDE_SIMPLE，使 LVGL 通过 #include "lv_conf.h" 引入本文件。
 */

/*====================== 1. 内存和标准库 =================================*/

/* 使用 LVGL 内置的动态内存、字符串和格式化实现，避免依赖额外运行库适配。 */
#define LV_USE_STDLIB_MALLOC             LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING             LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF            LV_STDLIB_BUILTIN

/* LVGL 内部动态内存池大小，单位为字节。该内存池不等同于 FreeRTOS Heap。 */
#define LV_MEM_SIZE                       (64U * 1024U)

/* 0 表示不使用用户指定的固定内存池地址，由 LVGL 内置分配器管理。 */
#define LV_MEM_ADR                        0x0

/* 标准 C 类型头文件。 */
#define LV_STDINT_INCLUDE                 "stdint.h"
#define LV_STDDEF_INCLUDE                 "stddef.h"
#define LV_STDBOOL_INCLUDE                "stdbool.h"
#define LV_INTTYPES_INCLUDE               "inttypes.h"
#define LV_LIMITS_INCLUDE                 "limits.h"
#define LV_STDARG_INCLUDE                 "stdarg.h"

/*====================== 2. 操作系统适配 =================================*/

/*
 * 使用 FreeRTOS OS 适配：LVGL 内部锁、线程和同步对象映射到 FreeRTOS。
 * 正常情况下仍由唯一 GUI 任务访问 LVGL；跨任务访问时必须使用 lv_lock/lv_unlock。
 */
#define LV_USE_OS                         LV_OS_FREERTOS
/* GUI 任务自己的唤醒也使用 Task Notify，LVGL 内部同步改用独立信号量避免共用通知槽。 */
#define LV_USE_FREERTOS_TASK_NOTIFY       0
#define LV_OS_IDLE_PERCENT_CUSTOM         0

/*====================== 3. 显示格式与刷新周期 ============================*/

/* LTDC 帧缓冲和 LVGL 绘图缓冲均采用 RGB565，每个像素占 2 字节。 */
#define LV_COLOR_FORMAT_DEFAULT           LV_COLOR_FORMAT_RGB565

/* 颜色混合计算的舍入偏移，保持官方默认值。 */
#define LV_COLOR_MIX_ROUND_OFS            0

/* 本工程修改：默认刷新、输入读取和动画步进周期为 16 ms，目标约 60 Hz。 */
#define LV_DEF_REFR_PERIOD                16

/* 默认 DPI，仅影响部分控件的默认尺寸和间距。 */
#define LV_DPI_DEF                        130

/* 绘图缓冲步长和首地址对齐要求，适用于当前 CPU 软件绘制。 */
#define LV_DRAW_BUF_STRIDE_ALIGN          1
#define LV_DRAW_BUF_ALIGN                 4

/*====================== 4. 软件绘制与硬件加速 ============================*/

/* 使用单个 CPU 软件绘制单元。 */
#define LV_USE_DRAW_SW                    1
#define LV_DRAW_SW_DRAW_UNIT_CNT          1

/* 当前显示链路只使用 RGB565；保留官方软件渲染器对 RGB565 的支持。 */
#define LV_DRAW_SW_SUPPORT_RGB565         1

/* 启用圆角、阴影、渐变等复杂软件绘制功能。 */
#define LV_DRAW_SW_COMPLEX                1
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS  0
#define LV_DRAW_SW_SHADOW_CACHE_SIZE      0
#define LV_DRAW_SW_CIRCLE_CACHE_SIZE      4

/* Cortex-M7 当前不使用额外汇编绘制后端。 */
#define LV_USE_DRAW_SW_ASM                LV_DRAW_SW_ASM_NONE

/* 当前由 CPU memcpy 刷新 SDRAM 帧缓冲，DMA2D 留待后续优化。 */
#define LV_USE_DRAW_DMA2D                 0
#define LV_USE_DRAW_DMA2D_INTERRUPT       0

/* 其他平台或 GPU 绘制后端均不使用。 */
#define LV_USE_DRAW_ARM2D_SYNC            0
#define LV_USE_DRAW_VG_LITE               0
#define LV_USE_DRAW_DAVE2D                0
#define LV_USE_NEMA_GFX                   0
#define LV_USE_PXP                        0
#define LV_USE_PPA                        0
#define LV_USE_DRAW_EVE                   0
#define LV_USE_G2D                        0
#define LV_USE_DRAW_NANOVG                0
#define LV_USE_DRAW_OPENGLES              0
#define LV_USE_DRAW_SDL                   0

/* 当前不使用矩阵变换、矢量图、快照和 ThorVG。 */
#define LV_USE_MATRIX                     0
#define LV_DRAW_TRANSFORM_USE_MATRIX      0
#define LV_USE_VECTOR_GRAPHIC             0
#define LV_USE_SNAPSHOT                   0
#define LV_USE_THORVG                     0

/*====================== 5. 日志、断言与调试 ==============================*/

/* LVGL 日志关闭；工程日志由现有 log 服务负责。 */
#define LV_USE_LOG                        0

/* 当前发布配置关闭 LVGL 断言，保留轻量参数检查。 */
#define LV_USE_ASSERT                     0
#define LV_USE_ASSERT_MALLOC              0
#define LV_USE_ASSERT_NULL                0
#define LV_USE_ASSERT_STYLE               0
#define LV_USE_ASSERT_MEM_INTEGRITY       0
#define LV_USE_ASSERT_OBJ                 0
#define LV_USE_CHECK_ARG                  1

/* 不启用性能、内存监视器、分析器和测试工具。 */
#define LV_USE_SYSMON                     0
#define LV_USE_PERF_MONITOR               0
#define LV_USE_MEM_MONITOR                0
#define LV_USE_PROFILER                   0
#define LV_USE_TEST                       0
#define LV_USE_MONKEY                     0
#define LV_USE_REFR_DEBUG                 0
#define LV_USE_LAYER_DEBUG                0
#define LV_USE_PARALLEL_DRAW_DEBUG        0

/* 不开放 LVGL 私有 API，应用只依赖稳定的公共接口。 */
#define LV_USE_PRIVATE_API                0

/*====================== 6. 字体与文本 ===================================*/

/* 文本编码使用 UTF-8。 */
#define LV_TXT_ENC                        LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS                " ,.;:-_)]}"
#define LV_TXT_LINE_BREAK_LONG_LEN        0
#define LV_TXT_COLOR_CMD                  "#"

/* 当前不启用双向文本及阿拉伯语/波斯语字符处理。 */
#define LV_USE_BIDI                       0
#define LV_USE_ARABIC_PERSIAN_CHARS       0

/* 只启用默认 Montserrat 14 字体。 */
#define LV_FONT_MONTSERRAT_14             1
#define LV_FONT_DEFAULT                   LV_FONT_DEFAULT_MONTSERRAT_14
#define LV_FONT_FMT_TXT_LARGE             0
#define LV_USE_FONT_COMPRESSED            0
#define LV_USE_FONT_PLACEHOLDER           1

/* 当前不使用动态字体管理和外部字体引擎。 */
#define LV_USE_FONT_MANAGER               0
#define LV_USE_IMGFONT                    0
#define LV_USE_FREETYPE                   0
#define LV_USE_TINY_TTF                   0

/*====================== 7. 控件和布局 ===================================*/

/* 主题决定控件的默认颜色、圆角、边框等外观；通常只需保留一种。 */
#define LV_USE_THEME_DEFAULT              1   /* 默认彩色主题，普通嵌入式 GUI 最常用。 */
#define LV_USE_THEME_SIMPLE               0   /* 简洁主题，样式效果比默认主题少。 */
#define LV_USE_THEME_MONO                 0   /* 单色主题，适合黑白屏或极简界面。 */

/* 布局用于自动排列子控件；如果全部使用固定坐标，可以关闭。 */
#define LV_USE_FLEX                       0   /* 弹性布局：按行或列自动排列、对齐控件。 */
#define LV_USE_GRID                       0   /* 网格布局：按照行列单元格排列控件。 */

/* 常用基础控件：1 表示编译进工程，0 表示裁剪掉。 */
#define LV_USE_ANIMIMG                    0   /* 动画图片：按顺序循环显示多张图片。 */
#define LV_USE_ARC                        1   /* 圆弧：常用于旋钮、圆形进度和仪表盘。 */
#define LV_USE_ARCLABEL                   0   /* 弧形文字：让文字沿圆弧方向排列。 */
#define LV_USE_BAR                        0   /* 进度条：只显示数值进度，不能直接拖动。 */
#define LV_USE_BUTTON                     1   /* 按钮：可点击的矩形控件，文字通常由 Label 添加。 */
#define LV_USE_BUTTONMATRIX               0   /* 按钮矩阵：在一个控件中生成多行多列按钮。 */
#define LV_USE_CALENDAR                   0   /* 日历：显示月份和日期，并支持日期选择。 */
#define LV_CALENDAR_WEEK_STARTS_MONDAY    0   /* 0：星期日开头；1：星期一开头。 */
#define LV_MONDAY_STR                     "Mo"
#define LV_TUESDAY_STR                    "Tu"
#define LV_WEDNESDAY_STR                  "We"
#define LV_THURSDAY_STR                   "Th"
#define LV_FRIDAY_STR                     "Fr"
#define LV_SATURDAY_STR                   "Sa"
#define LV_SUNDAY_STR                     "Su"
#define LV_JANUARY_STR                    "January"
#define LV_FEBRUARY_STR                   "February"
#define LV_MARCH_STR                      "March"
#define LV_APRIL_STR                      "April"
#define LV_MAY_STR                        "May"
#define LV_JUNE_STR                       "June"
#define LV_JULY_STR                       "July"
#define LV_AUGUST_STR                     "August"
#define LV_SEPTEMBER_STR                  "September"
#define LV_OCTOBER_STR                    "October"
#define LV_NOVEMBER_STR                   "November"
#define LV_DECEMBER_STR                   "December"
#define LV_USE_CALENDAR_HEADER_ARROW      0   /* 日历顶部使用左右箭头切换月份。 */
#define LV_USE_CALENDAR_HEADER_DROPDOWN   0   /* 日历顶部使用下拉框选择年月。 */
#define LV_USE_CANVAS                     0   /* 画布：在像素缓冲上自行画点、线、图形或图片。 */
#define LV_USE_CHART                      0   /* 图表：绘制折线图、柱状图和散点图等数据曲线。 */
#define LV_USE_CHECKBOX                   0   /* 复选框：显示选中/未选中状态和说明文字。 */
#define LV_USE_DROPDOWN                   1   /* 下拉列表：协议设置选择通道和参数。 */
#define LV_USE_IMAGE                      1   /* 图片：显示 C 数组、文件或符号形式的图片资源。 */
#define LV_USE_IMAGEBUTTON                0   /* 图片按钮：用不同图片表示普通、按下等状态。 */
#define LV_USE_KEYBOARD                   0   /* 屏幕键盘：通常与 Textarea 配合输入文字。 */
#define LV_USE_LABEL                      1   /* 标签：显示文字和 LV_SYMBOL_* 图标，最常用控件之一。 */
#define LV_USE_LED                        0   /* 虚拟 LED：使用亮度和颜色模拟指示灯。 */
#define LV_USE_LINE                       0   /* 折线：连接一组坐标点，适合静态线段或轮廓。 */
#define LV_USE_LIST                       0   /* 列表：纵向排列文本、图标和按钮项目。 */
#define LV_USE_MENU                       0   /* 菜单：支持页面、分组和返回导航的多级菜单。 */
#define LV_USE_MSGBOX                     0   /* 消息框：显示提示、警告以及确认/取消按钮。 */
#define LV_USE_ROLLER                     0   /* 滚轮选择器：上下滚动选择一个文本选项。 */
#define LV_USE_SCALE                      0   /* 刻度尺：显示刻度线、刻度文字和指示针。 */
#define LV_USE_SLIDER                     0   /* 滑块：可以拖动改变数值，内部基于 Bar。 */
#define LV_USE_SPAN                       0   /* 富文本：在同一段文字中使用不同字体和颜色。 */
#define LV_USE_SPINBOX                    0   /* 数字输入框：逐位增加或减少数值。 */
#define LV_USE_SPINNER                    0   /* 加载动画：持续旋转的圆弧，表示正在处理。 */
#define LV_USE_SWITCH                     1   /* 开关：协议设置的启用状态。 */
#define LV_USE_TABLE                      0   /* 表格：按行列显示文本数据。 */
#define LV_USE_TABVIEW                    0   /* 选项卡：通过标签页切换不同内容页面。 */
#define LV_USE_TEXTAREA                   0   /* 文本输入框：支持光标、编辑和多行文字。 */
#define LV_USE_TILEVIEW                   0   /* 平铺视图：通过滑动在多个页面之间切换。 */
#define LV_USE_WIN                        0   /* 窗口：带标题栏和内容区的组合控件。 */

/* 当前不使用需要额外依赖或资源较多的扩展控件。 */
#define LV_USE_3DTEXTURE                  0
#define LV_USE_BARCODE                    0
#define LV_USE_CALENDAR_CHINESE           0
#define LV_USE_FFMPEG                     0
#define LV_USE_GIF                        0
#define LV_USE_GLTF                       0
#define LV_USE_GSTREAMER                  0
#define LV_USE_IME_PINYIN                 0
#define LV_USE_LOTTIE                     0
#define LV_USE_QRCODE                     0
#define LV_USE_RLOTTIE                    0

/* 当前没有触摸导航、手势识别等输入扩展。 */
#define LV_USE_GRIDNAV                    0
#define LV_USE_GESTURE_RECOGNITION        0

/*====================== 8. 文件系统、图片与解码库 ========================*/

/* 当前不缓存图片头，也不启用压缩图片或外部图片解码库。 */
#define LV_CACHE_DEF_SIZE                 0
#define LV_IMAGE_HEADER_CACHE_DEF_CNT     0
#define LV_USE_RLE                        0
#define LV_USE_LZ4                        0
#define LV_BIN_DECODER_RAM_LOAD           0
#define LV_USE_LODEPNG                    0
#define LV_USE_LIBPNG                     0
#define LV_USE_BMP                        0
#define LV_USE_TJPGD                      0
#define LV_USE_LIBJPEG_TURBO              0
#define LV_USE_LIBWEBP                    0
#define LV_USE_SVG                        0

/* 当前未将 FatFS、LittleFS 或主机文件系统注册给 LVGL。 */
#define LV_FS_DEFAULT_DRIVER_LETTER       0
#define LV_USE_FS_STDIO                   0
#define LV_USE_FS_POSIX                   0
#define LV_USE_FS_WIN32                   0
#define LV_USE_FS_FATFS                   0
#define LV_USE_FS_LITTLEFS                0
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS    0
#define LV_USE_FS_ARDUINO_SD              0
#define LV_USE_FS_UEFI                    0
#define LV_USE_FS_FROGFS                  0
#define LV_USE_FS_MEMFS                   0

/* 不使用 LVGL 内置的平台显示、输入和桌面系统驱动。LTDC 由本工程自行适配。 */
#define LV_USE_LINUX_DRM                  0
#define LV_USE_LINUX_FBDEV                0
#define LV_USE_ST_LTDC                    0
#define LV_USE_EVDEV                      0
#define LV_USE_LIBINPUT                   0
#define LV_USE_NUTTX                      0
#define LV_USE_OPENGLES                   0
#define LV_USE_GLFW                       0
#define LV_USE_QNX                        0
#define LV_USE_SDL                        0
#define LV_USE_UEFI                       0
#define LV_USE_WAYLAND                    0
#define LV_USE_WINDOWS                    0
#define LV_USE_X11                        0
#define LV_USE_NANOVG                     0

/*====================== 9. 官方 Demo ====================================*/

/* 保持官方构建总开关；具体 Demo 全部关闭，不需要加入 demos 源码。 */
#define LV_BUILD_EXAMPLES                 0
#define LV_BUILD_DEMOS                    0
#define LV_USE_DEMO_BENCHMARK             0
#define LV_USE_DEMO_GLTF                  0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER    0
#define LV_USE_DEMO_MUSIC                 0
#define LV_USE_DEMO_RENDER                0
#define LV_USE_DEMO_STRESS                0
#define LV_USE_DEMO_VECTOR_GRAPHIC        0
#define LV_USE_DEMO_WIDGETS               0
#define LV_USE_DEMO_FLEX_LAYOUT           0
#define LV_USE_DEMO_MULTILANG             0
#define LV_USE_DEMO_SMARTWATCH            0
#define LV_USE_DEMO_EBIKE                 0
#define LV_USE_DEMO_HIGH_RES              0

/* clang-format on */

#endif /* LV_CONF_H */
