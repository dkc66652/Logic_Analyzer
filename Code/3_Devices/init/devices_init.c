/** @file devices_init.c @brief SDRAM、LCD 和触摸设备初始化。 */

#include "devices_init.h"

#include "lcd.h"
#include "sdram.h"
#include "touch.h"

/**
 * @brief Devices_Init：devices init。
 */
bool Devices_Init(void)
{
    touch_config_t touch_config;

    if (!sdram_init() || !sdram_selftest()) {
        return false;
    }
    if (lcd_init() != LCD_STATUS_OK) {
        return false;
    }

    touch_config.width = lcd_get_width();
    touch_config.height = lcd_get_height();
    (void)touch_init(&touch_config);
    return true;
}
