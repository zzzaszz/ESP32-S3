#include "BatteryDashboard.h"
#include "BatteryTransport.h"
#include "LVGL_Driver.h"
#include <Arduino.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(battery_font_12);

namespace {
constexpr uint32_t kText = 0xEAF4FF;
constexpr uint32_t kMuted = 0x8CA8C2;
constexpr uint32_t kWarning = 0xF2C14E;
constexpr uint32_t kDanger = 0xE76F51;
constexpr uint32_t kColors[] = {0x35C7FF, 0x29D98A};
constexpr uint32_t kBackground = 0x041529;
constexpr uint32_t kSurface = 0x082642;
constexpr uint32_t kBorder = 0x15466D;
const lv_font_t *const kFont = &battery_font_12;

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
  // CPU usage, CPU temperature, GPU usage, GPU temperature, memory usage.
  int values[5] = {-1, -1, -1, -1, -1};
  uint32_t up = 0, down = 0, updatedAt = 0;
};

DeviceState states[2];
PerformanceState performance;
lv_obj_t *views[2] = {};
lv_obj_t *viewport = nullptr, *pageDots[2] = {}, *usageBars[3] = {};
lv_obj_t *levels[2] = {}, *bars[2] = {}, *statuses[2] = {}, *ages[2] = {};
lv_obj_t *metrics[7] = {}, *metricValues[7] = {};
lv_obj_t *brandTitle = nullptr, *wifiIcon = nullptr, *clockLabel = nullptr;
lv_obj_t *message = nullptr, *pageNumber = nullptr;
lv_obj_t *refreshButton = nullptr;
uint8_t currentPage = 0;
bool wifiConnected = false;
bool wifiTransport = false;
bool refreshing = false;
uint8_t refreshResponses = 0;
uint32_t refreshStartedAt = 0, lastAgeRefresh = 0;
bool sliding = false;
uint8_t outgoingPage = 0;
int slideDirection = 1;

void updateClock() {
  const time_t now = time(nullptr);
  struct tm localTime;
  if (now < 1704067200 || !localtime_r(&now, &localTime)) {
    lv_label_set_text(clockLabel, "--/-- --:--");
    return;
  }
  lv_label_set_text_fmt(clockLabel, "%02d/%02d %02d:%02d",
                        localTime.tm_mon + 1, localTime.tm_mday,
                        localTime.tm_hour, localTime.tm_min);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, const lv_font_t *font,
                    uint32_t color, int x, int y, int width) {
  lv_obj_t *obj = lv_label_create(parent);
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_width(obj, width);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
  return obj;
}

lv_obj_t *makeSurface(lv_obj_t *parent, int x, int y, int width, int height) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, width, height);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  return obj;
}

const char *statusText(const DeviceState &state) {
  if (!state.seen) return "等待数据";
  if (!state.fresh) return "数据已过期";
  if (!state.connected) return "暂不可用";
  if (state.level < 0 || state.level > 100) return "电量未知";
  if (state.charging < 0) return "充电状态未知";
  return state.charging ? "充电中" : "已连接";
}

void updateDevice(size_t index) {
  const DeviceState &state = states[index];
  const bool valid = state.fresh && state.connected && state.level >= 0 && state.level <= 100;
  uint32_t color = kMuted;
  if (valid) {
    color = state.level <= 20 ? kDanger : state.charging > 0 ? kWarning : kColors[index];
    lv_label_set_text_fmt(levels[index], "%d%%", state.level);
  } else {
    lv_label_set_text(levels[index], "--");
  }
  lv_obj_set_style_text_color(levels[index], lv_color_hex(color), 0);
  lv_bar_set_value(bars[index], valid ? state.level : 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bars[index], lv_color_hex(color), LV_PART_INDICATOR);
  lv_label_set_text(statuses[index], statusText(state));
  const uint32_t stateColor = valid ? (state.charging > 0 ? kWarning : kColors[1]) :
      (!state.seen ? kMuted : state.fresh ? kWarning : kDanger);
  lv_obj_set_style_text_color(statuses[index], lv_color_hex(stateColor), 0);
  if (!state.seen) lv_label_set_text(ages[index], "更新 --");
  else lv_label_set_text_fmt(ages[index], "更新 %lus", (unsigned long)((millis() - state.updatedAt) / 1000));
}

