#include "LVGL_Driver.h"
#include "Display_ST7789.h"
#include "Touch_Driver.h"

#include <esp_err.h>
#include <esp_timer.h>

static lv_disp_draw_buf_t drawBuffer;
static lv_color_t drawBufferA[LVGL_BUF_LEN];
static lv_color_t drawBufferB[LVGL_BUF_LEN];
static uint16_t *capturePixels = nullptr;

static void flushDisplay(lv_disp_drv_t *display, const lv_area_t *area, lv_color_t *pixels) {
  if (capturePixels) {
    const int width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y) {
      if (y % 2) continue;
      for (int x = area->x1; x <= area->x2; ++x) {
        if (x % 2 == 0) capturePixels[(y / 2) * 160 + x / 2] = pixels[(y - area->y1) * width + x - area->x1].full;
      }
    }
  }
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

  TouchDriver_Init();
  static lv_indev_drv_t touch;
  lv_indev_drv_init(&touch);
  touch.type = LV_INDEV_TYPE_POINTER;
  touch.read_cb = TouchDriver_Read;
  lv_indev_drv_register(&touch);

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

void Lvgl_PrintFrame(void) {
  capturePixels = static_cast<uint16_t *>(calloc(160 * 120, sizeof(uint16_t)));
  if (!capturePixels) { Serial.println("ERR FRAME CAPTURE MEMORY"); return; }
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(nullptr);
  Serial.println("FRAME BEGIN 160 120");
  const char hex[] = "0123456789ABCDEF";
  char row[641];
  row[640] = '\0';
  for (unsigned y = 0; y < 120; ++y) {
    for (unsigned x = 0; x < 160; ++x) {
      const uint16_t value = capturePixels[y * 160 + x];
      for (unsigned digit = 0; digit < 4; ++digit) row[x * 4 + digit] = hex[(value >> (12 - 4 * digit)) & 15];
    }
    Serial.println(row);
  }
  Serial.println("FRAME END");
  free(capturePixels);
  capturePixels = nullptr;
}
