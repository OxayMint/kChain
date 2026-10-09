#pragma once

#include <stdint.h>

// One ASCII glyph. Rows are MSB-left in the shared bits array. left is the
// ink offset from the pen and may be negative. width 0 draws nothing.
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

// Title, row labels, and the secondary line under a website.
extern const UiFont kUiFont20;
extern const UiFont kUiFont14;
extern const UiFont kUiFont12;

inline const UiGlyph& uiGlyph(const UiFont& font, unsigned char code) {
  if (code < 32 || code > 126) code = '?';
  return font.glyphs[code - 32];
}

inline int uiTextWidth(const UiFont& font, const char* text) {
  int width = 0;
  if (text == nullptr) return 0;
  while (*text != '\0') {
    width += uiGlyph(font, static_cast<unsigned char>(*text)).advance;
    text++;
  }
  return width;
}
