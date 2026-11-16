#pragma once
/* u-feckless-0059: feckless_camera's API, as camera.h has it */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "tab5io.h"
esp_err_t camera_init(i2c_master_bus_handle_t bus);
bool camera_present(void);
esp_err_t camera_start(void);
void camera_luma_size(int step, int *w, int *h);
esp_err_t camera_grab_luma(uint8_t *dst, int step, int *w, int *h);
esp_err_t camera_stop(void);
