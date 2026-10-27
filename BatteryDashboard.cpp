#include "BatteryDashboard.h"
#include "LVGL_Driver.h"

#include <Arduino.h>

LV_FONT_DECLARE(battery_font_16);

namespace {

struct DeviceDefinition {
  const char *name;
  const lv_font_t *nameFont;
  uint32_t color;
};

struct DeviceState {
  bool seen = false;
  bool fresh = false;
  bool connected = false;
  int level = -1;
  int charging = -1;
  uint32_t updatedAt = 0;
};

struct PerformanceState {
  bool seen = false;
  int cpu = -1;
  int memory = -1;
  int gpu = -1;
  int gpuTemperature = -1;
  uint32_t uploadKbps = 0;
  uint32_t downloadKbps = 0;
  uint32_t diskFreeGb = 0;
  uint32_t uptimeSeconds = 0;
  uint32_t updatedAt = 0;
};

static const DeviceDefinition devices[] = {
    {"HyperX Cloud III S", &lv_font_montserrat_14, 0x42D6B5},
    {"罗技 PRO Wireless", &battery_font_16, 0x64B5F6},
};

static DeviceState states[2];
static lv_obj_t *levels[2] = {};
static lv_obj_t *bars[2] = {};
static lv_obj_t *statuses[2] = {};
static lv_obj_t *ages[2] = {};
static lv_obj_t *sourceLabel = nullptr;
static lv_obj_t *networkLabel = nullptr;
static lv_obj_t *headerNetworkLabel = nullptr;
static lv_obj_t *dashboardView = nullptr;
static lv_obj_t *monitorView = nullptr;
static lv_obj_t *dashboardScroll = nullptr;
static lv_obj_t *monitorScroll = nullptr;
static lv_obj_t *lockOverlay = nullptr;
static lv_obj_t *performanceLabels[8] = {};
static PerformanceState performance;
static bool monitorVisible = false;
static uint32_t lastAgeRefresh = 0;
static const uint32_t kText = 0xF2F5F4;
static const uint32_t kMuted = 0x91A3A5;
static const uint32_t kWarning = 0xF2C14E;
static const uint32_t kDanger = 0xE76F51;

static const lv_font_t *const kChineseFont = &battery_font_16;

static void showDashboard(lv_event_t *) {
  monitorVisible = false;
  lv_obj_add_flag(monitorView, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dashboardView, LV_OBJ_FLAG_HIDDEN);
}

static void showMonitor(lv_event_t *) {
  monitorVisible = true;
  lv_obj_add_flag(dashboardView, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(monitorView, LV_OBJ_FLAG_HIDDEN);
}

static void lockScreen(lv_event_t *) {
  lv_obj_clear_flag(lockOverlay, LV_OBJ_FLAG_HIDDEN);
}

static void unlockScreen(lv_event_t *) {
  lv_obj_add_flag(lockOverlay, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *createActionButton(lv_obj_t *parent, const char *text, lv_coord_t x,
                                    lv_event_cb_t callback) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_pos(button, x, 3);
  lv_obj_set_size(button, 104, 36);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x263840), 0);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(0x3B5962), 0);
  lv_obj_set_style_radius(button, 6, 0);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_center(label);
  return button;
}

static void setPerformanceLabel(lv_obj_t *label, const char *name, int value,
                                const char *unit) {
  if (value < 0) lv_label_set_text_fmt(label, "%s       -- %s", name, unit);
  else lv_label_set_text_fmt(label, "%s       %d %s", name, value, unit);
}

static void updatePerformanceView(void) {
  if (!performance.seen) {
    for (lv_obj_t *label : performanceLabels) {
      if (label) lv_label_set_text(label, "WAITING FOR PC DATA");
    }
    return;
  }
  setPerformanceLabel(performanceLabels[0], "CPU", performance.cpu, "%");
  setPerformanceLabel(performanceLabels[1], "MEM", performance.memory, "%");
  setPerformanceLabel(performanceLabels[2], "GPU", performance.gpu, "%");
  setPerformanceLabel(performanceLabels[3], "GPU TEMP", performance.gpuTemperature, "C");
  lv_label_set_text_fmt(performanceLabels[4], "NET UP    %lu KB/s",
                        static_cast<unsigned long>(performance.uploadKbps));
  lv_label_set_text_fmt(performanceLabels[5], "NET DOWN  %lu KB/s",
                        static_cast<unsigned long>(performance.downloadKbps));
  lv_label_set_text_fmt(performanceLabels[6], "DISK FREE %lu GB",
                        static_cast<unsigned long>(performance.diskFreeGb));
  const uint32_t days = performance.uptimeSeconds / 86400;
  const uint32_t hours = (performance.uptimeSeconds / 3600) % 24;
  const uint32_t minutes = (performance.uptimeSeconds / 60) % 60;
  lv_label_set_text_fmt(performanceLabels[7], "UPTIME    %lud %02lu:%02lu",
                        static_cast<unsigned long>(days), static_cast<unsigned long>(hours),
                        static_cast<unsigned long>(minutes));
}

