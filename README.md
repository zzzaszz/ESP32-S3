# Windows 10 Wireless Battery Display

An ESP32-S3-Touch-LCD-2.8 display for Windows wireless-device battery status. `windows/BatteryBridge.ps1` reads supported Logitech devices from G HUB and sends their live status to the display over USB serial or HTTP on Wi-Fi. Devices without a verified reader show `--`.

## Display

- 240 x 320 ST7789 screen with the Waveshare board pin mapping
- LVGL 8 UI with device status and battery level
- Unknown and stale data are rendered as `--`
- Touch initialization is omitted. The screen-only demo therefore does not depend on the V1 CST328 or V2 CST3530 touch controller.

Currently supported sources:

| Device | Reader | Status |
| --- | --- | --- |
| Logitech PRO Wireless (`pro_wireless_mouse`) | Logitech G HUB local WebSocket API | Live percentage and charging state verified |
| HyperX Cloud III S Wireless (`VID_03F0&PID_02CC`) | Not implemented | No matching public battery protocol confirmed; shows `--` |

## Build

1. Install Arduino IDE 2.x and the Espressif ESP32 board package. The display driver uses the Arduino-ESP32 3.x LEDC API.
2. Install LVGL 8.3.10. The bundled `lv_conf.h` is based on the Waveshare example configuration.
3. Open `ESP32-S3-Touch-LCD-2.8-Battery-Display.ino` from this folder. The LVGL dashboard is implemented in `BatteryDashboard.cpp`, outside Arduino's `.ino` prototype generation.
4. Select `ESP32S3 Dev Module`, select the board's detected USB port, and build/upload.
5. With the board on `COM12`, run the Windows bridge from PowerShell:

   ```powershell
   .\windows\BatteryBridge.ps1 -Once
   ```

   Leave out `-Once` to poll every 15 seconds. Close Arduino Serial Monitor first, because the bridge owns the COM port while it runs.

The board currently receives data over USB serial. To test its optional Wi-Fi HTTP receiver, first configure a 2.4 GHz network over the USB serial console with `WIFI<TAB>ssid<TAB>password`, query the board address with `IP`, then run `BatteryBridge.ps1 -PanelUrl http://BOARD_IP`. After the network transfer is verified, the board can run independently of a USB data connection (it still needs power).

## Board pin mapping

These pins come from Waveshare's `Display_ST7789` Arduino example for ESP32-S3-Touch-LCD-2.8. Do not reuse them for a different board.

| Signal | GPIO |
| --- | ---: |
| LCD MOSI | 45 |
| LCD SCLK | 40 |
| LCD CS | 42 |
| LCD DC | 41 |
| LCD RST | 39 |
| Backlight | 5 |

## Device and transport boundaries

- The current board and Windows bridge were compiled/run together. A successful serial frame is acknowledged by the board as `ACK BAT1 <level>`.
- The 2.4 GHz receiver attached to Windows cannot be queried by ESP32 BLE. Direct standalone access would require USB Host hardware plus a verified vendor protocol.
- The HyperX device is identified on USB as `VID_03F0&PID_02CC`; the BLE probe did not discover it because it uses a 2.4 GHz receiver. Do not send commands copied from unrelated HyperX headset models.
- G HUB's local WebSocket is read-only in this bridge. The bridge sends a versioned, validated frame and sends unknown status when G HUB has no current reading.

## Source notes

The ST7789 initialization and board pin mapping are adapted from the Waveshare ESP32-S3-Touch-LCD-2.8 Arduino demo. The upstream demo currently includes CST328 touch code; that touch path is intentionally excluded here. Reference: <https://docs.waveshare.net/ESP32-S3-Touch-LCD-2.8/Arduino>.
