#include "menu_draw.h"

#include <Arduino.h>

#include <cstring>

#include "config.h"
#include "utf8_text.h"

namespace {

void copyText(char* dest, size_t destSize, const char* text) {
  size_t length = 0;
  while (text[length] != '\0' && length + 1 < destSize) {
    dest[length] = text[length];
    length++;
  }
  dest[length] = '\0';
}

// Query band stays 8px. Two names share the rest, with a blank line between
// them. The selected name is the taller one.
constexpr int kQueryY = 0;
constexpr int kQueryInset = 2;
constexpr int kListTop = 8;
constexpr int kLargeH = 12;
constexpr int kSmallH = 9;
constexpr int kGap = 3;
constexpr int kIconGap = 2;
constexpr int kDot = 2;
constexpr int kDotGap = 2;
constexpr int kLeftReserve = 10;
constexpr int kIconCols = 5;
constexpr int kIconRows = 7;

// The exit spends its ticks opening the left edge, sliding the row through,
// then closing the edge. The menu only counts ticks.
constexpr int kOpenTicks = 6;
constexpr int kSlidePx = 6;
constexpr int kSlideTicks = (128 + kSlidePx - 1) / kSlidePx;
constexpr int kCloseTicks = 6;
static_assert(kOpenTicks + kSlideTicks + kCloseTicks <= SEND_ANIM_TICKS,
              "the exit animation has to finish within the tick budget");
static_assert(kListTop + kLargeH + kGap + kSmallH == 32, "the two lines fill the panel under the query");

// 5x7, bit 0 is the top row. A key, a globe, and a diamond.
constexpr uint8_t kPassword[] = {0x02, 0x05, 0x7D, 0x12, 0x00};
constexpr uint8_t kWebsite[] = {0x3E, 0x49, 0x6B, 0x49, 0x3E};
constexpr uint8_t kCrypto[] = {0x1C, 0x22, 0x49, 0x22, 0x1C};
// Left arrow, drawn in the query while delete is the blinking symbol.
constexpr uint8_t kDelete[] = {0x08, 0x1C, 0x2A, 0x08, 0x08};

const uint8_t* iconFor(EntryType type) {
  if (type == EntryType::Website) return kWebsite;
  if (type == EntryType::Crypto) return kCrypto;
  return kPassword;
}

// The last letter is omitted while it blinks off. Delete is not a query
// character, so the stored letters stay whole and the icon blinks instead.
void visibleQuery(const MenuFrame& frame, char* out, size_t outSize) {
  copyText(out, outSize, frame.query);
  if (!frame.queryEditing || frame.queryDeleting || frame.queryBlinkOn) return;
  const size_t length = strlen(out);
  if (length > 0) out[length - 1] = '\0';
}

// Short names stay whole. A name that does not fit keeps a leading run of
// whole characters and ends with "...".
void fittedLabel(const Display& display, const char* label, int maxWidth, int height, char* out, size_t outSize) {
  if (maxWidth <= 0) {
    out[0] = '\0';
    return;
  }
  if (display.textWidthBox(label, height) <= maxWidth) {
    copyText(out, outSize, label);
    return;
  }

  char trial[VAULT_NAME_MAX + 1];
  size_t kept = 0;
  const char* cursor = label;
  while (cursor[0] != '\0') {
    uint32_t codepoint = 0;
    const size_t length = nextCodepoint(cursor, codepoint);
    if (kept + length >= sizeof(trial)) break;
    memcpy(trial + kept, cursor, length);
    trial[kept + length] = '\0';

    char withDots[VAULT_NAME_MAX + 4];
    memcpy(withDots, trial, kept + length);
    memcpy(withDots + kept + length, "...", 3);
    withDots[kept + length + 3] = '\0';
    if (display.textWidthBox(withDots, height) > maxWidth) break;

    kept += length;
    cursor += length;
  }

  if (kept + 4 > outSize) kept = outSize > 4 ? outSize - 4 : 0;
  memcpy(out, trial, kept);
  memcpy(out + kept, "...", 3);
  out[kept + 3] = '\0';
}

bool sameFrame(const MenuFrame& a, const MenuFrame& b) {
  return a.active == b.active && a.sending == b.sending && a.sendTicks == b.sendTicks &&
         a.queryEditing == b.queryEditing && a.queryBlinkOn == b.queryBlinkOn &&
         a.queryDeleting == b.queryDeleting && a.notFound == b.notFound &&
         strcmp(a.query, b.query) == 0 && memcmp(a.rows, b.rows, sizeof(a.rows)) == 0;
}

bool rowsDiffer(const MenuFrame& a, const MenuFrame& b) {
  return a.active != b.active || a.notFound != b.notFound || memcmp(a.rows, b.rows, sizeof(a.rows)) != 0;
}

int approach(int value, int target) {
  if (value < target) return value + 1;
  if (value > target) return value - 1;
  return value;
}

void rowTarget(int index, bool selectedAtTop, int& y, int& textH) {
  if (selectedAtTop) {
    y = index == 0 ? kListTop : kListTop + kLargeH + kGap;
    textH = index == 0 ? kLargeH : kSmallH;
  } else {
    y = index == 0 ? kListTop : kListTop + kSmallH + kGap;
    textH = index == 0 ? kSmallH : kLargeH;
  }
}

int exitShift(const MenuFrame& frame) {
  if (!frame.sending) return 0;
  const int afterOpen = frame.sendTicks - kOpenTicks;
  if (afterOpen <= 0) return 0;
  const int shift = afterOpen * kSlidePx;
  const int limit = 128 + kIconCols * 4;
  return shift > limit ? limit : shift;
}

int exitGap(const MenuFrame& frame, int textH) {
  if (!frame.sending || textH <= 0) return 0;
  if (frame.sendTicks < kOpenTicks) return textH * frame.sendTicks / kOpenTicks;
  const int afterSlide = frame.sendTicks - kOpenTicks - kSlideTicks;
  if (afterSlide <= 0) return textH;
  if (afterSlide >= kCloseTicks) return 0;
  return textH * (kCloseTicks - afterSlide) / kCloseTicks;
}

void drawWall(Display& display, int gapY, int gapH) {
  const int height = display.height();
  if (gapH <= 0 || gapY >= height) {
    display.fillRect(0, 0, 1, height);
    return;
  }
  if (gapY < 0) {
    gapH += gapY;
    gapY = 0;
  }
  if (gapY + gapH > height) gapH = height - gapY;
  if (gapH <= 0) {
    display.fillRect(0, 0, 1, height);
    return;
  }
  if (gapY > 0) display.fillRect(0, 0, 1, gapY);
  const int bottom = gapY + gapH;
  if (bottom < height) display.fillRect(0, bottom, 1, height - bottom);
}

void drawQuery(Display& display, const MenuFrame& frame) {
  char query[SEARCH_QUERY_MAX + 1];
  visibleQuery(frame, query, sizeof(query));
  const bool showDelete = frame.queryDeleting && frame.queryBlinkOn;
  const int queryWidth = display.textWidth(query);
  const int deleteWidth = showDelete ? 6 : 0;
  int queryX = kQueryInset;
  if (queryWidth + deleteWidth > display.width() - kQueryInset) {
    queryX = display.width() - deleteWidth - queryWidth;
  }
  if (query[0] != '\0') display.text(queryX, kQueryY, query);
  if (showDelete) display.bits(queryX + queryWidth, kQueryY, kDelete, 5, 7, true);
}

}  // namespace

