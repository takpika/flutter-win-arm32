#pragma once
#include "flutter/shell/platform/windows/uwp/runtime_support.h"
#include <mutex>

namespace flutter::winrt::plugins::bitsdojo {

// Requested track sizes have the original package's logical-pixel units.
// These values do not substitute for actual CoreWindow bounds.
class WindowConstraints {
 public:
  // MINMAXINFO is not declared in the App partition. Keep its documented
  // message payload layout without enabling desktop header declarations.
  struct Information {
    POINT ptReserved;
    POINT ptMaxSize;
    POINT ptMaxPosition;
    POINT ptMinTrackSize;
    POINT ptMaxTrackSize;
  };
  static_assert(sizeof(Information) == 40);
  void SetMinimum(int width, int height) {
    std::lock_guard<std::mutex> guard(mutex_);
    minimum_ = {width, height};
  }
  void SetMaximum(int width, int height) {
    std::lock_guard<std::mutex> guard(mutex_);
    maximum_ = {width, height};
  }
  void Apply(Information* information, UINT window_dpi) {
    if (!information) return;
    std::lock_guard<std::mutex> guard(mutex_);
    const double scale = window_dpi / 96.0;
    if (minimum_.cx != 0 && minimum_.cy != 0) {
      information->ptMinTrackSize.x = static_cast<LONG>(minimum_.cx * scale);
      information->ptMinTrackSize.y = static_cast<LONG>(minimum_.cy * scale);
    }
    if (maximum_.cx != 0 && maximum_.cy != 0) {
      information->ptMaxTrackSize.x = static_cast<LONG>(maximum_.cx * scale);
      information->ptMaxTrackSize.y = static_cast<LONG>(maximum_.cy * scale);
    }
  }

 private:
  std::mutex mutex_;
  SIZE minimum_{};
  SIZE maximum_{};
};

}  // namespace flutter::winrt::plugins::bitsdojo
