#include "ui.h"

#include "config.h"
#include "st7789_display.h"
#include "ui_font.h"

namespace {

// RGB565 of the client palette. Background oklch(0.17 0.015 70),
// card oklch(0.22 0.016 70), foreground oklch(0.96 0.02 90),
// primary oklch(0.82 0.13 82), muted text oklch(0.75 0.02 85).
// The hairline is the client's border, foreground at 14% over the background.
constexpr uint16_t kBg = 0x1061;
constexpr uint16_t kSurface = 0x18C2;
constexpr uint16_t kBorder = 0x3164;
constexpr uint16_t kPrimary = 0xEDCB;
constexpr uint16_t kText = 0xF79C;
constexpr uint16_t kMuted = 0xB574;

// 16x16, MSB left.
constexpr uint8_t kLock[] = {
    0x00, 0x00, 0x07, 0xE0, 0x0C, 0x30, 0x08, 0x10, 0x08, 0x10, 0x0F, 0xF0, 0x1F, 0xF8, 0x18, 0x18,
    0x19, 0x98, 0x19, 0x98, 0x18, 0x18, 0x1F, 0xF8, 0x0F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr uint8_t kKey[] = {
    0x00, 0x00, 0x07, 0x80, 0x08, 0x40, 0x08, 0x40, 0x07, 0x80, 0x02, 0x00, 0x02, 0x00, 0x02, 0x60,
    0x02, 0x00, 0x02, 0x60, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr uint8_t kDiamond[] = {
    0x01, 0x80, 0x03, 0xC0, 0x07, 0xE0, 0x0E, 0x70, 0x1C, 0x38, 0x38, 0x1C, 0x70, 0x0E, 0xE0, 0x07,
    0x70, 0x0E, 0x38, 0x1C, 0x1C, 0x38, 0x0E, 0x70, 0x07, 0xE0, 0x03, 0xC0, 0x01, 0x80, 0x00, 0x00,
};
constexpr uint8_t kPage[] = {
    0x1F, 0xF8, 0x30, 0x04, 0x20, 0x04, 0x27, 0xE4, 0x20, 0x04, 0x27, 0xE4, 0x20, 0x04, 0x27, 0xE4,
    0x20, 0x04, 0x20, 0x04, 0x3F, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr uint8_t kChevron[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x06, 0x00, 0x03, 0x00, 0x01, 0x80, 0x00, 0xC0,
    0x01, 0x80, 0x03, 0x00, 0x06, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static_assert(St7789Display::kWidth == 240, "cards are laid out for the 240-wide panel");
static_assert(St7789Display::kHeight == 320, "the list window is laid out for the 320-tall panel");

const uint8_t* iconFor(UiScreen screen) {
  if (screen == UiScreen::Web) return kKey;
  if (screen == UiScreen::Crypto) return kDiamond;
  if (screen == UiScreen::Help) return kPage;
  return kLock;
}

void fitText(char* out, size_t outSize, const char* text, const UiFont& font, int maxWidth) {
  if (outSize == 0) return;
  out[0] = '\0';
  if (text == nullptr || maxWidth <= 0) return;
  if (uiTextWidth(font, text) <= maxWidth) {
    size_t i = 0;
    while (text[i] != '\0' && i + 1 < outSize) {
      out[i] = text[i];
      i++;
    }
    out[i] = '\0';
    return;
  }

  const int dots = uiTextWidth(font, "...");
  size_t n = 0;
  int width = 0;
  const int limit = dots < maxWidth ? maxWidth - dots : maxWidth;
  while (text[n] != '\0' && n + 1 < outSize) {
    const int advance = uiGlyph(font, static_cast<unsigned char>(text[n])).advance;
    if (width + advance > limit) break;
    out[n] = text[n];
    width += advance;
    n++;
  }
  if (dots < maxWidth) {
    const char* ellipsis = "...";
    for (int i = 0; i < 3 && n + 1 < outSize; i++) out[n++] = ellipsis[i];
  }
  out[n] = '\0';
}

void drawChrome(St7789Display& display) {
  display.fillRect(0, 0, display.width(), kUiBar, kPrimary);
  display.text(kUiMargin, kUiHeaderY, "kChain", kUiFont35, kText, kBg);
  display.fillRect(kUiMargin, kUiRuleY, display.width() - kUiMargin * 2, 1, kBorder);
}

int cardY(UiScreen screen, int index, int window) {
  const int stride = uiCardHeight(screen) + kUiCardGap;
  return kUiListTop + (index - window) * stride;
}

void drawCard(St7789Display& display, int index, int window, bool selected, UiScreen kind,
              const char* title, const char* detail, bool chevron) {
  const int height = uiCardHeight(kind == UiScreen::Home ? UiScreen::Single : kind);
  const int y = cardY(kind == UiScreen::Home ? UiScreen::Single : kind, index, window);
  const int x = kUiMargin;
  const int w = display.width() - kUiMargin * 2;
  const uint16_t fill = kSurface;
  if (selected) {
    display.fillRoundRect(x, y, w, height, kUiRadius, fill, kPrimary, kBg, 2);
  } else {
    display.fillRoundRect(x, y, w, height, kUiRadius, fill, kBorder, kBg);
  }

  const uint8_t* icon = iconFor(kind == UiScreen::Home ? UiScreen::Single : kind);
  if (kind == UiScreen::Home) {
    if (index >= 0 && index < kUiHomeCount) icon = iconFor(kUiHome[index].screen);
  }
  const uint16_t mark = selected ? kPrimary : kText;
  const int iconY = y + (height - kUiIcon) / 2;
  display.mask(x + kUiPad, iconY, kUiIcon, kUiIcon, icon, mark, fill);

  int textRight = x + w - kUiPad;
  if (chevron) {
    display.mask(textRight - kUiIcon, iconY, kUiIcon, kUiIcon, kChevron, mark, fill);
    textRight -= kUiIcon + 8;
  }
  const int textX = x + kUiPad + kUiIcon + 8;
  const int maxWidth = textRight - textX;
  char fitted[VAULT_HOSTNAME_MAX + 1];
  const bool twoLines = detail != nullptr && detail[0] != '\0';
  if (!twoLines) {
    fitText(fitted, sizeof(fitted), title == nullptr ? "" : title, kUiFont20, maxWidth);
    const int textY = y + (height - kUiFont20.height) / 2;
    display.text(textX, textY, fitted, kUiFont20, kText, fill);
    return;
  }

  const int block = kUiFont20.height + 4 + kUiFont16.height;
  const int textY = y + (height - block) / 2;
  fitText(fitted, sizeof(fitted), title == nullptr ? "" : title, kUiFont20, maxWidth);
  display.text(textX, textY, fitted, kUiFont20, kText, fill);
  fitText(fitted, sizeof(fitted), detail, kUiFont16, maxWidth);
  display.text(textX, textY + kUiFont20.height + 4, fitted, kUiFont16, kMuted, fill);
}

}  // namespace

void Ui::paint(St7789Display& display, const Vault& vault) {
  if (draw_ == Draw::None) return;
  const Draw mode = draw_;
  const int first = cardA_;
  const int second = cardB_;
  draw_ = Draw::None;

  const bool home = screen_ == UiScreen::Home;
  const int window = home || screen_ == UiScreen::Help ? 0 : placeFor(screen_).window;
  const int selected = home ? home_ : (screen_ == UiScreen::Help ? -1 : placeFor(screen_).index);
  const int count = home ? kUiHomeCount : listCount(vault);
  const int visible = uiVisibleRows(home ? UiScreen::Single : screen_);

  auto card = [&](int index) {
    if (index < 0) return;
    if (home) {
      if (index >= kUiHomeCount) return;
      drawCard(display, index, 0, index == selected, UiScreen::Home, kUiHome[index].label, nullptr,
               true);
      return;
    }
    if (index < window || index >= window + visible || index >= count) return;
    const VaultEntry* entry = entryAt(vault, index);
    if (entry == nullptr) return;
    const char* detail = nullptr;
    if (screen_ == UiScreen::Web) detail = entry->username.c_str();
    UiScreen kind = UiScreen::Single;
    if (screen_ == UiScreen::Web) kind = UiScreen::Web;
    if (screen_ == UiScreen::Crypto) kind = UiScreen::Crypto;
    drawCard(display, index, window, index == selected, kind, entry->name.c_str(), detail, false);
  };

  if (mode == Draw::Cards) {
    card(first);
    if (second != first) card(second);
    return;
  }

  display.fillRect(0, 0, display.width(), display.height(), kBg);
  drawChrome(display);
  if (screen_ == UiScreen::Help) {
    display.text(kUiMargin, kUiListTop, "Nothing here yet.", kUiFont20, kMuted, kBg);
    return;
  }
  if (!home && count == 0) {
    display.text(kUiMargin, kUiListTop, "None saved", kUiFont20, kMuted, kBg);
    return;
  }
  const int shown = count < visible ? count : visible;
  for (int i = 0; i < shown; i++) card(window + i);
}
