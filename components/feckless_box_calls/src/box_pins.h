/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0062: which of the Tab5's pins and I2C addresses a boxed app
 * may have. No ESP-IDF in here: the host test includes it.
 *
 * A boxed app is given what the board's own drivers do not hold. That is
 * a deny list, read from the drivers in this repository, not a list of
 * the pins on the Tab5's headers: a pin no driver here names is the
 * app's to ask for, connected or not.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BOX_PIN_COUNT 55        /* src: soc/esp32p4 soc_caps.h SOC_GPIO_PIN_COUNT */

/* A GPIO the board does not hold. Held:
 *    8-13, 15   the C6: SDIO2, and its EN        src: feckless_network_handler wifi.c
 *                                                 SDIO_PIN_*, wifi.h "SDIO2 on GPIO8-13"
 *   22          the backlight's PWM               src: feckless_graphics_handler lcd.c
 *   23          the touch controller's interrupt  src: feckless_drivers touch.c
 *   24, 25      USB full-speed PHY, the serial/   src: ESP32-P4 datasheet (as remembered;
 *               JTAG console's                        not checked against it)
 *   26-30       I2S to the codecs                 src: feckless_drivers audio_out.c
 *   31, 32      the board's I2C bus               src: feckless_drivers tab5io.c
 *   34-38       the P4's strapping pins, 35 the   src: ESP32-P4 datasheet (as remembered);
 *               boot button, 37/38 UART0's            esp32p4 uart_pins.h U0TXD 37, U0RXD 38
 *   39-44       the SD card: SDIO1                src: feckless_storage_handler storage.c
 */
static inline bool box_pin_free(int pin)
{
    if (pin < 0 || pin >= BOX_PIN_COUNT) return false;
    if (pin >= 8 && pin <= 13) return false;
    if (pin == 15) return false;
    if (pin >= 22 && pin <= 32) return false;
    if (pin >= 34 && pin <= 44) return false;
    return true;
}

/* A 7-bit address on the board's bus that no driver here talks to. Held:
 *   0x10  ES8388    0x40  ES7210                  src: feckless_drivers audio_out.c
 *   0x32  the RTC                                 src: feckless_drivers rtc8130.c
 *   0x41  the INA226                              src: feckless_drivers battery.h
 *   0x43, 0x44  the two expanders                 src: feckless_drivers tab5io.h
 *   0x14, 0x5d  GT911 touch (rev 1)               src: esp_lcd_touch_gt911.h
 *   0x55  ST7123 touch (rev 2)                    src: feckless_drivers touch.c
 *   0x36  the camera's sensor, SC202CS            src: esp_cam_sensor sc202cs.h
 * and I2C's reserved ranges, 0x00-0x07 and 0x78-0x7f. So the BMI270 at
 * 0x68, which nothing here drives, is an app's.
 */
static inline bool box_i2c_addr_free(uint32_t addr)
{
    if (addr < 0x08 || addr > 0x77) return false;
    switch (addr) {
    case 0x10: case 0x14: case 0x32: case 0x36: case 0x40: case 0x41:
    case 0x43: case 0x44: case 0x55: case 0x5d:
        return false;
    default:
        return true;
    }
}
