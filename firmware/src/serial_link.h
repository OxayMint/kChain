#pragma once

#include "vault.h"

// USB serial (native CDC) line protocol. One JSON object per line.
// Commands: list, add, edit, delete, focus.
// focus marks which entry a tap from idle wakes onto. It does not move
// the selection by itself. A null id clears that mark.
// Events are unsolicited and have no "ok".
//
// Typing is the USB keyboard on this port, not a command. usbTyping is
// whether the computer has configured that keyboard. This build has no
// Bluetooth keyboard.
class SerialLink {
 public:
  void poll(Vault& vault);

  void setActive(bool active) { active_ = active; }
  void setUsbTyping(bool ready) { usbTyping_ = ready; }

  // True once, when the USB keyboard becomes ready or goes away.
  bool consumeUsbChange(bool& ready);

  void emitReady(const Vault& vault);
  void emitSelected(const Vault& vault);
  void emitIdle();
  void emitTyped(const VaultEntry& entry);
  void emitTypeFailed(const char* error);
  void emitUsb(bool ready);

 private:
  char line_[1024] = {};
  bool active_ = false;
  bool usbTyping_ = false;
  bool usbReported_ = false;
  size_t length_ = 0;
  bool discarding_ = false;

  void handleLine(const char* line, Vault& vault);
  void sendSnapshot(const char* op, bool ok, const char* error, const Vault& vault, uint32_t req,
                    bool hasReq, uint32_t createdId, bool hasCreatedId);
};
