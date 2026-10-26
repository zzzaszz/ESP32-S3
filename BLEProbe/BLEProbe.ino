#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

static BLEScan *scanner;
static uint32_t scanNumber = 0;

static void printHex(const String &value) {
  for (size_t i = 0; i < value.length(); ++i) {
    Serial.printf("%02X", static_cast<uint8_t>(value[i]));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  BLEDevice::init("BatteryPanelProbe");
  scanner = BLEDevice::getScan();
  scanner->setActiveScan(true);
  scanner->setInterval(100);
  scanner->setWindow(80);
  Serial.println("BLE probe ready; scan results follow every 8 seconds.");
}

void loop() {
  ++scanNumber;
  Serial.printf("\nSCAN %lu BEGIN\n", static_cast<unsigned long>(scanNumber));

  BLEScanResults *results = scanner->start(8, false);
  if (results != nullptr) {
    for (int i = 0; i < results->getCount(); ++i) {
      BLEAdvertisedDevice device = results->getDevice(i);
      Serial.printf("DEVICE name=\"%s\" address=%s rssi=%d connectable=%s",
                    device.haveName() ? device.getName().c_str() : "",
                    device.getAddress().toString().c_str(), device.getRSSI(),
                    device.isConnectable() ? "yes" : "no");
      bool batteryService = false;
      for (int j = 0; j < device.getServiceUUIDCount(); ++j) {
        if (device.getServiceUUID(j).equals(BLEUUID((uint16_t)0x180F))) batteryService = true;
        Serial.printf(" service=%s", device.getServiceUUID(j).toString().c_str());
      }
      Serial.printf(" standardBatteryService=%s", batteryService ? "yes" : "no");
      if (device.haveManufacturerData()) {
        Serial.print(" manufacturer=");
        printHex(device.getManufacturerData());
      }
      if (device.getServiceDataCount() > 0) {
        Serial.printf(" serviceDataUUID=%s serviceData=",
                      device.getServiceDataUUID(0).toString().c_str());
        printHex(device.getServiceData(0));
      }
      Serial.println();
    }
    Serial.printf("SCAN %lu COUNT %d END\n", static_cast<unsigned long>(scanNumber),
                  results->getCount());
  } else {
    Serial.printf("SCAN %lu FAILED\n", static_cast<unsigned long>(scanNumber));
  }

  scanner->clearResults();
  delay(1500);
}
