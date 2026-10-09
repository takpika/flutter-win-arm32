// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../notification_open.h"

#include <cassert>
#include <iostream>
#include <vector>

using flutter::winrt::plugins::firebase_messaging::NotificationOpen;
int main() {
  std::vector<NotificationOpen::Event> events;
  NotificationOpen::Submit({"", true});
  NotificationOpen::Submit({std::string("bad\0launch", 10), true});
  NotificationOpen::Submit({"opaque-OS-argument-17", true});
  auto receiver = NotificationOpen::Attach([&](auto event) {
    events.push_back(event);
    return true;
  });
  assert(events.size() == 1 && events[0].launch == "opaque-OS-argument-17" &&
         events[0].initial);
  NotificationOpen::Submit({"opaque-OS-argument-18", false});
  assert(events.size() == 2 && events[1].launch == "opaque-OS-argument-18" &&
         !events[1].initial);
  receiver.reset();
  NotificationOpen::Submit({"opaque-OS-argument-19", true});
  auto rejecting = NotificationOpen::Attach([](auto) { return false; });
  NotificationOpen::Submit({"opaque-OS-argument-20", false});
  rejecting.reset();
  receiver = NotificationOpen::Attach([&](auto event) {
    events.push_back(event);
    return true;
  });
  assert(events.size() == 4 && events[2].launch == "opaque-OS-argument-19" &&
         events[2].initial && events[3].launch == "opaque-OS-argument-20" &&
         !events[3].initial);
  // Models an accepted worker task canceled before it can touch its Service.
  NotificationOpen::Requeue({"opaque-OS-argument-21", false});
  receiver.reset();
  receiver = NotificationOpen::Attach([&](auto event) {
    events.push_back(event);
    return true;
  });
  assert(events.size() == 5 && events[4].launch == "opaque-OS-argument-21" &&
         !events[4].initial);
  std::cout
      << "Notification activation: unaltered OS arguments, cold/warm routing, "
         "weak receiver and canceled worker retry PASS\n";
}
