#include "st7789_display.h"

#include <Arduino.h>
#include <SPI.h>

#include "config.h"

namespace {

// Jumper wires through the GPIO matrix drop bits at 40 MHz, and the panel
// then stays black. 10 MHz is still a full frame in a fraction of a second.
constexpr uint32_t kSpiHz = 10000000;
const SPISettings kSpi(kSpiHz, MSBFIRST, SPI_MODE0);

constexpr uint8_t kSwReset = 0x01;
constexpr uint8_t kSleepOut = 0x11;
constexpr uint8_t kNormalOn = 0x13;
constexpr uint8_t kInvertOn = 0x21;
constexpr uint8_t kDisplayOn = 0x29;
constexpr uint8_t kColumn = 0x2A;
constexpr uint8_t kRow = 0x2B;
constexpr uint8_t kWrite = 0x2C;
constexpr uint8_t kMadctl = 0x36;
constexpr uint8_t kColorMode = 0x3A;
constexpr uint8_t kGateCtrl = 0xB6;
constexpr uint8_t kRamCtrl = 0xB0;
constexpr uint8_t kPorch = 0xB2;
constexpr uint8_t kGate = 0xB7;
constexpr uint8_t kVcom = 0xBB;
constexpr uint8_t kLcm = 0xC0;
constexpr uint8_t kVdvVrhEn = 0xC2;
constexpr uint8_t kVrhs = 0xC3;
constexpr uint8_t kVdvs = 0xC4;
constexpr uint8_t kFrame = 0xC6;
constexpr uint8_t kPower = 0xD0;
constexpr uint8_t kGammaP = 0xE0;
constexpr uint8_t kGammaN = 0xE1;

// Portrait, RGB. 0x08 swaps red and blue if this glass is wired BGR.
constexpr uint8_t kPortrait = 0x00;

constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kBlue = 0x001F;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kBlack = 0x0000;

// 5x7, bit 0 is the top row. Same packing as the old panel font.
constexpr uint8_t kO[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
constexpr uint8_t kK[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};

}  // namespace

void St7789Display::command(uint8_t value, const uint8_t* args, size_t argCount) {
  SPI.beginTransaction(kSpi);
  digitalWrite(PIN_TFT_CS, LOW);
  digitalWrite(PIN_TFT_DC, LOW);
  SPI.transfer(value);
  digitalWrite(PIN_TFT_DC, HIGH);
  for (size_t i = 0; i < argCount; ++i) SPI.transfer(args[i]);
  digitalWrite(PIN_TFT_CS, HIGH);
  SPI.endTransaction();
}

bool St7789Display::begin() {
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);
  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_TFT_DC, OUTPUT);
  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_TFT_DC, HIGH);

  // The controller ignores SPI until reset has been low, then high, with
  // time to leave sleep afterwards.
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(10);
  digitalWrite(PIN_TFT_RST, LOW);
  delay(20);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(150);

  // Chip select is toggled around each command. The panel has no MISO wire.
  SPI.begin(PIN_TFT_SCL, -1, PIN_TFT_SDA, -1);

  command(kSwReset);
  delay(150);
  command(kSleepOut);
  delay(120);
  command(kNormalOn);
  command(kMadctl, &kPortrait, 1);

  const uint8_t gateCtrl[] = {0x0A, 0x82};
  command(kGateCtrl, gateCtrl, sizeof(gateCtrl));
  const uint8_t ramCtrl[] = {0x00, 0xE0};
  command(kRamCtrl, ramCtrl, sizeof(ramCtrl));
  const uint8_t colorMode = 0x55;
  command(kColorMode, &colorMode, 1);
  delay(10);

  const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
  command(kPorch, porch, sizeof(porch));
  const uint8_t gate = 0x35;
  command(kGate, &gate, 1);
  const uint8_t vcom = 0x28;
  command(kVcom, &vcom, 1);
  const uint8_t lcm = 0x0C;
  command(kLcm, &lcm, 1);
  const uint8_t vdv[] = {0x01, 0xFF};
  command(kVdvVrhEn, vdv, sizeof(vdv));
  const uint8_t vrhs = 0x10;
  command(kVrhs, &vrhs, 1);
  const uint8_t vdvs = 0x20;
  command(kVdvs, &vdvs, 1);
  const uint8_t frame = 0x0F;
  command(kFrame, &frame, 1);
  const uint8_t power[] = {0xA4, 0xA1};
  command(kPower, power, sizeof(power));
  const uint8_t gammaP[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44,
                            0x42, 0x06, 0x0E, 0x12, 0x14, 0x17};
  command(kGammaP, gammaP, sizeof(gammaP));
  const uint8_t gammaN[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54,
                            0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E};
  command(kGammaN, gammaN, sizeof(gammaN));

  command(kInvertOn);
  const uint8_t columns[] = {0x00, 0x00, 0x00, 0xEF};
  const uint8_t rows[] = {0x00, 0x00, 0x01, 0x3F};
  command(kColumn, columns, sizeof(columns));
  command(kRow, rows, sizeof(rows));
  delay(120);
  command(kDisplayOn);
  delay(120);
  Serial.println("ST7789 ready");
  return true;
}

