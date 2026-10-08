/** @file bsp_i2c_touch.c @brief 板级触摸软件I2C、复位和中断实现。 */
#include "bsp_i2c_touch.h"
#include "stm32h7xx_hal.h"

#define TOUCH_SCL_PORT GPIOH      /**< 触摸软件I2C的SCL端口。 */
#define TOUCH_SCL_PIN  GPIO_PIN_6 /**< 触摸软件I2C的SCL引脚。 */
#define TOUCH_SDA_PORT GPIOI      /**< 触摸软件I2C的SDA端口。 */
#define TOUCH_SDA_PIN  GPIO_PIN_3 /**< 触摸软件I2C的SDA引脚。 */
#define TOUCH_RST_PORT GPIOI      /**< 触摸控制器复位端口。 */
#define TOUCH_RST_PIN  GPIO_PIN_8 /**< 触摸控制器复位引脚。 */
#define TOUCH_INT_PORT GPIOH      /**< 触摸控制器中断端口。 */
#define TOUCH_INT_PIN  GPIO_PIN_7 /**< 触摸控制器中断引脚。 */

/** @brief 产生软件I2C GPIO翻转间隔。 */
static void i2c_delay(void)
{
    volatile uint32_t count;
    for (count = 0U; count < 150U; ++count) __NOP();
}

/** @brief 设置SCL引脚电平。 */
static void scl_write(GPIO_PinState state)
{
    HAL_GPIO_WritePin(TOUCH_SCL_PORT, TOUCH_SCL_PIN, state);
}

/** @brief 设置SDA引脚电平。 */
static void sda_write(GPIO_PinState state)
{
    HAL_GPIO_WritePin(TOUCH_SDA_PORT, TOUCH_SDA_PIN, state);
}

/** @brief 产生I2C起始条件。 */
static void i2c_start(void)
{
    sda_write(GPIO_PIN_SET);
    scl_write(GPIO_PIN_SET);
    i2c_delay();
    sda_write(GPIO_PIN_RESET);
    i2c_delay();
    scl_write(GPIO_PIN_RESET);
}

/** @brief 产生I2C停止条件。 */
static void i2c_stop(void)
{
    sda_write(GPIO_PIN_RESET);
    i2c_delay();
    scl_write(GPIO_PIN_SET);
    i2c_delay();
    sda_write(GPIO_PIN_SET);
    i2c_delay();
}

/** @brief 发送一个字节并读取从机ACK。 */
static bool i2c_write_byte(uint8_t value)
{
    uint8_t bit;
    bool acknowledged;
    for (bit = 0U; bit < 8U; ++bit)
    {
        sda_write((value & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        i2c_delay();
        scl_write(GPIO_PIN_SET);
        i2c_delay();
        scl_write(GPIO_PIN_RESET);
        value <<= 1U;
    }
    sda_write(GPIO_PIN_SET);
    i2c_delay();
    scl_write(GPIO_PIN_SET);
    i2c_delay();
    acknowledged = (HAL_GPIO_ReadPin(TOUCH_SDA_PORT, TOUCH_SDA_PIN) == GPIO_PIN_RESET);
    scl_write(GPIO_PIN_RESET);
    i2c_delay();
    return acknowledged;
}

/** @brief 读取一个字节并发送ACK或NACK。 */
static uint8_t i2c_read_byte(bool acknowledge)
{
    uint8_t bit;
    uint8_t value = 0U;
    sda_write(GPIO_PIN_SET);
    for (bit = 0U; bit < 8U; ++bit)
    {
        value <<= 1U;
        scl_write(GPIO_PIN_SET);
        i2c_delay();
        if (HAL_GPIO_ReadPin(TOUCH_SDA_PORT, TOUCH_SDA_PIN) == GPIO_PIN_SET) value |= 1U;
        scl_write(GPIO_PIN_RESET);
        i2c_delay();
    }
    sda_write(acknowledge ? GPIO_PIN_RESET : GPIO_PIN_SET);
    i2c_delay();
    scl_write(GPIO_PIN_SET);
    i2c_delay();
    scl_write(GPIO_PIN_RESET);
    sda_write(GPIO_PIN_SET);
    i2c_delay();
    return value;
}

/** @brief 初始化触摸总线、复位和中断GPIO。 */
void bsp_i2c_touch_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio.Pin = TOUCH_SCL_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(TOUCH_SCL_PORT, &gpio);
    gpio.Pin = TOUCH_SDA_PIN;
    HAL_GPIO_Init(TOUCH_SDA_PORT, &gpio);
    gpio.Pin = TOUCH_RST_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(TOUCH_RST_PORT, &gpio);
    gpio.Pin = TOUCH_INT_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(TOUCH_INT_PORT, &gpio);
    scl_write(GPIO_PIN_SET);
    sda_write(GPIO_PIN_SET);
    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_SET);
}

