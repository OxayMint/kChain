#pragma once

#include <stddef.h>
#include <stdint.h>

// kChain pin and device settings for the ESP32-C3 Super Mini.
//
// Header silkscreen: GPIO 0-10, GPIO 20, GPIO 21, 5V, G, 3.3V.
// Buttons are active-low. Wire each switch between its GPIO and GND.
// Internal pull-ups hold an idle pin high; a press reads low.
//
// Defaults:
//   previous entry   GPIO 4
//   type password    GPIO 5
//   next entry       GPIO 6
//
// To move a button, change the three PIN_BUTTON_* values and rebuild.
// The static asserts below reject pins this board cannot use.
//
// Leave these alone:
//   GPIO 2, 8, 9    strapping / boot. GPIO 9 is the BOOT button.
//   GPIO 20, 21     UART. Kept free; USB serial uses the native USB port.
//   GPIO 11-19      not on the header (SPI flash and native USB).
//   5V, G, 3.3V     power only. Do not use them as signals.

static constexpr int PIN_BUTTON_PREVIOUS = 4;
static constexpr int PIN_BUTTON_TYPE = 5;
static constexpr int PIN_BUTTON_NEXT = 6;

static constexpr bool BUTTON_ACTIVE_LOW = true;
static constexpr unsigned long BUTTON_DEBOUNCE_MS = 25;

static constexpr char BLE_DEVICE_NAME[] = "kChain";
static constexpr char BLE_MANUFACTURER[] = "kChain";
static constexpr uint32_t KEY_STROKE_DELAY_MS = 8;

static constexpr int VAULT_MAX_ENTRIES = 32;
static constexpr size_t VAULT_NAME_MAX = 48;
static constexpr size_t VAULT_PASSWORD_MAX = 128;

static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr int PROTOCOL_VERSION = 1;

constexpr bool pinIsUsable(int pin) {
  return pin == 0 || pin == 1 || pin == 3 || pin == 4 || pin == 5 || pin == 6 ||
         pin == 7 || pin == 10;
}

static_assert(pinIsUsable(PIN_BUTTON_PREVIOUS),
              "previous button must use a free Super Mini GPIO (not 2, 8, 9, 11-21)");
static_assert(pinIsUsable(PIN_BUTTON_TYPE),
              "type button must use a free Super Mini GPIO (not 2, 8, 9, 11-21)");
static_assert(pinIsUsable(PIN_BUTTON_NEXT),
              "next button must use a free Super Mini GPIO (not 2, 8, 9, 11-21)");
static_assert(PIN_BUTTON_PREVIOUS != PIN_BUTTON_TYPE, "button pins must differ");
static_assert(PIN_BUTTON_PREVIOUS != PIN_BUTTON_NEXT, "button pins must differ");
static_assert(PIN_BUTTON_TYPE != PIN_BUTTON_NEXT, "button pins must differ");
