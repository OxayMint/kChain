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
  releasedAtMs_ = now;
  // A button already held at boot is not a new hold or a tap.
  holdFired_ = pressed;
  secondTap_ = false;
  pressEdge_ = false;
  queued_ = InputEvent::None;
  phase_ = pressed ? Phase::Down : Phase::Idle;
}

bool ButtonInput::consumePress() {
  const bool edge = pressEdge_;
  pressEdge_ = false;
  return edge;
}

InputEvent ButtonInput::poll() {
  const unsigned long now = millis();
  const bool pressed = readPressed(PIN_BUTTON);
  if (pressed != lastReading_) {
    lastReading_ = pressed;
    lastChangeMs_ = now;
  }

  // Sampling still runs so a gesture queued for the next poll cannot hide an edge.
  if (queued_ != InputEvent::None) {
    const InputEvent event = queued_;
    queued_ = InputEvent::None;
    return event;
  }

  const bool edge =
      pressed != stablePressed_ && now - lastChangeMs_ >= BUTTON_DEBOUNCE_MS;
  if (edge) stablePressed_ = pressed;

  if (edge && stablePressed_) {
    pressEdge_ = true;
    pressedAtMs_ = now;
    holdFired_ = false;
    if (phase_ == Phase::WaitSecond && now - releasedAtMs_ <= BUTTON_DOUBLE_TAP_MS) {
      secondTap_ = true;
      phase_ = Phase::Down;
      return InputEvent::None;
    }
    const bool pendingTap = phase_ == Phase::WaitSecond;
    secondTap_ = false;
    phase_ = Phase::Down;
    // The window already closed on the same poll as the next press.
    if (pendingTap) return InputEvent::Tap;
    return InputEvent::None;
  }

  if (edge && !stablePressed_) {
    if (holdFired_) {
      holdFired_ = false;
      secondTap_ = false;
      phase_ = Phase::Idle;
      return InputEvent::None;
    }
    if (secondTap_) {
      secondTap_ = false;
      phase_ = Phase::Idle;
      return InputEvent::DoubleTap;
    }
    phase_ = Phase::WaitSecond;
    releasedAtMs_ = now;
    return InputEvent::None;
  }

  if (phase_ == Phase::WaitSecond && now - releasedAtMs_ > BUTTON_DOUBLE_TAP_MS) {
    phase_ = Phase::Idle;
    secondTap_ = false;
    return InputEvent::Tap;
  }

  if (phase_ == Phase::Down && stablePressed_ && !holdFired_ &&
      now - pressedAtMs_ >= BUTTON_HOLD_MS) {
    holdFired_ = true;
    // The first tap already finished. This press is a hold, not the second tap.
    if (secondTap_) {
      secondTap_ = false;
      queued_ = InputEvent::Hold;
      return InputEvent::Tap;
    }
    return InputEvent::Hold;
  }

  return InputEvent::None;
}
