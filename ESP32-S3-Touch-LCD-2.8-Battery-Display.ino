#include "Display_ST7789.h"
#include "LVGL_Driver.h"
#include "BatteryDashboard.h"
#include "BatteryTransport.h"

void setup(void) {
  Serial.begin(115200);
  Backlight_Init();
  LCD_Init();
  Lvgl_Init();
  BatteryDashboard_Create();
  BatteryTransport_Init();
  Serial.println("Battery panel ready; waiting for Windows BAT1 frames.");
}

void loop(void) {
  Lvgl_Loop();
  BatteryTransport_Loop();
  delay(5);
}
