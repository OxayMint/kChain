#include "vault.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include <cstdio>
#include <cstring>

#include <esp_partition.h>

#include "config.h"

namespace {

constexpr char kNamespace[] = "kchain";
constexpr char kVaultPath[] = "/vault.json";
constexpr char kVaultBak[] = "/vault.json.bak";
constexpr char kVaultTmp[] = "/vault.json.tmp";
constexpr size_t kLineMax = 1024;
constexpr size_t kHeapMargin = 4096;

const char* kWriteError = "Could not write the vault to flash.";

std::string entryKey(uint32_t id) { return std::string("e") + std::to_string(id); }

const char* fullError() {
  static char text[64];
  snprintf(text, sizeof(text), "The vault already holds %d entries.", VAULT_MAX_ENTRIES);
  return text;
}

bool containsId(const std::deque<VaultEntry>& entries, uint32_t id) {
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

bool fieldsFromJson(const JsonDocument& doc, VaultEntry& entry) {
  if (doc["type"].isUnbound()) {
    entry.type = EntryType::Generic;
  } else if (!doc["type"].is<const char*>() ||
             !entryTypeFrom(doc["type"].as<const char*>(), entry.type)) {
    return false;
  }

  const char* name = doc["name"].is<const char*>() ? doc["name"].as<const char*>() : nullptr;
  const char* username = doc["username"].is<const char*>() ? doc["username"].as<const char*>() : "";
  const char* password = doc["password"].is<const char*>() ? doc["password"].as<const char*>() : "";
  const char* phrase = doc["phrase"].is<const char*>() ? doc["phrase"].as<const char*>() : "";
  entry.name = textOrEmpty(name);
  entry.username = username;
  entry.password = password;
  entry.phrase = phrase;
  const char* fieldError = nullptr;
  if (!entryFieldsOk(entry, fieldError)) return false;
  keepUsedFields(entry);
  return true;
}

bool heapRoom(size_t bytes) { return ESP.getMaxAllocHeap() > bytes + kHeapMargin; }

// A blank partition is unformatted. Anything else that will not mount is
// left alone so a damaged file is not replaced with an empty vault.
bool partitionBlank(const esp_partition_t* part) {
  uint8_t buf[128];
  for (size_t offset = 0; offset < 4096; offset += sizeof(buf)) {
    if (esp_partition_read(part, offset, buf, sizeof(buf)) != ESP_OK) return false;
    for (size_t i = 0; i < sizeof(buf); i++) {
      if (buf[i] != 0xff) return false;
    }
  }
  return true;
}

bool mountFs() {
  const esp_partition_t* part =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "vault");
  if (part == nullptr) return false;
  // Format only a blank partition. begin(true) formats when the mount fails,
  // which would also wipe a damaged filesystem.
  if (!partitionBlank(part)) return LittleFS.begin(false, "/littlefs", 10, "vault");
  return LittleFS.begin(true, "/littlefs", 10, "vault");
}

bool readLine(File& file, std::string& line) {
  line.clear();
  if (!file.available()) return false;
  while (file.available()) {
    const int raw = file.read();
    if (raw < 0) break;
    if (raw == '\r') continue;
    if (raw == '\n') return true;
    if (line.size() >= kLineMax) return false;
    line.push_back(static_cast<char>(raw));
  }
  return !line.empty();
}

bool writeEntryLine(File& file, const VaultEntry& entry) {
  JsonDocument doc;
  doc["id"] = entry.id;
  doc["type"] = entryTypeName(entry.type);
  doc["name"] = entry.name;
  if (entry.type == EntryType::Website) doc["username"] = entry.username;
  if (entry.type != EntryType::Crypto) doc["password"] = entry.password;
  if (entry.type == EntryType::Crypto) doc["phrase"] = entry.phrase;
  if (serializeJson(doc, file) == 0) return false;
  return file.write(static_cast<uint8_t>('\n')) == 1;
}

enum class Legacy { Absent, Ready, Bad };

Legacy readLegacy(std::deque<VaultEntry>& loaded, uint32_t& next) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    next = 1;
    return Legacy::Absent;
  }

  next = prefs.getUInt("next", 1);
  if (next == 0) next = 1;

  const String idsRaw = prefs.getString("ids", "[]");
  JsonDocument idsDoc;
  const DeserializationError idsError = deserializeJson(idsDoc, idsRaw.c_str());
  if (idsError || !idsDoc.is<JsonArray>()) {
    prefs.end();
    return Legacy::Bad;
  }

  const JsonArray ids = idsDoc.as<JsonArray>();
  if (ids.size() > static_cast<size_t>(VAULT_MAX_ENTRIES)) {
    prefs.end();
    return Legacy::Bad;
  }

  uint32_t maxId = 0;
  for (JsonVariant value : ids) {
    if (!value.is<uint32_t>()) {
      prefs.end();
      return Legacy::Bad;
    }
    const uint32_t id = value.as<uint32_t>();
    if (id == 0 || containsId(loaded, id)) {
      prefs.end();
      return Legacy::Bad;
    }

    const std::string key = entryKey(id);
    const String raw = prefs.getString(key.c_str(), "");
    if (raw.isEmpty()) {
      prefs.end();
      return Legacy::Bad;
    }

    JsonDocument entryDoc;
    if (deserializeJson(entryDoc, raw.c_str())) {
      prefs.end();
      return Legacy::Bad;
    }

    VaultEntry entry;
    entry.id = id;
    if (!fieldsFromJson(entryDoc, entry)) {
      prefs.end();
      return Legacy::Bad;
    }
    loaded.push_back(entry);
    if (id > maxId) maxId = id;
  }

  prefs.end();
  if (next <= maxId) next = maxId + 1;
  return Legacy::Ready;
}

