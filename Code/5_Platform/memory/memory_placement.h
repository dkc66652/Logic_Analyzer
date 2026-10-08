/**
 * @file memory_placement.h
 * @brief Arm Compiler 6 静态对象内存放置属性
 * 
 * @note
 * 本模块只控制编译器生成的输入段名称和对象对齐；对象最终地址、
 * 区域容量及启动初始化行为由 STM32H743II.sct 决定，
 * Cache 属性由 MPU 配置决定
 * 所以更改本模块的定义一定要参考 .sct 和 mpu 的配置
 *
 * 使用限制：
 * 1. ZI 宏生成 .bss.* 段，只允许无初始化器或全零初始化器
 * 2. RW 宏生成 .data.* 段，允许非零初始化器
 * 3. DMA_NOCACHE 和 SRAM4_NOINIT 位于 UNINIT 区域，运行前不得假定内容为零
 * 4. 带 section 属性的对象应定义为文件作用域对象或 static 局部对象
 */

#ifndef MEMORY_PLACEMENT_H
#define MEMORY_PLACEMENT_H

/* ================================================== */

/* Cortex-M7 L1 D-Cache 的 Cache Line 大小。 */
#define PLATFORM_CACHE_LINE_SIZE_BYTES 32U

/* Cortex-M7 保守对齐，兼容u64、double的8字节对齐 */
#define PLATFORM_CONSERVATIVE_ALIGNMEN_BYTES 8U

/* ================================================== */

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)

/*
 * AC6/ArmClang 使用 .bss.* 名称生成 SHT_NOBITS 段
 * 非零初始化器会触发编译错误，而不会被静默转换
 */
#define PLATFORM_MEMORY_ZI_ATTRIBUTE(section_name) \
    __attribute__((section(".bss." section_name)))

/* 有非零初值的对象使用 .data.* 段，由启动代码从 Flash 复制到 RAM */
#define PLATFORM_MEMORY_RW_ATTRIBUTE(section_name) \
    __attribute__((section(".data." section_name)))

/* 类型对齐 */
#define PLATFORM_TYPE_ALIGNMENT_ATTRIBUTE(type) \
    __attribute__((aligned(__alignof__(type))))

/* Cache 对齐 */
#define PLATFORM_CACHE_ALIGNMENT_ATTRIBUTE \
    __attribute__((aligned(PLATFORM_CACHE_LINE_SIZE_BYTES)))

/* 保守对齐 */
#define PLATFORM_CONSERVATIVE_ALIGNMENT_ATTRIBUTE \
    __attribute__((aligned(PLATFORM_CONSERVATIVE_ALIGNMEN_BYTES)))

/* 自定义对齐 */
#define PLATFORM_CUSTOM_ALIGNMENT_ATTRIBUTE(alignment) \
    __attribute__((aligned(alignment)))

#else
    #error "memory_placement.h requires Arm Compiler 6"
#endif

/* ================================================== */

/* DTCM：仅供 CPU 使用，不得用于外设 DMA */
#define PLATFORM_DTCM_ZI \
    PLATFORM_MEMORY_ZI_ATTRIBUTE("DTCM_DATA")
#define PLATFORM_DTCM_RW \
    PLATFORM_MEMORY_RW_ATTRIBUTE("DTCM_DATA")

/* AXI SRAM：默认可缓存，适合 CPU 访问的大数组 */
#define PLATFORM_AXI_SRAM_ZI \
    PLATFORM_MEMORY_ZI_ATTRIBUTE("AXI_SRAM_DATA")
#define PLATFORM_AXI_SRAM_RW \
    PLATFORM_MEMORY_RW_ATTRIBUTE("AXI_SRAM_DATA")

/* SRAM1 前 32KB：MPU 配置为 Non-cacheable，Scatter 配置为 UNINIT */
#define PLATFORM_DMA_NOCACHE_ZI \
    PLATFORM_MEMORY_ZI_ATTRIBUTE("DMA_NOCACHE")

/* D2 SRAM1/2/3：当前 MPU 配置为可缓存普通内存 */
#define PLATFORM_SRAM1_ZI \
    PLATFORM_MEMORY_ZI_ATTRIBUTE("SRAM1_DATA")
#define PLATFORM_SRAM1_RW \
    PLATFORM_MEMORY_RW_ATTRIBUTE("SRAM1_DATA")

#define PLATFORM_SRAM2_ZI \
    PLATFORM_MEMORY_ZI_ATTRIBUTE("SRAM2_DATA")
#define PLATFORM_SRAM2_RW \
    PLATFORM_MEMORY_RW_ATTRIBUTE("SRAM2_DATA")

#define PLATFORM_SRAM3_ZI \
    PLATFORM_MEMORY_ZI_ATTRIBUTE("SRAM3_DATA")
#define PLATFORM_SRAM3_RW \
    PLATFORM_MEMORY_RW_ATTRIBUTE("SRAM3_DATA")

/* SRAM4 前 4KB：跨软件复位保留，Scatter 配置为 UNINIT */
#define PLATFORM_SRAM4_RETAINED_ZI \
    PLATFORM_MEMORY_ZI_ATTRIBUTE("SRAM4_NOINIT")

/* SRAM4 剩余 60KB：普通可缓存数据 */
#define PLATFORM_SRAM4_ZI \
    PLATFORM_MEMORY_ZI_ATTRIBUTE("SRAM4_DATA")
#define PLATFORM_SRAM4_RW \
    PLATFORM_MEMORY_RW_ATTRIBUTE("SRAM4_DATA")

/* SDRAM 前 8MB：UI 数据，MPU 配置为 Non-cacheable，Scatter 配置为 UNINIT */
#define PLATFORM_SDRAM_UI_ZI \
    PLATFORM_MEMORY_ZI_ATTRIBUTE("UI_DATA")

/* SDRAM 中的 2MB：协议解析结果，MPU 配置为 Non-cacheable，Scatter 配置为 UNINIT */
#define PLATFORM_SDRAM_PROTOCOL_ZI \
    PLATFORM_MEMORY_ZI_ATTRIBUTE("PROTOCOL_DATA")

/* SDRAM 剩余 22MB：LA 原始边沿数据，MPU 配置为 Non-cacheable，Scatter 配置为 UNINIT */
#define PLATFORM_SDRAM_LA_ZI \
    PLATFORM_MEMORY_ZI_ATTRIBUTE("LA_DATA")

/* ================================================== */

/* DMA缓冲区定义宏，会将缓冲区分配到为DMA准备的Non-cacheable UNINIT区域，采取类型对齐、私有定义，如有其它对齐需求不推荐使用 */
#define PLATFORM_DEFINE_DMA_NOCACHE_ARRAY(name, type, count) \
    PLATFORM_DMA_NOCACHE_ZI \
    PLATFORM_TYPE_ALIGNMENT_ATTRIBUTE(type) \
    static type name[(count)]

/* ================================================== */

#endif /* MEMORY_PLACEMENT_H */
