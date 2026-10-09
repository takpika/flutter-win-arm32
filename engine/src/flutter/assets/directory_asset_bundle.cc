// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/assets/directory_asset_bundle.h"

#include <regex>
#include <utility>

#include "flutter/fml/eintr_wrapper.h"
#include "flutter/fml/file.h"
#include "flutter/fml/mapping.h"
#include "flutter/fml/trace_event.h"
#if defined(FLUTTER_WINDOWS_PHONE)
#include <windows.h>
#include "flutter/fml/platform/win/wstring_conversion.h"
#endif

namespace flutter {

DirectoryAssetBundle::DirectoryAssetBundle(
    fml::UniqueFD descriptor,
    bool is_valid_after_asset_manager_change
#if defined(FLUTTER_WINDOWS_PHONE)
    , std::string absolute_assets_path
#endif
    )
    : descriptor_(std::move(descriptor))
#if defined(FLUTTER_WINDOWS_PHONE)
    , absolute_assets_path_(std::move(absolute_assets_path))
#endif
    {
  if (!fml::IsDirectory(descriptor_)) {
    return;
  }
  is_valid_after_asset_manager_change_ = is_valid_after_asset_manager_change;
  is_valid_ = true;
}

DirectoryAssetBundle::~DirectoryAssetBundle() = default;

// |AssetResolver|
bool DirectoryAssetBundle::IsValid() const {
  return is_valid_;
}

// |AssetResolver|
bool DirectoryAssetBundle::IsValidAfterAssetManagerChange() const {
  return is_valid_after_asset_manager_change_;
}

// |AssetResolver|
AssetResolver::AssetResolverType DirectoryAssetBundle::GetType() const {
  return AssetResolver::AssetResolverType::kDirectoryAssetBundle;
}

// |AssetResolver|
std::unique_ptr<fml::Mapping> DirectoryAssetBundle::GetAsMapping(
    const std::string& asset_name) const {
  if (!is_valid_) {
    FML_DLOG(WARNING) << "Asset bundle was not valid.";
    return nullptr;
  }

  // Mobile permits opening package files by the known package path but denies
  // GetFinalPathNameByHandle, which desktop FML uses for relative opens.
#if defined(FLUTTER_WINDOWS_PHONE)
  auto file = absolute_assets_path_.empty()
      ? fml::OpenFile(descriptor_, asset_name.c_str(), false, fml::FilePermission::kRead)
      : fml::OpenFile((absolute_assets_path_ + "/" + asset_name).c_str(), false, fml::FilePermission::kRead);
  auto mapping = std::make_unique<fml::FileMapping>(file);
#else
  auto mapping = std::make_unique<fml::FileMapping>(fml::OpenFile(
      descriptor_, asset_name.c_str(), false, fml::FilePermission::kRead));
#endif

  if (!mapping->IsValid()) {
    return nullptr;
  }

  return mapping;
}

std::vector<std::unique_ptr<fml::Mapping>> DirectoryAssetBundle::GetAsMappings(
    const std::string& asset_pattern,
    const std::optional<std::string>& subdir) const {
  std::vector<std::unique_ptr<fml::Mapping>> mappings;
  if (!is_valid_) {
    FML_DLOG(WARNING) << "Asset bundle was not valid.";
    return mappings;
  }

  std::regex asset_regex(asset_pattern);
#if defined(FLUTTER_WINDOWS_PHONE)
  if (!absolute_assets_path_.empty()) {
    std::vector<std::string> directories{absolute_assets_path_ + (subdir ? "/" + *subdir : "")};
    while (!directories.empty()) {
      auto directory = std::move(directories.back());
      directories.pop_back();
      WIN32_FIND_DATAW entry{};
      auto pattern = fml::Utf8ToWideString(directory + "/*");
      HANDLE search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &entry, FindExSearchNameMatch, nullptr, 0);
      if (search == INVALID_HANDLE_VALUE) continue;
      do {
        auto name = fml::WideStringToUtf8(entry.cFileName);
        if (name == "." || name == "..") continue;
        auto full_path = directory + "/" + name;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          if (!subdir && !(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) directories.push_back(full_path);
        } else if (std::regex_match(name, asset_regex)) {
          auto file = fml::OpenFile(full_path.c_str(), false, fml::FilePermission::kRead);
          auto mapping = std::make_unique<fml::FileMapping>(file);
          if (mapping->IsValid()) mappings.push_back(std::move(mapping));
        }
      } while (FindNextFileW(search, &entry));
      FindClose(search);
    }
    return mappings;
  }
#endif
  fml::FileVisitor visitor = [&](const fml::UniqueFD& directory,
                                 const std::string& filename) {
    TRACE_EVENT0("flutter", "DirectoryAssetBundle::GetAsMappings FileVisitor");

    if (std::regex_match(filename, asset_regex)) {
      TRACE_EVENT0("flutter", "Matched File");

      fml::UniqueFD fd = fml::OpenFile(directory, filename.c_str(), false,
                                       fml::FilePermission::kRead);

      if (fml::IsDirectory(fd)) {
        return true;
      }

      auto mapping = std::make_unique<fml::FileMapping>(fd);

      if (mapping && mapping->IsValid()) {
        mappings.push_back(std::move(mapping));
      } else {
        FML_LOG(ERROR) << "Mapping " << filename << " failed";
      }
    }
    return true;
  };
  if (!subdir) {
    fml::VisitFilesRecursively(descriptor_, visitor);
  } else {
    fml::UniqueFD subdir_fd =
        fml::OpenFileReadOnly(descriptor_, subdir.value().c_str());
    if (!fml::IsDirectory(subdir_fd)) {
      FML_LOG(ERROR) << "Subdirectory path " << subdir.value()
                     << " is not a directory";
      return mappings;
    }
    fml::VisitFiles(subdir_fd, visitor);
  }

  return mappings;
}

bool DirectoryAssetBundle::operator==(const AssetResolver& other) const {
  auto other_bundle = other.as_directory_asset_bundle();
  if (!other_bundle) {
    return false;
  }
  return is_valid_after_asset_manager_change_ ==
             other_bundle->is_valid_after_asset_manager_change_ &&
         descriptor_.get() == other_bundle->descriptor_.get();
}

}  // namespace flutter
