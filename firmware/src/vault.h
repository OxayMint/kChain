#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

enum class EntryType : uint8_t { Generic, Website, Crypto };

struct VaultEntry {
  uint32_t id = 0;
  EntryType type = EntryType::Generic;
  // Generic name, website hostname, or crypto label.
  std::string name;
  // Website only.
  std::string username;
  // Generic and website.
  std::string password;
  // Crypto words.
  std::string phrase;
};

const char* entryTypeName(EntryType type);
// False when text is not generic, website, or crypto.
bool entryTypeFrom(const char* text, EntryType& type);

// Plaintext vault in NVS (device flash). No PIN and no encryption.
// A missing vault is empty and valid. A stored entry with no type is generic.
class Vault {
 public:
  // Loads flash. Returns false when flash cannot be read; entries stay empty
  // and mutable() stays false so a bad file is not overwritten.
  bool begin();

  bool mutableOk() const { return mutable_; }
  const char* loadError() const { return loadError_; }

  int size() const { return static_cast<int>(entries_.size()); }
  const VaultEntry* at(int index) const;
  const VaultEntry* selected() const;
  int selectedIndex() const { return selected_; }
  uint32_t selectedId() const;

  // How many entries the wheel can reach. Zero when the vault is empty.
  int selectableCount() const;
  // Slot is 0-based within selectableCount(). False when out of range.
  bool selectSlot(int slot);
  // id 0 clears the login a tap should wake onto. The selection stays put
  // until that tap. False when id is not in the vault.
  bool setFocus(uint32_t id, const char*& error);
  // Index of the focused entry, or 0 when nothing is focused.
  int wakeSlot() const;

  // Unused strings for the type are ignored. newId is set on success.
  bool add(EntryType type, const char* name, const char* username, const char* password,
           const char* phrase, uint32_t& newId, const char*& error);
  // Rejects a type that differs from the stored entry.
  bool edit(uint32_t id, EntryType type, const char* name, const char* username,
            const char* password, const char* phrase, const char*& error);
  bool remove(uint32_t id, const char*& error);

  const std::vector<VaultEntry>& entries() const { return entries_; }

 private:
  std::vector<VaultEntry> entries_;
  std::vector<uint32_t> persistedIds_;
  uint32_t nextId_;
  uint32_t focusId_;
  int selected_;
  bool mutable_;
  const char* loadError_;

  bool load();
  bool save();
  int indexOf(uint32_t id) const;
};

bool vaultNameOk(const char* name, const char*& error);
bool vaultPasswordOk(const char* password, const char*& error);
