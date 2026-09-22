#pragma once

#include <string>

// BLE HID keyboard advertised as kChain. Types password characters and
// then releases every key. It does not send Enter or any other trailing key.
class KeyboardOut {
 public:
  void begin();
  bool connected() const;
  // True once, when the link goes up or down.
  bool consumeConnectionChange(bool& connected);
  void typeText(const std::string& text);

 private:
  bool wasConnected_ = false;
  bool started_ = false;
};
