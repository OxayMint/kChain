#include "menu.h"

#include <Arduino.h>

#include <cstring>

namespace {

void copyName(char* dest, const std::string& name) {
  size_t length = name.size();
  if (length > VAULT_NAME_MAX) length = VAULT_NAME_MAX;
  memcpy(dest, name.data(), length);
  dest[length] = '\0';
}

char foldAscii(char c) {
  const auto u = static_cast<unsigned char>(c);
  if (u >= 'a' && u <= 'z') return static_cast<char>(u - 'a' + 'A');
  return static_cast<char>(u);
}

int wrap(int value, int count) {
  int slot = value % count;
  if (slot < 0) slot += count;
  return slot;
}

void popChar(char* text) {
  const size_t length = strlen(text);
  if (length == 0) return;
  text[length - 1] = '\0';
}

}  // namespace

bool Menu::nameMatches(const std::string& name) const {
  const size_t length = strlen(query_);
  if (name.size() < length) return false;
  for (size_t i = 0; i < length; i++) {
    if (foldAscii(name[i]) != query_[i]) return false;
  }
  return true;
}

int Menu::matchingSlots(const Vault& vault, int* slots, int cap) const {
  int count = 0;
  const int total = vault.selectableCount();
  for (int i = 0; i < total && count < cap; i++) {
    const VaultEntry* entry = vault.at(i);
    if (entry != nullptr && nameMatches(entry->name)) slots[count++] = i;
  }
  return count;
}

void Menu::applySymbolStep(int step) {
  const int next = wrap(symbol_ + step, kSymbolCount);
  const bool wasLetter = symbol_ < kDeleteSymbol;
  const bool nowLetter = next < kDeleteSymbol;
  if (wasLetter && nowLetter) {
    const size_t length = strlen(query_);
    if (length > 0) query_[length - 1] = static_cast<char>('A' + next);
  } else if (wasLetter && !nowLetter) {
    // The draft letter is replaced by delete, which also drops the letter
    // before it. Landing here from A is one wheel-down detent.
    popChar(query_);
    popChar(query_);
  } else if (!wasLetter && nowLetter) {
    const size_t length = strlen(query_);
    const char letter = static_cast<char>('A' + next);
    if (length >= SEARCH_QUERY_MAX) {
      if (length > 0) query_[length - 1] = letter;
    } else {
      query_[length] = letter;
      query_[length + 1] = '\0';
    }
  }
  symbol_ = next;
  // A fast spin would otherwise catch the symbol while it is hidden.
  blinkOn_ = true;
  lastBlinkMs_ = millis();
}

void Menu::stepSymbol(int delta) {
  while (delta > 0) {
    applySymbolStep(1);
    delta--;
  }
  while (delta < 0) {
    applySymbolStep(-1);
    delta++;
  }
}

void Menu::clearRows() {
  for (int i = 0; i < MENU_LIST_ROWS; i++) rows_[i] = MenuRow{};
}

void Menu::placeRow(int row, const Vault& vault, int slot, int selected) {
  if (row < 0 || row >= MENU_LIST_ROWS) return;
  const VaultEntry* entry = vault.at(slot);
  if (entry == nullptr) return;
  rows_[row].present = true;
  rows_[row].selected = slot == selected;
  rows_[row].type = entry->type;
  rows_[row].slot = slot;
  copyName(rows_[row].label, entry->name);
  if (rows_[row].selected) copyName(label_, entry->name);
}

void Menu::fillRows(const Vault& vault) {
  clearRows();
  label_[0] = '\0';
  int slots[VAULT_MAX_ENTRIES];
  const int matchCount = matchingSlots(vault, slots, VAULT_MAX_ENTRIES);
  if (matchCount == 0) {
    notFound_ = true;
    return;
  }
  notFound_ = false;
  const int selected = vault.selectedIndex();
  int pos = 0;
  for (int i = 0; i < matchCount; i++) {
    if (slots[i] == selected) {
      pos = i;
      break;
    }
  }
  // Top pin shows the selection and the match after it. Bottom pin shows
  // the match before it and the selection, so a backward step shifts the view up.
  if (pinTop_) {
    placeRow(0, vault, slots[pos], selected);
    if (pos + 1 < matchCount) placeRow(1, vault, slots[pos + 1], selected);
  } else {
    if (pos > 0) placeRow(0, vault, slots[pos - 1], selected);
    placeRow(1, vault, slots[pos], selected);
  }
}

