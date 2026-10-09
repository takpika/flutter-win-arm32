// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "../../runner/package_paths.h"

#include "configuration.h"
#include "flutter/shell/platform/windows/uwp/runtime_support.h"

namespace flutter::winrt::plugins::firebase_core {
// Package.Current works in both the UI executable and the App Service host.
// The system background host's executable directory is not the app directory.
inline Configuration LoadPackagedConfiguration() {
  std::wstring filename;
  HRESULT hr = host::InstalledDirectory(filename);
  filename += L"\\data\\firebase.json";
  if (FAILED(hr)) throw StateError("firebase-resource-read-error");
  HANDLE file = CreateFile2(filename.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            OPEN_EXISTING, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
      return {};
    throw StateError("firebase-resource-read-error");
  }
  LARGE_INTEGER size{};
  bool valid =
      GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 65536;
  StateBytes bytes(valid ? static_cast<size_t>(size.QuadPart) : 0);
  DWORD read = 0;
  if (valid)
    valid = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                     &read, nullptr) &&
            read == bytes.size();
  CloseHandle(file);
  if (!valid) throw StateError("firebase-resource-read-error");
  return Configuration::Parse(bytes);
}
}  // namespace flutter::winrt::plugins::firebase_core
