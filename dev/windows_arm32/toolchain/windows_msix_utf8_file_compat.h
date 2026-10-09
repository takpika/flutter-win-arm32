#pragma once

// MSIX's cross-platform file contract takes UTF-8 names. Adapt its Windows
// narrow CRT call to the wide CRT API, independently of the host ANSI locale.
// This header is force-included only in the private MSIX host-tool build.
#include <windows.h>

#include <cerrno>
#include <cstdio>
#include <string>

inline errno_t flutter_msix_utf8_fopen_s(FILE** file, const char* name,
                                       const char* mode) {
  if (file) *file = nullptr;
  if (!file || !name || !mode) {
    errno = EINVAL;
    return EINVAL;
  }
  const int name_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           name, -1, nullptr, 0);
  const int mode_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           mode, -1, nullptr, 0);
  if (!name_size || !mode_size) {
    errno = EINVAL;
    return EINVAL;
  }
  std::wstring wide_name(name_size, L'\0');
  std::wstring wide_mode(mode_size, L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1,
                         &wide_name[0], name_size) != name_size ||
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1,
                         &wide_mode[0], mode_size) != mode_size) {
    errno = EINVAL;
    return EINVAL;
  }
  return _wfopen_s(file, wide_name.c_str(), wide_mode.c_str());
}

#define fopen_s flutter_msix_utf8_fopen_s
