// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"

namespace flutter::winrt::host {
// A view-independent SDK host for a retained entrypoint in the current package's
// AOT. Construct, pump and destroy on one WinRT-initialized platform thread.
// Plugins are registered from the same generated registrant as the UI host.
class HeadlessEngine {
 public:
  struct Entrypoint {
    std::string library_uri, function;
  };
  using Post = std::function<bool(std::function<void()>)>;
  HeadlessEngine();
  ~HeadlessEngine();
  HeadlessEngine(const HeadlessEngine&) = delete;
  HeadlessEngine& operator=(const HeadlessEngine&) = delete;
  HRESULT Start(Entrypoint entrypoint);
  BinaryMessenger& messenger();
  bool PumpUntil(const std::function<bool()>& complete,
                 std::chrono::steady_clock::time_point deadline,
                 const std::function<bool()>& canceled);

 private:
  class State;
  std::unique_ptr<State> state_;
};
}  // namespace flutter::winrt::host
