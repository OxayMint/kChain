#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ui_font.h"

// 2.0 inch ST7789, 240x320, SPI. The module labels MOSI as SDA and the
// clock as SCL. Nothing above this class knows the bus.
class St7789Display {
 public:
  static constexpr int kWidth = 240;
  static constexpr int kHeight = 320;

  bool begin();
  // Display off, then sleep in: the oscillator and the panel scan stop.
  // The backlight goes off after the controller is asleep.
  void sleep();
  // Sleep out, then display on. The controller needs 120 ms before it
  // will take another command. The backlight comes on after that.
  void wake();
  int width() const { return kWidth; }
  int height() const { return kHeight; }

  void fillRect(int x, int y, int w, int h, uint16_t color);
  // Pixels outside the rounded shape are `outside`. The outer `borderWidth`
  // pixels of the shape are `border`; the rest is `fill`. Pass the same color
  // for border and fill when the card is solid.
  void fillRoundRect(int x, int y, int w, int h, int radius, uint16_t fill, uint16_t border,
                     uint16_t outside, int borderWidth = 1);
  // Text on a flat color. Edge pixels blend ink over paper.
  void text(int x, int y, const char* text, const UiFont& font, uint16_t ink, uint16_t paper);
  // 1-bit icon, MSB left, (width + 7) / 8 bytes per row.
  void mask(int x, int y, int w, int h, const uint8_t* bits, uint16_t ink, uint16_t paper);

  // Red, green, and blue bands, a white frame, and a black OK. Bring-up only.
  void drawTest();

 private:
  void command(uint8_t value, const uint8_t* args = nullptr, size_t argCount = 0);
  void glyph(int x, int y, const uint8_t columns[5], int scale, uint16_t color);
  bool beginPixels(int x, int y, int w, int h);
  void writeRow(int w);
  void endPixels();
  uint8_t* row();
};