/** @brief 提供触摸设备初始化所需的毫秒延时。 */
void bsp_i2c_touch_delay_ms(uint32_t delay_ms)
{
    HAL_Delay(delay_ms);
}

/** @brief 按指定低电平和启动等待时间复位触摸控制器。 */
void bsp_i2c_touch_reset(uint32_t low_ms, uint32_t high_ms)
{
    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(low_ms);
    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(high_ms);
}

/** @brief 读取低电平有效的触摸中断状态。 */
bool bsp_i2c_touch_interrupt_active(void)
{
    return HAL_GPIO_ReadPin(TOUCH_INT_PORT, TOUCH_INT_PIN) == GPIO_PIN_RESET;
}

/** @brief 使用指定长度的寄存器地址执行通用I2C连续读取。 */
static bool read_data(uint8_t write_address, uint8_t read_address,
                      const uint8_t *reg_bytes, uint8_t reg_length,
                      uint8_t *data, uint8_t length)
{
    uint8_t index;
    bool ok = false;
    i2c_start();
    if (!i2c_write_byte(write_address)) goto done;
    for (index = 0U; index < reg_length; ++index)
        if (!i2c_write_byte(reg_bytes[index])) goto done;
    i2c_start();
    if (!i2c_write_byte(read_address)) goto done;
    for (index = 0U; index < length; ++index)
        data[index] = i2c_read_byte(index + 1U < length);
    ok = true;
done:
    i2c_stop();
    return ok;
}

/** @brief 使用指定长度的寄存器地址执行通用I2C连续写入。 */
static bool write_data(uint8_t write_address, const uint8_t *reg_bytes,
                       uint8_t reg_length, const uint8_t *data, uint8_t length)
{
    uint8_t index;
    bool ok = false;
    i2c_start();
    if (!i2c_write_byte(write_address)) goto done;
    for (index = 0U; index < reg_length; ++index)
        if (!i2c_write_byte(reg_bytes[index])) goto done;
    for (index = 0U; index < length; ++index)
        if (!i2c_write_byte(data[index])) goto done;
    ok = true;
done:
    i2c_stop();
    return ok;
}

/** @brief 从8位寄存器地址的触摸控制器读取数据。 */
bool bsp_i2c_touch_read8(uint8_t write_address, uint8_t read_address,
                         uint8_t reg, uint8_t *data, uint8_t length)
{
    return read_data(write_address, read_address, &reg, 1U, data, length);
}

/** @brief 向8位寄存器地址的触摸控制器写入数据。 */
bool bsp_i2c_touch_write8(uint8_t write_address, uint8_t reg,
                          const uint8_t *data, uint8_t length)
{
    return write_data(write_address, &reg, 1U, data, length);
}

/** @brief 从16位寄存器地址的触摸控制器读取数据。 */
bool bsp_i2c_touch_read16(uint8_t write_address, uint8_t read_address,
                          uint16_t reg, uint8_t *data, uint8_t length)
{
    const uint8_t bytes[2] = { (uint8_t)(reg >> 8U), (uint8_t)reg };
    return read_data(write_address, read_address, bytes, 2U, data, length);
}

/** @brief 向16位寄存器地址的触摸控制器写入数据。 */
bool bsp_i2c_touch_write16(uint8_t write_address, uint16_t reg,
                           const uint8_t *data, uint8_t length)
{
    const uint8_t bytes[2] = { (uint8_t)(reg >> 8U), (uint8_t)reg };
    return write_data(write_address, bytes, 2U, data, length);
}