void updateMetrics() {
  const char *names[] = {"CPU", "CPU TEMP", "GPU", "GPU TEMP", "MEM", "UP", "DOWN"};
  const bool fresh = performance.seen && (uint32_t)(millis() - performance.updatedAt) <= 45000;
  for (size_t i = 0; i < 5; ++i) {
    lv_label_set_text(metrics[i], names[i]);
    if (!fresh || performance.values[i] < 0) lv_label_set_text(metricValues[i], "--");
    else lv_label_set_text_fmt(metricValues[i], "%d%s", performance.values[i],
                               i == 1 || i == 3 ? " C" : "%");
  }
  const size_t barValues[] = {0, 2, 4};
  for (size_t i = 0; i < 3; ++i) {
    const int value = performance.values[barValues[i]];
    lv_bar_set_value(usageBars[i], fresh && value >= 0 ? value : 0, LV_ANIM_OFF);
  }
  if (!fresh) {
    lv_label_set_text(metricValues[5], "--");
    lv_label_set_text(metricValues[6], "--");
    return;
  }
  lv_label_set_text(metrics[5], names[5]);
  lv_label_set_text(metrics[6], names[6]);
  lv_label_set_text_fmt(metricValues[5], "%lu KB/s", (unsigned long)performance.up);
  lv_label_set_text_fmt(metricValues[6], "%lu KB/s", (unsigned long)performance.down);
}

void settlePages() {
  for (uint8_t i = 0; i < 2; ++i) {
    lv_obj_set_x(views[i], i == currentPage ? 0 : 304);
    lv_obj_set_style_bg_color(pageDots[i], lv_color_hex(i == currentPage ? kColors[i] : kBorder), 0);
  }
  sliding = false;
  lv_obj_invalidate(lv_scr_act());
}

void slideStep(void *, int32_t progress) {
  lv_obj_set_x(views[outgoingPage], -slideDirection * progress);
  lv_obj_set_x(views[currentPage], slideDirection * (304 - progress));
}

void slideReady(lv_anim_t *) { settlePages(); }

void selectPage(uint8_t page, bool animated = false, int direction = 1) {
  lv_anim_del(viewport, slideStep);
  settlePages();
  outgoingPage = currentPage;
  currentPage = page;
  lv_label_set_text(pageNumber, page == 0 ? "1 / 2" : "2 / 2");
  if (!animated || outgoingPage == page) {
    settlePages();
    lv_refr_now(nullptr);
    return;
  }
  sliding = true;
  slideDirection = direction;
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, viewport);
  lv_anim_set_exec_cb(&animation, slideStep);
  lv_anim_set_values(&animation, 0, 304);
  lv_anim_set_time(&animation, 180);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_ready_cb(&animation, slideReady);
  lv_anim_start(&animation);
}

void refreshEvent(lv_event_t *) { BatteryTransport_RequestRefresh(); }

void horizontalSwipe(int direction) {
  if (!sliding) selectPage(currentPage == 0 ? 1 : 0, true, direction);
}

void gestureEvent(lv_event_t *event) {
  lv_indev_t *input = lv_indev_get_act();
  if (!input) return;
  const lv_dir_t direction = lv_indev_get_gesture_dir(input);
  if (direction == LV_DIR_LEFT || direction == LV_DIR_RIGHT) horizontalSwipe(direction == LV_DIR_LEFT ? 1 : -1);
  else if (direction == LV_DIR_BOTTOM) BatteryTransport_RequestRefresh();
  lv_event_stop_bubbling(event);
  lv_indev_wait_release(input);
}

