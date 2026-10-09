#pragma once

#include <stdint.h>

// Hardware boundary for the menu drawer. A later color panel implements
// the same calls. text() clips to the panel, and x may be negative so a
// name can leave off the left edge.
class Display {
 public:
  virtual ~Display() {}
  virtual bool begin() = 0;
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual int textHeight() const = 0;
  virtual void clear() = 0;
  virtual void fillRect(int x, int y, int w, int h) = 0;
  // ink false punches the glyph out of a filled row, so the text reads inverted.
  virtual void text(int x, int y, const char* text, bool ink = true) = 0;
  // columns are bit 0 at the top, the same packing as the font.
  virtual void bits(int x, int y, const uint8_t* columns, int columnCount, int rows, bool ink) = 0;
  virtual int textWidth(const char* text) const = 0;
  // Same font, scaled so the glyph box is `height` pixels tall. 7 is native.
  virtual void textBox(int x, int y, const char* text, int height, bool ink) = 0;
  virtual int textWidthBox(const char* text, int height) const = 0;
  virtual int glyphWidthBox(int height) const = 0;
  virtual void bitsBox(int x, int y, const uint8_t* columns, int columnCount, int rows, int height,
                       bool ink) = 0;
  // Keeps rows from top inclusive to bottom exclusive. bottom <= top draws every row.
  virtual void setClip(int top, int bottom) = 0;
  virtual void flush() = 0;
};
