#pragma once

#include "keyboard_out.h"
#include "vault.h"

// USB serial (native CDC) line protocol. One JSON object per line.
// Commands: list, add, edit, delete, usb_ready, type_ack.
// Events are unsolicited and have no "ok".
//
// The ESP32-C3 USB port is serial, not a keyboard. When the USB typer has the
// port open it sends usb_ready, and a double tap asks it to type.
class SerialLink {
 public:
  void poll(Vault& vault, const KeyboardOut& keyboard);

  void setActive(bool active) { active_ = active; }

  bool usbTypingReady() const;
  // True once, when the USB typer becomes ready or goes quiet.
  bool consumeUsbChange(bool& ready);
  // Sends type_usb and waits for type_ack. error is set when this returns false.
  bool requestUsbType(const VaultEntry& entry, Vault& vault, const KeyboardOut& keyboard,
                      const char*& error);

  void emitReady(const Vault& vault, bool keyboardConnected);
  void emitSelected(const Vault& vault);
  void emitIdle();
  void emitTyped(const VaultEntry& entry);
  void emitTypeFailed(const char* error);
  void emitKeyboard(bool connected);
  void emitUsb(bool ready);

 private:
  char line_[1024] = {};
  bool active_ = false;
  size_t length_ = 0;
  bool discarding_ = false;
  bool typeAckPending_ = false;
  bool typeAckGot_ = false;
  bool typeAckOk_ = false;
  char typeError_[96] = {};
  bool usbSeen_ = false;
  bool usbReported_ = false;
  unsigned long lastUsbReadyMs_ = 0;

  void noteUsbReady();
  bool usbReadyNow() const;
  void handleLine(const char* line, Vault& vault, const KeyboardOut& keyboard);
  void sendSnapshot(const char* op, bool ok, const char* error, const Vault& vault,
                    const KeyboardOut& keyboard, uint32_t req, bool hasReq, uint32_t createdId,
                    bool hasCreatedId);
};