void clearLegacy() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  const bool hasIds = prefs.isKey("ids");
  prefs.end();
  if (!hasIds) return;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.clear();
  prefs.end();
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
  nextId_ = 1;
  focusId_ = 0;
  selected_ = -1;
  mutable_ = false;
  loadError_ = "Could not read the vault from flash.";

  if (!mountFs() || !load()) {
    entries_.clear();
    return false;
  }

  mutable_ = true;
  loadError_ = nullptr;
  if (!entries_.empty()) selected_ = 0;
  return true;
}

bool Vault::load() {
  if (LittleFS.exists(kVaultPath)) {
    if (!loadFile(kVaultPath)) return false;
    LittleFS.remove(kVaultTmp);
    LittleFS.remove(kVaultBak);
    clearLegacy();
    return true;
  }

  // A save that stopped after moving the live file aside still has that copy.
  if (LittleFS.exists(kVaultBak)) {
    if (!loadFile(kVaultBak)) return false;
    LittleFS.remove(kVaultTmp);
    if (!save()) return false;
    clearLegacy();
    return true;
  }

  std::deque<VaultEntry> legacy;
  uint32_t next = 1;
  const Legacy found = readLegacy(legacy, next);
  if (found == Legacy::Bad) return false;

  entries_.swap(legacy);
  nextId_ = next;
  if (!save()) return false;
  clearLegacy();
  return true;
}

bool Vault::loadFile(const char* path) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;

  std::string line;
  if (!readLine(file, line) || line.empty()) {
    file.close();
    return false;
  }

  JsonDocument meta;
  if (deserializeJson(meta, line) || !meta["next"].is<uint32_t>()) {
    file.close();
    return false;
  }
  uint32_t next = meta["next"].as<uint32_t>();
  if (next == 0) next = 1;

  std::deque<VaultEntry> loaded;
  uint32_t maxId = 0;
  while (true) {
    const bool got = readLine(file, line);
    if (!got) {
      if (!line.empty() || file.available()) {
        file.close();
        return false;
      }
      break;
    }
    if (line.empty()) continue;
    if (loaded.size() >= static_cast<size_t>(VAULT_MAX_ENTRIES)) {
      file.close();
      return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, line) || !doc["id"].is<uint32_t>()) {
      file.close();
      return false;
    }
    VaultEntry entry;
    entry.id = doc["id"].as<uint32_t>();
    if (entry.id == 0 || containsId(loaded, entry.id) || !fieldsFromJson(doc, entry)) {
      file.close();
      return false;
    }
    loaded.push_back(std::move(entry));
    if (loaded.back().id > maxId) maxId = loaded.back().id;
  }
  file.close();

  if (next <= maxId) next = maxId + 1;
  entries_.swap(loaded);
  nextId_ = next;
  return true;
}

