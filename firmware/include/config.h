#pragma once

#include <stddef.h>
#include <stdint.h>

// kChain pin and device settings for the ESP32-S3.
//
// Header silkscreen: 1-13, GND, 5V, 3V, RX, TX. The number is the GPIO.
// The button is active-low. Wire the switch between its GPIO and GND.
// The internal pull-up holds the idle pin high; a press reads low.
//
// The button is GPIO 7. A tap on the home screen opens the highlighted
// section. The tap is reported after the double-tap window, so a double tap
// does not also open it. Holding the button for BUTTON_HOLD_MS returns to
// home. A double tap types the highlighted stored entry.
//
// The mouse-wheel encoder navigates (body marked H-13, 13 mm tall).
// A is GPIO 8, B is GPIO 9, and the common pin is ground. Wheel up moves
// to the next row and wheel down moves to the previous one. Both wrap.
// These wheels are 24 detents and 12 pulses
// per turn, so one click is two quadrature transitions.
//
// The panel is a 2.0 inch ST7789, 240x320, SPI. The module silkscreen reads
// GND, VCC, SCL, SDA, RST, DC, CS, BL. VCC is 3.3 V. SCL (the clock) is
// GPIO 1, SDA (MOSI) is GPIO 2, RST is GPIO 3, DC is GPIO 4, CS is GPIO 5,
// and BL is GPIO 6. BL driven high turns the backlight on. GPIO 3 is a
// strapping pin; holding reset high after boot is that strap's idle level.
// The glass shows the menu: kChain at the top left, and a gold outline on
// the selected row.
//
// Leave these alone:
//   GPIO 0, 45, 46  strapping. Not on this header.
//   GPIO 19, 20     native USB. Serial and the keyboard share that port, not RX and TX.
//   GPIO 26-32      SPI flash. Not on the header.
//   RX, TX          UART, GPIO 44 and GPIO 43. Kept free.
//   5V, GND, 3V     power only. Do not use them as signals.

static constexpr int PIN_BUTTON = 7;
static constexpr int PIN_ENCODER_A = 8;
static constexpr int PIN_ENCODER_B = 9;
// Module pin names. SDA is SPI MOSI, not an I2C data line.
static constexpr int PIN_TFT_SCL = 1;
static constexpr int PIN_TFT_SDA = 2;
static constexpr int PIN_TFT_RST = 3;
static constexpr int PIN_TFT_DC = 4;
static constexpr int PIN_TFT_CS = 5;
static constexpr int PIN_TFT_BL = 6;
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
// The panel enters sleep this long after the button is released and the
// wheel stays still. The editor hears idle at the same moment. A hold keeps
// the glass awake. The press or turn that leaves sleep only wakes it.
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
// 240 MHz is the Arduino default on this chip. Button and USB do not need it.
static constexpr uint32_t CPU_MHZ = 80;

// How long a HID key stays down, and how long it stays up, before the next
// one. A back-to-back report can leave the computer on the key-down only.
static constexpr unsigned long USB_KEY_GAP_MS = 12;

// Upper 2 MB filesystem. A largest website entry is about 580 bytes on
// disk, so 2048 of those fit with room left for the filesystem itself.
static constexpr int VAULT_MAX_ENTRIES = 2048;
static constexpr size_t VAULT_NAME_MAX = 48;
static constexpr size_t VAULT_HOSTNAME_MAX = 253;
static constexpr size_t VAULT_USERNAME_MAX = 128;
static constexpr size_t VAULT_PASSWORD_MAX = 128;
static constexpr size_t VAULT_PHRASE_MAX = 256;

static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr int PROTOCOL_VERSION = 1;

constexpr bool pinIsUsable(int pin) { return pin >= 1 && pin <= 13; }

static_assert(pinIsUsable(PIN_BUTTON), "button must use GPIO 1-13");
static_assert(pinIsUsable(PIN_ENCODER_A) && pinIsUsable(PIN_ENCODER_B),
              "encoder must use GPIO 1-13");
