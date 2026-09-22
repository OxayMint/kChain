#include <Arduino.h>

#include "button_input.h"
#include "config.h"
#include "keyboard_out.h"
#include "serial_link.h"
#include "vault.h"

ButtonInput buttonInput;
Input& input = buttonInput;
Vault vault;
KeyboardOut keyboard;
SerialLink serialLink;

void setup() {
  // Native USB CDC. Do not wait for a host: the keyboard must work
  // with the cable unplugged. GPIO 20 and 21 are not used.
  Serial.begin(SERIAL_BAUD);

  input.begin();
  vault.begin();
  keyboard.begin();
  serialLink.emitReady(vault, keyboard.connected());
}

void loop() {
  serialLink.poll(vault, keyboard);

  const InputEvent event = input.poll();
  if (event == InputEvent::Previous || event == InputEvent::Next) {
    const bool moved =
        event == InputEvent::Previous ? vault.selectPrevious() : vault.selectNext();
    if (moved) serialLink.emitSelected(vault);
  } else if (event == InputEvent::Type) {
    const VaultEntry* entry = vault.selected();
    if (entry == nullptr) {
      serialLink.emitTypeFailed("No entry is selected.");
    } else if (!keyboard.connected()) {
      serialLink.emitTypeFailed("Bluetooth keyboard is not connected.");
    } else {
      keyboard.typeText(entry->password);
      serialLink.emitTyped(*entry);
    }
  }

  bool connected = false;
  if (keyboard.consumeConnectionChange(connected)) serialLink.emitKeyboard(connected);
}
