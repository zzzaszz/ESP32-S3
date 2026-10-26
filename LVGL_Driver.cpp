#include "LVGL_Driver.h"
#include "Display_ST7789.h"

#include <esp_err.h>
#include <esp_timer.h>

static lv_disp_draw_buf_t drawBuffer;
static lv_color_t drawBufferA[LVGL_BUF_LEN];
static lv_color_t drawBufferB[LVGL_BUF_LEN];

static void flushDisplay(lv_disp_drv_t *display, const lv_area_t *area, lv_color_t *pixels) {
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2,
                reinterpret_cast<uint16_t *>(&pixels->full));
  lv_disp_flush_ready(display);
}

static void advanceLvglTick(void *) {
  lv_tick_inc(2);
}

void Lvgl_Init(void) {
  lv_init();
  lv_disp_draw_buf_init(&drawBuffer, drawBufferA, drawBufferB, LVGL_BUF_LEN);

  static lv_disp_drv_t display;
  lv_disp_drv_init(&display);
  display.hor_res = LVGL_WIDTH;
  display.ver_res = LVGL_HEIGHT;
  display.flush_cb = flushDisplay;
  display.draw_buf = &drawBuffer;
  lv_disp_drv_register(&display);

  esp_timer_create_args_t tickTimerArgs = {};
  tickTimerArgs.callback = advanceLvglTick;
  tickTimerArgs.name = "lvgl_tick";
  esp_timer_handle_t tickTimer = nullptr;
  ESP_ERROR_CHECK(esp_timer_create(&tickTimerArgs, &tickTimer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(tickTimer, 2000));
}

void Lvgl_Loop(void) {
  lv_timer_handler();
}
