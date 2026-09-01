/*
 * display.cpp - Display and touch screen implementation
 */

#include "display.h"

#if defined(USE_CUSTOM_TOUCH)
#include <algorithm>

static uint16_t xpt2046Read(uint8_t cmd) {
  // Clock out command (8 bits)
  for (int i = 7; i >= 0; i--) {
    digitalWrite(TOUCH_DIN, (cmd >> i) & 1);
    digitalWrite(TOUCH_CLK, LOW);
    delayMicroseconds(2);
    digitalWrite(TOUCH_CLK, HIGH);
    delayMicroseconds(2);
  }
  digitalWrite(TOUCH_DIN, LOW);
  digitalWrite(TOUCH_CLK, LOW);
  delayMicroseconds(2);

  // Clock in 16 bits (1 busy bit + 12 data bits + 3 trailing bits)
  uint16_t result = 0;
  for (int i = 15; i >= 0; i--) {
    digitalWrite(TOUCH_CLK, HIGH);
    delayMicroseconds(2);
    digitalWrite(TOUCH_CLK, LOW);
    delayMicroseconds(2);
    if (digitalRead(TOUCH_DOUT)) {
      result |= (1 << i);
    }
  }
  return result >> 4; // Extract 12-bit ADC value
}
#endif

TFT_eSPI tft = TFT_eSPI();

void displayInit() {
#if defined(USE_CUSTOM_TOUCH)
  // Initialize touch pins (XPT2046 software SPI for separate-SPI boards like ESP32-2432S028R)
  pinMode(TOUCH_CLK, OUTPUT);
  pinMode(TOUCH_DIN, OUTPUT);
  pinMode(TOUCH_DOUT, INPUT);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  digitalWrite(TOUCH_CLK, LOW);
#endif

  // Initialize TFT (backlight is automatically managed via TFT_BL in TFT_eSPI)
  tft.init();
  tft.setRotation(1); // Landscape mode
  
#if !defined(USE_CUSTOM_TOUCH)
  // Set touch calibration data for FNK0103L_3P2 (shared SPI)
  // Format: {x_min, x_max, y_min, y_max, rotation}
  // Calibrated for landscape mode (rotation 1)
  uint16_t calData[5] = {300, 3600, 400, 3600, 1};
  tft.setTouch(calData);
#endif

  tft.fillScreen(TFT_BLACK);
  
  // Set font
  tft.setTextFont(1); // Default 6x8 font
  tft.setTextSize(1);
}

bool getTouch(uint16_t *x, uint16_t *y) {
#if defined(USE_CUSTOM_TOUCH)
  digitalWrite(TOUCH_CS, LOW);
  delayMicroseconds(5);

  // Measure pressure: Z1 and Z2
  uint16_t z1 = xpt2046Read(0xB1);
  uint16_t z2 = xpt2046Read(0xC1);
  uint32_t z = z1 + 4095 - z2;

  if (z < TOUCH_PRESSURE_THRESHOLD || z1 < 100) {
    digitalWrite(TOUCH_CS, HIGH);
    return false;
  }

  // Read raw coordinates (3 samples for median filtering)
  uint16_t xSamples[3];
  uint16_t ySamples[3];
  for (int i = 0; i < 3; i++) {
    xSamples[i] = xpt2046Read(0xD0);
    ySamples[i] = xpt2046Read(0x90);
  }

  digitalWrite(TOUCH_CS, HIGH);

  // Median filter for noise reduction
  if (xSamples[0] > xSamples[1]) std::swap(xSamples[0], xSamples[1]);
  if (xSamples[1] > xSamples[2]) std::swap(xSamples[1], xSamples[2]);
  if (xSamples[0] > xSamples[1]) std::swap(xSamples[0], xSamples[1]);
  uint16_t rawX = xSamples[1];

  if (ySamples[0] > ySamples[1]) std::swap(ySamples[0], ySamples[1]);
  if (ySamples[1] > ySamples[2]) std::swap(ySamples[1], ySamples[2]);
  if (ySamples[0] > ySamples[1]) std::swap(ySamples[0], ySamples[1]);
  uint16_t rawY = ySamples[1];

  // Map sensor axes to landscape screen coordinates (320x240)
  // Sensor long axis (rawY) corresponds to Screen X (0..320)
  // Sensor short axis (rawX) corresponds to Screen Y (0..240)
  int32_t mappedX = map(rawY, TOUCH_CAL_Y_MIN, TOUCH_CAL_Y_MAX, 0, SCREEN_WIDTH);
  int32_t mappedY = map(rawX, TOUCH_CAL_X_MIN, TOUCH_CAL_X_MAX, 0, SCREEN_HEIGHT);

  mappedX = constrain(mappedX, 0, SCREEN_WIDTH - 1);
  mappedY = constrain(mappedY, 0, SCREEN_HEIGHT - 1);

  if (x) *x = (uint16_t)mappedX;
  if (y) *y = (uint16_t)mappedY;

  return true;
#else
  // Use built-in TFT_eSPI touch support with calibration (shared SPI)
  bool pressed = tft.getTouch(x, y);
  
  if (pressed) {
    // Invert X coordinate (screen is mirrored horizontally)
    *x = SCREEN_WIDTH - *x;
  }
  
  return pressed;
#endif
}
