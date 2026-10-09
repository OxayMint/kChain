#include <Arduino.h>

#include "button_input.h"
#include "config.h"
#include "encoder_input.h"
#include "serial_link.h"
#include "st7789_display.h"
#include "ui.h"
#include "usb_keyboard.h"
#include "vault.h"

ButtonInput buttonInput;
EncoderInput encoderInput;
Input& input = buttonInput;
Vault vault;
SerialLink serialLink;
Ui ui;
St7789Display panel;
bool panelReady = false;

// The S3 HAL keeps the Bluetooth controller unless this returns false.
// initArduino then releases that memory before setup, so the radio cannot start.
// This program never starts Wi-Fi either.
extern "C" bool btInUse() { return false; }

enum class Activity : uint8_t { Idle, Active };

Activity activity = Activity::Idle;
unsigned long lastActivityMs = 0;
bool glassAwake = true;
// The contact that leaves sleep only wakes the panel.
// The button gesture already in progress is dropped, including the tap
// that arrives after the double-tap window.
bool swallowGesture = false;

void noteActivity() { lastActivityMs = millis(); }

void setGlassAwake(bool awake) {
  if (!panelReady || glassAwake == awake) return;
  glassAwake = awake;
  if (awake) {
    panel.wake();
    // Detents that arrived during the 120 ms wake wait are part of the
    // contact that only turns the panel on.
    encoderInput.takeSteps();
    ui.invalidate();
  } else {
    panel.sleep();
  }
}

void showMenu() {
  if (panelReady) ui.paint(panel, vault);
}

// Runs between HID keys. A command on the serial port would otherwise wait
// until typing ends, and an edit would leave the glass on the old names.
void tickWhileTyping() {
  serialLink.poll(vault);
  ui.sync(vault);
  showMenu();
}

void setup() {
  // USB on this chip uses its own clock, so the serial port stays up.
  setCpuFrequencyMhz(CPU_MHZ);

  // Native USB is serial and a keyboard. The keyboard interface is already
  // registered. RX and TX are not used. The radio stays down: Bluetooth
  // memory was released at boot, and Wi-Fi is never started.
  usbKeyboard.begin();
  Serial.begin(SERIAL_BAUD);

  panelReady = panel.begin();
  input.begin();
  vault.begin();
  encoderInput.begin();
  ui.sync(vault);
  showMenu();
  noteActivity();
  serialLink.emitReady(vault);
}

void goIdle() {
  if (activity == Activity::Idle) return;
  activity = Activity::Idle;
  serialLink.setActive(false);
  serialLink.emitIdle();
}

void publishSelection() {
  const int slot = ui.highlightedSlot(vault);
  if (slot < 0) return;
  const bool woke = activity == Activity::Idle;
  const int before = vault.selectedIndex();
  activity = Activity::Active;
  serialLink.setActive(true);
  lastActivityMs = millis();
  if (!vault.selectSlot(slot)) return;
  if (woke || vault.selectedIndex() != before) serialLink.emitSelected(vault);
}

bool typeSelected() {
  const VaultEntry* current = vault.selected();
  if (current == nullptr) {
    serialLink.emitTypeFailed("No entry is selected.");
    return false;
  }
  // Copy before typing. A command handled between keys can edit the vault.
  const VaultEntry entry = *current;
  if (!usbKeyboard.mounted()) {
    serialLink.emitTypeFailed("The USB keyboard is not connected.");
    return false;
  }
  const char* error = nullptr;
  if (!usbKeyboard.typeEntry(entry, error, tickWhileTyping)) {
    serialLink.emitTypeFailed(error);
    return false;
  }
  serialLink.emitTyped(entry);
  return true;
}

void loop() {
  serialLink.setUsbTyping(usbKeyboard.mounted());
  serialLink.poll(vault);

  // A tap opens a home row. A hold returns home. A double tap types.
  // The tap is reported only after the double-tap window closes.
  const InputEvent event = buttonInput.poll();
  buttonInput.consumePress();
  // Wheel up is positive. Wheel down is negative.
  const int steps = encoderInput.takeSteps();
  const bool gesture = event != InputEvent::None || buttonInput.busy();

  if (!glassAwake) {
    if (gesture || steps != 0) {
      setGlassAwake(true);
      noteActivity();
      swallowGesture = gesture;
    }
  } else if (swallowGesture) {
    noteActivity();
    if (steps != 0) {
      ui.onWheel(vault, steps);
      publishSelection();
    }
    if (!gesture) swallowGesture = false;
  } else {
    if (event == InputEvent::Tap) {
      noteActivity();
      if (ui.onTap()) publishSelection();
    } else if (event == InputEvent::Hold) {
      noteActivity();
      ui.onHold();
    } else if (event == InputEvent::DoubleTap) {
      noteActivity();
      if (ui.canType(vault)) {
        publishSelection();
        if (typeSelected()) goIdle();
        // Typing can outlast the idle window. The wait starts when it finishes.
        noteActivity();
      }
    }

    if (steps != 0) {
      noteActivity();
      ui.onWheel(vault, steps);
      publishSelection();
    }
  }

  // A held button is activity. The idle timeout starts at release.
  if (buttonInput.held()) noteActivity();

  ui.sync(vault);
  if (activity == Activity::Active) {
    const uint32_t beforeId = vault.selectedId();
    const int slot = ui.highlightedSlot(vault);
    if (slot >= 0 && vault.selectSlot(slot) && vault.selectedId() != beforeId) {
      serialLink.emitSelected(vault);
    }
    if (vault.selectableCount() == 0) goIdle();
  }

  if (millis() - lastActivityMs >= ACTIVE_IDLE_MS) {
    if (activity == Activity::Active) goIdle();
    setGlassAwake(false);
  }

  if (glassAwake) showMenu();

  bool usbReady = false;
  if (serialLink.consumeUsbChange(usbReady)) serialLink.emitUsb(usbReady);

  delay(LOOP_POLL_MS);
}
