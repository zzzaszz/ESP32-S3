#pragma once

#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE
#endif

#include <lvgl.h>

#define LVGL_WIDTH 240
#define LVGL_HEIGHT 320
#define LVGL_BUF_LEN (LVGL_WIDTH * LVGL_HEIGHT / 20)

void Lvgl_Init(void);
void Lvgl_Loop(void);
