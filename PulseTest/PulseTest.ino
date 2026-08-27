// PulseTest — coin-pulse bring-up helper for openCreditIC.
//
// Fires GPIO13 active-HIGH for a tunable pulse length on a serial keypress, so
// you can validate a PC817 opto -> coin-switch wiring in ISOLATION — no WiFi,
// no PN532, no OLED, no enrollment. Just the pulse.
//
// Wiring (current ESP32 bench board):
//   GPIO13 -> PC817 module INPUT +
//   GND    -> PC817 module INPUT -
//   PC817 OUT + GND  -> across the game's coin switch  (leave the module's VCC empty)
//
// Serial Monitor @ 115200:
//   Enter / p / space  = fire ONE pulse
//   +                  = pulse length +10 ms
//   -                  = pulse length -10 ms
//   ?                  = reprint help
//
// Test on Joust: each pulse should blink the module LED and add exactly 1 credit.
//   0 credits -> pulse too short (press +) or polarity (swap OUT/GND)
//   2 credits -> pulse too long/bounce (press -)

#define RELAY_PIN 13

int pulseMs = 200;                 // default; matches GameReader's RELAY_MS
unsigned long lastFireMs = 0;      // debounce so CR/LF or "p<enter>" = one pulse

void printHelp() {
  Serial.println();
  Serial.println(F("=== openCreditIC PulseTest ==="));
  Serial.println(F("Enter/p/space = fire pulse   + = longer   - = shorter   ? = help"));
  Serial.printf("Pulse length: %d ms\n", pulseMs);
}

void firePulse() {
  if (millis() - lastFireMs < 300) return;   // one pulse per keypress (ignores CRLF)
  lastFireMs = millis();
  digitalWrite(RELAY_PIN, HIGH);
  delay(pulseMs);
  digitalWrite(RELAY_PIN, LOW);
  Serial.printf("Pulsed %d ms\n", pulseMs);
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  delay(200);
  printHelp();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c) {
    case '\r': case '\n': case 'p': case 'P': case ' ':
      firePulse();
      break;
    case '+': case '=':
      pulseMs += 10;
      Serial.printf("Pulse length: %d ms\n", pulseMs);
      break;
    case '-': case '_':
      pulseMs -= 10;
      if (pulseMs < 10) pulseMs = 10;
      Serial.printf("Pulse length: %d ms\n", pulseMs);
      break;
    case '?': case 'h': case 'H':
      printHelp();
      break;
    default:
      break;  // ignore anything else
  }
}
