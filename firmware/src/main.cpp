#include <Arduino.h>

#include "button_input.h"
#include "config.h"
#include "encoder_input.h"
#include "keyboard_out.h"
#include "serial_link.h"
#include "vault.h"

ButtonInput buttonInput;
EncoderInput encoderInput;
Input& input = buttonInput;
Vault vault;
KeyboardOut keyboard;
SerialLink serialLink;

enum class Activity : uint8_t { Idle, Active };

Activity activity = Activity::Idle;
// The gesture that leaves idle only wakes the board. A later double tap types.
bool wakeGesture = false;
unsigned long lastActivityMs = 0;

void setup() {
  // Drop the clock before the radio starts. USB on this chip uses its own
  // clock, so the serial port stays up.
  setCpuFrequencyMhz(CPU_MHZ);

  // Native USB CDC. Do not wait for a host: the keyboard must work
  // with the cable unplugged. GPIO 20 and 21 are not used.
  Serial.begin(SERIAL_BAUD);

  input.begin();
  vault.begin();
  keyboard.begin();
  // After Bluetooth. Starting the radio clears GPIO interrupts attached earlier.
  encoderInput.begin();
  serialLink.emitReady(vault, keyboard.connected());
}

void goIdle() {
  activity = Activity::Idle;
  wakeGesture = false;
  serialLink.setActive(false);
  serialLink.emitIdle();
}

void typeOnKeyboard(const VaultEntry& entry) {
  switch (entry.type) {
    case EntryType::Website:
      keyboard.typeText(entry.username);
      keyboard.typeTab();
      keyboard.typeText(entry.password);
      break;
    case EntryType::Crypto:
      keyboard.typeText(entry.phrase);
      break;
    case EntryType::Generic:
      keyboard.typeText(entry.password);
      break;
  }
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
  typeOnKeyboard(entry);
  serialLink.emitTyped(entry);
  return true;
}

void applySelection(int slot) {
  if (!vault.selectSlot(slot)) return;
  serialLink.emitSelected(vault);
}

// One detent per step. Wheel up moves toward higher slots and wraps.
// Wheel down moves the other way and wraps. A turn also wakes the board.
void stepSelection(int delta) {
  const int count = vault.selectableCount();
  if (count <= 0 || delta == 0) return;
  int index = vault.selectedIndex();
  if (index < 0) index = 0;
  int slot = (index + delta) % count;
  if (slot < 0) slot += count;
  activity = Activity::Active;
  serialLink.setActive(true);
  lastActivityMs = millis();
  applySelection(slot);
}

void loop() {
  serialLink.poll(vault, keyboard);

  // Tap, double tap, and hold. A press wakes immediately, before the
  // gesture is classified.
  const InputEvent event = buttonInput.poll();
  if (buttonInput.consumePress()) {
    if (activity == Activity::Idle) {
      if (vault.selectSlot(0)) {
        activity = Activity::Active;
        wakeGesture = true;
        serialLink.setActive(true);
        lastActivityMs = millis();
        serialLink.emitSelected(vault);
      }
    } else {
      lastActivityMs = millis();
    }
  }
  if (event == InputEvent::DoubleTap && wakeGesture) {
    wakeGesture = false;
    lastActivityMs = millis();
  } else if (activity == Activity::Active && event == InputEvent::DoubleTap) {
    lastActivityMs = millis();
    if (typeSelected()) goIdle();
  } else if (event == InputEvent::Tap || event == InputEvent::Hold) {
    wakeGesture = false;
    lastActivityMs = millis();
  }

  // Wheel up is positive. Wheel down is negative.
  stepSelection(encoderInput.takeSteps());

  // A held button is activity. The idle timeout starts at release.
  if (buttonInput.held()) lastActivityMs = millis();

  if (activity == Activity::Active) {
    const int count = vault.selectableCount();
    if (count == 0 || millis() - lastActivityMs >= ACTIVE_IDLE_MS) {
      goIdle();
    } else {
      const int index = vault.selectedIndex();
      if (index < 0 || index >= count) applySelection(index < 0 ? 0 : count - 1);
    }
  }

  bool connected = false;
  if (keyboard.consumeConnectionChange(connected)) serialLink.emitKeyboard(connected);

  bool usbReady = false;
  if (serialLink.consumeUsbChange(usbReady)) serialLink.emitUsb(usbReady);

  delay(LOOP_POLL_MS);
}
