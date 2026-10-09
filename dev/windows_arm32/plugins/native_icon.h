#pragma once
#include "os_module.h"
#include "flutter/shell/platform/windows/uwp/diagnostics.h"
#include <memory>
#include <string>

namespace flutter::winrt::plugins {
using DestroyNativeHandle = BOOL(WINAPI*)(HANDLE);
struct NativeHandleDeleter {
  DestroyNativeHandle destroy = nullptr;
  void operator()(HANDLE handle) const {
    if (handle && destroy && !destroy(handle))
      flutter::winrt::Diagnostic("native resource release", OsModule::Error());
  }
};
using OwnedNativeHandle = std::unique_ptr<void, NativeHandleDeleter>;

inline HRESULT NativeUtf16(const std::string& text, std::wstring* wide) {
  int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (!size) return OsModule::Error();
  wide->resize(size);
  return MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide->data(), size)
             ? S_OK : OsModule::Error();
}

class NativeIconLoader {
 public:
  HRESULT Load(const std::string& path, UINT flags, bool square, OwnedNativeHandle* icon) {
    using Metrics = int(WINAPI*)(int);
    using LoadImage = HANDLE(WINAPI*)(HINSTANCE, LPCWSTR, UINT, int, int, UINT);
    HRESULT result = user_.Open(L"MinUser.dll");
    Metrics metrics = nullptr;
    LoadImage load = nullptr;
    DestroyNativeHandle destroy = nullptr;
    if (SUCCEEDED(result)) result = user_.Resolve("GetSystemMetrics", &metrics);
    if (SUCCEEDED(result)) result = user_.Resolve("LoadImageW", &load);
    if (SUCCEEDED(result)) result = user_.Resolve("DestroyIcon", &destroy);
    if (FAILED(result)) return result;
    std::wstring wide;
    result = NativeUtf16(path, &wide);
    if (FAILED(result)) return result;
    HANDLE handle = load(nullptr, wide.c_str(), 1, metrics(49), metrics(square ? 49 : 50), flags);
    if (!handle) return OsModule::Error();
    *icon = OwnedNativeHandle(handle, NativeHandleDeleter{destroy});
    return S_OK;
  }
 private:
  OsModule user_;
};
}
