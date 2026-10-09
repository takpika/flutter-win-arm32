#pragma once
#include "core_window_handle.h"

namespace flutter::winrt::plugins {

// Own the SDK module reference for the lifetime of these OS function pointers.
// This is the Win32 ABI boundary used by native UWP package implementations.
class NativeWindowApi {
 public:
  using Procedure = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);
  using GetLong = LONG_PTR(WINAPI*)(HWND, int);
  using SetLong = LONG_PTR(WINAPI*)(HWND, int, LONG_PTR);
  using CallProcedure = LRESULT(WINAPI*)(Procedure, HWND, UINT, WPARAM, LPARAM);
  using Send = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
  using Post = BOOL(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
  using SetPosition = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
  using SetText = BOOL(WINAPI*)(HWND, LPCWSTR);
  using GetDpi = UINT(WINAPI*)(HWND);
  struct MonitorInformation {
    DWORD size;
    RECT monitor;
    RECT work;
    DWORD flags;
  };
  static_assert(sizeof(MonitorInformation) == 40);
  using GetMonitor = HANDLE(WINAPI*)(HWND, DWORD);
  using ReadMonitor = BOOL(WINAPI*)(HANDLE, MonitorInformation*);
  using IsVisible = BOOL(WINAPI*)(HWND);
  using GetRectangle = BOOL(WINAPI*)(HWND, RECT*);

  NativeWindowApi() = default;
  NativeWindowApi(const NativeWindowApi&) = delete;
  NativeWindowApi& operator=(const NativeWindowApi&) = delete;
  ~NativeWindowApi() { if (module_) FreeLibrary(module_); }

  HRESULT Open() {
    if (module_) return E_UNEXPECTED;
    module_ = LoadPackagedLibrary(L"flutter_winrt_compat.dll", 0);
    if (!module_) return HRESULT_FROM_WIN32(GetLastError());
    get = Resolve<GetLong>("GetWindowLongPtrW");
    set = Resolve<SetLong>("SetWindowLongPtrW");
    call = Resolve<CallProcedure>("CallWindowProcW");
    send = Resolve<Send>("SendMessageW");
    post = Resolve<Post>("PostMessageW");
    set_position = Resolve<SetPosition>("SetWindowPos");
    set_text = Resolve<SetText>("SetWindowTextW");
    // The original package treats this newer OS export as optional and uses
    // 96 DPI when it is absent. Do not synthesize a replacement export.
    get_dpi = Resolve<GetDpi>("GetDpiForWindow");
    monitor_from_window = Resolve<GetMonitor>("MonitorFromWindow");
    get_monitor_info = Resolve<ReadMonitor>("GetMonitorInfoW");
    is_visible = Resolve<IsVisible>("IsWindowVisible");
    get_rectangle = Resolve<GetRectangle>("GetWindowRect");
    return get && set && call && send && post && set_position && set_text
               && monitor_from_window && get_monitor_info
               && is_visible && get_rectangle
               ? S_OK : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
  }

  GetLong get = nullptr;
  SetLong set = nullptr;
  CallProcedure call = nullptr;
  Send send = nullptr;
  Post post = nullptr;
  SetPosition set_position = nullptr;
  SetText set_text = nullptr;
  GetDpi get_dpi = nullptr;
  GetMonitor monitor_from_window = nullptr;
  ReadMonitor get_monitor_info = nullptr;
  IsVisible is_visible = nullptr;
  GetRectangle get_rectangle = nullptr;

 private:
  template<class Function> Function Resolve(const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(module_, name));
  }
  HMODULE module_ = nullptr;
};

}  // namespace flutter::winrt::plugins