bool Menu::onWheel(Vault& vault, int delta) {
  if (sending_ || delta == 0) return false;
  if (editing_) {
    stepSymbol(delta);
    refit(vault);
    return true;
  }

  // Forward pins the new match to the top line. Back pins it to the bottom.
  pinTop_ = delta > 0;
  scrollDir_ = delta > 0 ? 1 : -1;

  const int count = vault.selectableCount();
  if (count <= 0) return false;
  int matches[VAULT_MAX_ENTRIES];
  const int matchCount = matchingSlots(vault, matches, VAULT_MAX_ENTRIES);
  if (matchCount == 0) return false;

  const int index = vault.selectedIndex();
  int pos = -1;
  for (int i = 0; i < matchCount; i++) {
    if (matches[i] == index) {
      pos = i;
      break;
    }
  }
  // The selected entry is not a match. One step lands on the nearest match
  // in the direction of the wheel, and further detents continue from there.
  const int origin = pos >= 0 ? pos : (delta > 0 ? -1 : matchCount);
  const int slot = matches[wrap(origin + delta, matchCount)];
  if (slot == index) return true;
  return vault.selectSlot(slot);
}

bool Menu::beginSearchEdit(Vault& vault) {
  if (sending_ || editing_) return false;
  memcpy(beforePress_, query_, sizeof(query_));
  beforePressSlot_ = vault.selectedIndex();
  const size_t length = strlen(query_);
  if (length >= SEARCH_QUERY_MAX) {
    symbol_ = kDeleteSymbol;
  } else {
    query_[length] = 'A';
    query_[length + 1] = '\0';
    symbol_ = 0;
  }
  editing_ = true;
  blinkOn_ = true;
  lastBlinkMs_ = millis();
  return refit(vault);
}

void Menu::endSearchEdit() {
  editing_ = false;
  blinkOn_ = true;
}

bool Menu::cancelLastLetter(Vault& vault) {
  editing_ = false;
  blinkOn_ = true;
  symbol_ = 0;
  memcpy(query_, beforePress_, sizeof(query_));
  // The press may already have jumped to a match for the new letter.
  // Typing should use the entry that was highlighted before that press.
  if (beforePressSlot_ >= 0) vault.selectSlot(beforePressSlot_);
  return refit(vault);
}

bool Menu::canType() const {
  return active_ && !sending_ && !editing_ && !notFound_ && label_[0] != '\0';
}

bool Menu::refit(Vault& vault) {
  if (sending_) return false;
  const int count = vault.selectableCount();
  if (count <= 0) return false;
  const int current = vault.selectedIndex();
  if (current >= 0 && current < count) {
    const VaultEntry* entry = vault.at(current);
    if (entry != nullptr && nameMatches(entry->name)) return false;
  }
  for (int i = 0; i < count; i++) {
    const VaultEntry* entry = vault.at(i);
    if (entry == nullptr || !nameMatches(entry->name)) continue;
    if (i == current) return false;
    return vault.selectSlot(i);
  }
  return false;
}

void Menu::sync(const Vault& vault, bool active) {
  // Idle has to win over a send, or a finished type would leave the name
  // parked on the glass. An awake send keeps its snapshot.
  if (sending_ && active) return;
  sending_ = false;
  sendTicks_ = 0;
  if (!active) {
    active_ = false;
    editing_ = false;
    blinkOn_ = true;
    notFound_ = false;
    symbol_ = 0;
    label_[0] = '\0';
    query_[0] = '\0';
    beforePress_[0] = '\0';
    beforePressSlot_ = -1;
    pinTop_ = true;
    scrollDir_ = 1;
    clearRows();
    return;
  }
  const VaultEntry* entry = vault.selected();
  if (entry == nullptr) {
    active_ = false;
    label_[0] = '\0';
    notFound_ = false;
    clearRows();
    return;
  }
  active_ = true;
  fillRows(vault);
}

void Menu::beginSend() {
  sending_ = true;
  sendTicks_ = 0;
  lastSendMs_ = millis();
}

void Menu::tickSend(unsigned long now) {
  if (!sending_) return;
  if (sendTicks_ >= SEND_ANIM_TICKS) return;
  if (now - lastSendMs_ < SEND_STEP_MS) return;
  // One step per call. A slow redraw then shows every step instead of
  // jumping ahead by however many intervals the draw consumed.
  lastSendMs_ = now;
  sendTicks_++;
}

void Menu::tickBlink(unsigned long now) {
  if (!editing_) return;
  if (now - lastBlinkMs_ < SEARCH_BLINK_MS) return;
  lastBlinkMs_ = now;
  blinkOn_ = !blinkOn_;
}

void Menu::cancelSend() {
  sending_ = false;
  sendTicks_ = 0;
}

MenuFrame Menu::frame() const {
  MenuFrame frame;
  frame.active = active_;
  frame.sending = sending_;
  frame.sendTicks = sendTicks_;
  frame.scrollDir = scrollDir_;
  frame.queryEditing = editing_;
  frame.queryBlinkOn = blinkOn_;
  frame.queryDeleting = editing_ && symbol_ == kDeleteSymbol;
  frame.notFound = notFound_;
  memcpy(frame.query, query_, sizeof(frame.query));
  memcpy(frame.rows, rows_, sizeof(frame.rows));
  return frame;
}
