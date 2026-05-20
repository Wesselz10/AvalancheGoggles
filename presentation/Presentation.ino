#include <LiquidCrystal.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

// ── LCD ──────────────────────────────────────────────────────
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);
const int contrastPin = 9;

// ── MAX7219 LED Matrix ────────────────────────────────────────
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   1
#define CS_PIN        10
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// ── Speed bar settings ────────────────────────────────────────
const float SPEED_MIN_KMH = 0.5;
const float SPEED_MAX_KMH = 5.0;

// ── Speed tracking ────────────────────────────────────────────
int           lastDistMM     = -1;
unsigned long lastTimeMs     = 0;
float         displayedSpeed = 0.0;

// ── LCD refresh tracking ──────────────────────────────────────
unsigned long lastLcdUpdate  = 0;
int           lastDistCM     = -1;
float         lastShownSpeed = -1.0;

// ── Approach confirmation ─────────────────────────────────────
int   confirmCount      = 0;
bool  confirmedApproach = false;
int   prevDistMM        = -1;
const int CONFIRM_NEEDED = 3;

// ── Matrix fade ───────────────────────────────────────────────
float matrixFade = 0.0;
const float FADE_IN_STEP  = 0.08;
const float FADE_OUT_STEP = 0.08;

String inputBuffer = "";

// ── Radar pattern: rings expanding from center ───────────────
// Each ring is a list of (row, col) pixels
// Ring 0 = center 2x2, ring 1 = next layer, ring 2 = outer edge
const int RING_COUNT = 4;

// Bitmask rows for each ring (8 pixels wide)
// Center outward on an 8x8 grid
const uint8_t ringMask[RING_COUNT][8] = {
  // Ring 0: centre 2x2
  { 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00 },
  // Ring 1: 4x4 square
  { 0x00, 0x00, 0x3C, 0x24, 0x24, 0x3C, 0x00, 0x00 },
  // Ring 2: 6x6 square
  { 0x00, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00 },
  // Ring 3: full 8x8 border
  { 0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF },
};

// ── Map speed to how many rings to show (0–4) ─────────────────
int speedToRings(float speedKmh) {
  if (speedKmh < SPEED_MIN_KMH) return 0;
  float clamped = constrain(speedKmh, SPEED_MIN_KMH, SPEED_MAX_KMH);
  return map(
    (long)(clamped * 10),
    (long)(SPEED_MIN_KMH * 10),
    (long)(SPEED_MAX_KMH * 10),
    1, RING_COUNT
  );
}

// ── Map speed to green→orange→red, scaled by fade ─────────────
void speedToColor(float speedKmh, float fade, uint8_t &r, uint8_t &g) {
  float t = (speedKmh - SPEED_MIN_KMH) / (SPEED_MAX_KMH - SPEED_MIN_KMH);
  t = constrain(t, 0.0, 1.0);

  float rf, gf;
  if (t <= 0.5) {
    rf = t / 0.5;
    gf = 1.0;
  } else {
    rf = 1.0;
    gf = 1.0 - (t - 0.5) / 0.5;
  }

  r = (uint8_t)(rf * fade * 255);
  g = (uint8_t)(gf * fade * 255);
}

// ── Draw rings on matrix ──────────────────────────────────────
// MAX7219 is monochrome so we use intensity to simulate brightness.
// We draw all active rings and set intensity based on color brightness.
void updateMatrix(float speedKmh, float fade) {
  int rings = speedToRings(speedKmh);

  if (rings == 0 || fade < 0.01) {
    mx.clear();
    return;
  }

  uint8_t r, g;
  speedToColor(speedKmh, fade, r, g);

  // Perceived brightness: weight green more than red
  uint8_t bright = (uint8_t)((r * 0.4 + g * 0.6) / 255.0 * 15);
  bright = constrain(bright, 1, 15);
  mx.control(MD_MAX72XX::INTENSITY, bright);

  // Build combined bitmask of all active rings
  uint8_t frame[8] = {0};
  for (int ring = 0; ring < rings; ring++) {
    for (int row = 0; row < 8; row++) {
      frame[row] |= ringMask[ring][row];
    }
  }

  // Write to matrix
  mx.clear();
  for (int row = 0; row < 8; row++) {
    mx.setRow(0, row, frame[row]);
  }
}

