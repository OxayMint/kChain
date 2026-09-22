#pragma once

#include "input.h"

// One active-low button. Debounced. A hold does not repeat: a long press
// is reported once, and the release after it is not also a short press.
class ButtonInput : public Input {
 public:
  void begin() override;
  InputEvent poll() override;
  bool held() const { return stablePressed_; }

 private:
  bool stablePressed_ = false;
  bool lastReading_ = false;
  bool longFired_ = false;
  unsigned long lastChangeMs_ = 0;
  unsigned long pressedAtMs_ = 0;

  static bool readPressed(int pin);
};
