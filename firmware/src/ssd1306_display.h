#pragma once

#include <stdint.h>

#include "display.h"

// 0.91 inch SSD1306, 128x32, I2C address 0x3C. SDA and the clock come from
// the board pin constants. Nothing above this class knows the bus.
class Ssd1306Display : public Display {
 public:
  bool begin() override;
  int width() const override { return kWidth; }
  int height() const override { return kHeight; }
  int textHeight() const override { return kTextHeight; }
  void clear() override;
  void fillRect(int x, int y, int w, int h) override;
  void text(int x, int y, const char* text, bool ink) override;
  void bits(int x, int y, const uint8_t* columns, int columnCount, int rows, bool ink) override;
  int textWidth(const char* text) const override;
  void textBox(int x, int y, const char* text, int height, bool ink) override;
  int textWidthBox(const char* text, int height) const override;
  int glyphWidthBox(int height) const override;
  void bitsBox(int x, int y, const uint8_t* columns, int columnCount, int rows, int height, bool ink) override;
  void setClip(int top, int bottom) override { clipTop_ = top; clipBottom_ = bottom; }
  void flush() override;

 private:
  static constexpr int kWidth = 128;
  static constexpr int kHeight = 32;
  static constexpr int kTextHeight = 7;
  static constexpr int kPages = kHeight / 8;
  static constexpr uint8_t kAddress = 0x3C;

  bool ready_ = false;
  int clipTop_ = 0;
  int clipBottom_ = 0;
  uint8_t frame_[kWidth * kPages] = {};

  bool command(uint8_t value);
  bool command2(uint8_t value, uint8_t arg);
  void pixel(int x, int y, bool on);
  void glyph(int x, int y, uint32_t codepoint, bool ink);
  void blit(int x, int y, const uint8_t* columns, int srcCols, int srcRows, int dstW, int dstH, bool ink);
  int letterGap(int height) const;
};
