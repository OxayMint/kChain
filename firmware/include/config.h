#pragma once

#include <stddef.h>
#include <stdint.h>

// kChain pin and device settings for the ESP32-C3 Super Mini.
//
// Header silkscreen: GPIO 0-10, GPIO 20, GPIO 21, 5V, G, 3.3V.
// The button is active-low. Wire the switch between its GPIO and GND.
// The internal pull-up holds the idle pin high; a press reads low.
//
// The button is GPIO 5. A tap wakes the board. While awake, a press held
// past SEARCH_EDIT_ARM_MS appends a blinking search letter. Releasing it
// leaves the letter solid. A shorter press is a click, so two of them can
// be a double tap. A double tap types the selected entry, and drops a letter
// if that gesture already added one. The wheel changes the blinking letter
// until release. Delete sits between Z and A.
//
// The mouse-wheel encoder navigates (body marked H-13, 13 mm tall).
// A is GPIO 6, B is GPIO 7, and the common pin is ground. Wheel up steps
// to the next matching entry and wheel down steps to the previous one;
// both wrap. While a search letter is blinking, the wheel changes that
// letter instead, with a delete mark between Z and A.
// These wheels are 24 detents and 12 pulses
// per turn, so one click is two quadrature transitions.
//
// GPIO 8 is the onboard LED and a strapping pin. The firmware does not
// drive it. While the board is awake the 0.91 inch panel shows the search
// query and two matching names, right-aligned, with the type icon after the
// name. The selected name is larger. A line runs from it to a bar on the
// left edge. A type opens that bar and slides the name through. Idle clears
// the glass.
//
// Leave these alone:
//   GPIO 2, 9       strapping / boot. GPIO 9 is the BOOT button.
//   GPIO 20, 21     UART. Kept free; USB serial uses the native USB port.
//   GPIO 11-19      not on the header (SPI flash and native USB).
//   5V, G, 3.3V     power only. Do not use them as signals.

static constexpr int PIN_BUTTON = 5;
static constexpr int PIN_ENCODER_A = 6;
static constexpr int PIN_ENCODER_B = 7;
// 0.91 inch SSD1306, 128x32, on I2C. The module silkscreen says SCK for the
// clock. SDA is GPIO 1 and that clock is GPIO 0. The module pull-ups hold
// both lines high while the bus is idle.
static constexpr int PIN_OLED_SDA = 1;
static constexpr int PIN_OLED_SCL = 0;
// Two same-direction transitions are one detent. One edge is only half
// a click, which is what made the selection hop to the neighbor and back.
static constexpr int ENCODER_COUNTS_PER_DETENT = 2;

static constexpr bool BUTTON_ACTIVE_LOW = true;
static constexpr unsigned long BUTTON_DEBOUNCE_MS = 25;
static constexpr unsigned long BUTTON_HOLD_MS = 1000;
// The second tap has to start within this long after the first release.
// Both taps are released before the hold time, or the press is a hold.
static constexpr unsigned long BUTTON_DOUBLE_TAP_MS = 500;
// A press released before this does not open search edit. A normal click
// stays down longer than a few tens of milliseconds, so this has to cover
// that. It stays a click that can pair with a second click into a double
// tap. Search edit starts once the button has been down this long.
static constexpr unsigned long SEARCH_EDIT_ARM_MS = 200;
// Active mode ends this long after the button is released, if it is not
// pressed again. A hold keeps the board active.
static constexpr unsigned long ACTIVE_IDLE_MS = 5000;
// One step of the exit animation. The left edge opens, the selected name
// slides through, and the edge closes.
static constexpr unsigned long SEND_STEP_MS = 20;
// Long enough for that exit on the 128-wide panel. The drawer owns the pixels.
static constexpr int SEND_ANIM_TICKS = 40;
// The last search letter hides and shows this often while the button is held.
static constexpr unsigned long SEARCH_BLINK_MS = 500;
// One glyph is 6 pixels wide, so 21 letters fill a 128-wide panel.
static constexpr size_t SEARCH_QUERY_MAX = 21;

