// GameReader — a minimal, reliable openCreditIC game/cabinet reader.
//
// WiFi + PN532 (I2C 21/22) + SSD1306 OLED (software I2C 32/33) + relay (GPIO13).
// On each card tap it charges the machine's price via the server's /withdraw
// endpoint and pulses the relay to drop a credit into the cabinet. No coin-block
// state machine, no HappyCAB, no multicore — the same single-core pattern that
// made CardEnroller rock-solid.
//
// config.h (same folder) must define:
//   const char *ssid, *password, *apiUrl (".../", trailing slash), *deviceKey;

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include "config.h"

#define RELAY_PIN 13
#define RELAY_MS  200   // how long to hold the credit pulse

Adafruit_PN532 nfc(21, 22);                                                  // PN532 on primary I2C
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /*SCL=*/ 33, /*SDA=*/ 32, U8X8_PIN_NONE);

String machineName = "Machine";
long   playCost = -1;      // credits per play, from server config
String lastUID = "";
unsigned long lastTapMs = 0;

// ---- display helpers ----
int centerX(const char *s) {
  int x = (128 - u8g2.getStrWidth(s)) / 2;
  return x < 0 ? 0 : x;
}

void showTwoLines(const char *l1, const char *l2) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(centerX(l1), 26, l1);
  u8g2.drawStr(centerX(l2), 46, l2);
  u8g2.sendBuffer();
}

void showStandby() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);

  // Wrap the machine name onto up to two centered lines so it never clips.
  String a = machineName, b = "";
  if (u8g2.getStrWidth(machineName.c_str()) > 126) {
    int splitIdx = -1;
    for (int p = 0; p < (int)machineName.length(); p++)
      if (machineName[p] == ' ' && u8g2.getStrWidth(machineName.substring(0, p).c_str()) <= 126)
        splitIdx = p;  // last space that still fits on line 1
    if (splitIdx > 0) { a = machineName.substring(0, splitIdx); b = machineName.substring(splitIdx + 1); }
  }
  if (b == "") {
    u8g2.drawStr(centerX(a.c_str()), 24, a.c_str());
  } else {
    u8g2.drawStr(centerX(a.c_str()), 16, a.c_str());
    u8g2.drawStr(centerX(b.c_str()), 32, b.c_str());
  }

  char l2[24];
  if (playCost >= 0) snprintf(l2, sizeof(l2), "Tap card - %ld cr", playCost);
  else               snprintf(l2, sizeof(l2), "Tap your card");
  u8g2.drawStr(centerX(l2), 56, l2);
  u8g2.sendBuffer();
}

// ---- wifi ----
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  showTwoLines(machineName.c_str(), "Connecting WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println();
  Serial.print("IP:  "); Serial.println(WiFi.localIP());
  Serial.print("MAC: "); Serial.println(WiFi.macAddress());
}

// ---- pull machine name + price from the server ----
void fetchConfig() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String(apiUrl) + "get/machine/" + WiFi.macAddress() + "?key=" + deviceKey;
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    StaticJsonDocument<512> doc;
    if (!deserializeJson(doc, body)) {
      if (doc.containsKey("name")) machineName = String((const char *)doc["name"]);
      if (doc.containsKey("cost") && !doc["cost"].isNull()) playCost = (long)doc["cost"];
      Serial.printf("Config: name=%s cost=%ld\n", machineName.c_str(), playCost);
    }
  } else {
    Serial.printf("Config fetch HTTP %d\n", code);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  delay(300);                       // PN532 power-up
  Serial.println("\n=== openCreditIC GameReader ===");

  u8g2.begin();
  showTwoLines("GameReader", "starting...");

  Wire.begin(21, 22);
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  while (!ver) {                    // retry, never hang
    Serial.println("PN532 not found - retrying...");
    showTwoLines("PN532", "not found...");
    delay(500);
    nfc.begin();
    ver = nfc.getFirmwareVersion();
  }
  Serial.printf("Found PN532, fw 0x%lX\n", (unsigned long)ver);
  nfc.SAMConfig();

  connectWiFi();
  fetchConfig();
  showStandby();
  Serial.println("Ready - tap a card to play.");
}

void loop() {
  connectWiFi();
  showStandby();

  // Blocking read until a card is present (same proven pattern as CardEnroller).
  uint8_t uid[7] = { 0 };
  uint8_t uidLen = 0;
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen)) return;

  String card = "";
  for (uint8_t i = 0; i < uidLen; i++) {
    if (uid[i] < 0x10) card += "0";           // zero-pad to standard UID
    card += String(uid[i], HEX);
  }
  card.toUpperCase();

  if (card == lastUID && millis() - lastTapMs < 1500) return;   // debounce
  lastUID = card;
  lastTapMs = millis();

  Serial.print("Card: "); Serial.println(card);
  showTwoLines("Reading...", "");

  HTTPClient http;
  String url = String(apiUrl) + "withdraw/" + WiFi.macAddress() + "/" + card + "?key=" + deviceKey;
  http.begin(url);
  int code = http.GET();
  String resp = http.getString();
  http.end();
  Serial.printf("HTTP %d: %s\n", code, resp.c_str());

  long balance = -1;
  StaticJsonDocument<384> doc;
  if (!deserializeJson(doc, resp) && doc.containsKey("balance")) balance = (long)doc["balance"];

  if (code >= 200 && code < 300) {
    // Approved — pulse the relay to add a credit to the cabinet.
    digitalWrite(RELAY_PIN, HIGH);
    delay(RELAY_MS);
    digitalWrite(RELAY_PIN, LOW);
    char l2[24];
    if (balance >= 0) snprintf(l2, sizeof(l2), "Balance: %ld", balance);
    else              snprintf(l2, sizeof(l2), "");
    showTwoLines("CREDIT ADDED", l2);
    Serial.println("Credit dispensed");
  } else if (code == 402 || code == 403) {
    showTwoLines("LOW BALANCE", "");
  } else {
    showTwoLines("CARD DENIED", "");
  }
  delay(1500);   // let the message show before returning to standby
}