static lv_obj_t *createLabel(lv_obj_t *parent, const char *text, uint32_t color,
                             const lv_font_t *font, lv_coord_t x, lv_coord_t y,
                             lv_coord_t width = LV_SIZE_CONTENT) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_pos(label, x, y);
  if (width != LV_SIZE_CONTENT) lv_obj_set_width(label, width);
  return label;
}

static const char *statusText(const DeviceState &state) {
  if (!state.seen) return "等待数据";
  if (!state.fresh) return "数据已过期";
  if (!state.connected) return "暂不可用";
  if (state.level < 0 || state.level > 100) return "等待数据";
  if (state.charging < 0) return "充电状态未知";
  return state.charging ? "充电中" : "已连接";
}

static uint32_t statusColor(const DeviceState &state) {
  if (!state.seen || (state.fresh && state.connected && state.level >= 0)) return kMuted;
  return state.fresh ? kWarning : kDanger;
}

static void updateAgeLabel(size_t index, uint32_t now) {
  if (!states[index].seen) {
    lv_label_set_text(ages[index], "最近更新 --");
    return;
  }
  const uint32_t ageSeconds = (now - states[index].updatedAt) / 1000;
  lv_label_set_text_fmt(ages[index], "最近更新 %lu秒前", static_cast<unsigned long>(ageSeconds));
}

