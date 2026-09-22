#pragma once

#include "input.h"

// Three active-low buttons. Debounced. One press produces one event;
// holding a button does not repeat.
class ButtonInput : public Input {
 public:
  void begin() override;
  InputEvent poll() override;

 private:
  struct Button {
    int pin;
    InputEvent event;
    bool stablePressed;
    bool lastReading;
    unsigned long lastChangeMs;
  };

  Button buttons_[3];

  static bool readPressed(int pin);
};
