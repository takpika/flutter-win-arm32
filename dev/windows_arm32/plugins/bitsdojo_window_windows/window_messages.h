#pragma once
#include "dev/windows_arm32/plugins/native_window_api.h"
#include "flutter/shell/platform/windows/uwp/diagnostics.h"
#include "window_util.h"

namespace flutter::winrt::plugins::bitsdojo {

// A retired host may still be chained by an external subclass. Only release
// the original posted allocations in that state; do not mutate its OS window.
inline bool ReleaseWindowActionPayload(UINT message, WPARAM action, LPARAM payload) {
  if (message != WM_BDW_ACTION) return false;
  if (action == BDW_SETWINDOWPOS) {
    CoTaskMemFree(reinterpret_cast<SWPParam*>(payload));
    return true;
  }
  if (action == BDW_SETWINDOWTEXT) {
    auto* parameter = reinterpret_cast<SWTParam*>(payload);
    if (parameter) CoTaskMemFree(const_cast<wchar_t*>(parameter->text));
    CoTaskMemFree(parameter);
    return true;
  }
  return false;
}

// Consume the original package's posted-message payloads. Its unchanged Dart
// ffi allocator uses CoTaskMemAlloc on Windows, including UTF-16 text buffers.
inline bool HandleWindowAction(const NativeWindowApi& native, HWND window,
                               UINT message, WPARAM action, LPARAM payload,
                               FlutterEngine engine,
                               const FlutterEngineProcTable& api) {
  if (message != WM_BDW_ACTION) return false;
  if (action == BDW_SETWINDOWPOS) {
    auto* parameter = reinterpret_cast<SWPParam*>(payload);
    if (!parameter) {
      Diagnostic("bitsdojo position payload", E_POINTER);
      return true;
    }
    BOOL applied = native.set_position(window, nullptr, parameter->x,
        parameter->y, parameter->cx, parameter->cy, parameter->uFlags);
    DWORD error = applied ? ERROR_SUCCESS : GetLastError();
    CoTaskMemFree(parameter);
    Diagnostic("bitsdojo native window position",
               applied ? S_OK : HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE));
    return true;
  }
  if (action == BDW_SETWINDOWTEXT) {
    auto* parameter = reinterpret_cast<SWTParam*>(payload);
    if (!parameter) {
      Diagnostic("bitsdojo title payload", E_POINTER);
      return true;
    }
    BOOL applied = native.set_text(window, parameter->text);
    DWORD error = applied ? ERROR_SUCCESS : GetLastError();
    CoTaskMemFree(const_cast<wchar_t*>(parameter->text));
    CoTaskMemFree(parameter);
    Diagnostic("bitsdojo native window title",
               applied ? S_OK : HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE));
    return true;
  }
  if (action == BDW_FORCECHILDREFRESH) {
    // The UWP host renders into one CoreWindow rather than a Win32 child view.
    // Request an actual Flutter frame instead of resizing a nonexistent child.
    auto result = api.ScheduleFrame(engine);
    Diagnostic("bitsdojo Flutter frame requested", static_cast<HRESULT>(result));
    return true;
  }
  return false;
}

}  // namespace flutter::winrt::plugins::bitsdojo
