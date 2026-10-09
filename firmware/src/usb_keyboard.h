#pragma once

#include "vault.h"

// The native USB port is a serial port and a keyboard at the same time.
// A double tap types the selected entry on that keyboard. The computer
// does not run a helper to press the keys.
class UsbKeyboard {
 public:
  // Call from setup(). The keyboard interface is already registered.
  void begin();

  // True once the computer has configured this keyboard.
  bool mounted() const;

  // Types the entry and releases every key when it finishes. It does not
  // press Enter. error is set when this returns false. tick runs between
  // keys so the screen and the serial port keep moving. It may be null.
  bool typeEntry(const VaultEntry& entry, const char*& error, void (*tick)() = nullptr);
};

extern UsbKeyboard usbKeyboard;
