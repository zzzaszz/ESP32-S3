#pragma once

#include <lvgl.h>

// Waveshare V1/V2 share I2C pins; the controller is selected by probing the
// official CST328 and CST3530 addresses during startup.
bool TouchDriver_Init(void);
void TouchDriver_Read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
