#pragma once

#include <stdint.h>

#include <vector>

#include "vault.h"

// On-device screens. Home opens the other four. Single, Web, and Crypto
// list stored entries of that type. Help is a placeholder.
enum class UiScreen : uint8_t { Home, Single, Web, Crypto, Help };

struct UiHomeItem {
  const char* label;
  UiScreen screen;
};

// Order is the wheel order on the home screen.
constexpr UiHomeItem kUiHome[] = {
    {"Single", UiScreen::Single},
    {"Web", UiScreen::Web},
    {"Crypto", UiScreen::Crypto},
    {"Help", UiScreen::Help},
};
constexpr int kUiHomeCount = 4;

// Layout shared by the screen state and the drawer. A gold bar is kUiBar
// pixels on the top edge. The wordmark is 35px at kUiHeaderY, a hairline
// sits at kUiRuleY, and the list starts at kUiListTop.
static constexpr int kUiMargin = 12;
static constexpr int kUiBar = 4;
static constexpr int kUiHeaderY = 12;
static constexpr int kUiRuleY = 52;
static constexpr int kUiListTop = 60;
static constexpr int kUiCardGap = 8;
static constexpr int kUiCardH = 52;
static constexpr int kUiWebH = 72;
static constexpr int kUiRadius = 14;
static constexpr int kUiPad = 16;
static constexpr int kUiIcon = 16;
static constexpr int kUiBottom = 12;

inline int uiCardHeight(UiScreen screen) {
  return screen == UiScreen::Web ? kUiWebH : kUiCardH;
}

// How many cards fit under the title. The panel is 320px tall.
inline int uiVisibleRows(UiScreen screen) {
  const int stride = uiCardHeight(screen) + kUiCardGap;
  const int room = 320 - kUiListTop - kUiBottom + kUiCardGap;
  const int count = room / stride;
  return count < 1 ? 1 : count;
}

class St7789Display;

// Wheel, tap, and hold for the glass. Pixels stay in paint().
class Ui {
 public:
  void sync(const Vault& vault);
  // One detent per step. Wheel up is positive. Wraps. False when the
  // highlight did not move.
  bool onWheel(const Vault& vault, int delta);
  // Opens the highlighted home row. False on every other screen.
  bool onTap();
  // Returns to home. False when home is already showing.
  bool onHold();
  bool canType(const Vault& vault) const;
  // Vault index of the highlighted stored entry, or -1 on home, help,
  // and an empty list.
  int highlightedSlot(const Vault& vault) const;

  UiScreen screen() const { return screen_; }
  // The next paint redraws the whole screen. Used when the panel wakes,
  // because a command while it was asleep would have been ignored.
  void invalidate() { draw_ = Draw::Full; }
  void paint(St7789Display& display, const Vault& vault);

 private:
  struct Place {
    int index = 0;
    int window = 0;
    uint32_t id = 0;
  };

  enum class Draw : uint8_t { None, Full, Cards };

  UiScreen screen_ = UiScreen::Home;
  Draw draw_ = Draw::Full;
  int home_ = 0;
  int cardA_ = -1;
  int cardB_ = -1;
  uint32_t stamp_ = 0;
  Place single_{};
  Place web_{};
  Place crypto_{};

  Place& placeFor(UiScreen screen);
  const Place& placeFor(UiScreen screen) const;
  int collect(const Vault& vault, EntryType type, std::vector<int>& slots) const;
  bool listType(UiScreen screen, EntryType& type) const;
  void retarget(const Vault& vault, Place& place, EntryType type);
  void placeWindow(Place& place, int count, int visible) const;
  uint32_t contentStamp(const Vault& vault) const;
  const VaultEntry* entryAt(const Vault& vault, int index) const;
  int listCount(const Vault& vault) const;
};