// loop() polls the button, the encoder steps, and USB. A tight loop holds this
// core out of idle, and that is most of the heat while the board is on the
// 5V from USB-C. This is shorter than the button debounce.
static constexpr unsigned long LOOP_POLL_MS = 10;
// 160 MHz is the Arduino default. Button and USB do not need it.
static constexpr uint32_t CPU_MHZ = 80;

// Typing waits this long for the USB typer to finish the password.
// 128 characters at the host's per-key delay fits inside it.
static constexpr unsigned long USB_TYPE_ACK_MS = 5000;
// The USB typer refreshes faster than this. When the refreshes stop, the
// cable is no longer the path that types.
static constexpr unsigned long USB_READY_MS = 3000;

static constexpr int VAULT_MAX_ENTRIES = 32;
static constexpr size_t VAULT_NAME_MAX = 48;
static constexpr size_t VAULT_HOSTNAME_MAX = 253;
static constexpr size_t VAULT_USERNAME_MAX = 128;
static constexpr size_t VAULT_PASSWORD_MAX = 128;
static constexpr size_t VAULT_PHRASE_MAX = 256;

static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr int PROTOCOL_VERSION = 1;

constexpr bool pinIsUsable(int pin) {
  return pin == 0 || pin == 1 || pin == 3 || pin == 4 || pin == 5 || pin == 6 ||
         pin == 7 || pin == 10;
}

static_assert(pinIsUsable(PIN_BUTTON),
              "button must use a free Super Mini GPIO (not 2, 8, 9, 11-21)");
static_assert(pinIsUsable(PIN_ENCODER_A) && pinIsUsable(PIN_ENCODER_B),
              "encoder must use free Super Mini GPIOs (not 2, 8, 9, 11-21)");
static_assert(PIN_ENCODER_A != PIN_ENCODER_B && PIN_ENCODER_A != PIN_BUTTON &&
                  PIN_ENCODER_B != PIN_BUTTON,
              "encoder pins must differ from each other and the button");
static_assert(pinIsUsable(PIN_OLED_SDA) && pinIsUsable(PIN_OLED_SCL),
              "oled must use a free Super Mini GPIO (not 2, 8, 9, 11-21)");
static_assert(PIN_OLED_SDA != PIN_OLED_SCL && PIN_OLED_SDA != PIN_BUTTON &&
                  PIN_OLED_SCL != PIN_BUTTON && PIN_OLED_SDA != PIN_ENCODER_A &&
                  PIN_OLED_SDA != PIN_ENCODER_B && PIN_OLED_SCL != PIN_ENCODER_A &&
                  PIN_OLED_SCL != PIN_ENCODER_B,
              "oled pins must differ from each other, the button, and the encoder");
static_assert(ENCODER_COUNTS_PER_DETENT >= 1, "a detent needs at least one edge");
static_assert(BUTTON_DOUBLE_TAP_MS > BUTTON_DEBOUNCE_MS, "double tap must outlast debounce");
static_assert(BUTTON_HOLD_MS > BUTTON_DOUBLE_TAP_MS, "hold must outlast a double tap");
static_assert(SEARCH_EDIT_ARM_MS > 0 && SEARCH_EDIT_ARM_MS < BUTTON_HOLD_MS,
              "search edit must arm after a click and before a hold");
static_assert(SEARCH_BLINK_MS > 0, "a blink needs a period");
static_assert(SEARCH_QUERY_MAX >= 1, "a search needs room for one letter");
static_assert(VAULT_HOSTNAME_MAX >= VAULT_NAME_MAX, "a hostname needs at least a name's room");
static_assert(VAULT_PHRASE_MAX > VAULT_PASSWORD_MAX, "a seed phrase needs more room than a password");
