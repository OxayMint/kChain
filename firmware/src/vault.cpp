#include "vault.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include <cstring>

#include "config.h"

namespace {

constexpr char kNamespace[] = "kchain";

std::string entryKey(uint32_t id) { return std::string("e") + std::to_string(id); }

bool containsId(const std::vector<VaultEntry>& entries, uint32_t id) {
  for (size_t i = 0; i < entries.size(); i++) {
    if (entries[i].id == id) return true;
  }
  return false;
}

}  // namespace

bool vaultNameOk(const char* name, const char*& error) {
  if (name == nullptr || name[0] == '\0') {
    error = "Name is required.";
    return false;
  }
  const size_t length = strlen(name);
  if (length > VAULT_NAME_MAX) {
    error = "Name must be 48 bytes or fewer.";
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    const unsigned char c = static_cast<unsigned char>(name[i]);
    if (c < 0x20 || c == 0x7f) {
      error = "Name cannot include control characters.";
      return false;
    }
  }
  return true;
}

bool vaultPasswordOk(const char* password, const char*& error) {
  if (password == nullptr || password[0] == '\0') {
    error = "Password is required.";
    return false;
  }
  const size_t length = strlen(password);
  if (length > VAULT_PASSWORD_MAX) {
    error = "Password must be 128 characters or fewer.";
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    const unsigned char c = static_cast<unsigned char>(password[i]);
    if (c < 0x20 || c > 0x7e) {
      error = "Password must be printable ASCII so it can be typed.";
      return false;
    }
  }
  return true;
}

bool Vault::begin() {
  entries_.clear();
  persistedIds_.clear();
  nextId_ = 1;
  selected_ = -1;
  mutable_ = false;
  loadError_ = "Could not read the vault from flash.";

  if (!load()) return false;

  mutable_ = true;
  loadError_ = nullptr;
  if (!entries_.empty()) selected_ = 0;
  return true;
}

bool Vault::load() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    // A fresh board has no namespace yet. Create an empty vault; that is valid.
    if (!prefs.begin(kNamespace, false)) return false;
    if (prefs.putString("ids", "[]") == 0 || prefs.putUInt("next", 1) == 0) {
      prefs.end();
      return false;
    }
    prefs.end();
    entries_.clear();
    persistedIds_.clear();
    nextId_ = 1;
    return true;
  }

  nextId_ = prefs.getUInt("next", 1);
  if (nextId_ == 0) nextId_ = 1;

  const String idsRaw = prefs.getString("ids", "[]");
  JsonDocument idsDoc;
  const DeserializationError idsError = deserializeJson(idsDoc, idsRaw.c_str());
  if (idsError || !idsDoc.is<JsonArray>()) {
    prefs.end();
    loadError_ = "The vault on flash could not be read, so it was left unchanged.";
    return false;
  }

  const JsonArray ids = idsDoc.as<JsonArray>();
  if (ids.size() > static_cast<size_t>(VAULT_MAX_ENTRIES)) {
    prefs.end();
    loadError_ = "The vault on flash could not be read, so it was left unchanged.";
    return false;
  }

  std::vector<VaultEntry> loaded;
  std::vector<uint32_t> loadedIds;
  uint32_t maxId = 0;

  for (JsonVariant value : ids) {
    if (!value.is<uint32_t>()) {
      prefs.end();
      loadError_ = "The vault on flash could not be read, so it was left unchanged.";
      return false;
    }
    const uint32_t id = value.as<uint32_t>();
    if (id == 0) {
      prefs.end();
      loadError_ = "The vault on flash could not be read, so it was left unchanged.";
      return false;
    }
    for (size_t i = 0; i < loadedIds.size(); i++) {
      if (loadedIds[i] == id) {
        prefs.end();
        loadError_ = "The vault on flash could not be read, so it was left unchanged.";
        return false;
      }
    }

    const std::string key = entryKey(id);
    const String raw = prefs.getString(key.c_str(), "");
    if (raw.isEmpty()) {
      prefs.end();
      loadError_ = "The vault on flash could not be read, so it was left unchanged.";
      return false;
    }

    JsonDocument entryDoc;
    if (deserializeJson(entryDoc, raw.c_str())) {
      prefs.end();
      loadError_ = "The vault on flash could not be read, so it was left unchanged.";
      return false;
    }

    const char* name = entryDoc["name"];
    const char* password = entryDoc["password"];
    const char* fieldError = nullptr;
    if (!vaultNameOk(name, fieldError) || !vaultPasswordOk(password, fieldError)) {
      prefs.end();
      loadError_ = "The vault on flash could not be read, so it was left unchanged.";
      return false;
    }

    VaultEntry entry;
    entry.id = id;
    entry.name = name;
    entry.password = password;
    loaded.push_back(entry);
    loadedIds.push_back(id);
    if (id > maxId) maxId = id;
  }

  prefs.end();

  if (nextId_ <= maxId) nextId_ = maxId + 1;
  entries_.swap(loaded);
  persistedIds_.swap(loadedIds);
  return true;
}

