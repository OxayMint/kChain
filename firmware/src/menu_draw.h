#pragma once

#include "display.h"
#include "menu.h"

// Reads a menu frame and paints it. Layout lives here, not in the menu
// and not in the panel driver.
class MenuDrawer {
 public:
  void draw(Display& display, const MenuFrame& frame);

 private:
  struct Motion {
    bool used = false;
    bool exiting = false;
    bool selected = false;
    int slot = -1;
    EntryType type = EntryType::Generic;
    char label[VAULT_NAME_MAX + 1] = {};
    int y = 0;
    int textH = 0;
    int targetY = 0;
    int targetH = 0;
  };

  static constexpr int kMotionCap = 4;

  bool drawn_ = false;
  bool animating_ = false;
  bool hasMotion_ = false;
  unsigned long motionMs_ = 0;
  MenuFrame last_{};
  Motion motion_[kMotionCap]{};

  void resetMotion();
  void retarget(const MenuFrame& frame);
  void dropExiting();
  void snapMotion();
  bool inMotion() const;
  bool stepMotion(unsigned long now);
  void paint(Display& display, const MenuFrame& frame);
};
