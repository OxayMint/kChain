#include "encoder_input.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "config.h"

volatile uint8_t EncoderInput::level_ = 0;
volatile int8_t EncoderInput::quarter_ = 0;
volatile int EncoderInput::pending_ = 0;
volatile unsigned long EncoderInput::quarterMs_ = 0;

// A half-finished detent older than this is dropped, so it cannot cancel
// the next real turn. A click's two edges are closer than this.
static constexpr unsigned long ENCODER_PARTIAL_MS = 80;

uint8_t IRAM_ATTR EncoderInput::readLevel() {
  return (gpio_get_level(static_cast<gpio_num_t>(PIN_ENCODER_A)) ? 1u : 0u) |
         (gpio_get_level(static_cast<gpio_num_t>(PIN_ENCODER_B)) ? 2u : 0u);
}

void IRAM_ATTR EncoderInput::onEdge() {
  const uint8_t next = readLevel();
  const uint8_t prev = level_;
  level_ = next;
  // Exactly one pin moved. Both at once is a missed intermediate: keep the
  // new level and wait for a real edge, instead of inventing a direction.
  const uint8_t changed = prev ^ next;
  if (changed != 1u && changed != 2u) return;
  const bool a = next & 1u;
  const bool b = next & 2u;
  const bool phase = (changed & 1u) ? (a == b) : (a != b);
  // This wheel's A/B phasing reads backwards, so the sign is flipped.
  // Positive stays wheel up.
  const int8_t step = phase ? -1 : 1;

  const unsigned long now = millis();
  if (quarter_ != 0 && now - quarterMs_ > ENCODER_PARTIAL_MS) quarter_ = 0;
  if (quarter_ == 0) quarterMs_ = now;

  quarter_ = static_cast<int8_t>(quarter_ + step);
  if (quarter_ >= ENCODER_COUNTS_PER_DETENT || quarter_ <= -ENCODER_COUNTS_PER_DETENT) {
    pending_ += quarter_ / ENCODER_COUNTS_PER_DETENT;
    quarter_ = static_cast<int8_t>(quarter_ % ENCODER_COUNTS_PER_DETENT);
  }
}

void EncoderInput::begin() {
  pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_B, INPUT_PULLUP);
  level_ = readLevel();
  quarter_ = 0;
  pending_ = 0;
  quarterMs_ = millis();
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), onEdge, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), onEdge, CHANGE);
}

int EncoderInput::takeSteps() {
  noInterrupts();
  const int steps = pending_;
  pending_ = 0;
  interrupts();
  return steps;
}
