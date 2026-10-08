/** @file bsp_i2c_touch.h @brief 板级触摸软件I2C、复位和中断接口。 */
#ifndef __BSP_I2C_TOUCH_H
#define __BSP_I2C_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

void bsp_i2c_touch_init(void);
void bsp_i2c_touch_delay_ms(uint32_t delay_ms);
void bsp_i2c_touch_reset(uint32_t low_ms, uint32_t high_ms);
bool bsp_i2c_touch_interrupt_active(void);
bool bsp_i2c_touch_read8(uint8_t write_address, uint8_t read_address,
                         uint8_t reg, uint8_t *data, uint8_t length);
bool bsp_i2c_touch_write8(uint8_t write_address, uint8_t reg,
                          const uint8_t *data, uint8_t length);
bool bsp_i2c_touch_read16(uint8_t write_address, uint8_t read_address,
                          uint16_t reg, uint8_t *data, uint8_t length);
bool bsp_i2c_touch_write16(uint8_t write_address, uint16_t reg,
                           const uint8_t *data, uint8_t length);

#endif /* __BSP_I2C_TOUCH_H */