static void updateDeviceRow(size_t index, uint32_t now) {
  const DeviceState &state = states[index];
  const bool validLevel = state.fresh && state.connected && state.level >= 0 && state.level <= 100;
  if (validLevel) {
    lv_label_set_text_fmt(levels[index], "%d%%", state.level);
    lv_bar_set_value(bars[index], state.level, LV_ANIM_OFF);
    uint32_t color = devices[index].color;
    if (state.level <= 20) color = kDanger;
    else if (state.charging > 0) color = kWarning;
    lv_obj_set_style_text_color(levels[index], lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(bars[index], lv_color_hex(color), LV_PART_INDICATOR);
  } else {
    lv_label_set_text(levels[index], "--");
    lv_bar_set_value(bars[index], 0, LV_ANIM_OFF);
    lv_obj_set_style_text_color(levels[index], lv_color_hex(kMuted), 0);
  }
  lv_label_set_text(statuses[index], statusText(state));
  lv_obj_set_style_text_color(statuses[index], lv_color_hex(statusColor(state)), 0);
  updateAgeLabel(index, now);
}

static void createDeviceRow(lv_obj_t *screen, size_t index, lv_coord_t y) {
  const DeviceDefinition &device = devices[index];
  lv_obj_t *row = lv_obj_create(screen);
  lv_obj_set_pos(row, 12, y);
  lv_obj_set_size(row, 216, 96);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x1B252C), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(0x2B3941), 0);
  lv_obj_set_style_radius(row, 8, 0);
  lv_obj_set_style_pad_all(row, 0, 0);

  if (index == 0) {
    createLabel(row, LV_SYMBOL_AUDIO, device.color, &lv_font_montserrat_14, 10, 8, 16);
  } else {
    lv_obj_t *mouse = lv_obj_create(row);
    lv_obj_set_pos(mouse, 10, 8);
    lv_obj_set_size(mouse, 16, 22);
    lv_obj_clear_flag(mouse, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(mouse, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mouse, 1, 0);
    lv_obj_set_style_border_color(mouse, lv_color_hex(device.color), 0);
    lv_obj_set_style_radius(mouse, 8, 0);
    lv_obj_set_style_shadow_width(mouse, 0, 0);
    lv_obj_set_style_pad_all(mouse, 0, 0);

    lv_obj_t *wheel = lv_obj_create(row);
    lv_obj_set_pos(wheel, 17, 11);
    lv_obj_set_size(wheel, 2, 5);
    lv_obj_clear_flag(wheel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(wheel, lv_color_hex(device.color), 0);
    lv_obj_set_style_bg_opa(wheel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wheel, 0, 0);
    lv_obj_set_style_radius(wheel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(wheel, 0, 0);
  }

  lv_obj_t *name = createLabel(row, device.name, kText, device.nameFont, 28, 8, 134);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  levels[index] = createLabel(row, "--", kMuted, &lv_font_montserrat_16, 164, 6, 40);
  lv_obj_set_style_text_align(levels[index], LV_TEXT_ALIGN_RIGHT, 0);

  bars[index] = lv_bar_create(row);
  lv_obj_set_pos(bars[index], 10, 42);
  lv_obj_set_size(bars[index], 196, 8);
  lv_bar_set_range(bars[index], 0, 100);
  lv_bar_set_value(bars[index], 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bars[index], lv_color_hex(0x34434A), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bars[index], LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bars[index], LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bars[index], lv_color_hex(device.color), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bars[index], LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bars[index], LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

  statuses[index] = createLabel(row, "等待数据", kMuted, kChineseFont, 10, 54, 194);
  lv_label_set_long_mode(statuses[index], LV_LABEL_LONG_DOT);
  ages[index] = createLabel(row, "最近更新 --", kMuted, kChineseFont, 10, 76, 194);
}

}  // namespace

void BatteryDashboard_Create(void) {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x10171C), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  dashboardView = lv_obj_create(screen);
  lv_obj_set_size(dashboardView, 240, 320);
  lv_obj_set_pos(dashboardView, 0, 0);
  lv_obj_clear_flag(dashboardView, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(dashboardView, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dashboardView, 0, 0);
  lv_obj_set_style_pad_all(dashboardView, 0, 0);

  createLabel(dashboardView, "设备电量", kText, kChineseFont, 12, 8);
  headerNetworkLabel = createLabel(dashboardView, "Wi-Fi 未连接", kMuted, kChineseFont, 108, 8, 120);
  lv_obj_set_style_text_align(headerNetworkLabel, LV_TEXT_ALIGN_RIGHT, 0);

  dashboardScroll = lv_obj_create(dashboardView);
  lv_obj_set_pos(dashboardScroll, 0, 42);
  lv_obj_set_size(dashboardScroll, 240, 232);
  lv_obj_set_style_bg_opa(dashboardScroll, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dashboardScroll, 0, 0);
  lv_obj_set_style_pad_all(dashboardScroll, 0, 0);
  lv_obj_set_scroll_dir(dashboardScroll, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(dashboardScroll, LV_SCROLLBAR_MODE_AUTO);

  createDeviceRow(dashboardScroll, 0, 8);
  createDeviceRow(dashboardScroll, 1, 112);
  sourceLabel = createLabel(dashboardScroll, "数据来源 --", kMuted, kChineseFont, 12, 220, 216);
  networkLabel = createLabel(dashboardScroll, "Wi-Fi 未连接", kMuted, kChineseFont, 12, 244, 216);

  lv_obj_t *dashboardNav = lv_obj_create(dashboardView);
  lv_obj_set_pos(dashboardNav, 0, 278);
  lv_obj_set_size(dashboardNav, 240, 42);
  lv_obj_set_style_bg_opa(dashboardNav, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dashboardNav, lv_color_hex(0x10171C), 0);
  lv_obj_set_style_border_width(dashboardNav, 0, 0);
  lv_obj_set_style_pad_all(dashboardNav, 0, 0);
  createActionButton(dashboardNav, "MONITOR", 8, showMonitor);
  createActionButton(dashboardNav, "LOCK", 120, lockScreen);

  monitorView = lv_obj_create(screen);
  lv_obj_set_size(monitorView, 240, 320);
  lv_obj_set_pos(monitorView, 0, 0);
  lv_obj_clear_flag(monitorView, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(monitorView, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(monitorView, lv_color_hex(0x10171C), 0);
  lv_obj_set_style_border_width(monitorView, 0, 0);
  lv_obj_set_style_pad_all(monitorView, 0, 0);
  lv_obj_add_flag(monitorView, LV_OBJ_FLAG_HIDDEN);
  createLabel(monitorView, "PC MONITOR", kText, &lv_font_montserrat_16, 12, 8);
  monitorScroll = lv_obj_create(monitorView);
  lv_obj_set_pos(monitorScroll, 0, 42);
  lv_obj_set_size(monitorScroll, 240, 232);
  lv_obj_set_style_bg_opa(monitorScroll, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(monitorScroll, 0, 0);
  lv_obj_set_style_pad_all(monitorScroll, 0, 0);
  lv_obj_set_scroll_dir(monitorScroll, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(monitorScroll, LV_SCROLLBAR_MODE_AUTO);
  const char *names[] = {"CPU", "MEM", "GPU", "GPU TEMP", "NET UP", "NET DOWN", "DISK FREE", "UPTIME"};
  for (size_t i = 0; i < 8; ++i) {
    performanceLabels[i] = createLabel(monitorScroll, names[i], kText, &lv_font_montserrat_14,
                                        14, static_cast<lv_coord_t>(10 + i * 28), 210);
  }
  lv_obj_t *monitorNav = lv_obj_create(monitorView);
  lv_obj_set_pos(monitorNav, 0, 278);
  lv_obj_set_size(monitorNav, 240, 42);
  lv_obj_set_style_bg_opa(monitorNav, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(monitorNav, lv_color_hex(0x10171C), 0);
  lv_obj_set_style_border_width(monitorNav, 0, 0);
  lv_obj_set_style_pad_all(monitorNav, 0, 0);
  createActionButton(monitorNav, "BACK", 8, showDashboard);
  createActionButton(monitorNav, "LOCK", 120, lockScreen);

  lockOverlay = lv_btn_create(screen);
  lv_obj_set_pos(lockOverlay, 0, 0);
  lv_obj_set_size(lockOverlay, 240, 320);
  lv_obj_set_style_bg_color(lockOverlay, lv_color_hex(0x10171C), 0);
  lv_obj_set_style_bg_opa(lockOverlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(lockOverlay, 0, 0);
  lv_obj_set_style_radius(lockOverlay, 0, 0);
  lv_obj_add_event_cb(lockOverlay, unlockScreen, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lockTitle = lv_label_create(lockOverlay);
  lv_label_set_text(lockTitle, "SCREEN LOCKED");
  lv_obj_set_style_text_color(lockTitle, lv_color_hex(kText), 0);
  lv_obj_set_style_text_font(lockTitle, &lv_font_montserrat_16, 0);
  lv_obj_align(lockTitle, LV_ALIGN_CENTER, 0, -18);
  lv_obj_t *lockHint = lv_label_create(lockOverlay);
  lv_label_set_text(lockHint, "TAP TO UNLOCK");
  lv_obj_set_style_text_color(lockHint, lv_color_hex(kMuted), 0);
  lv_obj_set_style_text_font(lockHint, &lv_font_montserrat_12, 0);
  lv_obj_align(lockHint, LV_ALIGN_CENTER, 0, 18);
  lv_obj_add_flag(lockOverlay, LV_OBJ_FLAG_HIDDEN);
  updatePerformanceView();
  BatteryDashboard_Loop();
}

void BatteryDashboard_SetTransport(bool wifi) {
  if (sourceLabel) lv_label_set_text(sourceLabel, wifi ? "数据来源 Wi-Fi" : "数据来源 USB");
}

void BatteryDashboard_SetNetwork(bool connected) {
  if (networkLabel) lv_label_set_text(networkLabel, connected ? "Wi-Fi 已连接" : "Wi-Fi 未连接");
  if (headerNetworkLabel) {
    lv_label_set_text(headerNetworkLabel, connected ? "Wi-Fi 已连接" : "Wi-Fi 未连接");
    lv_obj_set_style_text_color(headerNetworkLabel,
                                lv_color_hex(connected ? 0x42D6B5 : kWarning), 0);
  }
}

void BatteryDashboard_Loop(void) {
  const uint32_t now = millis();
  if (now - lastAgeRefresh < 1000) return;
  lastAgeRefresh = now;
  for (size_t i = 0; i < 2; ++i) updateDeviceRow(i, now);
  updatePerformanceView();
}

void BatteryDashboard_Update(int level, bool connected, bool charging, bool fresh) {
  BatteryDashboard_UpdateDevice(1, level, connected, charging, fresh);
}

void BatteryDashboard_UpdateDevice(int device, int level, bool connected, int charging, bool fresh) {
  if (device < 0 || device > 1) return;
  DeviceState &state = states[device];
  state.seen = true;
  state.fresh = fresh;
  state.level = level;
  state.connected = connected;
  state.charging = charging;
  if (fresh) state.updatedAt = millis();
  updateDeviceRow(static_cast<size_t>(device), millis());
}

void BatteryDashboard_UpdatePerformance(int cpu, int memory, int gpu, int gpuTemperature,
                                        uint32_t uploadKbps, uint32_t downloadKbps,
                                        uint32_t diskFreeGb, uint32_t uptimeSeconds) {
  performance.seen = true;
  performance.cpu = cpu;
  performance.memory = memory;
  performance.gpu = gpu;
  performance.gpuTemperature = gpuTemperature;
  performance.uploadKbps = uploadKbps;
  performance.downloadKbps = downloadKbps;
  performance.diskFreeGb = diskFreeGb;
  performance.uptimeSeconds = uptimeSeconds;
  performance.updatedAt = millis();
  updatePerformanceView();
}
