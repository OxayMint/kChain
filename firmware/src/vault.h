#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

struct VaultEntry {
  uint32_t id;
  std::string name;
  std::string password;
};

// Plaintext vault in NVS (device flash). No PIN and no encryption.
// A missing vault is empty and valid.
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

  // False when the vault is empty. A single entry still reports true.
  bool selectPrevious();
  bool selectNext();

  bool add(const char* name, const char* password, uint32_t& newId, const char*& error);
  bool edit(uint32_t id, const char* name, const char* password, const char*& error);
  bool remove(uint32_t id, const char*& error);

  const std::vector<VaultEntry>& entries() const { return entries_; }

 private:
  std::vector<VaultEntry> entries_;
  std::vector<uint32_t> persistedIds_;
  uint32_t nextId_;
  int selected_;
  bool mutable_;
  const char* loadError_;

  bool load();
  bool save();
  int indexOf(uint32_t id) const;
};

bool vaultNameOk(const char* name, const char*& error);
bool vaultPasswordOk(const char* password, const char*& error);
