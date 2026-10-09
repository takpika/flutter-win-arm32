// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <cassert>
#include <iostream>
#include <string>

#include "flutter/lib/ui/plugins/callback_cache.h"

int main(int argc, char** argv) {
  assert(argc >= 3);
  flutter::DartCallbackCache::SetCachePath(argv[2]);
  if (std::string(argv[1]) == "write") {
    flutter::DartCallbackCache::GetCallbackHandle(
        "syntheticCallback", "", "package:synthetic/background.dart");
  } else {
    assert(std::string(argv[1]) == "read" && argc == 4);
    const int64_t handle = std::stoll(argv[3]);
    assert(!flutter::DartCallbackCache::GetCallbackInformation(handle));
    flutter::DartCallbackCache::LoadCacheFromDisk();
    auto information =
        flutter::DartCallbackCache::GetCallbackInformation(handle);
    assert(information && information->name == "syntheticCallback" &&
           information->class_name.empty() &&
           information->library_path == "package:synthetic/background.dart");
    std::cout
        << "Original Dart callback cache restored in a new host process PASS\n";
  }
}
