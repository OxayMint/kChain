#include "serial_link.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"

namespace {

void writeJson(const JsonDocument& doc) {
  serializeJson(doc, Serial);
  Serial.print('\n');
}

void copyReq(const JsonDocument& in, JsonDocument& out) {
  if (in["req"].is<uint32_t>()) out["req"] = in["req"].as<uint32_t>();
}

}  // namespace

void SerialLink::poll(Vault& vault, const KeyboardOut& keyboard) {
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) break;
    const char c = static_cast<char>(raw);
    if (c == '\r') continue;
    if (c == '\n') {
      if (discarding_) {
        discarding_ = false;
        JsonDocument err;
        err["ok"] = false;
        err["error"] = "Command line is too long.";
        writeJson(err);
      } else if (length_ > 0) {
        line_[length_] = '\0';
        length_ = 0;
        handleLine(line_, vault, keyboard);
      }
      continue;
    }
    if (discarding_) continue;
    if (length_ + 1 >= sizeof(line_)) {
      length_ = 0;
      discarding_ = true;
      continue;
    }
    line_[length_++] = c;
  }
}

void SerialLink::sendSnapshot(const char* op, bool ok, const char* error, const Vault& vault,
                              const KeyboardOut& keyboard, uint32_t req, bool hasReq,
                              uint32_t createdId, bool hasCreatedId) {
  JsonDocument doc;
  doc["op"] = op;
  doc["ok"] = ok;
  if (hasReq) doc["req"] = req;
  if (!ok) {
    doc["error"] = error == nullptr ? "The device rejected the command." : error;
    writeJson(doc);
    return;
  }

  if (hasCreatedId) doc["id"] = createdId;
  JsonArray entries = doc["entries"].to<JsonArray>();
  for (int i = 0; i < vault.size(); i++) {
    const VaultEntry* entry = vault.at(i);
    JsonObject object = entries.add<JsonObject>();
    object["id"] = entry->id;
    object["name"] = entry->name;
    object["password"] = entry->password;
  }
  if (vault.selectedId() == 0) doc["selectedId"] = nullptr;
  else doc["selectedId"] = vault.selectedId();
  doc["keyboardConnected"] = keyboard.connected();
  writeJson(doc);
}

void SerialLink::handleLine(const char* line, Vault& vault, const KeyboardOut& keyboard) {
  JsonDocument in;
  if (deserializeJson(in, line)) {
    JsonDocument err;
    err["ok"] = false;
    err["error"] = "Command was not valid JSON.";
    writeJson(err);
    return;
  }

  const char* op = in["op"];
  const bool hasReq = in["req"].is<uint32_t>();
  const uint32_t req = hasReq ? in["req"].as<uint32_t>() : 0;
  if (op == nullptr) {
    JsonDocument err;
    err["ok"] = false;
    err["error"] = "Command needs an op field.";
    copyReq(in, err);
    writeJson(err);
    return;
  }

  if (strcmp(op, "list") == 0) {
    if (!vault.mutableOk()) {
      sendSnapshot(op, false, vault.loadError(), vault, keyboard, req, hasReq, 0, false);
      return;
    }
    sendSnapshot(op, true, nullptr, vault, keyboard, req, hasReq, 0, false);
    return;
  }

  if (strcmp(op, "add") == 0) {
    const char* error = nullptr;
    uint32_t id = 0;
    const char* name = in["name"];
    const char* password = in["password"];
    if (!in["name"].is<const char*>() || !in["password"].is<const char*>()) {
      sendSnapshot(op, false, "Add needs a name and a password.", vault, keyboard, req, hasReq, 0,
                   false);
      return;
    }
    if (!vault.add(name, password, id, error)) {
      sendSnapshot(op, false, error, vault, keyboard, req, hasReq, 0, false);
      return;
    }
    sendSnapshot(op, true, nullptr, vault, keyboard, req, hasReq, id, true);
    return;
  }

  if (strcmp(op, "edit") == 0 || strcmp(op, "delete") == 0) {
    if (!in["id"].is<uint32_t>() || in["id"].as<uint32_t>() == 0) {
      sendSnapshot(op, false, "Command needs an entry id.", vault, keyboard, req, hasReq, 0, false);
      return;
    }
    const uint32_t id = in["id"].as<uint32_t>();
    const char* error = nullptr;
    bool ok = false;
    if (strcmp(op, "delete") == 0) {
      ok = vault.remove(id, error);
    } else {
      if (!in["name"].is<const char*>() || !in["password"].is<const char*>()) {
        sendSnapshot(op, false, "Edit needs a name and a password.", vault, keyboard, req, hasReq,
                     0, false);
        return;
      }
      ok = vault.edit(id, in["name"], in["password"], error);
    }
    sendSnapshot(op, ok, error, vault, keyboard, req, hasReq, 0, false);
    return;
  }

  sendSnapshot(op, false, "Unknown command.", vault, keyboard, req, hasReq, 0, false);
}

void SerialLink::emitReady(const Vault& vault, bool keyboardConnected) {
  JsonDocument doc;
  doc["event"] = "ready";
  doc["version"] = PROTOCOL_VERSION;
  doc["keyboardConnected"] = keyboardConnected;
  if (!vault.mutableOk()) doc["error"] = vault.loadError();
  else if (vault.selectedId() == 0) doc["selectedId"] = nullptr;
  else doc["selectedId"] = vault.selectedId();
  writeJson(doc);
}

void SerialLink::emitSelected(const Vault& vault) {
  JsonDocument doc;
  doc["event"] = "selected";
  doc["index"] = vault.selectedIndex();
  if (vault.selected() == nullptr) {
    doc["id"] = nullptr;
    doc["name"] = nullptr;
  } else {
    doc["id"] = vault.selected()->id;
    doc["name"] = vault.selected()->name;
  }
  writeJson(doc);
}

void SerialLink::emitTyped(const VaultEntry& entry) {
  JsonDocument doc;
  doc["event"] = "typed";
  doc["id"] = entry.id;
  doc["name"] = entry.name;
  writeJson(doc);
}

void SerialLink::emitTypeFailed(const char* error) {
  JsonDocument doc;
  doc["event"] = "type_failed";
  doc["error"] = error == nullptr ? "Could not type." : error;
  writeJson(doc);
}

void SerialLink::emitKeyboard(bool connected) {
  JsonDocument doc;
  doc["event"] = "keyboard";
  doc["connected"] = connected;
  writeJson(doc);
}
