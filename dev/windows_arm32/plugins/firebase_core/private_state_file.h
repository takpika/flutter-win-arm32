// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "flutter/shell/platform/windows/uwp/runtime_support.h"
#include "state_document.h"

namespace flutter::winrt::plugins::firebase_core {

// Package-private state shared by the foreground app and its background task.
// Call from a worker: a transaction holds an exclusive file handle across the
// read, update, flush and atomic replacement. Contention is reported for retry;
// neither a corrupt file nor a failed update is treated as an empty store.
class PrivateStateFile {
 public:
  using Bytes = std::vector<uint8_t>;
  // Return S_FALSE for a read-only transaction, S_OK to commit replacement.
  using Update = std::function<HRESULT(const Bytes&, bool missing, Bytes&)>;
  static constexpr size_t kMaxBytes = 16 * 1024 * 1024;

  HRESULT Initialize() {
    using namespace ABI::Windows::Storage;
    HSTRING name = nullptr;
    const wchar_t* type = L"Windows.Storage.ApplicationData";
    HRESULT hr =
        WindowsCreateString(type, static_cast<UINT32>(wcslen(type)), &name);
    ComPtr<IApplicationDataStatics> factory;
    if (SUCCEEDED(hr))
      hr = RoGetActivationFactory(name, IID_PPV_ARGS(&factory));
    WindowsDeleteString(name);
    ComPtr<IApplicationData> data;
    ComPtr<IStorageFolder> folder;
    ComPtr<IStorageItem> item;
    HSTRING path = nullptr;
    if (SUCCEEDED(hr)) hr = factory->get_Current(&data);
    if (SUCCEEDED(hr)) hr = data->get_LocalFolder(&folder);
    if (SUCCEEDED(hr)) hr = folder.As(&item);
    if (SUCCEEDED(hr)) hr = item->get_Path(&path);
    if (SUCCEEDED(hr)) {
      UINT32 size = 0;
      const auto* text = WindowsGetStringRawBuffer(path, &size);
      path_.assign(text, size);
      path_ += L"\\flutter-firebase-state-v1.bin";
    }
    WindowsDeleteString(path);
    return hr;
  }

  HRESULT Transaction(const Update& update) const {
    if (path_.empty()) return E_UNEXPECTED;
    File lock(CreateFile2((path_ + L".lock").c_str(),
                          GENERIC_READ | GENERIC_WRITE, 0, OPEN_ALWAYS,
                          nullptr));
    if (!lock.valid()) return LastError();
    Bytes current, replacement;
    bool missing = false;
    HRESULT hr = Read(current, missing);
    if (FAILED(hr)) return hr;
    hr = update(current, missing, replacement);
    if (hr != S_OK) return hr;
    if (replacement.size() > kMaxBytes)
      return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto temporary = path_ + L".new";
    hr = Write(temporary, replacement);
    if (SUCCEEDED(hr) &&
        !MoveFileExW(temporary.c_str(), path_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      hr = LastError();
    }
    if (FAILED(hr)) DeleteFileW(temporary.c_str());
    return hr;
  }

 private:
  class File {
   public:
    explicit File(HANDLE value) : value_(value) {}
    ~File() {
      if (valid()) CloseHandle(value_);
    }
    bool valid() const { return value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return value_; }
    File(const File&) = delete;
    File& operator=(const File&) = delete;

   private:
    HANDLE value_;
  };
  static HRESULT LastError() {
    DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
  }
  HRESULT Read(Bytes& bytes, bool& missing) const {
    File file(CreateFile2(path_.c_str(), GENERIC_READ, FILE_SHARE_READ,
                          OPEN_EXISTING, nullptr));
    if (!file.valid()) {
      const DWORD error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND) {
        missing = true;
        return S_OK;
      }
      return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size)) return LastError();
    if (size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > kMaxBytes)
      return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    bytes.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset != bytes.size()) {
      DWORD read = 0;
      if (!ReadFile(file.get(), bytes.data() + offset,
                    static_cast<DWORD>(bytes.size() - offset), &read, nullptr))
        return LastError();
      if (!read) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
      offset += read;
    }
    return S_OK;
  }
  static HRESULT Write(const std::wstring& path, const Bytes& bytes) {
    File file(
        CreateFile2(path.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, nullptr));
    if (!file.valid()) return LastError();
    size_t offset = 0;
    while (offset != bytes.size()) {
      DWORD written = 0;
      if (!WriteFile(file.get(), bytes.data() + offset,
                     static_cast<DWORD>(bytes.size() - offset), &written,
                     nullptr))
        return LastError();
      if (!written) return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
      offset += written;
    }
    return FlushFileBuffers(file.get()) ? S_OK : LastError();
  }
  std::wstring path_;
};
inline StateTransaction NativeStateTransaction(
    const char* error_code = "storage-error") {
  return [code = std::string(error_code)](const StateUpdate& update) {
    PrivateStateFile file;
    HRESULT hr = file.Initialize();
    if (SUCCEEDED(hr)) {
      hr = file.Transaction([&](const auto& current, bool missing, auto& next) {
        return update(current, missing, next) ? S_OK : S_FALSE;
      });
    }
    if (FAILED(hr)) throw StateError(code.c_str());
  };
}
}  // namespace flutter::winrt::plugins::firebase_core
