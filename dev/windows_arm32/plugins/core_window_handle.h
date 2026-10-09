#pragma once
#include "flutter/shell/platform/windows/uwp/runtime_support.h"

namespace flutter::winrt::plugins {

// ICoreWindowInterop's IUnknown prefix and get_WindowHandle slot. The retained
// SDK projection does not declare this interop interface for the App partition.
inline HRESULT GetCoreWindowHandle(ABI::Windows::UI::Core::ICoreWindow* window,
                                   HWND* handle) {
  if (!handle) return E_POINTER;
  *handle = nullptr;
  if (!window) return E_INVALIDARG;
  struct Interop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetWindowHandle(HWND*) = 0;
  };
  constexpr GUID iid = {0x45d64a29, 0xa63e, 0x4cb6,
                       {0xb4, 0x98, 0x57, 0x81, 0xd2, 0x98, 0xcb, 0x4f}};
  ComPtr<Interop> interop;
  HRESULT result = window->QueryInterface(
      iid, reinterpret_cast<void**>(interop.GetAddressOf()));
  if (SUCCEEDED(result)) result = interop->GetWindowHandle(handle);
  if (SUCCEEDED(result) && !*handle) return E_UNEXPECTED;
  return result;
}

}  // namespace flutter::winrt::plugins
