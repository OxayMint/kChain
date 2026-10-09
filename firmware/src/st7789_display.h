#pragma once

#include <stddef.h>
#include <stdint.h>

// 2.0 inch ST7789, 240x320, SPI. The module labels MOSI as SDA and the
// clock as SCL. Nothing above this class knows the bus.
class St7789Display {
 public:
  bool begin();
  // Red, green, and blue bands, a white frame, and a black OK. Bring-up only.
  void drawTest();

 private:
  static constexpr int kWidth = 240;
  static constexpr int kHeight = 320;

  void command(uint8_t value, const uint8_t* args = nullptr, size_t argCount = 0);
  void fillRect(int x, int y, int w, int h, uint16_t color);
  void glyph(int x, int y, const uint8_t columns[5], int scale, uint16_t color);
};
