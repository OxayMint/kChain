#include "button_input.h"

#include <Arduino.h>

#include "config.h"

bool ButtonInput::readPressed(int pin) {
  const int level = digitalRead(pin);
  return BUTTON_ACTIVE_LOW ? level == LOW : level == HIGH;
}

void ButtonInput::begin() {
  buttons_[0] = {PIN_BUTTON_PREVIOUS, InputEvent::Previous, false, false, 0};
  buttons_[1] = {PIN_BUTTON_TYPE, InputEvent::Type, false, false, 0};
  buttons_[2] = {PIN_BUTTON_NEXT, InputEvent::Next, false, false, 0};

  const unsigned long now = millis();
  for (int i = 0; i < 3; i++) {
    pinMode(buttons_[i].pin, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    const bool pressed = readPressed(buttons_[i].pin);
    buttons_[i].stablePressed = pressed;
    buttons_[i].lastReading = pressed;
    buttons_[i].lastChangeMs = now;
  }
}

InputEvent ButtonInput::poll() {
  const unsigned long now = millis();
  for (int i = 0; i < 3; i++) {
    Button& button = buttons_[i];
    const bool pressed = readPressed(button.pin);
    if (pressed != button.lastReading) {
      button.lastReading = pressed;
      button.lastChangeMs = now;
    }
    if (pressed == button.stablePressed) continue;
    if (now - button.lastChangeMs < BUTTON_DEBOUNCE_MS) continue;

    button.stablePressed = pressed;
    if (pressed) return button.event;
  }
  return InputEvent::None;
}
