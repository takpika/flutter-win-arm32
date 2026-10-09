// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <windows.applicationmodel.h>
#include "flutter/shell/platform/windows/uwp/runtime_support.h"

namespace flutter::winrt::host {
inline HRESULT FolderPath(ABI::Windows::Storage::IStorageFolder* folder,
                          std::wstring& result) {
  ComPtr<ABI::Windows::Storage::IStorageItem> item;
  HRESULT hr = folder->QueryInterface(IID_PPV_ARGS(&item));
  HSTRING path = nullptr;
  if (SUCCEEDED(hr)) hr = item->get_Path(&path);
  if (SUCCEEDED(hr)) {
    UINT32 size = 0;
    const auto* text = WindowsGetStringRawBuffer(path, &size);
    result.assign(text, size);
  }
  WindowsDeleteString(path);
  return hr;
}
inline HRESULT InstalledDirectory(std::wstring& result) {
  const wchar_t* type = L"Windows.ApplicationModel.Package";
  HSTRING name = nullptr;
  HRESULT hr = WindowsCreateString(type, static_cast<UINT32>(wcslen(type)), &name);
  ComPtr<ABI::Windows::ApplicationModel::IPackageStatics> factory;
  if (SUCCEEDED(hr)) hr = RoGetActivationFactory(name, IID_PPV_ARGS(&factory));
  WindowsDeleteString(name);
  ComPtr<ABI::Windows::ApplicationModel::IPackage> package;
  ComPtr<ABI::Windows::Storage::IStorageFolder> folder;
  if (SUCCEEDED(hr)) hr = factory->get_Current(&package);
  if (SUCCEEDED(hr)) hr = package->get_InstalledLocation(&folder);
  return SUCCEEDED(hr) ? FolderPath(folder.Get(), result) : hr;
}
inline HRESULT LocalDirectory(std::wstring& result) {
  const wchar_t* type = L"Windows.Storage.ApplicationData";
  HSTRING name = nullptr;
  HRESULT hr = WindowsCreateString(type, static_cast<UINT32>(wcslen(type)), &name);
  ComPtr<ABI::Windows::Storage::IApplicationDataStatics> factory;
  if (SUCCEEDED(hr)) hr = RoGetActivationFactory(name, IID_PPV_ARGS(&factory));
  WindowsDeleteString(name);
  ComPtr<ABI::Windows::Storage::IApplicationData> data;
  ComPtr<ABI::Windows::Storage::IStorageFolder> folder;
  if (SUCCEEDED(hr)) hr = factory->get_Current(&data);
  if (SUCCEEDED(hr)) hr = data->get_LocalFolder(&folder);
  return SUCCEEDED(hr) ? FolderPath(folder.Get(), result) : hr;
}
inline std::string Utf8Path(const std::wstring& path) {
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(),
                                      static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(size, '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(),
                          static_cast<int>(path.size()), result.data(), size, nullptr, nullptr))
    return {};
  return result;
}
}  // namespace flutter::winrt::host