bool Vault::save() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;

  for (size_t i = 0; i < entries_.size(); i++) {
    JsonDocument doc;
    doc["name"] = entries_[i].name;
    doc["password"] = entries_[i].password;
    std::string payload;
    serializeJson(doc, payload);
    const std::string key = entryKey(entries_[i].id);
    if (prefs.putString(key.c_str(), payload.c_str()) == 0) {
      prefs.end();
      return false;
    }
  }

  JsonDocument idsDoc;
  JsonArray ids = idsDoc.to<JsonArray>();
  for (size_t i = 0; i < entries_.size(); i++) ids.add(entries_[i].id);
  std::string idPayload;
  serializeJson(idsDoc, idPayload);
  if (prefs.putString("ids", idPayload.c_str()) == 0) {
    prefs.end();
    return false;
  }
  if (prefs.putUInt("next", nextId_) == 0 && nextId_ != 0) {
    // putUInt returns 0 both for a stored 0 and for failure. nextId_ is never 0.
    prefs.end();
    return false;
  }

  for (size_t i = 0; i < persistedIds_.size(); i++) {
    if (!containsId(entries_, persistedIds_[i])) {
      const std::string key = entryKey(persistedIds_[i]);
      prefs.remove(key.c_str());
    }
  }

  prefs.end();
  persistedIds_.clear();
  for (size_t i = 0; i < entries_.size(); i++) persistedIds_.push_back(entries_[i].id);
  return true;
}

const VaultEntry* Vault::at(int index) const {
  if (index < 0 || index >= size()) return nullptr;
  return &entries_[static_cast<size_t>(index)];
}

const VaultEntry* Vault::selected() const { return at(selected_); }

uint32_t Vault::selectedId() const {
  const VaultEntry* entry = selected();
  return entry == nullptr ? 0 : entry->id;
}

int Vault::selectableCount() const {
  const int count = size();
  if (count < VAULT_BUTTON_SLOTS) return count;
  return VAULT_BUTTON_SLOTS;
}

bool Vault::selectSlot(int slot) {
  if (slot < 0 || slot >= selectableCount()) return false;
  selected_ = slot;
  return true;
}

int Vault::indexOf(uint32_t id) const {
  for (int i = 0; i < size(); i++) {
    if (entries_[static_cast<size_t>(i)].id == id) return i;
  }
  return -1;
}

bool Vault::add(const char* name, const char* password, uint32_t& newId, const char*& error) {
  if (!mutable_) {
    error = loadError_ == nullptr ? "Could not write the vault to flash." : loadError_;
    return false;
  }
  if (!vaultNameOk(name, error) || !vaultPasswordOk(password, error)) return false;
  if (size() >= VAULT_MAX_ENTRIES) {
    error = "The vault already holds 32 entries.";
    return false;
  }
  if (nextId_ == 0) {
    error = "Could not write the vault to flash.";
    return false;
  }

  const std::vector<VaultEntry> backup = entries_;
  const std::vector<uint32_t> backupIds = persistedIds_;
  const uint32_t backupNext = nextId_;
  const int backupSelected = selected_;

  VaultEntry entry;
  entry.id = nextId_++;
  entry.name = name;
  entry.password = password;
  entries_.push_back(entry);
  if (selected_ < 0) selected_ = 0;

  if (!save()) {
    entries_ = backup;
    persistedIds_ = backupIds;
    nextId_ = backupNext;
    selected_ = backupSelected;
    error = "Could not write the vault to flash.";
    return false;
  }

  newId = entry.id;
  return true;
}

bool Vault::edit(uint32_t id, const char* name, const char* password, const char*& error) {
  if (!mutable_) {
    error = loadError_ == nullptr ? "Could not write the vault to flash." : loadError_;
    return false;
  }
  if (!vaultNameOk(name, error) || !vaultPasswordOk(password, error)) return false;

  const int index = indexOf(id);
  if (index < 0) {
    error = "No entry with that id.";
    return false;
  }

  const std::vector<VaultEntry> backup = entries_;
  const std::vector<uint32_t> backupIds = persistedIds_;

  entries_[static_cast<size_t>(index)].name = name;
  entries_[static_cast<size_t>(index)].password = password;

  if (!save()) {
    entries_ = backup;
    persistedIds_ = backupIds;
    error = "Could not write the vault to flash.";
    return false;
  }
  return true;
}

bool Vault::remove(uint32_t id, const char*& error) {
  if (!mutable_) {
    error = loadError_ == nullptr ? "Could not write the vault to flash." : loadError_;
    return false;
  }

  const int index = indexOf(id);
  if (index < 0) {
    error = "No entry with that id.";
    return false;
  }

  const std::vector<VaultEntry> backup = entries_;
  const std::vector<uint32_t> backupIds = persistedIds_;
  const int backupSelected = selected_;

  entries_.erase(entries_.begin() + index);
  if (entries_.empty()) {
    selected_ = -1;
  } else if (selected_ == index) {
    if (selected_ >= size()) selected_ = size() - 1;
  } else if (selected_ > index) {
    selected_--;
  }

  if (!save()) {
    entries_ = backup;
    persistedIds_ = backupIds;
    selected_ = backupSelected;
    error = "Could not write the vault to flash.";
    return false;
  }
  return true;
}
