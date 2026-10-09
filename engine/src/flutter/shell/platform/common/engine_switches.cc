// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/common/engine_switches.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>

namespace flutter {

std::vector<std::string> GetSwitchesFromEnvironment() {
  std::vector<std::string> switches;
  const char* switch_count_key = "FLUTTER_ENGINE_SWITCHES";
  const int kMaxSwitchCount = 50;
  const char* switch_count_string = std::getenv(switch_count_key);
  if (!switch_count_string) {
    return switches;
  }
  // In release mode, allow only a small whitelist for diagnostic/compat
  // scenarios.
#ifdef FLUTTER_RELEASE
  const std::set<std::string> kReleaseAllowedSwitches = {
      "enable-software-rendering",
      "enable-impeller=true",
      "enable-impeller=false",
      "disable-impeller",
  };
#endif

  int switch_count = std::min(kMaxSwitchCount, atoi(switch_count_string));
  for (int i = 1; i <= switch_count; ++i) {
    std::ostringstream switch_key;
    switch_key << "FLUTTER_ENGINE_SWITCH_" << i;
    const char* switch_value = std::getenv(switch_key.str().c_str());
    if (switch_value) {
#ifdef FLUTTER_RELEASE
      if (kReleaseAllowedSwitches.find(switch_value) ==
          kReleaseAllowedSwitches.end()) {
        continue;
      }
#endif
      std::ostringstream switch_value_as_flag;
      switch_value_as_flag << "--" << switch_value;
      switches.push_back(switch_value_as_flag.str());
    } else {
      std::cerr << switch_count << " keys expected from " << switch_count_key
                << ", but " << switch_key.str() << " is missing." << std::endl;
    }
  }
  return switches;
}

}  // namespace flutter
