#pragma once

#include <stdint.h>

// Gestures the menu will handle:
//   wheel up, wheel down, tap, double tap, hold.
// The wheel is a signed step count from the encoder: positive is up,
// negative is down. The button reports the three press gestures below.
// Tap is a release before the hold time, once the double-tap window closes.
// DoubleTap is a second tap that starts inside that window. Hold fires once
// when a press reaches the hold time, and that press is not also a tap.
enum class InputEvent : uint8_t {
  None = 0,
  Tap,
  DoubleTap,
  Hold,
};

class Input {
 public:
  virtual ~Input() {}
  virtual void begin() = 0;
  virtual InputEvent poll() = 0;
};