void updateLcd(int distMM, float speedKmh) {
  unsigned long now  = millis();
  int distCM         = distMM / 10;
  bool valuesChanged = (distCM != lastDistCM || abs(speedKmh - lastShownSpeed) >= 0.1);
  bool timeElapsed   = (now - lastLcdUpdate >= 500);

  if (!valuesChanged && !timeElapsed) return;

  lcd.setCursor(0, 0);
  lcd.print("Dist:");
  lcd.print(distCM);
  lcd.print("cm      ");

  lcd.setCursor(0, 1);
  lcd.print("Spd:");
  lcd.print(speedKmh, 1);
  lcd.print("km/h      ");

  lastDistCM     = distCM;
  lastShownSpeed = speedKmh;
  lastLcdUpdate  = now;
}

void parseLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  int distMM = line.toInt();
  if (distMM < 0 || distMM > 9999) return;

  unsigned long now = millis();

  if (lastDistMM < 0 || lastTimeMs == 0) {
    lastDistMM = distMM;
    prevDistMM = distMM;
    lastTimeMs = now;
    return;
  }

  // ── Direction detection ───────────────────────────────────
  if (prevDistMM >= 0) {
    int delta = prevDistMM - distMM;

    if (delta > 10) {
      confirmCount = min(confirmCount + 1, CONFIRM_NEEDED);
    } else if (delta < -10) {
      confirmCount      = 0;
      confirmedApproach = false;
    }

    if (confirmCount >= CONFIRM_NEEDED) {
      confirmedApproach = true;
    }
  }

  prevDistMM = distMM;

  // ── Speed calculation over 200ms window ───────────────────
  unsigned long deltaMs = now - lastTimeMs;

  if (deltaMs >= 200) {
    long deltaMM = lastDistMM - distMM;

    if (deltaMM > 0 && confirmedApproach) {
      float speedMs  = (float)deltaMM / (float)deltaMs;
      float speedKmh = speedMs * 3.6f;
      if (speedKmh <= 200.0) {
        displayedSpeed = 0.4f * speedKmh + 0.6f * displayedSpeed;
      }
    } else {
      displayedSpeed *= 0.7f;
    }

    lastDistMM = distMM;
    lastTimeMs = now;
  }

  // ── Fade in / out ─────────────────────────────────────────
  if (confirmedApproach) {
    matrixFade = min(1.0f, matrixFade + FADE_IN_STEP);
  } else {
    matrixFade = max(0.0f, matrixFade - FADE_OUT_STEP);
  }

  // ── Outputs ───────────────────────────────────────────────
  if (confirmedApproach || matrixFade > 0.01) {
    updateLcd(distMM, displayedSpeed);
    updateMatrix(displayedSpeed, matrixFade);
  } else {
    updateLcd(distMM, 0.0);
    mx.clear();
  }
}

// ─────────────────────────────────────────────────────────────

void setup() {
  analogWrite(contrastPin, 100);
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("Waiting...");

  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 15);
  mx.clear();

  // Startup sweep — rings expanding outward
  for (int ring = 0; ring < RING_COUNT; ring++) {
    mx.clear();
    for (int r = 0; r <= ring; r++) {
      for (int row = 0; row < 8; row++) {
        uint8_t current = 0;
        for (int rr = 0; rr <= r; rr++) current |= ringMask[rr][row];
        mx.setRow(0, row, current);
      }
    }
    delay(200);
  }
  delay(400);
  mx.clear();

  Serial.begin(9600);
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      parseLine(inputBuffer);
      inputBuffer = "";
    } else {
      inputBuffer += c;
    }
  }
}