static_assert(PIN_ENCODER_A != PIN_ENCODER_B && PIN_ENCODER_A != PIN_BUTTON &&
                  PIN_ENCODER_B != PIN_BUTTON,
              "encoder pins must differ from each other and the button");
static_assert(pinIsUsable(PIN_TFT_SCL) && pinIsUsable(PIN_TFT_SDA) && pinIsUsable(PIN_TFT_RST) &&
                  pinIsUsable(PIN_TFT_DC) && pinIsUsable(PIN_TFT_CS) && pinIsUsable(PIN_TFT_BL),
              "display must use GPIO 1-13");
static_assert(PIN_TFT_SCL != PIN_TFT_SDA && PIN_TFT_SCL != PIN_TFT_RST && PIN_TFT_SCL != PIN_TFT_DC &&
                  PIN_TFT_SCL != PIN_TFT_CS && PIN_TFT_SCL != PIN_TFT_BL && PIN_TFT_SDA != PIN_TFT_RST &&
                  PIN_TFT_SDA != PIN_TFT_DC && PIN_TFT_SDA != PIN_TFT_CS && PIN_TFT_SDA != PIN_TFT_BL &&
                  PIN_TFT_RST != PIN_TFT_DC && PIN_TFT_RST != PIN_TFT_CS && PIN_TFT_RST != PIN_TFT_BL &&
                  PIN_TFT_DC != PIN_TFT_CS && PIN_TFT_DC != PIN_TFT_BL && PIN_TFT_CS != PIN_TFT_BL,
              "display pins must all differ");
static_assert(PIN_TFT_SCL != PIN_BUTTON && PIN_TFT_SDA != PIN_BUTTON && PIN_TFT_RST != PIN_BUTTON &&
                  PIN_TFT_DC != PIN_BUTTON && PIN_TFT_CS != PIN_BUTTON && PIN_TFT_BL != PIN_BUTTON &&
                  PIN_TFT_SCL != PIN_ENCODER_A && PIN_TFT_SDA != PIN_ENCODER_A &&
                  PIN_TFT_RST != PIN_ENCODER_A && PIN_TFT_DC != PIN_ENCODER_A &&
                  PIN_TFT_CS != PIN_ENCODER_A && PIN_TFT_BL != PIN_ENCODER_A &&
                  PIN_TFT_SCL != PIN_ENCODER_B && PIN_TFT_SDA != PIN_ENCODER_B &&
                  PIN_TFT_RST != PIN_ENCODER_B && PIN_TFT_DC != PIN_ENCODER_B &&
                  PIN_TFT_CS != PIN_ENCODER_B && PIN_TFT_BL != PIN_ENCODER_B,
              "display pins must differ from the button and the encoder");
static_assert(ENCODER_COUNTS_PER_DETENT >= 1, "a detent needs at least one edge");
static_assert(BUTTON_DOUBLE_TAP_MS > BUTTON_DEBOUNCE_MS, "double tap must outlast debounce");
static_assert(BUTTON_HOLD_MS > BUTTON_DOUBLE_TAP_MS, "hold must outlast a double tap");
static_assert(SEARCH_EDIT_ARM_MS > 0 && SEARCH_EDIT_ARM_MS < BUTTON_HOLD_MS,
              "search edit must arm after a click and before a hold");
static_assert(USB_KEY_GAP_MS > 0, "a key needs time down and up");
static_assert(SEARCH_BLINK_MS > 0, "a blink needs a period");
static_assert(SEARCH_QUERY_MAX >= 1, "a search needs room for one letter");
static_assert(VAULT_HOSTNAME_MAX >= VAULT_NAME_MAX, "a hostname needs at least a name's room");
static_assert(VAULT_PHRASE_MAX > VAULT_PASSWORD_MAX, "a seed phrase needs more room than a password");
