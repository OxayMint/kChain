#include "keyboard_out.h"

#include <BleKeyboard.h>

#include "config.h"

namespace {
BleKeyboard bleKeyboard(BLE_DEVICE_NAME, BLE_MANUFACTURER, 100);
}

void KeyboardOut::begin() {
  started_ = false;
  wasConnected_ = false;
  bleKeyboard.setDelay(KEY_STROKE_DELAY_MS);
  bleKeyboard.begin();
  wasConnected_ = bleKeyboard.isConnected();
  started_ = true;
}

bool KeyboardOut::connected() const {
  if (!started_) return false;
  return bleKeyboard.isConnected();
}

bool KeyboardOut::consumeConnectionChange(bool& connected) {
  if (!started_) return false;
  const bool now = bleKeyboard.isConnected();
  if (now == wasConnected_) return false;
  wasConnected_ = now;
  connected = now;
  return true;
}

void KeyboardOut::typeText(const std::string& text) {
  if (!connected()) return;
  for (size_t i = 0; i < text.size(); i++) {
    bleKeyboard.write(static_cast<uint8_t>(text[i]));
  }
  // Drop any modifier that a symbol key may have held, then stop.
  // No Enter, Tab, or extra character is sent.
  bleKeyboard.releaseAll();
}
