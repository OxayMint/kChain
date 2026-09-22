#pragma once

#include "keyboard_out.h"
#include "vault.h"

// USB serial (native CDC) line protocol. One JSON object per line.
// Commands: list, add, edit, delete. Events are unsolicited and have no "ok".
class SerialLink {
 public:
  void poll(Vault& vault, const KeyboardOut& keyboard);

  void emitReady(const Vault& vault, bool keyboardConnected);
  void emitSelected(const Vault& vault);
  void emitTyped(const VaultEntry& entry);
  void emitTypeFailed(const char* error);
  void emitKeyboard(bool connected);

 private:
  char line_[512] = {};
  size_t length_ = 0;
  bool discarding_ = false;

  void handleLine(const char* line, Vault& vault, const KeyboardOut& keyboard);
  void sendSnapshot(const char* op, bool ok, const char* error, const Vault& vault,
                    const KeyboardOut& keyboard, uint32_t req, bool hasReq, uint32_t createdId,
                    bool hasCreatedId);
};
