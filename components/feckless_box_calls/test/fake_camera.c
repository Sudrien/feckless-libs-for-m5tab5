/* SPDX-License-Identifier: MIT
 * u-feckless-0059: a stand-in for feckless_camera. A 12x8 frame once
 * started, each pixel its own index; it records the bus it was given, the
 * buffer it wrote and how often it was started and stopped. */
#include <stddef.h>
#include "camera.h"

#define CAM_W 12
#define CAM_H 8

int cam_inits, cam_starts, cam_stops, cam_grabs;
bool cam_sensor = true, cam_on;
const void *cam_bus, *cam_dst;
esp_err_t cam_grab_result = ESP_OK;
static bool s_present;

esp_err_t camera_init(i2c_master_bus_handle_t bus)
{
    cam_inits++;
    cam_bus = bus;
    if (!cam_sensor) return ESP_FAIL;
    s_present = true;
    return ESP_OK;
}
bool camera_present(void) { return s_present; }
esp_err_t camera_start(void)
{
    if (!s_present) return ESP_ERR_INVALID_STATE;
    cam_starts++;
    cam_on = true;
    return ESP_OK;
}
void camera_luma_size(int step, int *w, int *h)
{
    if (step < 1) step = 1;
    if (w) *w = cam_on ? (CAM_W + step - 1) / step : 0;
    if (h) *h = cam_on ? (CAM_H + step - 1) / step : 0;
}
esp_err_t camera_grab_luma(uint8_t *dst, int step, int *w, int *h)
{
    if (!cam_on) return ESP_ERR_INVALID_STATE;
    cam_grabs++;
    cam_dst = dst;
    if (cam_grab_result != ESP_OK) return cam_grab_result;
    if (step < 1) step = 1;
    int ow, oh;
    camera_luma_size(step, &ow, &oh);
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) dst[y * ow + x] = (uint8_t)(y * step * CAM_W + x * step);
    if (w) *w = ow;
    if (h) *h = oh;
    return ESP_OK;
}
esp_err_t camera_stop(void)
{
    if (cam_on) cam_stops++;
    cam_on = false;
    return ESP_OK;
}
