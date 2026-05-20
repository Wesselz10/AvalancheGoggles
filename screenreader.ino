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
const float SPEED_MIN_KMH = 0.2;
const float SPEED_MAX_KMH = 2.0;

// ── Speed tracking ────────────────────────────────────────────
int           lastDistMM     = -1;
unsigned long lastTimeMs     = 0;
float         displayedSpeed = 0.0;

// ── LCD refresh tracking ──────────────────────────────────────
unsigned long lastLcdUpdate = 0;
int           lastLeftCM    = -1;
float         lastShownSpeed = -1.0;

String inputBuffer = "";

// ─────────────────────────────────────────────────────────────

void showBar(int level) {
  level = constrain(level, 0, 8);
  mx.clear();
  for (int row = 0; row < level; row++) {
    mx.setRow(0, row, 0xFF);
  }
}

void updateMatrix(float speedKmh) {
  if (speedKmh < SPEED_MIN_KMH) {
    showBar(0);
    return;
  }
  float clamped = constrain(speedKmh, SPEED_MIN_KMH, SPEED_MAX_KMH);
  int level = map(
    (long)(clamped * 10),
    (long)(SPEED_MIN_KMH * 10),
    (long)(SPEED_MAX_KMH * 10),
    1, 8
  );
  showBar(level);
}

void printBar(int mm, int maxChars) {
  int filled = map(mm, 50, 950, maxChars, 0);
  filled = constrain(filled, 0, maxChars);
  for (int i = 0; i < maxChars; i++) {
    lcd.write(i < filled ? (byte)255 : ' ');
  }
}

void updateLcd(int leftMM, float speedKmh) {
  unsigned long now = millis();
  int leftCM = leftMM / 10;

  bool valuesChanged = (leftCM != lastLeftCM || abs(speedKmh - lastShownSpeed) >= 0.1);
  bool timeElapsed   = (now - lastLcdUpdate >= 500);

  if (!valuesChanged && !timeElapsed) return;

  // Row 0: distance
  lcd.setCursor(0, 0);
  lcd.print("L:");
  lcd.print(leftCM);
  lcd.print("cm      ");  // extra spaces to clear any leftover characters

  // Row 1: speed
  lcd.setCursor(0, 1);
  lcd.print("Spd:");
  lcd.print(speedKmh*10, 1);
  lcd.print("km/h      ");  // extra spaces to clear leftovers

  lastLeftCM    = leftCM;
  lastShownSpeed = speedKmh;
  lastLcdUpdate  = now;
}

void parseLine(String line) {
  int commaIndex = line.indexOf(',');
  if (commaIndex == -1) return;

  int leftMM  = line.substring(0, commaIndex).toInt();
  int rightMM = line.substring(commaIndex + 1).toInt();

  if (leftMM  < 0 || leftMM  > 9999) return;
  if (rightMM < 0 || rightMM > 9999) return;

  // ── Speed calculation over a 200ms window ─────────────────
  int currentDist   = leftMM;
  unsigned long now = millis();

  if (lastDistMM < 0 || lastTimeMs == 0) {
    lastDistMM = currentDist;
    lastTimeMs = now;
    return;
  }

  unsigned long deltaMs = now - lastTimeMs;

  if (deltaMs >= 200) {
    long deltaMM = lastDistMM - currentDist;

    if (deltaMM > 0) {
      float speedMs  = (float)deltaMM / (float)deltaMs;
      float speedKmh = speedMs * 3.6f;

      if (speedKmh <= 200.0) {
        displayedSpeed = 0.4f * speedKmh + 0.6f * displayedSpeed;
      }
    } else {
      displayedSpeed *= 0.7f;
    }

    lastDistMM = currentDist;
    lastTimeMs = now;
  }

  // ── Update LCD and matrix ──────────────────────────────────
  updateLcd(leftMM, displayedSpeed);
  updateMatrix(displayedSpeed);
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

  // Startup sweep
  for (int row = 0; row < 8; row++) {
    mx.setRow(0, row, 0xFF);
    delay(150);
  }
  delay(500);
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