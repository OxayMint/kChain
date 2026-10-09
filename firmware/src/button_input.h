#pragma once

#include "input.h"

// One active-low button. Debounced. Reports tap, double tap, and hold.
// A hold does not repeat. The release after a hold is not also a tap.
class ButtonInput : public Input {
 public:
  void begin() override;
  InputEvent poll() override;
  // True once for each debounced press, including the start of a hold.
  bool consumePress();
  bool held() const { return stablePressed_; }
  // True while a press is down or a tap is still waiting out the double-tap window.
  bool busy() const {
    return stablePressed_ || phase_ != Phase::Idle || queued_ != InputEvent::None;
  }
  // True after poll() sees the second press of a double tap, until that press ends.
  bool inSecondTap() const { return secondTap_; }

 private:
  enum class Phase : uint8_t { Idle, Down, WaitSecond };

  bool stablePressed_ = false;
  bool lastReading_ = false;
  bool holdFired_ = false;
  bool secondTap_ = false;
  bool pressEdge_ = false;
  Phase phase_ = Phase::Idle;
  InputEvent queued_ = InputEvent::None;
  unsigned long lastChangeMs_ = 0;
  unsigned long pressedAtMs_ = 0;
  unsigned long releasedAtMs_ = 0;

  static bool readPressed(int pin);
};
