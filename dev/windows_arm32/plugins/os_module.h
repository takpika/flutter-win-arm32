#pragma once
#include "flutter/shell/platform/windows/uwp/runtime_support.h"
#include <string>

namespace flutter::winrt::plugins {

// Resolve optional OS exports at the native plugin boundary. Missing modules
// or procedures are real errors, rather than imports that prevent app launch.
class OsModule {
 public:
  OsModule() = default;
  OsModule(const OsModule&) = delete;
  OsModule& operator=(const OsModule&) = delete;
  ~OsModule() { if (module_) FreeLibrary(module_); }

  HRESULT Open(const wchar_t* name) {
    if (!name || !*name) return E_INVALIDARG;
    if (module_) return name_ == name ? S_OK : E_UNEXPECTED;
    module_ = LoadLibraryExW(name, nullptr, 0);
    if (!module_) return Error();
    name_ = name;
    return S_OK;
  }
  template<class Function> HRESULT Resolve(const char* name, Function* result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (!module_) return E_UNEXPECTED;
    auto address = GetProcAddress(module_, name);
    if (!address) return Error();
    *result = reinterpret_cast<Function>(address);
    return S_OK;
  }
  static HRESULT Error() {
    DWORD error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
  }

 private:
  HMODULE module_ = nullptr;
  std::wstring name_;
};

}  // namespace flutter::winrt::plugins
