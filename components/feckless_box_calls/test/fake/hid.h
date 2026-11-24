/* Test stand-in (u-feckless-0086) */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef enum { HID_BTN_VOL_UP = 0, HID_BTN_VOL_DOWN, HID_BTN_MUTE, HID_BTN_MIC_MUTE, HID_BTN_PLAY_PAUSE,
               HID_BTN_NEXT, HID_BTN_PREV, HID_BTN_STOP } hid_button_t;
typedef void (*hid_button_cb_t)(hid_button_t button);
esp_err_t hid_init(hid_button_cb_t cb);
bool hid_present(void);
