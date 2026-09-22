#include <Arduino.h>

#include "button_input.h"
#include "config.h"
#include "keyboard_out.h"
#include "serial_link.h"
#include "status_led.h"
#include "vault.h"

ButtonInput buttonInput;
Input& input = buttonInput;
Vault vault;
KeyboardOut keyboard;
SerialLink serialLink;
StatusLed led;

enum class Activity : uint8_t { Idle, Active };

Activity activity = Activity::Idle;
// The press that leaves idle only wakes the board. Its release, and a
// hold that crosses the long-press time, do not also change the slot.
bool wakeHold = false;
unsigned long lastActivityMs = 0;

void setup() {
  // Drop the clock before the radio starts. USB on this chip uses its own
  // clock, so the serial port stays up.
  setCpuFrequencyMhz(CPU_MHZ);

  // Native USB CDC. Do not wait for a host: the keyboard must work
  // with the cable unplugged. GPIO 20 and 21 are not used.
  Serial.begin(SERIAL_BAUD);

  input.begin();
  led.begin();
  vault.begin();
  keyboard.begin();
  serialLink.emitReady(vault, keyboard.connected());
}

void goIdle() {
  activity = Activity::Idle;
  wakeHold = false;
  led.showIdle();
}

bool typeSelected() {
  const VaultEntry* current = vault.selected();
  if (current == nullptr) {
    serialLink.emitTypeFailed("No entry is selected.");
    return false;
  }
  // Copy before the USB wait. A command handled while waiting can edit the vault.
  const VaultEntry entry = *current;
  // The USB typer is the computer this cable is plugged into. It wins while
  // it has the port open, including when a Bluetooth host is also paired.
  if (serialLink.usbTypingReady()) {
    const char* error = nullptr;
    if (!serialLink.requestUsbType(entry, vault, keyboard, error)) {
      serialLink.emitTypeFailed(error);
      return false;
    }
    serialLink.emitTyped(entry);
    return true;
  }
  if (!keyboard.connected()) {
    serialLink.emitTypeFailed(
        "Bluetooth is not connected, and the USB typer is not running.");
    return false;
  }
  keyboard.typeText(entry.password);
  serialLink.emitTyped(entry);
  return true;
}

void applySelection(int slot) {
  if (!vault.selectSlot(slot)) return;
  serialLink.emitSelected(vault);
}

void loop() {
  serialLink.poll(vault, keyboard);

  const InputEvent event = input.poll();
  if (event == InputEvent::Pressed) {
    if (activity == Activity::Idle) {
      if (vault.selectSlot(0)) {
        activity = Activity::Active;
        wakeHold = true;
        lastActivityMs = millis();
        serialLink.emitSelected(vault);
      }
    } else {
      wakeHold = false;
      lastActivityMs = millis();
    }
  } else if (wakeHold &&
             (event == InputEvent::ShortPress || event == InputEvent::LongPress)) {
    wakeHold = false;
    lastActivityMs = millis();
  } else if (activity == Activity::Active && event == InputEvent::ShortPress) {
    const int count = vault.selectableCount();
    const int index = vault.selectedIndex();
    if (count > 0 && index >= 0) applySelection((index + 1) % count);
    lastActivityMs = millis();
  } else if (activity == Activity::Active && event == InputEvent::LongPress) {
    lastActivityMs = millis();
    if (typeSelected()) goIdle();
  }

  // A held button is activity. The idle timeout starts at release.
  if (buttonInput.held()) lastActivityMs = millis();

  if (activity == Activity::Active) {
    const int count = vault.selectableCount();
    if (count == 0 || millis() - lastActivityMs >= ACTIVE_IDLE_MS) {
      goIdle();
    } else {
      int index = vault.selectedIndex();
      if (index < 0 || index >= count) {
        applySelection(index < 0 ? 0 : count - 1);
        index = vault.selectedIndex();
      }
      led.showCount(index + 1);
    }
  } else {
    led.showIdle();
  }
  led.poll();

  bool connected = false;
  if (keyboard.consumeConnectionChange(connected)) serialLink.emitKeyboard(connected);

  bool usbReady = false;
  if (serialLink.consumeUsbChange(usbReady)) serialLink.emitUsb(usbReady);

  delay(LOOP_POLL_MS);
}