bool Vault::save() {
  LittleFS.remove(kVaultTmp);
  File file = LittleFS.open(kVaultTmp, "w");
  if (!file) return false;

  JsonDocument meta;
  meta["next"] = nextId_;
  bool wrote = serializeJson(meta, file) > 0 && file.write(static_cast<uint8_t>('\n')) == 1;
  for (size_t i = 0; wrote && i < entries_.size(); i++) wrote = writeEntryLine(file, entries_[i]);
  file.close();
  if (!wrote) {
    LittleFS.remove(kVaultTmp);
    return false;
  }

  if (LittleFS.exists(kVaultPath)) {
    LittleFS.remove(kVaultBak);
    if (!LittleFS.rename(kVaultPath, kVaultBak)) {
      LittleFS.remove(kVaultTmp);
      return false;
    }
  }
  if (!LittleFS.rename(kVaultTmp, kVaultPath)) {
    LittleFS.rename(kVaultBak, kVaultPath);
    LittleFS.remove(kVaultTmp);
    return false;
  }
  LittleFS.remove(kVaultBak);
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

int Vault::selectableCount() const { return size(); }

bool Vault::selectSlot(int slot) {
  if (slot < 0 || slot >= selectableCount()) return false;
  selected_ = slot;
  return true;
}

bool Vault::setFocus(uint32_t id, const char*& error) {
  if (id == 0) {
    focusId_ = 0;
    return true;
  }
  if (indexOf(id) < 0) {
    error = "No entry with that id.";
    return false;
  }
  focusId_ = id;
  return true;
}

int Vault::wakeSlot() const {
  if (focusId_ != 0) {
    const int index = indexOf(focusId_);
    if (index >= 0) return index;
  }
  return 0;
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
    error = loadError_ == nullptr ? kWriteError : loadError_;
    return false;
  }
  if (size() >= VAULT_MAX_ENTRIES) {
    error = fullError();
    return false;
  }
  if (nextId_ == 0) {
    error = kWriteError;
    return false;
  }

  const size_t incoming = strlen(textOrEmpty(name)) + strlen(textOrEmpty(username)) +
                          strlen(textOrEmpty(password)) + strlen(textOrEmpty(phrase));
  if (!heapRoom(incoming)) {
    error = kWriteError;
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

  const int backupSelected = selected_;
  const uint32_t id = nextId_;
  entry.id = id;
  nextId_++;
  entries_.push_back(std::move(entry));
  if (selected_ < 0) selected_ = 0;

  if (!save()) {
    entries_.pop_back();
    nextId_ = id;
    selected_ = backupSelected;
    error = kWriteError;
    return false;
  }

  newId = id;
  return true;
}

bool Vault::edit(uint32_t id, EntryType type, const char* name, const char* username,
                 const char* password, const char* phrase, const char*& error) {
  if (!mutable_) {
    error = loadError_ == nullptr ? kWriteError : loadError_;
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

  const size_t incoming = strlen(textOrEmpty(name)) + strlen(textOrEmpty(username)) +
                          strlen(textOrEmpty(password)) + strlen(textOrEmpty(phrase));
  if (!heapRoom(incoming)) {
    error = kWriteError;
    return false;
  }

  VaultEntry next;
  next.id = id;
  next.type = type;
  next.name = textOrEmpty(name);
  next.username = textOrEmpty(username);
  next.password = textOrEmpty(password);
  next.phrase = textOrEmpty(phrase);
  if (!entryFieldsOk(next, error)) return false;
  keepUsedFields(next);

  VaultEntry previous = std::move(entries_[static_cast<size_t>(index)]);
  entries_[static_cast<size_t>(index)] = std::move(next);

  if (!save()) {
    entries_[static_cast<size_t>(index)] = std::move(previous);
    error = kWriteError;
    return false;
  }
  return true;
}

bool Vault::remove(uint32_t id, const char*& error) {
  if (!mutable_) {
    error = loadError_ == nullptr ? kWriteError : loadError_;
    return false;
  }

  const int index = indexOf(id);
  if (index < 0) {
    error = "No entry with that id.";
    return false;
  }

  VaultEntry removed = std::move(entries_[static_cast<size_t>(index)]);
  const int backupSelected = selected_;
  const uint32_t backupFocus = focusId_;

  entries_.erase(entries_.begin() + index);
  if (focusId_ == id) focusId_ = 0;
  if (entries_.empty()) {
    selected_ = -1;
  } else if (selected_ == index) {
    if (selected_ >= size()) selected_ = size() - 1;
  } else if (selected_ > index) {
    selected_--;
  }

  if (!save()) {
    entries_.insert(entries_.begin() + index, std::move(removed));
    selected_ = backupSelected;
    focusId_ = backupFocus;
    error = kWriteError;
    return false;
  }
  return true;
}
