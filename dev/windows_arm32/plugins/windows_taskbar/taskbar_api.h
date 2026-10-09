#pragma once
#include "flutter/shell/platform/windows/uwp/runtime_support.h"

namespace flutter::winrt::plugins::taskbar {

// App-partition headers omit the desktop shell declarations. Retain the
// documented IUnknown/ITaskbarList/ITaskbarList2/ITaskbarList3 ABI, without
// changing the compiler's API partition or providing a substitute shell.
struct ThumbnailButton {
  DWORD mask;
  UINT id;
  UINT bitmap;
  HANDLE icon;
  WCHAR tooltip[260];
  DWORD flags;
};
static_assert(sizeof(ThumbnailButton) == 540);

struct TaskbarList : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE HrInit() = 0;
  virtual HRESULT STDMETHODCALLTYPE AddTab(HWND) = 0;
  virtual HRESULT STDMETHODCALLTYPE DeleteTab(HWND) = 0;
  virtual HRESULT STDMETHODCALLTYPE ActivateTab(HWND) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetActiveAlt(HWND) = 0;
  virtual HRESULT STDMETHODCALLTYPE MarkFullscreenWindow(HWND, BOOL) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetProgressValue(HWND, ULONGLONG, ULONGLONG) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetProgressState(HWND, DWORD) = 0;
  virtual HRESULT STDMETHODCALLTYPE RegisterTab(HWND, HWND) = 0;
  virtual HRESULT STDMETHODCALLTYPE UnregisterTab(HWND) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetTabOrder(HWND, HWND) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetTabActive(HWND, HWND, DWORD) = 0;
  virtual HRESULT STDMETHODCALLTYPE ThumbBarAddButtons(HWND, UINT, ThumbnailButton*) = 0;
  virtual HRESULT STDMETHODCALLTYPE ThumbBarUpdateButtons(HWND, UINT, ThumbnailButton*) = 0;
  virtual HRESULT STDMETHODCALLTYPE ThumbBarSetImageList(HWND, HANDLE) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetOverlayIcon(HWND, HANDLE, LPCWSTR) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetThumbnailTooltip(HWND, LPCWSTR) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetThumbnailClip(HWND, RECT*) = 0;
};

class TaskbarApi {
 public:
  HRESULT Open() {
    if (taskbar_) return S_OK;
    constexpr GUID cls = {0x56fdf344, 0xfd6d, 0x11d0,
                         {0x95, 0x8a, 0x00, 0x60, 0x97, 0xc9, 0xa0, 0x90}};
    constexpr GUID iid = {0xea1afb91, 0x9e28, 0x4b86,
                         {0x90, 0xe9, 0x9e, 0x9f, 0x8a, 0x5e, 0xef, 0xaf}};
    ComPtr<TaskbarList> instance;
    HRESULT result = CoCreateInstance(cls, nullptr, CLSCTX_INPROC_SERVER, iid,
                                     reinterpret_cast<void**>(instance.GetAddressOf()));
    if (SUCCEEDED(result) && !instance) return E_UNEXPECTED;
    if (SUCCEEDED(result)) result = instance->HrInit();
    if (SUCCEEDED(result)) taskbar_ = std::move(instance);
    return result;
  }
  TaskbarList* get() const { return taskbar_.Get(); }

 private:
  ComPtr<TaskbarList> taskbar_;
};

}  // namespace flutter::winrt::plugins::taskbar
