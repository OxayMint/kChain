#pragma once

#include <string>

// BLE HID keyboard advertised as kChain. Types characters and then releases
// every key. typeText does not send Enter or Tab. typeTab sends Tab.
class KeyboardOut {
 public:
  void begin();
  bool connected() const;
  // True once, when the link goes up or down.
  bool consumeConnectionChange(bool& connected);
  void typeText(const std::string& text);
  void typeTab();

 private:
  bool wasConnected_ = false;
  bool started_ = false;
};
