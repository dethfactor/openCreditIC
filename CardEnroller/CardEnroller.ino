// CardEnroller — a minimal, rock-solid card enroller for openCreditIC.
//
// Just WiFi + a PN532 (I2C on SDA=21, SCL=22) + the /callback POST. No FastLED,
// no OLED, no on-board web server — none of which a bare enroller needs, and all
// of which (FastLED's RMT driver vs WiFi, and the OLED sharing the PN532's I2C
// bus) can crash/reset the full reader firmware on a headless build.
//
// It behaves exactly like a POS terminal to the server: each tap hits
//   GET /callback/<MAC>/<UID>?key=<device_key>
// so the web UI's "leave Serial blank -> Create -> tap" enrollment flow works.
//
// config.h (same folder) must define:
//   const char *ssid, *password, *apiUrl (".../", trailing slash), *deviceKey;

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "config.h"

Adafruit_PN532 nfc(21, 22);  // I2C

String lastUID = "";
unsigned long lastTapMs = 0;

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println();
  Serial.print("IP:  "); Serial.println(WiFi.localIP());
  Serial.print("MAC: "); Serial.println(WiFi.macAddress());
}

void setup() {
  Serial.begin(115200);
  delay(300);  // let the PN532 finish powering up
  Serial.println("\n=== openCreditIC Card Enroller (PN532 I2C) ===");

  Wire.begin(21, 22);
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  while (!ver) {  // retry (delay feeds the watchdog) instead of hanging
    Serial.println("PN532 not found - retrying...");
    delay(500);
    nfc.begin();
    ver = nfc.getFirmwareVersion();
  }
  Serial.print("Found PN532, firmware 0x");
  Serial.println(ver, HEX);
  nfc.SAMConfig();

  connectWiFi();
  Serial.println("Ready - tap a card to enroll/deposit.");
}

void loop() {
  connectWiFi();

  uint8_t uid[7] = { 0 };
  uint8_t uidLen = 0;
  // Blocking read (matches the proven standalone test): waits here until a card
  // is present. The WiFi link is maintained by the background core meanwhile.
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen)) return;

  String card = "";
  for (uint8_t i = 0; i < uidLen; i++) {
    if (uid[i] < 0x10) card += "0";  // zero-pad to 2 hex chars (standard UID)
    card += String(uid[i], HEX);
  }
  card.toUpperCase();

  // Debounce: ignore the same card held on the reader for 1.5s.
  if (card == lastUID && millis() - lastTapMs < 1500) return;
  lastUID = card;
  lastTapMs = millis();

  Serial.print("Card: ");
  Serial.println(card);

  HTTPClient http;
  String url = String(apiUrl) + "callback/" + WiFi.macAddress() + "/" + card + "?key=" + deviceKey;
  http.begin(url);
  int code = http.GET();
  String resp = http.getString();
  http.end();
  Serial.print("HTTP ");
  Serial.print(code);
  Serial.print(": ");
  Serial.println(resp);
}