void MenuDrawer::resetMotion() {
  for (int i = 0; i < kMotionCap; i++) motion_[i] = Motion{};
  hasMotion_ = false;
  animating_ = false;
  motionMs_ = 0;
}

void MenuDrawer::dropExiting() {
  for (int i = 0; i < kMotionCap; i++) {
    if (motion_[i].exiting) motion_[i].used = false;
  }
}

void MenuDrawer::snapMotion() {
  for (int i = 0; i < kMotionCap; i++) {
    Motion& motion = motion_[i];
    if (!motion.used) continue;
    motion.y = motion.targetY;
    motion.textH = motion.targetH;
  }
}

bool MenuDrawer::inMotion() const {
  for (int i = 0; i < kMotionCap; i++) {
    const Motion& motion = motion_[i];
    if (!motion.used) continue;
    if (motion.y != motion.targetY || motion.textH != motion.targetH) return true;
  }
  return false;
}

void MenuDrawer::retarget(const MenuFrame& frame) {
  const bool selectedAtTop = !(frame.rows[1].present && frame.rows[1].selected);
  bool matched[kMotionCap] = {};

  for (int i = 0; i < MENU_LIST_ROWS; i++) {
    const MenuRow& src = frame.rows[i];
    if (!src.present || src.slot < 0) continue;
    int y = 0;
    int textH = 0;
    rowTarget(i, selectedAtTop, y, textH);

    int found = -1;
    for (int m = 0; m < kMotionCap; m++) {
      if (motion_[m].used && motion_[m].slot == src.slot) {
        found = m;
        break;
      }
    }
    if (found < 0) {
      for (int m = 0; m < kMotionCap; m++) {
        if (!motion_[m].used) {
          found = m;
          break;
        }
      }
      // A fast wheel can still be sliding the previous rows off. Reuse one
      // of those so the new name is not dropped.
      if (found < 0) {
        for (int m = 0; m < kMotionCap; m++) {
          if (motion_[m].exiting) {
            found = m;
            break;
          }
        }
      }
      if (found < 0) continue;
      motion_[found] = Motion{};
      motion_[found].used = true;
      if (!hasMotion_) {
        motion_[found].y = y;
        motion_[found].textH = textH;
      } else {
        const int travel = kLargeH + kGap;
        motion_[found].y = frame.scrollDir < 0 ? y - travel : y + travel;
        motion_[found].textH = src.selected ? kSmallH : textH;
      }
    }

    Motion& motion = motion_[found];
    matched[found] = true;
    motion.exiting = false;
    motion.selected = src.selected;
    motion.slot = src.slot;
    motion.type = src.type;
    motion.targetY = y;
    motion.targetH = textH;
    copyText(motion.label, sizeof(motion.label), src.label);
  }

  const int travel = kLargeH + kGap;
  for (int m = 0; m < kMotionCap; m++) {
    Motion& motion = motion_[m];
    if (!motion.used || matched[m] || motion.exiting) continue;
    motion.exiting = true;
    motion.selected = false;
    motion.targetH = motion.textH;
    motion.targetY = motion.y + (frame.scrollDir < 0 ? travel : -travel);
  }
  hasMotion_ = true;
}

