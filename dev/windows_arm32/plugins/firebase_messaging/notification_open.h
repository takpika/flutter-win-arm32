// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace flutter::winrt::plugins::firebase_messaging {
// A CoreWindow may receive its first OS activation before its engine exists.
// Preserve the original OS launch argument across that boundary. Only the SDK
// interprets modern or legacy activation strings. A headless engine never
// attaches here and cannot turn a cold foreground launch into a resumed one.
class NotificationOpen {
 public:
  struct Event {
    std::string launch;
    bool initial;
  };
  using Receive = std::function<bool(Event)>;
  static void Submit(Event event) {
    if (event.launch.empty() || event.launch.find('\0') != std::string::npos)
      return;
    std::shared_ptr<NotificationOpen> current;
    std::deque<Event> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      current = current_.lock();
      if (!current) {
        pending_.push_back(event);
        return;
      }
      pending.swap(pending_);
    }
    pending.push_back(event);
    for (const auto& queued : pending)
      if (!current->receive_(queued)) Requeue(queued);
  }
  static void Requeue(Event event) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(event);
  }
  static std::shared_ptr<NotificationOpen> Attach(Receive receive) {
    auto result = std::shared_ptr<NotificationOpen>(
        new NotificationOpen(std::move(receive)));
    std::deque<Event> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      current_ = result;
      pending.swap(pending_);
    }
    for (const auto& event : pending)
      if (!result->receive_(event)) Requeue(event);
    return result;
  }

 private:
  explicit NotificationOpen(Receive receive) : receive_(std::move(receive)) {}
  Receive receive_;
  inline static std::mutex mutex_;
  inline static std::weak_ptr<NotificationOpen> current_;
  inline static std::deque<Event> pending_;
};
}  // namespace flutter::winrt::plugins::firebase_messaging
