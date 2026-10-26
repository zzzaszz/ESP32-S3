#include "BatteryDashboard.h"
#include "LVGL_Driver.h"

#include <Arduino.h>

struct DemoDevice {
  const char *name;
  const char *kind;
  uint8_t level;
  uint32_t color;
};

static constexpr DemoDevice devices[] = {
    {"HyperX Cloud III S", "WAITING", 0, 0x42D6B5},
    {"PRO Wireless", "WAITING", 0, 0x64B5F6},
};

static lv_obj_t *levels[2];
static lv_obj_t *bars[2];
static lv_obj_t *statuses[2];
static lv_obj_t *footer;

static lv_obj_t *createLabel(lv_obj_t *parent, const char *text, uint32_t color,
                             const lv_font_t *font, lv_coord_t x, lv_coord_t y) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_pos(label, x, y);
  return label;
}

static void createDeviceRow(lv_obj_t *screen, const DemoDevice &device, lv_coord_t y) {
  lv_obj_t *row = lv_obj_create(screen);
  lv_obj_set_pos(row, 10, y);
  lv_obj_set_size(row, 220, 56);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x1B252C), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(0x2B3941), 0);
  lv_obj_set_style_radius(row, 8, 0);
  lv_obj_set_style_pad_all(row, 0, 0);

  lv_obj_t *name = createLabel(row, device.name, 0xF2F5F4, &lv_font_montserrat_14, 10, 7);
  lv_obj_set_width(name, 150);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_t *status = createLabel(row, device.kind, 0x91A3A5, &lv_font_montserrat_12, 10, 29);

  char levelText[6];
  snprintf(levelText, sizeof(levelText), "--");
  lv_obj_t *level = createLabel(row, levelText, device.color, &lv_font_montserrat_16, 168, 6);
  lv_obj_set_width(level, 42);
  lv_obj_set_style_text_align(level, LV_TEXT_ALIGN_RIGHT, 0);

  lv_obj_t *bar = lv_bar_create(row);
  lv_obj_set_pos(bar, 10, 45);
  lv_obj_set_size(bar, 200, 5);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, device.level, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x34434A), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(device.color), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  const size_t index = &device - devices;
  levels[index] = level;
  bars[index] = bar;
  statuses[index] = status;
}

void BatteryDashboard_Create(void) {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x10171C), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  createLabel(screen, "BATTERY PANEL", 0xF2F5F4, &lv_font_montserrat_16, 12, 12);
  createLabel(screen, "WINDOWS BATTERY", 0x91A3A5, &lv_font_montserrat_12, 12, 36);

  lv_obj_t *demoTag = lv_label_create(screen);
  lv_label_set_text(demoTag, "LIVE");
  lv_obj_set_style_text_color(demoTag, lv_color_hex(0x10171C), 0);
  lv_obj_set_style_text_font(demoTag, &lv_font_montserrat_12, 0);
  lv_obj_set_style_bg_color(demoTag, lv_color_hex(0xF2C14E), 0);
  lv_obj_set_style_bg_opa(demoTag, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(demoTag, 4, 0);
  lv_obj_set_style_pad_hor(demoTag, 7, 0);
  lv_obj_set_style_pad_ver(demoTag, 3, 0);
  lv_obj_align(demoTag, LV_ALIGN_TOP_RIGHT, -12, 12);

  for (size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); ++i) {
    createDeviceRow(screen, devices[i], 66 + static_cast<lv_coord_t>(i) * 64);
  }

  footer = createLabel(screen, "WAITING FOR WINDOWS", 0xF2C14E, &lv_font_montserrat_12, 12, 218);
  createLabel(screen, "HEADSET + MOUSE", 0x91A3A5,
              &lv_font_montserrat_12, 12, 246);
  createLabel(screen, "Unknown battery shown as --", 0x91A3A5,
              &lv_font_montserrat_12, 12, 267);
}

void BatteryDashboard_Update(int level, bool connected, bool charging, bool fresh) {
  BatteryDashboard_UpdateDevice(1, level, connected, charging, fresh);
}

void BatteryDashboard_UpdateDevice(int device, int level, bool connected, int charging, bool fresh) {
  if (device < 0 || device > 1) return;
  if (!fresh || !connected || level < 0 || level > 100) {
    lv_label_set_text(levels[device], "--");
    lv_bar_set_value(bars[device], 0, LV_ANIM_OFF);
    lv_label_set_text(statuses[device], !fresh ? "STALE / WAITING" : !connected ? "UNAVAILABLE" : "UNKNOWN");
  } else {
    lv_label_set_text_fmt(levels[device], "%d%%", level);
    lv_bar_set_value(bars[device], level, LV_ANIM_OFF);
    lv_label_set_text(statuses[device], charging < 0 ? "CHARGE UNKNOWN" : charging ? "ON CHARGE" : "CONNECTED");
  }
  lv_label_set_text(footer, "SOURCE: WINDOWS");
}
