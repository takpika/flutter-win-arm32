#pragma once
#include "taskbar_api.h"
#include "dev/windows_arm32/plugins/native_icon.h"
#include "dev/windows_arm32/plugins/os_module.h"
#include "flutter/shell/platform/windows/uwp/diagnostics.h"
#include "utils.h"
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace flutter::winrt::plugins::taskbar {

using DestroyHandle = DestroyNativeHandle;
using HandleDeleter = NativeHandleDeleter;
using OwnedHandle = OwnedNativeHandle;

struct ToolbarButton {
  std::string icon;
  std::string tooltip;
  DWORD flags;
};

class ShellImages {
 public:
  HRESULT LoadIcon(const std::string& path, OwnedHandle* icon) {
    return icons_.Load(path, 0x0030, true, icon);
  }

  HRESULT SetToolbar(TaskbarList* taskbar, HWND window,
                     const std::vector<ToolbarButton>& buttons, bool* added) {
    if (buttons.size() > 7) return E_INVALIDARG;
    using Metrics = int(WINAPI*)(int);
    using Create = HANDLE(WINAPI*)(int, int, UINT, int, int);
    using Replace = int(WINAPI*)(HANDLE, int, HANDLE);
    HRESULT result = controls_.Open(L"comctl32.dll");
    Create create = nullptr;
    Replace replace = nullptr;
    DestroyHandle destroy = nullptr;
    Metrics metrics = nullptr;
    if (SUCCEEDED(result)) result = controls_.Resolve("ImageList_Create", &create);
    if (SUCCEEDED(result)) result = controls_.Resolve("ImageList_ReplaceIcon", &replace);
    if (SUCCEEDED(result)) result = controls_.Resolve("ImageList_Destroy", &destroy);
    if (SUCCEEDED(result)) result = user_.Open(L"MinUser.dll");
    if (SUCCEEDED(result)) result = user_.Resolve("GetSystemMetrics", &metrics);
    if (FAILED(result)) return result;
    int size = metrics(49);
    HANDLE handle = create(size, size, 0x0021, 0, 0);
    if (!handle) return OsModule::Error();
    OwnedHandle list(handle, HandleDeleter{destroy});
    std::array<ThumbnailButton, 7> native{};
    for (size_t index = 0; index < native.size(); ++index) {
      native[index].id = static_cast<UINT>(40001 + index);
      if (index >= buttons.size()) {
        native[index].mask = 8;  // THB_FLAGS
        native[index].flags = 8;  // THBF_HIDDEN
        continue;
      }
      OwnedHandle icon(nullptr, HandleDeleter{});
      result = LoadIcon(buttons[index].icon, &icon);
      if (FAILED(result)) return result;
      if (replace(handle, -1, icon.get()) < 0) return OsModule::Error();
      native[index].mask = 1 | 4 | 8;  // THB_BITMAP | THB_TOOLTIP | THB_FLAGS
      native[index].bitmap = static_cast<UINT>(index);
      native[index].flags = buttons[index].flags;
      auto tooltip = Utf16FromUtf8(buttons[index].tooltip);
      std::copy_n(tooltip.data(), std::min<size_t>(259, tooltip.size()), native[index].tooltip);
    }
    result = taskbar->ThumbBarSetImageList(window, handle);
    if (FAILED(result)) return result;
    result = *added ? taskbar->ThumbBarUpdateButtons(window, 7, native.data())
                    : taskbar->ThumbBarAddButtons(window, 7, native.data());
    if (SUCCEEDED(result)) *added = true;
    return result;
  }

 private:
  NativeIconLoader icons_;
  OsModule user_;
  OsModule controls_;
};

}  // namespace flutter::winrt::plugins::taskbar