bool MenuDrawer::stepMotion(unsigned long now) {
  if (!inMotion()) {
    motionMs_ = 0;
    return false;
  }
  if (motionMs_ == 0) {
    motionMs_ = now;
    return true;
  }
  const unsigned long elapsed = now - motionMs_;
  int ticks = static_cast<int>(elapsed / 16);
  if (ticks < 1) return true;
  if (ticks > 2) ticks = 2;
  motionMs_ += static_cast<unsigned long>(ticks) * 16;
  for (int tick = 0; tick < ticks; tick++) {
    for (int i = 0; i < kMotionCap; i++) {
      Motion& motion = motion_[i];
      if (!motion.used) continue;
      const int yLeft = motion.targetY - motion.y;
      motion.y = approach(motion.y, motion.targetY);
      // The size change runs with the slide, and still finishes if the row
      // is already on its line.
      if (motion.y == motion.targetY || yLeft % 4 == 0) {
        motion.textH = approach(motion.textH, motion.targetH);
      }
    }
  }
  for (int i = 0; i < kMotionCap; i++) {
    Motion& motion = motion_[i];
    if (!motion.used || !motion.exiting) continue;
    if (motion.y == motion.targetY && motion.textH == motion.targetH) motion.used = false;
  }
  return inMotion();
}

void MenuDrawer::paint(Display& display, const MenuFrame& frame) {
  display.setClip(0, 0);
  display.clear();
  drawQuery(display, frame);

  int gapY = 0;
  int gapH = 0;
  if (!frame.notFound) {
    const int shift = exitShift(frame);
    display.setClip(kListTop, display.height());
    for (int pass = 0; pass < 2; pass++) {
      for (int i = 0; i < kMotionCap; i++) {
        const Motion& motion = motion_[i];
        if (!motion.used || motion.textH <= 0) continue;
        const bool chrome = motion.selected && !motion.exiting;
        if (pass == 0 && chrome) continue;
        if (pass == 1 && !chrome) continue;

        const int slide = chrome ? shift : 0;
        const int iconW = display.glyphWidthBox(motion.textH);
        const int restIconX = display.width() - iconW;
        const int maxName = restIconX - kIconGap - kLeftReserve;
        char fitted[VAULT_NAME_MAX + 4];
        fittedLabel(display, motion.label, maxName, motion.textH, fitted, sizeof(fitted));
        const int nameW = display.textWidthBox(fitted, motion.textH);
        const int nameX = restIconX - kIconGap - nameW - slide;
        const int iconX = restIconX - slide;

        if (chrome) {
          const int rayY = motion.y + motion.textH / 2;
          const int dotX = nameX - kDotGap - kDot;
          if (dotX > 1) display.fillRect(1, rayY, dotX - 1, 1);
          display.fillRect(dotX, rayY - (kDot - 1), kDot, kDot);
          const int open = exitGap(frame, motion.textH);
          if (open > 0) {
            gapH = open;
            gapY = rayY - open / 2;
          }
        }

        display.textBox(nameX, motion.y, fitted, motion.textH, true);
        display.bitsBox(iconX, motion.y, iconFor(motion.type), kIconCols, kIconRows, motion.textH, true);
      }
    }
    display.setClip(0, 0);
  } else {
    const char* missing = "not found";
    const int width = display.textWidthBox(missing, kLargeH);
    int x = display.width() - width;
    if (x < kQueryInset) x = kQueryInset;
    display.textBox(x, kListTop, missing, kLargeH, true);
  }

  drawWall(display, gapY, gapH);
  display.flush();
}

void MenuDrawer::draw(Display& display, const MenuFrame& frame) {
  const bool frameSame = drawn_ && sameFrame(last_, frame);
  if (frameSame && !animating_) return;

  const bool listChanged = !drawn_ || rowsDiffer(last_, frame);
  const bool sendStarting = frame.sending && (!drawn_ || !last_.sending);

  if (!frame.active) {
    resetMotion();
    display.setClip(0, 0);
    display.clear();
    display.flush();
    last_ = frame;
    drawn_ = true;
    return;
  }

  if (frame.notFound) resetMotion();
  else if (listChanged) retarget(frame);

  if (sendStarting) {
    dropExiting();
    snapMotion();
  }

  animating_ = frame.notFound ? false : stepMotion(millis());
  paint(display, frame);
  last_ = frame;
  drawn_ = true;
}
