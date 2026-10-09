#pragma once
#include "dev/windows_arm32/plugins/native_icon.h"
#include <flutter/encodable_value.h>
#include <set>

namespace flutter::winrt::plugins::tray {
using Value = flutter::EncodableValue;
using Map = flutter::EncodableMap;
template<class T> const T* Argument(const Map* arguments, const char* key) {
  if (!arguments) return nullptr;
  auto found = arguments->find(Value(key));
  return found == arguments->end() ? nullptr : std::get_if<T>(&found->second);
}

class NativeMenu {
 public:
  HRESULT Set(const Map* menu) {
    HRESULT result = user_.Open(L"MinUser.dll");
    if (SUCCEEDED(result)) result = user_.Resolve("CreatePopupMenu", &create_);
    if (SUCCEEDED(result)) result = user_.Resolve("AppendMenuW", &append_);
    if (SUCCEEDED(result)) result = user_.Resolve("DestroyMenu", &destroy_);
    if (FAILED(result)) return result;
    OwnedNativeHandle staged(nullptr, NativeHandleDeleter{});
    std::set<int32_t> ids;
    result = Build(menu, &staged, &ids);
    if (SUCCEEDED(result)) {
      menu_ = std::shared_ptr<void>(std::move(staged));
      ids_ = std::move(ids);
    }
    return result;
  }
  HRESULT Popup(HWND window, UINT* command) {
    if (!menu_) return E_UNEXPECTED;
    auto menu = menu_;  // A reentrant update must not destroy the tracked menu.
    using Cursor = BOOL(WINAPI*)(POINT*);
    using Foreground = BOOL(WINAPI*)(HWND);
    using Track = BOOL(WINAPI*)(HANDLE, UINT, int, int, int, HWND, const RECT*);
    Cursor cursor = nullptr;
    Foreground foreground = nullptr;
    Track track = nullptr;
    HRESULT result = user_.Resolve("GetCursorPos", &cursor);
    if (SUCCEEDED(result)) result = user_.Resolve("SetForegroundWindow", &foreground);
    if (SUCCEEDED(result)) result = user_.Resolve("TrackPopupMenu", &track);
    if (FAILED(result)) return result;
    POINT position{};
    if (!cursor(&position)) return OsModule::Error();
    foreground(window);
    SetLastError(ERROR_SUCCESS);
    *command = static_cast<UINT>(track(menu.get(), 0x0120, position.x, position.y,
                                     0, window, nullptr));
    // TPM_RETURNCMD distinguishes selection from cancellation without causing
    // a second WM_COMMAND notification. Zero with no error is cancellation.
    return *command || GetLastError() == ERROR_SUCCESS ? S_OK : OsModule::Error();
  }
  bool Contains(UINT id) const { return ids_.contains(static_cast<int32_t>(id)); }
  void Reset() { menu_.reset(); ids_.clear(); }

 private:
  HRESULT Build(const Map* menu, OwnedNativeHandle* owned, std::set<int32_t>* ids) {
    auto items = Argument<flutter::EncodableList>(menu, "items");
    if (!items) return E_INVALIDARG;
    HANDLE handle = create_();
    if (!handle) return OsModule::Error();
    OwnedNativeHandle result(handle, NativeHandleDeleter{destroy_});
    for (const auto& item : *items) {
      auto map = std::get_if<Map>(&item);
      auto id = Argument<int32_t>(map, "id");
      auto type = Argument<std::string>(map, "type");
      auto label = Argument<std::string>(map, "label");
      auto disabled = Argument<bool>(map, "disabled");
      if (!id || !type || !label || !disabled) return E_INVALIDARG;
      UINT flags = *disabled ? 1 : 0;
      UINT_PTR identifier = *id;
      OwnedNativeHandle child(nullptr, NativeHandleDeleter{});
      if (*type == "separator") flags = 0x0800;
      else if (*type == "submenu") {
        HRESULT status = Build(Argument<Map>(map, "submenu"), &child, ids);
        if (FAILED(status)) return status;
        flags |= 0x0010;
        identifier = reinterpret_cast<UINT_PTR>(child.get());
      } else {
        ids->insert(*id);
        if (*type == "checkbox") {
          auto checked = Argument<bool>(map, "checked");
          if (checked && *checked) flags |= 8;
        }
      }
      std::wstring text;
      HRESULT status = NativeUtf16(*label, &text);
      if (FAILED(status)) return status;
      if (!append_(handle, flags, identifier, *type == "separator" ? nullptr : text.c_str()))
        return OsModule::Error();
      if (child) child.release();  // The root menu now owns this submenu.
    }
    *owned = std::move(result);
    return S_OK;
  }
  using Create = HANDLE(WINAPI*)();
  using Append = BOOL(WINAPI*)(HANDLE, UINT, UINT_PTR, LPCWSTR);
  OsModule user_;
  Create create_ = nullptr;
  Append append_ = nullptr;
  DestroyNativeHandle destroy_ = nullptr;
  std::shared_ptr<void> menu_;
  std::set<int32_t> ids_;
};
}