void createDeviceTile(size_t index, int x) {
  lv_obj_t *column = makeSurface(views[0], x, 6, 148, 80);
  lv_obj_set_style_bg_color(column, lv_color_hex(kSurface), 0);
  lv_obj_set_style_bg_opa(column, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(column, 1, 0);
  lv_obj_set_style_border_color(column, lv_color_hex(kBorder), 0);
  lv_obj_set_style_radius(column, 8, 0);
  makeLabel(column, index == 0 ? LV_SYMBOL_AUDIO : LV_SYMBOL_BLUETOOTH,
            &lv_font_montserrat_14, kColors[index], 8, 5, 16);
  makeLabel(column, index == 0 ? "HyperX Cloud III S" : "PRO Wireless",
            &lv_font_montserrat_12, kText, 28, 5, 116);
  makeLabel(column, index == 0 ? "HEADSET" : "MOUSE", &lv_font_montserrat_12, kMuted, 28, 22, 116);
  levels[index] = makeLabel(column, "--", &lv_font_montserrat_16, kColors[index], 2, 37, 142);
  lv_obj_set_style_text_align(levels[index], LV_TEXT_ALIGN_CENTER, 0);
  bars[index] = lv_bar_create(column);
  lv_obj_set_pos(bars[index], 2, 57);
  lv_obj_set_size(bars[index], 142, 5);
  lv_bar_set_range(bars[index], 0, 100);
  lv_obj_set_style_bg_color(bars[index], lv_color_hex(kBorder), LV_PART_MAIN);
  lv_obj_set_style_radius(bars[index], 3, LV_PART_MAIN);
  lv_obj_set_style_radius(bars[index], 3, LV_PART_INDICATOR);
  statuses[index] = makeLabel(column, "等待数据", kFont, kMuted, 2, 66, 76);
  ages[index] = makeLabel(column, "最近更新 --", kFont, kMuted, 78, 66, 66);
  updateDevice(index);
}
}  // namespace