void St7789Display::fillRect(int x, int y, int w, int h, uint16_t color) {
  if (w <= 0 || h <= 0) return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x >= kWidth || y >= kHeight) return;
  if (x + w > kWidth) w = kWidth - x;
  if (y + h > kHeight) h = kHeight - y;
  if (w <= 0 || h <= 0) return;

  const int x2 = x + w - 1;
  const int y2 = y + h - 1;
  const uint8_t column[] = {uint8_t(x >> 8), uint8_t(x), uint8_t(x2 >> 8), uint8_t(x2)};
  const uint8_t row[] = {uint8_t(y >> 8), uint8_t(y), uint8_t(y2 >> 8), uint8_t(y2)};

  // The ESP32 SPI writer reads this as 32-bit words. Alignment keeps a
  // short row from faulting, which reboots before a pixel lands.
  static uint8_t line[kWidth * 2] __attribute__((aligned(4)));
  const uint8_t hi = uint8_t(color >> 8);
  const uint8_t lo = uint8_t(color);
  for (int i = 0; i < w; ++i) {
    line[i * 2] = hi;
    line[i * 2 + 1] = lo;
  }

  SPI.beginTransaction(kSpi);
  digitalWrite(PIN_TFT_CS, LOW);
  digitalWrite(PIN_TFT_DC, LOW);
  SPI.transfer(kColumn);
  digitalWrite(PIN_TFT_DC, HIGH);
  for (uint8_t byte : column) SPI.transfer(byte);
  digitalWrite(PIN_TFT_DC, LOW);
  SPI.transfer(kRow);
  digitalWrite(PIN_TFT_DC, HIGH);
  for (uint8_t byte : row) SPI.transfer(byte);
  digitalWrite(PIN_TFT_DC, LOW);
  SPI.transfer(kWrite);
  digitalWrite(PIN_TFT_DC, HIGH);
  const uint32_t lineBytes = static_cast<uint32_t>(w) * 2;
  for (int drawn = 0; drawn < h; ++drawn) SPI.writeBytes(line, lineBytes);
  digitalWrite(PIN_TFT_CS, HIGH);
  SPI.endTransaction();
}

void St7789Display::glyph(int x, int y, const uint8_t columns[5], int scale, uint16_t color) {
  for (int col = 0; col < 5; ++col) {
    for (int row = 0; row < 7; ++row) {
      if ((columns[col] & (1u << row)) == 0) continue;
      fillRect(x + col * scale, y + row * scale, scale, scale, color);
    }
  }
}

void St7789Display::drawTest() {
  fillRect(0, 0, kWidth, 107, kRed);
  fillRect(0, 107, kWidth, 106, kGreen);
  fillRect(0, 213, kWidth, 107, kBlue);
  constexpr int frame = 8;
  fillRect(0, 0, kWidth, frame, kWhite);
  fillRect(0, kHeight - frame, kWidth, frame, kWhite);
  fillRect(0, 0, frame, kHeight, kWhite);
  fillRect(kWidth - frame, 0, frame, kHeight, kWhite);

  constexpr int scale = 8;
  constexpr int letterW = 5 * scale;
  constexpr int gap = scale;
  constexpr int textW = letterW * 2 + gap;
  constexpr int textH = 7 * scale;
  const int textX = (kWidth - textW) / 2;
  const int textY = (kHeight - textH) / 2;
  glyph(textX, textY, kO, scale, kBlack);
  glyph(textX + letterW + gap, textY, kK, scale, kBlack);
}
