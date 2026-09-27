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

const char* textOrEmpty(const char* text) { return text == nullptr ? "" : text; }

bool printableAscii(const char* text, size_t maxLen, const char* emptyError, const char* lengthError,
                    const char* asciiError, const char*& error) {
  if (text == nullptr || text[0] == '\0') {
    error = emptyError;
    return false;
  }
  const size_t length = strlen(text);
  if (length > maxLen) {
    error = lengthError;
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x20 || c > 0x7e) {
      error = asciiError;
      return false;
    }
  }
  return true;
}

bool vaultHostnameOk(const char* hostname, const char*& error) {
  return printableAscii(hostname, VAULT_HOSTNAME_MAX, "Hostname is required.",
                        "Hostname must be 253 characters or fewer.",
                        "Hostname must be printable ASCII.", error);
}

bool vaultUsernameOk(const char* username, const char*& error) {
  return printableAscii(username, VAULT_USERNAME_MAX, "Username is required.",
                        "Username must be 128 characters or fewer.",
                        "Username must be printable ASCII so it can be typed.", error);
}

bool vaultPhraseOk(const char* phrase, const char*& error) {
  if (phrase == nullptr || phrase[0] == '\0') {
    error = "Phrase is required.";
    return false;
  }
  const size_t length = strlen(phrase);
  if (length > VAULT_PHRASE_MAX) {
    error = "Phrase must be 256 characters or fewer.";
    return false;
  }
  if (phrase[0] == ' ' || phrase[length - 1] == ' ') {
    error = "Phrase cannot start or end with a space.";
    return false;
  }
  int words = 0;
  bool inWord = false;
  for (size_t i = 0; i < length; i++) {
    const unsigned char c = static_cast<unsigned char>(phrase[i]);
    if (c == ' ') {
      if (!inWord) {
        error = "Phrase words must be separated by a single space.";
        return false;
      }
      inWord = false;
      continue;
    }
    if (c < 0x21 || c > 0x7e) {
      error = "Phrase must be printable ASCII.";
      return false;
    }
    if (!inWord) {
      words++;
      inWord = true;
    }
  }
  if (words < 2) {
    error = "Phrase must be at least two words.";
    return false;
  }
  return true;
}

bool entryFieldsOk(const VaultEntry& entry, const char*& error) {
  switch (entry.type) {
    case EntryType::Website:
      return vaultHostnameOk(entry.name.c_str(), error) &&
             vaultUsernameOk(entry.username.c_str(), error) &&
             vaultPasswordOk(entry.password.c_str(), error);
    case EntryType::Crypto:
      return vaultNameOk(entry.name.c_str(), error) && vaultPhraseOk(entry.phrase.c_str(), error);
    case EntryType::Generic:
      return vaultNameOk(entry.name.c_str(), error) && vaultPasswordOk(entry.password.c_str(), error);
  }
  error = "Type must be generic, website, or crypto.";
  return false;
}

void keepUsedFields(VaultEntry& entry) {
  if (entry.type != EntryType::Website) entry.username.clear();
  if (entry.type == EntryType::Crypto) entry.password.clear();
  else entry.phrase.clear();
}

bool loadFailed(Preferences& prefs, const char*& loadError) {
  prefs.end();
  loadError = "The vault on flash could not be read, so it was left unchanged.";
  return false;
}

}  // namespace

const char* entryTypeName(EntryType type) {
  switch (type) {
    case EntryType::Website:
      return "website";
    case EntryType::Crypto:
      return "crypto";
    case EntryType::Generic:
      return "generic";
  }
  return "generic";
}

