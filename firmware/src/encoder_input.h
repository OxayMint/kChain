#pragma once

#include <Arduino.h>

// Mouse-wheel encoder (the H-13 body is the 13 mm Kailh-style wheel).
// A and B idle high on the pull-ups. Each detent is two quadrature
// transitions; a menu step is emitted only after both, so a contact
// that snaps back does not move the selection.
class EncoderInput {
 public:
  void begin();
  // Detents since the last call. Positive is wheel up, negative is wheel down.
  int takeSteps();

 private:
  static void IRAM_ATTR onEdge();
  static uint8_t IRAM_ATTR readLevel();

  static volatile uint8_t level_;
  static volatile int8_t quarter_;
  static volatile int pending_;
  static volatile unsigned long quarterMs_;
};
