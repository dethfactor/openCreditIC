// I2CScan — verify the OLED on its new pins (SDA=25, SCL=26).
//
// Flash this INSTEAD of GameReader, open Serial @115200.
//   Device found at 0x3C  -> OLED alive on 25/26; flash GameReader, it'll draw.
//   Device found at 0x3D  -> alive but address mismatch; tell me.
//   No I2C devices found   -> still power/ground/wiring: reseat OLED VCC->3V3,
//                             GND->common ground, SDA->25, SCL->26.

#include <Wire.h>

#define SDA_PIN 25
#define SCL_PIN 26

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== I2CScan on SDA=25 SCL=26 ===");
  Wire.begin(SDA_PIN, SCL_PIN);
}

void loop() {
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Device found at 0x%02X\n", addr);
      found++;
    }
  }
  if (!found) Serial.println("No I2C devices found");
  else        Serial.println("---");
  delay(1000);
}
