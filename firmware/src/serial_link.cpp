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

void writeEntry(JsonObject object, const VaultEntry& entry) {
  object["id"] = entry.id;
  object["type"] = entryTypeName(entry.type);
  object["name"] = entry.name;
  if (entry.type == EntryType::Website) object["username"] = entry.username;
  if (entry.type != EntryType::Crypto) object["password"] = entry.password;
  if (entry.type == EntryType::Crypto) object["phrase"] = entry.phrase;
}

void addTypeSteps(JsonArray steps, const VaultEntry& entry) {
  switch (entry.type) {
    case EntryType::Website:
      steps.add(entry.username);
      steps.add("\t");
      steps.add(entry.password);
      break;
    case EntryType::Crypto:
      steps.add(entry.phrase);
      break;
    case EntryType::Generic:
      steps.add(entry.password);
      break;
  }
}

bool textField(const JsonDocument& in, const char* key, const char*& out, const char*& error) {
  if (in[key].isUnbound()) {
    out = "";
    return true;
  }
  if (!in[key].is<const char*>()) {
    error = "A field was not text.";
    return false;
  }
  out = in[key].as<const char*>();
  return true;
}

// A missing type is generic, so an older name-and-password command still adds.
bool commandType(const JsonDocument& in, EntryType& type, const char*& error) {
  if (in["type"].isUnbound()) {
    type = EntryType::Generic;
    return true;
  }
  if (!in["type"].is<const char*>() || !entryTypeFrom(in["type"].as<const char*>(), type)) {
    error = "Type must be generic, website, or crypto.";
    return false;
  }
  return true;
}

}  // namespace

void SerialLink::noteUsbReady() {
  lastUsbReadyMs_ = millis();
  usbSeen_ = true;
}

bool SerialLink::usbReadyNow() const {
  if (!usbSeen_) return false;
  return millis() - lastUsbReadyMs_ < USB_READY_MS;
}

bool SerialLink::usbTypingReady() const { return usbReadyNow(); }

bool SerialLink::consumeUsbChange(bool& ready) {
  const bool now = usbReadyNow();
  if (now == usbReported_) return false;
  usbReported_ = now;
  ready = now;
  return true;
}

bool SerialLink::requestUsbType(const VaultEntry& entry, Vault& vault, const char*& error) {
  typeAckPending_ = true;
  typeAckGot_ = false;
  typeAckOk_ = false;
  typeError_[0] = '\0';

  JsonDocument doc;
  doc["event"] = "type_usb";
  doc["id"] = entry.id;
  doc["name"] = entry.name;
  doc["type"] = entryTypeName(entry.type);
  addTypeSteps(doc["steps"].to<JsonArray>(), entry);
  writeJson(doc);
  Serial.flush();

  const unsigned long start = millis();
  while (millis() - start < USB_TYPE_ACK_MS) {
    poll(vault);
    if (typeAckGot_) break;
    delay(LOOP_POLL_MS);
  }
  typeAckPending_ = false;

  if (!typeAckGot_) {
    error = "The USB typer did not answer.";
    return false;
  }
  if (!typeAckOk_) {
    error = typeError_[0] == '\0' ? "The USB typer could not type." : typeError_;
    return false;
  }
  return true;
}

void SerialLink::poll(Vault& vault) {
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
        handleLine(line_, vault);
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
                              uint32_t req, bool hasReq, uint32_t createdId, bool hasCreatedId) {
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
    writeEntry(entries.add<JsonObject>(), *entry);
  }
  if (vault.selectedId() == 0) doc["selectedId"] = nullptr;
  else doc["selectedId"] = vault.selectedId();
  doc["active"] = active_;
  // This build has no Bluetooth keyboard. The field stays so older editors
  // still parse the snapshot.
  doc["keyboardConnected"] = false;
  doc["usbTyping"] = usbReadyNow();
  writeJson(doc);
}

void SerialLink::handleLine(const char* line, Vault& vault) {
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

  if (strcmp(op, "usb_ready") == 0) {
    noteUsbReady();
    return;
  }

  if (strcmp(op, "type_ack") == 0) {
    if (!typeAckPending_) return;
    typeAckGot_ = true;
    typeAckOk_ = !in["ok"].is<bool>() || in["ok"].as<bool>();
    typeError_[0] = '\0';
    if (in["error"].is<const char*>()) {
      const char* message = in["error"].as<const char*>();
      size_t i = 0;
      while (message[i] != '\0' && i + 1 < sizeof(typeError_)) {
        typeError_[i] = message[i];
        i++;
      }
      typeError_[i] = '\0';
    }
    return;
  }

  if (strcmp(op, "list") == 0) {
    if (!vault.mutableOk()) {
      sendSnapshot(op, false, vault.loadError(), vault, req, hasReq, 0, false);
      return;
    }
    sendSnapshot(op, true, nullptr, vault, req, hasReq, 0, false);
    return;
  }

  if (strcmp(op, "add") == 0) {
    const char* error = nullptr;
    EntryType type = EntryType::Generic;
    const char* name = "";
    const char* username = "";
    const char* password = "";
    const char* phrase = "";
    if (!commandType(in, type, error) || !textField(in, "name", name, error) ||
        !textField(in, "username", username, error) || !textField(in, "password", password, error) ||
        !textField(in, "phrase", phrase, error)) {
      sendSnapshot(op, false, error, vault, req, hasReq, 0, false);
      return;
    }
    uint32_t id = 0;
    if (!vault.add(type, name, username, password, phrase, id, error)) {
      sendSnapshot(op, false, error, vault, req, hasReq, 0, false);
      return;
    }
    sendSnapshot(op, true, nullptr, vault, req, hasReq, id, true);
    return;
  }

  if (strcmp(op, "edit") == 0 || strcmp(op, "delete") == 0) {
    if (!in["id"].is<uint32_t>() || in["id"].as<uint32_t>() == 0) {
      sendSnapshot(op, false, "Command needs an entry id.", vault, req, hasReq, 0, false);
      return;
    }
    const uint32_t id = in["id"].as<uint32_t>();
    const char* error = nullptr;
    bool ok = false;
    if (strcmp(op, "delete") == 0) {
      ok = vault.remove(id, error);
    } else {
      EntryType type = EntryType::Generic;
      const char* name = "";
      const char* username = "";
      const char* password = "";
      const char* phrase = "";
      if (!commandType(in, type, error) || !textField(in, "name", name, error) ||
          !textField(in, "username", username, error) ||
          !textField(in, "password", password, error) || !textField(in, "phrase", phrase, error)) {
        sendSnapshot(op, false, error, vault, req, hasReq, 0, false);
        return;
      }
      ok = vault.edit(id, type, name, username, password, phrase, error);
    }
    sendSnapshot(op, ok, error, vault, req, hasReq, 0, false);
    return;
  }

  sendSnapshot(op, false, "Unknown command.", vault, req, hasReq, 0, false);
}

void SerialLink::emitReady(const Vault& vault) {
  JsonDocument doc;
  doc["event"] = "ready";
  doc["version"] = PROTOCOL_VERSION;
  doc["keyboardConnected"] = false;
  doc["usbTyping"] = usbReadyNow();
  doc["active"] = active_;
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

void SerialLink::emitIdle() {
  JsonDocument doc;
  doc["event"] = "idle";
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

void SerialLink::emitUsb(bool ready) {
  JsonDocument doc;
  doc["event"] = "usb";
  doc["ready"] = ready;
  writeJson(doc);
}
