#include "button_input.h"

#include <Arduino.h>

#include "config.h"

bool ButtonInput::readPressed(int pin) {
  const int level = digitalRead(pin);
  return BUTTON_ACTIVE_LOW ? level == LOW : level == HIGH;
}

void ButtonInput::begin() {
  pinMode(PIN_BUTTON, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  const unsigned long now = millis();
  const bool pressed = readPressed(PIN_BUTTON);
  stablePressed_ = pressed;
  lastReading_ = pressed;
  lastChangeMs_ = now;
  pressedAtMs_ = now;
  longFired_ = pressed;
}

InputEvent ButtonInput::poll() {
  const unsigned long now = millis();
  const bool pressed = readPressed(PIN_BUTTON);
  if (pressed != lastReading_) {
    lastReading_ = pressed;
    lastChangeMs_ = now;
  }

  if (pressed != stablePressed_ && now - lastChangeMs_ >= BUTTON_DEBOUNCE_MS) {
    stablePressed_ = pressed;
    if (pressed) {
      pressedAtMs_ = now;
      longFired_ = false;
      return InputEvent::Pressed;
    }
    if (!longFired_) return InputEvent::ShortPress;
    return InputEvent::None;
  }

  if (stablePressed_ && !longFired_ && now - pressedAtMs_ >= BUTTON_LONG_PRESS_MS) {
    longFired_ = true;
    return InputEvent::LongPress;
  }
  return InputEvent::None;
}
