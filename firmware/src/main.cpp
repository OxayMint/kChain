#include <Arduino.h>

#include "button_input.h"
#include "config.h"
#include "encoder_input.h"
#include "menu.h"
#include "serial_link.h"
#include "st7789_display.h"
#include "vault.h"

ButtonInput buttonInput;
EncoderInput encoderInput;
Input& input = buttonInput;
Vault vault;
SerialLink serialLink;
Menu menu;
St7789Display panel;
bool panelReady = false;

// The S3 HAL keeps the Bluetooth controller unless this returns false.
// initArduino then releases that memory before setup, so the radio cannot start.
// This program never starts Wi-Fi either.
extern "C" bool btInUse() { return false; }

enum class Activity : uint8_t { Idle, Active };

Activity activity = Activity::Idle;
// The gesture that leaves idle only wakes the board. A later double tap types.
bool wakeGesture = false;
unsigned long lastActivityMs = 0;
// A press may open search edit once it has been down long enough. A release
// before that is a click, and two clicks are a double tap.
bool searchArm = false;
unsigned long searchArmMs = 0;
// That press did open search edit, so a double tap drops the new letter.
bool searchLetterPending = false;

// The panel is showing the bring-up pattern. The menu is not drawn on it yet.
void showMenu() {}

// Runs while requestUsbType waits. loop() is not turning, so the name
// would otherwise sit still until the computer answers.
void tickSendAnimation() {
  menu.tickSend(millis());
  showMenu();
}

void setup() {
  // USB on this chip uses its own clock, so the serial port stays up.
  setCpuFrequencyMhz(CPU_MHZ);

  // Native USB CDC. RX and TX are not used. The radio stays down:
  // Bluetooth memory was released at boot, and Wi-Fi is never started.
  Serial.begin(SERIAL_BAUD);

  panelReady = panel.begin();
  if (panelReady) panel.drawTest();
  input.begin();
  vault.begin();
  encoderInput.begin();
  menu.sync(vault, false);
  showMenu();
  serialLink.emitReady(vault);
}

void goIdle() {
  activity = Activity::Idle;
  wakeGesture = false;
  searchArm = false;
  searchLetterPending = false;
  serialLink.setActive(false);
  serialLink.emitIdle();
}

bool typeSelected() {
  const VaultEntry* current = vault.selected();
  if (current == nullptr) {
    serialLink.emitTypeFailed("No entry is selected.");
    return false;
  }
  // Copy before the USB wait. A command handled while waiting can edit the vault.
  const VaultEntry entry = *current;
  if (!serialLink.usbTypingReady()) {
    serialLink.emitTypeFailed("The USB typer is not running.");
    return false;
  }
  menu.beginSend();
  showMenu();
  const char* error = nullptr;
  if (!serialLink.requestUsbType(entry, vault, error, tickSendAnimation)) {
    menu.cancelSend();
    menu.sync(vault, true);
    showMenu();
    serialLink.emitTypeFailed(error);
    return false;
  }
  serialLink.emitTyped(entry);
  return true;
}

void applySelection(int slot) {
  if (!vault.selectSlot(slot)) return;
  serialLink.emitSelected(vault);
}

void loop() {
  serialLink.poll(vault);

  // Tap, double tap, and hold. A press wakes immediately, before the
  // gesture is classified.
  const InputEvent event = buttonInput.poll();
  if (buttonInput.consumePress()) {
    if (activity == Activity::Idle) {
      if (vault.selectSlot(vault.wakeSlot())) {
        activity = Activity::Active;
        wakeGesture = true;
        serialLink.setActive(true);
        lastActivityMs = millis();
        serialLink.emitSelected(vault);
      }
    } else {
      lastActivityMs = millis();
      // The second press of a double tap types. It must not add another letter.
      if (!buttonInput.inSecondTap()) {
        searchArm = true;
        searchArmMs = millis();
      }
    }
  }
  // Still down after the click window: this press is search edit, not a click.
  if (searchArm && buttonInput.held() && millis() - searchArmMs >= SEARCH_EDIT_ARM_MS) {
    searchArm = false;
    const int selectedBefore = vault.selectedIndex();
    menu.beginSearchEdit(vault);
    searchLetterPending = menu.editing();
    if (vault.selectedIndex() != selectedBefore) serialLink.emitSelected(vault);
  }
  if (!buttonInput.held()) searchArm = false;

  if (event == InputEvent::DoubleTap && wakeGesture) {
    wakeGesture = false;
    lastActivityMs = millis();
  } else if (activity == Activity::Active && event == InputEvent::DoubleTap) {
    lastActivityMs = millis();
    const int selectedBefore = vault.selectedIndex();
    if (searchLetterPending) menu.cancelLastLetter(vault);
    searchLetterPending = false;
    if (vault.selectedIndex() != selectedBefore) serialLink.emitSelected(vault);
    menu.sync(vault, true);
    if (menu.canType() && typeSelected()) goIdle();
  } else if (event == InputEvent::Tap || event == InputEvent::Hold) {
    wakeGesture = false;
    lastActivityMs = millis();
    // The click stood alone, so a letter added by holding it stays.
    searchLetterPending = false;
  }

  if (menu.editing() && !buttonInput.held()) menu.endSearchEdit();

  // Wheel up is positive. Wheel down is negative. A turn also wakes the board.
  // A letter change that stays on the same entry does not tell the host again.
  const int selectedBefore = vault.selectedIndex();
  if (menu.onWheel(vault, encoderInput.takeSteps())) {
    activity = Activity::Active;
    serialLink.setActive(true);
    lastActivityMs = millis();
    if (vault.selectedIndex() != selectedBefore) serialLink.emitSelected(vault);
  }

  // A held button is activity. The idle timeout starts at release.
  if (buttonInput.held()) lastActivityMs = millis();

  if (activity == Activity::Active) {
    const int count = vault.selectableCount();
    if (count == 0 || millis() - lastActivityMs >= ACTIVE_IDLE_MS) {
      goIdle();
    } else {
      const int index = vault.selectedIndex();
      if (index < 0 || index >= count) applySelection(index < 0 ? 0 : count - 1);
      if (menu.refit(vault)) serialLink.emitSelected(vault);
    }
  }

  menu.tickBlink(millis());
  menu.sync(vault, activity == Activity::Active);
  showMenu();

  bool usbReady = false;
  if (serialLink.consumeUsbChange(usbReady)) serialLink.emitUsb(usbReady);

  delay(LOOP_POLL_MS);
}
