/** @file platform_dwt.c @brief DWT 周期计数和时间转换。 */

#include "platform_dwt.h"
#include "platform_dwt_config.h"

#include PLATFORM_DWT_CMSIS_HEADER

#if (PLATFORM_DWT_CLOCK_HZ == 0U)
#error "PLATFORM_DWT_CLOCK_HZ must be non-zero"
#endif

static volatile uint64_t s_cycles64;
static uint32_t s_last_cycle;
static bool s_initialized;

/**
 * @brief platform_dwt_lock：DWT 周期计数和时间转换。
 */
static uint32_t platform_dwt_lock(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

/**
 * @brief platform_dwt_unlock：DWT 周期计数和时间转换。
 */
static void platform_dwt_unlock(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/**
 * @brief platform_dwt_update_locked：DWT 周期计数和时间转换。
 */
static void platform_dwt_update_locked(void)
{
    uint32_t current_cycle = DWT->CYCCNT;

    s_cycles64 += current_cycle - s_last_cycle;
    s_last_cycle = current_cycle;
}

/**
 * @brief platform_dwt_init：DWT 周期计数和时间转换。
 */
bool platform_dwt_init(void)
{
    uint32_t primask;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

        __DSB();
        __ISB();
    }

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        return false;
    }

    primask = platform_dwt_lock();
    s_cycles64 = 0U;
    s_last_cycle = DWT->CYCCNT;
    s_initialized = true;
    platform_dwt_unlock(primask);

    return true;
}

/**
 * @brief platform_dwt_maintain：DWT 周期计数和时间转换。
 */
void platform_dwt_maintain(void)
{
    uint32_t primask;

    if (!platform_dwt_is_enabled()) {
        return;
    }

    primask = platform_dwt_lock();
    platform_dwt_update_locked();
    platform_dwt_unlock(primask);
}

/**
 * @brief platform_dwt_is_enabled：DWT 周期计数和时间转换。
 */
bool platform_dwt_is_enabled(void)
{
    return s_initialized && ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U);
}

/**
 * @brief platform_dwt_cycles：DWT 周期计数和时间转换。
 */
uint32_t platform_dwt_cycles(void)
{
    return DWT->CYCCNT;
}

/**
 * @brief platform_dwt_elapsed：DWT 周期计数和时间转换。
 */
uint32_t platform_dwt_elapsed(uint32_t start_cycle)
{
    return platform_dwt_cycles() - start_cycle;
}

/**
 * @brief platform_dwt_cycles64：DWT 周期计数和时间转换。
 */
uint64_t platform_dwt_cycles64(void)
{
    uint64_t cycles;
    uint32_t primask;

    if (!platform_dwt_is_enabled()) {
        return 0U;
    }

    primask = platform_dwt_lock();
    platform_dwt_update_locked();
    cycles = s_cycles64;
    platform_dwt_unlock(primask);

    return cycles;
}

/**
 * @brief platform_dwt_time_us：DWT 周期计数和时间转换。
 */
uint64_t platform_dwt_time_us(void)
{
    uint64_t cycles = platform_dwt_cycles64();

    return ((cycles / PLATFORM_DWT_CLOCK_HZ) * 1000000ULL) +
           (((cycles % PLATFORM_DWT_CLOCK_HZ) * 1000000ULL) /
            PLATFORM_DWT_CLOCK_HZ);
}

/**
 * @brief platform_dwt_time_ms：DWT 周期计数和时间转换。
 */
uint64_t platform_dwt_time_ms(void)
{
    uint64_t cycles = platform_dwt_cycles64();

    return ((cycles / PLATFORM_DWT_CLOCK_HZ) * 1000ULL) +
           (((cycles % PLATFORM_DWT_CLOCK_HZ) * 1000ULL) /
            PLATFORM_DWT_CLOCK_HZ);
}

/**
 * @brief platform_dwt_cycles_to_us：DWT 周期计数和时间转换。
 */
uint32_t platform_dwt_cycles_to_us(uint32_t cycles)
{
    return (uint32_t)(((uint64_t)cycles * 1000000ULL) /
                      PLATFORM_DWT_CLOCK_HZ);
}

/**
 * @brief platform_dwt_max_maintain_interval_us：DWT 周期计数和时间转换。
 */
uint32_t platform_dwt_max_maintain_interval_us(void)
{
    return (uint32_t)(((uint64_t)UINT32_MAX * 1000000ULL) /
                      PLATFORM_DWT_CLOCK_HZ);
}
