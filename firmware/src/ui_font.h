#pragma once

#include <stdint.h>

// One ASCII glyph. Coverage is 4 bits per pixel, two pixels per byte, high
// nibble on the left. Rows are stored bottom-first. left is the ink offset
// from the pen and may be negative. width 0 draws nothing. 0 is paper and
// 15 is solid ink.
struct UiGlyph {
  uint8_t advance;
  int8_t left;
  uint8_t width;
  uint16_t offset;
};

// glyphs has 95 entries, index code - 32, for ASCII 32 through 126.
struct UiFont {
  uint8_t height;
  const UiGlyph* glyphs;
  const uint8_t* bits;
};

// Wordmark is Source Serif 4. Row labels and the line under a website are Geist.
extern const UiFont kUiFont35;
extern const UiFont kUiFont20;
extern const UiFont kUiFont16;

inline const UiGlyph& uiGlyph(const UiFont& font, unsigned char code) {
  if (code < 32 || code > 126) code = '?';
  return font.glyphs[code - 32];
}

// Linear-light mix of ink over paper. coverage is 0..15.
uint16_t uiBlend(uint16_t ink, uint16_t paper, uint8_t coverage);

inline int uiTextWidth(const UiFont& font, const char* text) {
  int width = 0;
  if (text == nullptr) return 0;
  while (*text != '\0') {
    width += uiGlyph(font, static_cast<unsigned char>(*text)).advance;
    text++;
  }
  return width;
}
