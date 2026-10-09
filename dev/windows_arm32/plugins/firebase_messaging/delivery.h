// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <flutter/encodable_value.h>

#include <functional>
#include <memory>
#include <mutex>

namespace flutter::winrt::plugins::firebase_messaging {
// The background activation and foreground engine have independent lifetimes.
// A copied receiver only posts through the worker's weak lifetime guard.
class ForegroundReceiver {
 public:
  using Complete = std::function<void(bool)>;
  using Receive =
      std::function<bool(std::string, std::string, EncodableValue, Complete)>;
  static std::shared_ptr<ForegroundReceiver> Attach(Receive receive) {
    auto result = std::shared_ptr<ForegroundReceiver>(
        new ForegroundReceiver(std::move(receive)));
    std::lock_guard<std::mutex> lock(mutex_);
    current_ = result;
    return result;
  }
  static bool Deliver(std::string app, std::string method,
                      EncodableValue message, Complete complete) {
    std::shared_ptr<ForegroundReceiver> receiver;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      receiver = current_.lock();
    }
    return receiver &&
           receiver->receive_(std::move(app), std::move(method),
                              std::move(message), std::move(complete));
  }

 private:
  explicit ForegroundReceiver(Receive receive) : receive_(std::move(receive)) {}
  Receive receive_;
  inline static std::mutex mutex_;
  inline static std::weak_ptr<ForegroundReceiver> current_;
};
}  // namespace flutter::winrt::plugins::firebase_messaging
