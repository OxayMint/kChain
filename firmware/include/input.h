#pragma once

#include <stdint.h>

// What the rest of the firmware can ask the hardware to do.
// A later wheel-with-button should emit these same events.
// Selection and typing must not read pins themselves.
enum class InputEvent : uint8_t {
  None = 0,
  Previous,
  Type,
  Next,
};

class Input {
 public:
  virtual ~Input() {}
  virtual void begin() = 0;
  virtual InputEvent poll() = 0;
};
