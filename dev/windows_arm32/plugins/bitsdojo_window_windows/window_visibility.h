#pragma once
#include "flutter/shell/platform/windows/uwp/runtime_support.h"
#include <atomic>

namespace flutter::winrt::plugins::bitsdojo {

class WindowVisibility {
 public:
  // WINDOWPOS's layout is needed for WM_WINDOWPOSCHANGING in the App
  // partition, where its desktop header declaration is unavailable.
  struct Position {
    HWND hwnd;
    HWND hwndInsertAfter;
    int x, y, cx, cy;
    UINT flags;
  };
  static_assert(sizeof(Position) == 28);

  void Configure(unsigned flags) { hidden_on_startup_.store((flags & 0x2) != 0); }
  void AllowDisplay(bool allowed) { display_allowed_.store(allowed); }
  void Apply(Position* position) const {
    constexpr UINT show_window = 0x0040;
    if (position && (position->flags & show_window) &&
        hidden_on_startup_.load() && !display_allowed_.load()) {
      position->flags &= ~show_window;
    }
  }

 private:
  std::atomic<bool> hidden_on_startup_{false};
  std::atomic<bool> display_allowed_{false};
};

}  // namespace flutter::winrt::plugins::bitsdojo
