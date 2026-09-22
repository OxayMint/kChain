#pragma once

#include <stdint.h>

// Gestures from the single button. Selection and typing must not read pins.
// Pressed is the debounced press. ShortPress is a release before the
// long-press time. LongPress fires once when the hold reaches that time.
enum class InputEvent : uint8_t {
  None = 0,
  Pressed,
  ShortPress,
  LongPress,
};

class Input {
 public:
  virtual ~Input() {}
  virtual void begin() = 0;
  virtual InputEvent poll() = 0;
};
