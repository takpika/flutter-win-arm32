#pragma once
#include "dev/windows_arm32/plugins/native_window_api.h"
#include "dev/windows_arm32/plugins/os_module.h"
#include <limits>
#include <optional>
#include <string>

namespace flutter::winrt::plugins::taskbar {

// Window titles do not require an Explorer taskbar COM object. Keep the real
// HWND title and its first saved value, matching the original plugin contract.
class WindowTitle {
 public:
  HRESULT Set(const NativeWindowApi& native, HWND window, const std::wstring& title) {
    if (!window) return E_INVALIDARG;
    if (!native.set_text) return E_UNEXPECTED;
    if (!original_) {
      std::wstring current;
      HRESULT result = Read(window, &current);
      if (FAILED(result)) return result;
      original_ = std::move(current);
    }
    return native.set_text(window, title.c_str()) ? S_OK : OsModule::Error();
  }
  HRESULT Reset(const NativeWindowApi& native, HWND window) {
    if (!window) return E_INVALIDARG;
    if (!original_) return S_OK;
    if (!native.set_text) return E_UNEXPECTED;
    return native.set_text(window, original_->c_str()) ? S_OK : OsModule::Error();
  }

 private:
  HRESULT Read(HWND window, std::wstring* value) {
    using GetText = int(WINAPI*)(HWND, LPWSTR, int);
    HRESULT result = user_.Open(L"MinUser.dll");
    GetText get_text = nullptr;
    if (SUCCEEDED(result)) result = user_.Resolve("GetWindowTextW", &get_text);
    if (FAILED(result)) return result;
    // GetWindowTextLengthW is absent on the tested OS. Read the actual title
    // with increasing buffers instead of assuming a maximum or returning blank.
    for (size_t capacity = 256;
         capacity <= static_cast<size_t>(std::numeric_limits<int>::max());
         capacity *= 2) {
      std::wstring buffer(capacity, L'\0');
      SetLastError(ERROR_SUCCESS);
      int length = get_text(window, buffer.data(), static_cast<int>(capacity));
      if (!length && GetLastError() != ERROR_SUCCESS) return OsModule::Error();
      if (length < static_cast<int>(capacity) - 1) {
        buffer.resize(length);
        *value = std::move(buffer);
        return S_OK;
      }
    }
    return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
  }
  OsModule user_;
  std::optional<std::wstring> original_;
};

}  // namespace flutter::winrt::plugins::taskbar
