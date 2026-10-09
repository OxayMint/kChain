#include "usb_keyboard.h"

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>

#include "config.h"

// This object has to exist before app_main starts USB. Its constructor
// registers the keyboard interface. setup() is too late for that: the
// computer would see serial and no keyboard. begin() only arms reports,
// and that waits until setup().
namespace {

USBHIDKeyboard keyboard;

extern "C" bool tud_mounted(void);

void pause(void (*tick)()) {
  const unsigned long start = millis();
  while (millis() - start < USB_KEY_GAP_MS) {
    if (tick != nullptr) tick();
    delay(1);
  }
}

bool tap(uint8_t key, void (*tick)()) {
  if (keyboard.press(key) == 0) return false;
  pause(tick);
  keyboard.release(key);
  keyboard.releaseAll();
  pause(tick);
  return true;
}

bool textOk(const char* text) {
  for (const char* p = text; *p != '\0'; ++p) {
    const auto c = static_cast<unsigned char>(*p);
    if (c < 0x20 || c > 0x7e) return false;
  }
  return true;
}

bool typeText(const char* text, void (*tick)()) {
  for (const char* p = text; *p != '\0'; ++p) {
    if (!tap(static_cast<uint8_t>(*p), tick)) return false;
  }
  return true;
}

}  // namespace

void UsbKeyboard::begin() { keyboard.begin(); }

bool UsbKeyboard::mounted() const { return tud_mounted(); }

bool UsbKeyboard::typeEntry(const VaultEntry& entry, const char*& error, void (*tick)()) {
  if (!mounted()) {
    error = "The USB keyboard is not connected.";
    return false;
  }

  const char* first = "";
  const char* second = nullptr;
  bool tab = false;
  switch (entry.type) {
    case EntryType::Website:
      first = entry.username.c_str();
      second = entry.password.c_str();
      tab = true;
      break;
    case EntryType::Crypto:
      first = entry.phrase.c_str();
      break;
    case EntryType::Generic:
      first = entry.password.c_str();
      break;
  }

  if (!textOk(first) || (second != nullptr && !textOk(second))) {
    error = "That entry has a character the keyboard cannot type.";
    return false;
  }
  if (first[0] == '\0' && !tab && (second == nullptr || second[0] == '\0')) {
    error = "Missing text to type.";
    return false;
  }

  // A US layout. The computer maps these key positions to its own layout,
  // so symbols land where a US keyboard would put them.
  keyboard.releaseAll();
  if (first[0] != '\0' && !typeText(first, tick)) {
    keyboard.releaseAll();
    error = "The USB keyboard could not type.";
    return false;
  }
  if (tab && !tap(KEY_TAB, tick)) {
    keyboard.releaseAll();
    error = "The USB keyboard could not type.";
    return false;
  }
  if (second != nullptr && second[0] != '\0' && !typeText(second, tick)) {
    keyboard.releaseAll();
    error = "The USB keyboard could not type.";
    return false;
  }
  keyboard.releaseAll();
  error = nullptr;
  return true;
}

UsbKeyboard usbKeyboard;
