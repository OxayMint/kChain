#include "status_led.h"

#include <Arduino.h>

#include "config.h"

void StatusLed::writeOn(bool on) {
  const bool high = LED_ACTIVE_LOW ? !on : on;
  digitalWrite(PIN_LED, high ? HIGH : LOW);
}

void StatusLed::begin() {
  pinMode(PIN_LED, OUTPUT);
  showIdle();
}

void StatusLed::showIdle() {
  if (phase_ == Phase::Idle && count_ == 0) {
    writeOn(false);
    return;
  }
  count_ = 0;
  flashed_ = 0;
  phase_ = Phase::Idle;
  writeOn(false);
}

void StatusLed::beginFlash(unsigned long now) {
  phase_ = Phase::On;
  phaseStartedMs_ = now;
  writeOn(true);
}

void StatusLed::showCount(int count) {
  if (count < 1) {
    showIdle();
    return;
  }
  if (count == count_ && phase_ != Phase::Idle) return;
  count_ = count;
  flashed_ = 0;
  beginFlash(millis());
}

void StatusLed::poll() {
  if (phase_ == Phase::Idle) return;

  const unsigned long now = millis();
  const unsigned long elapsed = now - phaseStartedMs_;

  if (phase_ == Phase::On) {
    if (elapsed < LED_FLASH_ON_MS) return;
    writeOn(false);
    flashed_++;
    if (flashed_ >= count_) phase_ = Phase::CyclePause;
    else if (flashed_ % LED_GROUP_SIZE == 0) phase_ = Phase::GroupGap;
    else phase_ = Phase::Gap;
    phaseStartedMs_ = now;
    return;
  }

  const unsigned long wait = phase_ == Phase::Gap          ? LED_FLASH_GAP_MS
                             : phase_ == Phase::GroupGap    ? LED_GROUP_GAP_MS
                                                            : LED_CYCLE_PAUSE_MS;
  if (elapsed < wait) return;
  if (phase_ == Phase::CyclePause) flashed_ = 0;
  beginFlash(now);
}
