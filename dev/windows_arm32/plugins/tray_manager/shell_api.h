#pragma once
#include "dev/windows_arm32/plugins/os_module.h"

namespace flutter::winrt::plugins::tray {
#pragma pack(push, 1)
struct Notification {
  DWORD size;
  HWND window;
  UINT id, flags, callback;
  HANDLE icon;
  WCHAR tooltip[128];
  DWORD state, state_mask;
  WCHAR information[256];
  UINT timeout;
  WCHAR title[64];
  DWORD information_flags;
  GUID guid;
  HANDLE balloon_icon;
};
struct Identifier { DWORD size; HWND window; UINT id; GUID guid; };
#pragma pack(pop)
static_assert(sizeof(Notification) == 956);
static_assert(sizeof(Identifier) == 28);

class ShellApi {
 public:
  HRESULT Notify(DWORD action, Notification* notification) {
    using Function = BOOL(WINAPI*)(DWORD, Notification*);
    Function function = nullptr;
    HRESULT result = shell_.Open(L"shell32.dll");
    if (SUCCEEDED(result)) result = shell_.Resolve("Shell_NotifyIconW", &function);
    if (FAILED(result)) return result;
    return function(action, notification) ? S_OK : OsModule::Error();
  }
  HRESULT Bounds(const Identifier& identifier, RECT* rectangle) {
    using Function = HRESULT(WINAPI*)(const Identifier*, RECT*);
    Function function = nullptr;
    HRESULT result = shell_.Open(L"shell32.dll");
    if (SUCCEEDED(result)) result = shell_.Resolve("Shell_NotifyIconGetRect", &function);
    return FAILED(result) ? result : function(&identifier, rectangle);
  }
 private:
  OsModule shell_;
};
}
