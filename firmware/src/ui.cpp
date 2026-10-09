#include "ui.h"

#include "config.h"

namespace {

int wrap(int value, int count) {
  int slot = value % count;
  if (slot < 0) slot += count;
  return slot;
}

void mix(uint32_t& hash, uint32_t value) {
  hash ^= value;
  hash *= 16777619u;
}

void mixText(uint32_t& hash, const std::string& text) {
  for (size_t i = 0; i < text.size(); i++) {
    mix(hash, static_cast<unsigned char>(text[i]));
  }
  mix(hash, 0);
}

}  // namespace

Ui::Place& Ui::placeFor(UiScreen screen) {
  if (screen == UiScreen::Web) return web_;
  if (screen == UiScreen::Crypto) return crypto_;
  return single_;
}

const Ui::Place& Ui::placeFor(UiScreen screen) const {
  if (screen == UiScreen::Web) return web_;
  if (screen == UiScreen::Crypto) return crypto_;
  return single_;
}

bool Ui::listType(UiScreen screen, EntryType& type) const {
  if (screen == UiScreen::Single) {
    type = EntryType::Generic;
    return true;
  }
  if (screen == UiScreen::Web) {
    type = EntryType::Website;
    return true;
  }
  if (screen == UiScreen::Crypto) {
    type = EntryType::Crypto;
    return true;
  }
  return false;
}

int Ui::collect(const Vault& vault, EntryType type, int* slots) const {
  int count = 0;
  const int total = vault.size();
  for (int i = 0; i < total && count < VAULT_MAX_ENTRIES; i++) {
    const VaultEntry* entry = vault.at(i);
    if (entry != nullptr && entry->type == type) slots[count++] = i;
  }
  return count;
}

int Ui::listCount(const Vault& vault) const {
  EntryType type;
  if (!listType(screen_, type)) return 0;
  int slots[VAULT_MAX_ENTRIES];
  return collect(vault, type, slots);
}

void Ui::placeWindow(Place& place, int count, int visible) const {
  if (count <= 0 || visible <= 0) {
    place.window = 0;
    return;
  }
  if (count <= visible) {
    place.window = 0;
    return;
  }
  if (place.index < place.window) place.window = place.index;
  if (place.index >= place.window + visible) place.window = place.index - visible + 1;
  const int maxWindow = count - visible;
  if (place.window > maxWindow) place.window = maxWindow;
  if (place.window < 0) place.window = 0;
}

void Ui::retarget(const Vault& vault, Place& place, EntryType type) {
  int slots[VAULT_MAX_ENTRIES];
  const int count = collect(vault, type, slots);
  if (count == 0) {
    place.index = 0;
    place.window = 0;
    place.id = 0;
    return;
  }
  int found = -1;
  if (place.id != 0) {
    for (int i = 0; i < count; i++) {
      const VaultEntry* entry = vault.at(slots[i]);
      if (entry != nullptr && entry->id == place.id) {
        found = i;
        break;
      }
    }
  }
  if (found >= 0) place.index = found;
  else if (place.index < 0 || place.index >= count) place.index = count - 1;
  const VaultEntry* entry = vault.at(slots[place.index]);
  place.id = entry == nullptr ? 0 : entry->id;
  UiScreen screen = UiScreen::Single;
  if (type == EntryType::Website) screen = UiScreen::Web;
  if (type == EntryType::Crypto) screen = UiScreen::Crypto;
  placeWindow(place, count, uiVisibleRows(screen));
}

uint32_t Ui::contentStamp(const Vault& vault) const {
  // Names and accounts are on the glass. A password edit is not.
  uint32_t hash = 2166136261u;
  const int total = vault.size();
  for (int i = 0; i < total; i++) {
    const VaultEntry* entry = vault.at(i);
    if (entry == nullptr) continue;
    mix(hash, entry->id);
    mix(hash, static_cast<uint32_t>(entry->type));
    mixText(hash, entry->name);
    mixText(hash, entry->username);
  }
  mix(hash, static_cast<uint32_t>(total));
  return hash;
}

const VaultEntry* Ui::entryAt(const Vault& vault, int index) const {
  EntryType type;
  if (!listType(screen_, type)) return nullptr;
  int slots[VAULT_MAX_ENTRIES];
  const int count = collect(vault, type, slots);
  if (index < 0 || index >= count) return nullptr;
  return vault.at(slots[index]);
}

void Ui::sync(const Vault& vault) {
  retarget(vault, single_, EntryType::Generic);
  retarget(vault, web_, EntryType::Website);
  retarget(vault, crypto_, EntryType::Crypto);
  const uint32_t next = contentStamp(vault);
  if (next != stamp_) {
    stamp_ = next;
    draw_ = Draw::Full;
  }
}

bool Ui::onWheel(const Vault& vault, int delta) {
  if (delta == 0 || screen_ == UiScreen::Help) return false;
  if (screen_ == UiScreen::Home) {
    const int next = wrap(home_ + delta, kUiHomeCount);
    if (next == home_) return false;
    const int previous = home_;
    home_ = next;
    if (draw_ != Draw::Full) {
      draw_ = Draw::Cards;
      cardA_ = previous;
      cardB_ = next;
    }
    return true;
  }

  EntryType type;
  if (!listType(screen_, type)) return false;
  int slots[VAULT_MAX_ENTRIES];
  const int count = collect(vault, type, slots);
  if (count <= 0) return false;
  Place& place = placeFor(screen_);
  const int next = wrap(place.index + delta, count);
  if (next == place.index) return false;
  const int previous = place.index;
  const int previousWindow = place.window;
  place.index = next;
  const VaultEntry* entry = vault.at(slots[next]);
  place.id = entry == nullptr ? 0 : entry->id;
  placeWindow(place, count, uiVisibleRows(screen_));
  if (place.window != previousWindow) {
    draw_ = Draw::Full;
  } else if (draw_ != Draw::Full) {
    draw_ = Draw::Cards;
    cardA_ = previous;
    cardB_ = next;
  }
  return true;
}

bool Ui::onTap() {
  if (screen_ != UiScreen::Home) return false;
  if (home_ < 0 || home_ >= kUiHomeCount) home_ = 0;
  screen_ = kUiHome[home_].screen;
  draw_ = Draw::Full;
  return true;
}

bool Ui::onHold() {
  if (screen_ == UiScreen::Home) return false;
  screen_ = UiScreen::Home;
  draw_ = Draw::Full;
  return true;
}

int Ui::highlightedSlot(const Vault& vault) const {
  EntryType type;
  if (!listType(screen_, type)) return -1;
  int slots[VAULT_MAX_ENTRIES];
  const int count = collect(vault, type, slots);
  if (count <= 0) return -1;
  int index = placeFor(screen_).index;
  if (index < 0 || index >= count) index = 0;
  return slots[index];
}

bool Ui::canType(const Vault& vault) const { return highlightedSlot(vault) >= 0; }
