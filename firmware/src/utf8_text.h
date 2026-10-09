#pragma once

#include <stddef.h>
#include <stdint.h>

// One Unicode scalar, or one byte when the sequence is ill-formed.
// A truncated sequence stops at the NUL and counts as one scalar.
inline size_t nextCodepoint(const char* text, uint32_t& codepoint) {
  const auto lead = static_cast<uint8_t>(text[0]);
  if (lead < 0x80) {
    codepoint = lead;
    return 1;
  }

  size_t need = 0;
  uint32_t value = 0;
  if ((lead & 0xE0) == 0xC0) {
    need = 2;
    value = lead & 0x1Fu;
  } else if ((lead & 0xF0) == 0xE0) {
    need = 3;
    value = lead & 0x0Fu;
  } else if ((lead & 0xF8) == 0xF0) {
    need = 4;
    value = lead & 0x07u;
  } else {
    codepoint = 0xFFFD;
    return 1;
  }

  for (size_t i = 1; i < need; i++) {
    const auto next = static_cast<uint8_t>(text[i]);
    if ((next & 0xC0) != 0x80) {
      codepoint = 0xFFFD;
      return 1;
    }
    value = (value << 6) | (next & 0x3Fu);
  }
  codepoint = value;
  return need;
}

inline int countCodepoints(const char* text) {
  int count = 0;
  while (text[0] != '\0') {
    uint32_t codepoint = 0;
    text += nextCodepoint(text, codepoint);
    count++;
  }
  return count;
}
