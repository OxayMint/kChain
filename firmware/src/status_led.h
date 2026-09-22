#pragma once

#include <stdint.h>

// Onboard diode. Idle keeps it dark. A count plays as short flashes in
// groups of three, then a one-second pause, then the same count again.
class StatusLed {
 public:
  void begin();
  void showIdle();
  // 1-based count. The same count leaves a running pattern alone.
  void showCount(int count);
  void poll();

 private:
  enum class Phase : uint8_t { Idle, On, Gap, GroupGap, CyclePause };

  int count_ = 0;
  int flashed_ = 0;
  Phase phase_ = Phase::Idle;
  unsigned long phaseStartedMs_ = 0;

  void writeOn(bool on);
  void beginFlash(unsigned long now);
};
