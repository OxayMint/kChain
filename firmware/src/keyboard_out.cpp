#include "keyboard_out.h"

#include <Arduino.h>
#include <BLESecurity.h>
#include <BleKeyboard.h>
#include <esp_bt.h>

#include "config.h"

namespace {
BleKeyboard bleKeyboard(BLE_DEVICE_NAME, BLE_MANUFACTURER, 100);

// The prebuilt Arduino controller defaults to modem-sleep off and +9 dBm.
// On the Super Mini that keeps the radio hot the whole time it is plugged
// into USB. There is no 32 kHz crystal on this board; the main crystal is
// the sleep clock that still lets the radio power down between events.
// +3 dBm is enough for a keyboard sitting at the computer.
constexpr esp_power_level_t kTxPower = ESP_PWR_LVL_P3;

void configureController() {
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) return;

  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  cfg.sleep_mode = ESP_BT_SLEEP_MODE_1;
  cfg.sleep_clock = ESP_BT_SLEEP_CLOCK_MAIN_XTAL;
  cfg.txpwr_dft = static_cast<uint8_t>(kTxPower);
  // If this fails, status stays idle and BleKeyboard starts the default
  // controller. The keyboard still works; it just runs warmer.
  esp_bt_controller_init(&cfg);
}

void applyTxPower() {
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, kTxPower);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, kTxPower);
}
}  // namespace

void KeyboardOut::begin() {
  started_ = false;
  wasConnected_ = false;
  // Must run before BleKeyboard::begin(), which enables the controller and
  // will not replace a config that is already applied.
  configureController();
  bleKeyboard.begin();
  esp_bt_sleep_enable();
  applyTxPower();
  // The library asks for MITM secure-connections bonding. This board cannot
  // show a passkey, so the host connects and then drops HID reports. Just
  // Works bonding is the mode the C3 can finish. An old bond stays broken
  // until the host forgets kChain and pairs again.
  BLESecurity security;
  security.setAuthenticationMode(ESP_LE_AUTH_BOND);
  security.setCapability(ESP_IO_CAP_NONE);
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  wasConnected_ = bleKeyboard.isConnected();
  started_ = true;
}

bool KeyboardOut::connected() const {
  if (!started_) return false;
  return bleKeyboard.isConnected();
}

bool KeyboardOut::consumeConnectionChange(bool& connected) {
  if (!started_) return false;
  const bool now = bleKeyboard.isConnected();
  if (now == wasConnected_) return false;
  wasConnected_ = now;
  connected = now;
  // Per-connection power only sticks after the link exists. Handle 0 is the
  // first, and the only, HID connection this board opens.
  if (now) esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL0, kTxPower);
  return true;
}

void KeyboardOut::typeText(const std::string& text) {
  if (!connected()) return;
  // write() presses and releases with no wait, and setDelay() only applies
  // to the NimBLE build. Hold each key long enough for its report to leave.
  for (size_t i = 0; i < text.size(); i++) {
    const uint8_t key = static_cast<uint8_t>(text[i]);
    bleKeyboard.press(key);
    delay(KEY_STROKE_DELAY_MS);
    bleKeyboard.release(key);
    delay(KEY_STROKE_DELAY_MS);
  }
  // Drop any modifier that a symbol key may have held, then stop.
  // No Enter, Tab, or extra character is sent.
  bleKeyboard.releaseAll();
}

void KeyboardOut::typeTab() {
  if (!connected()) return;
  // KEY_TAB is the library's non-printing tab, not the ASCII tab byte.
  bleKeyboard.press(KEY_TAB);
  delay(KEY_STROKE_DELAY_MS);
  bleKeyboard.release(KEY_TAB);
  delay(KEY_STROKE_DELAY_MS);
}
