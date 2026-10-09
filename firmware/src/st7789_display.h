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
  int width() const { return kWidth; }
  int height() const { return kHeight; }

  void fillRect(int x, int y, int w, int h, uint16_t color);
  // Pixels outside the rounded shape are `outside`. The outer pixel of the
  // shape is `border`; the rest is `fill`. Pass the same color for border
  // and fill when the card is solid.
  void fillRoundRect(int x, int y, int w, int h, int radius, uint16_t fill, uint16_t border,
                     uint16_t outside);
  // Opaque text. paper is the color already behind the glyphs.
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
