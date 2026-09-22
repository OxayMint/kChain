#pragma once

#include <stddef.h>
#include <stdint.h>

// kChain pin and device settings for the ESP32-C3 Super Mini.
//
// Header silkscreen: GPIO 0-10, GPIO 20, GPIO 21, 5V, G, 3.3V.
// The button is active-low. Wire the switch between its GPIO and GND.
// The internal pull-up holds the idle pin high; a press reads low.
//
// One button for now, on GPIO 5. A short press steps the selection.
// Holding it for BUTTON_LONG_PRESS_MS types the selected password.
//
// The onboard LED is the blue diode on GPIO 8. It is active-low:
// driving the pin low lights it. GPIO 8 is also a strapping pin;
// it is only driven after boot, as an output.
//
// Leave these alone:
//   GPIO 2, 9       strapping / boot. GPIO 9 is the BOOT button.
//   GPIO 20, 21     UART. Kept free; USB serial uses the native USB port.
//   GPIO 11-19      not on the header (SPI flash and native USB).
//   5V, G, 3.3V     power only. Do not use them as signals.

static constexpr int PIN_BUTTON = 5;
static constexpr int PIN_LED = 8;

static constexpr bool BUTTON_ACTIVE_LOW = true;
static constexpr bool LED_ACTIVE_LOW = true;
static constexpr unsigned long BUTTON_DEBOUNCE_MS = 25;
static constexpr unsigned long BUTTON_LONG_PRESS_MS = 1000;
// Active mode ends this long after the button is released, if it is not
// pressed again. A hold keeps the board active.
static constexpr unsigned long ACTIVE_IDLE_MS = 5000;

// loop() only polls the button, the LED, and USB. A tight loop holds this
// core out of idle, and that is most of the heat while the board is on the
// 5V from USB-C. This is shorter than the button debounce and the LED flashes.
static constexpr unsigned long LOOP_POLL_MS = 10;
// 160 MHz is the Arduino default. Button, LED, USB, and BLE HID do not need it.
static constexpr uint32_t CPU_MHZ = 80;

// One count is a burst of short flashes, split into groups of three.
// Inside a group the diode blinks LED_FLASH_ON_MS, then stays dark for
// LED_FLASH_GAP_MS. Groups are separated by LED_GROUP_GAP_MS.
// After the whole count, the diode stays dark for LED_CYCLE_PAUSE_MS
// and the burst repeats. Count 2 is two short flashes. Count 4 is
// three flashes, 200 ms, then one more.
static constexpr unsigned long LED_FLASH_ON_MS = 70;
static constexpr unsigned long LED_FLASH_GAP_MS = 80;
static constexpr unsigned long LED_GROUP_GAP_MS = 200;
static constexpr unsigned long LED_CYCLE_PAUSE_MS = 1000;
static constexpr int LED_GROUP_SIZE = 3;

static constexpr char BLE_DEVICE_NAME[] = "kChain";
static constexpr char BLE_MANUFACTURER[] = "kChain";
// Gap after a key-down report and again after key-up. The BLE stack keeps
// one pending notification, so a faster release replaces the key-down and
// the host sees no character. This also has to cover a connection interval.
static constexpr uint32_t KEY_STROKE_DELAY_MS = 30;
// A long press waits this long for the USB typer to finish the password.
// 128 characters at the host's per-key delay fits inside it.
static constexpr unsigned long USB_TYPE_ACK_MS = 5000;
// The USB typer refreshes faster than this. When the refreshes stop, the
// cable is no longer the path that types.
static constexpr unsigned long USB_READY_MS = 3000;

static constexpr int VAULT_MAX_ENTRIES = 32;
static constexpr int VAULT_BUTTON_SLOTS = 5;
static constexpr size_t VAULT_NAME_MAX = 48;
static constexpr size_t VAULT_PASSWORD_MAX = 128;

static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr int PROTOCOL_VERSION = 1;

constexpr bool pinIsUsable(int pin) {
  return pin == 0 || pin == 1 || pin == 3 || pin == 4 || pin == 5 || pin == 6 ||
         pin == 7 || pin == 10;
}

static_assert(pinIsUsable(PIN_BUTTON),
              "button must use a free Super Mini GPIO (not 2, 8, 9, 11-21)");
static_assert(PIN_LED == 8, "onboard LED on the Super Mini is GPIO 8");
static_assert(VAULT_BUTTON_SLOTS >= 1, "the button needs at least one slot");
static_assert(VAULT_BUTTON_SLOTS <= VAULT_MAX_ENTRIES, "button slots exceed the vault");
static_assert(LED_GROUP_SIZE >= 1, "flash groups must be non-empty");