void BatteryDashboard_Create(void) {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(screen, gestureEvent, LV_EVENT_GESTURE, nullptr);
  lv_obj_t *header = makeSurface(screen, 8, 3, 304, 28);
  lv_obj_set_style_bg_color(header, lv_color_hex(kSurface), 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(header, 1, 0);
  lv_obj_set_style_border_color(header, lv_color_hex(kBorder), 0);
  lv_obj_set_style_radius(header, 4, 0);
  wifiIcon = makeLabel(screen, LV_SYMBOL_WIFI, &lv_font_montserrat_14, kMuted, 14, 8, 18);
  brandTitle = makeLabel(screen, "DEVICE HUB", &lv_font_montserrat_12, kText, 115, 10, 90);
  lv_obj_set_style_text_align(brandTitle, LV_TEXT_ALIGN_CENTER, 0);
  clockLabel = makeLabel(screen, "--/-- --:--", &lv_font_montserrat_12, kMuted, 205, 10, 72);
  lv_obj_set_style_text_align(clockLabel, LV_TEXT_ALIGN_RIGHT, 0);
  refreshButton = lv_btn_create(screen);
  lv_obj_set_pos(refreshButton, 282, 2);
  lv_obj_set_size(refreshButton, 30, 28);
  lv_obj_set_style_radius(refreshButton, 4, 0);
  lv_obj_set_style_bg_color(refreshButton, lv_color_hex(kSurface), 0);
  lv_obj_set_style_border_width(refreshButton, 1, 0);
  lv_obj_set_style_border_color(refreshButton, lv_color_hex(kBorder), 0);
  lv_obj_set_style_shadow_width(refreshButton, 0, 0);
  lv_obj_add_event_cb(refreshButton, refreshEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *icon = lv_label_create(refreshButton);
  lv_label_set_text(icon, LV_SYMBOL_REFRESH);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
  lv_obj_center(icon);
  viewport = makeSurface(screen, 8, 34, 304, 174);
  lv_obj_set_style_bg_color(viewport, lv_color_hex(kBackground), 0);
  lv_obj_set_style_bg_opa(viewport, LV_OPA_COVER, 0);
  lv_obj_clear_flag(viewport, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  views[0] = makeSurface(viewport, 0, 0, 304, 174);
  views[1] = makeSurface(viewport, 304, 0, 304, 174);
  for (unsigned i = 0; i < 2; ++i) {
    lv_obj_set_style_bg_color(views[i], lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(views[i], LV_OPA_COVER, 0);
  }
  createDeviceTile(0, 0);
  createDeviceTile(1, 156);
  const char *names[] = {"CPU", "CPU TEMP", "GPU", "GPU TEMP", "MEM", "UP", "DOWN"};
  for (size_t i = 0; i < 7; ++i) {
    const bool memoryRow = i == 4;
    const int x = memoryRow ? 0 : (i % 2 == 0 ? 0 : 156);
    const int y = i < 4 ? 4 + (i / 2) * 42 : (memoryRow ? 88 : 130);
    const int width = memoryRow ? 304 : 148;
    lv_obj_t *card = makeSurface(views[1], x, y, width, 36);
    lv_obj_set_style_bg_color(card, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(kBorder), 0);
    metrics[i] = makeLabel(card, names[i], &lv_font_montserrat_12, kMuted, 8, 3, width - 16);
    metricValues[i] = makeLabel(card, "--", &lv_font_montserrat_16, kText, 8, 13, width - 16);
    lv_obj_set_style_text_align(metricValues[i], LV_TEXT_ALIGN_CENTER, 0);
    if (i == 0 || i == 2 || i == 4) {
      const size_t barIndex = i == 0 ? 0 : i == 2 ? 1 : 2;
      usageBars[barIndex] = lv_bar_create(card);
      lv_obj_set_pos(usageBars[barIndex], 8, 32);
      lv_obj_set_size(usageBars[barIndex], width - 16, 3);
      lv_bar_set_range(usageBars[barIndex], 0, 100);
      lv_obj_set_style_bg_color(usageBars[barIndex], lv_color_hex(kBorder), LV_PART_MAIN);
      lv_obj_set_style_bg_color(usageBars[barIndex], lv_color_hex(barIndex == 1 ? kColors[1] : kColors[0]), LV_PART_INDICATOR);
    }
  }
  message = makeLabel(screen, "等待数据", kFont, kMuted, 8, 218, 210);
  for (unsigned i = 0; i < 2; ++i) {
    pageDots[i] = makeSurface(screen, 230 + i * 17, 225, 12, 4);
    lv_obj_set_style_bg_opa(pageDots[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pageDots[i], 2, 0);
    lv_obj_clear_flag(pageDots[i], LV_OBJ_FLAG_CLICKABLE);
  }
  pageNumber = makeLabel(screen, "1 / 2", &lv_font_montserrat_12, kMuted, 270, 218, 42);
  lv_obj_set_style_text_align(pageNumber, LV_TEXT_ALIGN_RIGHT, 0);
  selectPage(0);
  for (uint8_t i = 0; i < 2; ++i) {
    lv_obj_add_event_cb(views[i], gestureEvent, LV_EVENT_GESTURE, nullptr);
  }
  updateMetrics();
  updateClock();
}

namespace {
void opacityStep(void *object, int32_t value) { lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), value, 0); }
}

void BatteryDashboard_PrepareRotation(void) {
  lv_anim_del(viewport, slideStep);
  lv_anim_del(viewport, opacityStep);
  settlePages();
  lv_obj_set_style_opa(viewport, LV_OPA_70, 0);
}

void BatteryDashboard_FinishRotation(void) {
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(nullptr);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, viewport);
  lv_anim_set_exec_cb(&animation, opacityStep);
  lv_anim_set_values(&animation, LV_OPA_70, LV_OPA_COVER);
  lv_anim_set_time(&animation, 140);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_start(&animation);
}

void BatteryDashboard_RefreshStarted(void) {
  refreshing = true;
  refreshResponses = 0;
  refreshStartedAt = millis();
  lv_label_set_text(message, "正在读取电量");
  lv_obj_add_state(refreshButton, LV_STATE_DISABLED);
}

void BatteryDashboard_Loop(void) {
  const uint32_t now = millis();
  if (refreshing && (uint32_t)(now - refreshStartedAt) > 20000) {
    refreshing = false;
    lv_label_set_text(message, "采集程序未响应");
    lv_obj_clear_state(refreshButton, LV_STATE_DISABLED);
  }
  if ((uint32_t)(now - lastAgeRefresh) < 1000) return;
  lastAgeRefresh = now;
  updateDevice(0);
  updateDevice(1);
  updateMetrics();
  updateClock();
}

void BatteryDashboard_SetTransport(bool wifi) {
  wifiTransport = wifi;
  lv_obj_set_style_text_color(wifiIcon, lv_color_hex(wifiTransport && wifiConnected ? kColors[0] :
                                                     wifiTransport ? kMuted : kColors[1]), 0);
}

void BatteryDashboard_SetNetwork(bool connected) {
  wifiConnected = connected;
  BatteryDashboard_SetTransport(wifiTransport);
}

void BatteryDashboard_Update(int level, bool connected, bool charging, bool fresh) {
  BatteryDashboard_UpdateDevice(1, level, connected, charging, fresh);
}

void BatteryDashboard_UpdateDevice(int device, int level, bool connected, int charging, bool fresh) {
  if (device < 0 || device > 1) return;
  DeviceState &state = states[device];
  state.seen = true;
  state.fresh = fresh;
  state.level = connected ? level : -1;
  state.connected = connected;
  state.charging = charging;
  if (fresh) state.updatedAt = millis();
  updateDevice(device);
  if (refreshing && fresh) {
    refreshResponses |= 1 << device;
    if (refreshResponses == 3) {
      refreshing = false;
      lv_label_set_text(message, "已更新");
      lv_obj_clear_state(refreshButton, LV_STATE_DISABLED);
    }
  } else if (!refreshing) {
    if (fresh) lv_label_set_text(message, "已更新");
    else if (!states[0].fresh && !states[1].fresh) lv_label_set_text(message, "数据已过期");
  }
}

void BatteryDashboard_UpdatePerformance(int cpu, int cpuTemperature, int memory, int gpu, int temperature,
                                        uint32_t up, uint32_t down, uint32_t disk, uint32_t uptime) {
  performance.seen = true;
  performance.values[0] = cpu;
  performance.values[1] = cpuTemperature;
  performance.values[2] = gpu;
  performance.values[3] = temperature;
  performance.values[4] = memory;
  performance.up = up;
  performance.down = down;
  (void)disk;
  (void)uptime;
  performance.updatedAt = millis();
  updateMetrics();
}

namespace {
unsigned checkBounds(lv_obj_t *parent) {
  unsigned invalid = 0;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
    lv_obj_t *child = lv_obj_get_child(parent, i);
    if (child == views[1 - currentPage] || sliding) continue;
    lv_area_t area;
    lv_obj_get_coords(child, &area);
    if (area.x1 < 0 || area.y1 < 0 || area.x2 >= 320 || area.y2 >= 240) ++invalid;
    invalid += checkBounds(child);
  }
  return invalid;
}
}

void BatteryDashboard_PrintDiagnostics(void) {
  lv_obj_update_layout(lv_scr_act());
  Serial.printf("UI screen=%dx%d page=%u font=12 lock=removed boundsErrors=%u refreshing=%u\n",
      lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr), currentPage,
      checkBounds(lv_scr_act()), refreshing);
  for (unsigned i = 0; i < 2; ++i) {
    Serial.printf("UI device=%u level=%s connected=%u charging=%d fresh=%u\n",
        i, lv_label_get_text(levels[i]), states[i].connected, states[i].charging, states[i].fresh);
  }
  Serial.printf("UI perfSeen=%u perfAge=%lu monitorVisible=%u\n", performance.seen,
      performance.seen ? (unsigned long)((millis() - performance.updatedAt) / 1000) : 0UL,
      currentPage == 1 && lv_obj_is_visible(views[1]));
  const bool bitmapReady = lv_font_get_glyph_bitmap(kFont, 'C') && lv_font_get_glyph_bitmap(kFont, 0x5185);
  Serial.printf("UI sliding=%u fontBitmap=%s render=opaque-cards\n", sliding, bitmapReady ? "ready" : "missing");
  for (unsigned i = 0; i < 7; ++i) {
    Serial.printf("UI metric=%u label=%s value=%s\n", i,
        lv_label_get_text(metrics[i]), lv_label_get_text(metricValues[i]));
  }
}

bool BatteryDashboard_Command(const char *command) {
  if (strcmp(command, "PAGE 1") == 0) selectPage(0);
  else if (strcmp(command, "PAGE 2") == 0) selectPage(1);
  else if (strcmp(command, "SWIPE LEFT") == 0 || strcmp(command, "SWIPE RIGHT") == 0) horizontalSwipe(strcmp(command, "SWIPE LEFT") == 0 ? 1 : -1);
  else return false;
  BatteryDashboard_PrintDiagnostics();
  return true;
}
