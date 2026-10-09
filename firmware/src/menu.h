#pragma once

#include <stdint.h>

#include "config.h"
#include "vault.h"

// Two matches fit under the query. The next match sits on the top line.
// The previous match sits on the bottom line.
static constexpr int MENU_LIST_ROWS = 2;

// One visible match. The name is a copy so a later edit of the vault cannot
// free the text during a draw.
struct MenuRow {
  bool present = false;
  bool selected = false;
  EntryType type = EntryType::Generic;
  // Vault index, or -1 when the row is empty. The drawer slides a name from
  // its old line to its new one by this id.
  int slot = -1;
  char label[VAULT_NAME_MAX + 1] = {};
};

// What the drawer paints. sendTicks advances the exit: the left edge opens,
// the selected name slides through, and the edge closes. Pixels stay in the drawer.
struct MenuFrame {
  bool active = false;
  bool sending = false;
  int sendTicks = 0;
  // +1 walks toward the next match, -1 toward the previous one.
  int scrollDir = 1;
  char query[SEARCH_QUERY_MAX + 1] = {};
  // The last query letter blinks while the button is held.
  bool queryEditing = false;
  bool queryBlinkOn = true;
  // The wheel is on the delete mark, which is not a letter in the query.
  bool queryDeleting = false;
  // The query matches nothing. A double tap must not type.
  bool notFound = false;
  MenuRow rows[MENU_LIST_ROWS] = {};
};

// Wheel navigation, the search query, and the names the glass is showing.
// No display hardware.
class Menu {
 public:
  // One detent per step. While a search letter is blinking, the wheel changes
  // that symbol and wraps from Z through delete back to A. Otherwise it walks
  // entries whose names start with the query, and wraps. False when the wheel
  // did not move, or when nothing matches and no letter is being edited.
  bool onWheel(Vault& vault, int delta);

  // A press held past SEARCH_EDIT_ARM_MS appends A, which blinks until
  // release. A shorter press does not change the query. Nothing is appended
  // while a type is leaving the glass or while a letter is already blinking.
  // When the query already fills the panel, the press opens on the delete
  // mark instead. True when the selection moved to a different match.
  bool beginSearchEdit(Vault& vault);
  // The blinking letter stays, solid, and the wheel returns to the list.
  void endSearchEdit();
  // Puts the query back to what it was before the latest press. A double tap
  // uses this so the letter from that gesture is not kept.
  bool cancelLastLetter(Vault& vault);
  bool editing() const { return editing_; }

  // False while the query matches nothing, or while a letter is still blinking.
  bool canType() const;

  // Keeps the selection when its name still matches. Otherwise selects the
  // first match in vault order. False when the slot did not change, including
  // when nothing matches.
  bool refit(Vault& vault);

  // Awake copies the visible matches when they fit the query. Idle clears
  // the names and the query. A send in progress is left alone while the board
  // stays awake, so the names are not restored mid-animation. Idle ends the send.
  void sync(const Vault& vault, bool active);

  // The selected name starts the exit off the left edge.
  void beginSend();
  // Advances the exit once SEND_STEP_MS has passed.
  void tickSend(unsigned long now);
  // Hides and shows the blinking letter.
  void tickBlink(unsigned long now);
  // Stops the exit so the next sync can show the full name again.
  void cancelSend();

  MenuFrame frame() const;

 private:
  static constexpr int kSymbolCount = 27;
  static constexpr int kDeleteSymbol = 26;

  bool active_ = false;
  bool sending_ = false;
  bool editing_ = false;
  bool blinkOn_ = true;
  bool notFound_ = false;
  int symbol_ = 0;
  int beforePressSlot_ = -1;
  int sendTicks_ = 0;
  // The next match is pinned to the top line. The previous match is pinned
  // to the bottom line.
  bool pinTop_ = true;
  int scrollDir_ = 1;
  unsigned long lastSendMs_ = 0;
  unsigned long lastBlinkMs_ = 0;
  char query_[SEARCH_QUERY_MAX + 1] = {};
  char beforePress_[SEARCH_QUERY_MAX + 1] = {};
  char label_[VAULT_NAME_MAX + 1] = {};
  MenuRow rows_[MENU_LIST_ROWS] = {};

  bool nameMatches(const std::string& name) const;
  int matchingSlots(const Vault& vault, int* slots, int cap) const;
  void applySymbolStep(int step);
  void stepSymbol(int delta);
  void fillRows(const Vault& vault);
  void clearRows();
  void placeRow(int row, const Vault& vault, int slot, int selected);
};