bool entryTypeFrom(const char* text, EntryType& type) {
  if (text == nullptr) return false;
  if (strcmp(text, "generic") == 0) {
    type = EntryType::Generic;
    return true;
  }
  if (strcmp(text, "website") == 0) {
    type = EntryType::Website;
    return true;
  }
  if (strcmp(text, "crypto") == 0) {
    type = EntryType::Crypto;
    return true;
  }
  return false;
}

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
  return printableAscii(password, VAULT_PASSWORD_MAX, "Password is required.",
                        "Password must be 128 characters or fewer.",
                        "Password must be printable ASCII so it can be typed.", error);
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
  if (idsError || !idsDoc.is<JsonArray>()) return loadFailed(prefs, loadError_);

  const JsonArray ids = idsDoc.as<JsonArray>();
  if (ids.size() > static_cast<size_t>(VAULT_MAX_ENTRIES)) return loadFailed(prefs, loadError_);

  std::vector<VaultEntry> loaded;
  std::vector<uint32_t> loadedIds;
  uint32_t maxId = 0;

  for (JsonVariant value : ids) {
    if (!value.is<uint32_t>()) return loadFailed(prefs, loadError_);
    const uint32_t id = value.as<uint32_t>();
    if (id == 0) return loadFailed(prefs, loadError_);
    for (size_t i = 0; i < loadedIds.size(); i++) {
      if (loadedIds[i] == id) return loadFailed(prefs, loadError_);
    }

    const std::string key = entryKey(id);
    const String raw = prefs.getString(key.c_str(), "");
    if (raw.isEmpty()) return loadFailed(prefs, loadError_);

    JsonDocument entryDoc;
    if (deserializeJson(entryDoc, raw.c_str())) return loadFailed(prefs, loadError_);

    VaultEntry entry;
    entry.id = id;
    // Records written before types existed are name-and-password pairs.
    if (entryDoc["type"].isUnbound()) {
      entry.type = EntryType::Generic;
    } else if (!entryDoc["type"].is<const char*>() ||
               !entryTypeFrom(entryDoc["type"].as<const char*>(), entry.type)) {
      return loadFailed(prefs, loadError_);
    }

    const char* name = entryDoc["name"].is<const char*>() ? entryDoc["name"].as<const char*>() : nullptr;
    const char* username =
        entryDoc["username"].is<const char*>() ? entryDoc["username"].as<const char*>() : "";
    const char* password =
        entryDoc["password"].is<const char*>() ? entryDoc["password"].as<const char*>() : "";
    const char* phrase = entryDoc["phrase"].is<const char*>() ? entryDoc["phrase"].as<const char*>() : "";
    entry.name = textOrEmpty(name);
    entry.username = username;
    entry.password = password;
    entry.phrase = phrase;
    const char* fieldError = nullptr;
    if (!entryFieldsOk(entry, fieldError)) return loadFailed(prefs, loadError_);
    keepUsedFields(entry);

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
    const VaultEntry& entry = entries_[i];
    doc["type"] = entryTypeName(entry.type);
    doc["name"] = entry.name;
    if (entry.type == EntryType::Website) doc["username"] = entry.username;
    if (entry.type != EntryType::Crypto) doc["password"] = entry.password;
    if (entry.type == EntryType::Crypto) doc["phrase"] = entry.phrase;
    std::string payload;
    serializeJson(doc, payload);
    const std::string key = entryKey(entry.id);
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

bool Vault::add(EntryType type, const char* name, const char* username, const char* password,
                const char* phrase, uint32_t& newId, const char*& error) {
  if (!mutable_) {
    error = loadError_ == nullptr ? "Could not write the vault to flash." : loadError_;
    return false;
  }
  if (size() >= VAULT_MAX_ENTRIES) {
    error = "The vault already holds 32 entries.";
    return false;
  }
  if (nextId_ == 0) {
    error = "Could not write the vault to flash.";
    return false;
  }

  VaultEntry entry;
  entry.type = type;
  entry.name = textOrEmpty(name);
  entry.username = textOrEmpty(username);
  entry.password = textOrEmpty(password);
  entry.phrase = textOrEmpty(phrase);
  if (!entryFieldsOk(entry, error)) return false;
  keepUsedFields(entry);

  const std::vector<VaultEntry> backup = entries_;
  const std::vector<uint32_t> backupIds = persistedIds_;
  const uint32_t backupNext = nextId_;
  const int backupSelected = selected_;

  entry.id = nextId_++;
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

bool Vault::edit(uint32_t id, EntryType type, const char* name, const char* username,
                 const char* password, const char* phrase, const char*& error) {
  if (!mutable_) {
    error = loadError_ == nullptr ? "Could not write the vault to flash." : loadError_;
    return false;
  }

  const int index = indexOf(id);
  if (index < 0) {
    error = "No entry with that id.";
    return false;
  }
  if (entries_[static_cast<size_t>(index)].type != type) {
    error = "Type cannot be changed.";
    return false;
  }

  VaultEntry next = entries_[static_cast<size_t>(index)];
  next.name = textOrEmpty(name);
  next.username = textOrEmpty(username);
  next.password = textOrEmpty(password);
  next.phrase = textOrEmpty(phrase);
  if (!entryFieldsOk(next, error)) return false;
  keepUsedFields(next);

  const std::vector<VaultEntry> backup = entries_;
  const std::vector<uint32_t> backupIds = persistedIds_;

  entries_[static_cast<size_t>(index)] = next;

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
