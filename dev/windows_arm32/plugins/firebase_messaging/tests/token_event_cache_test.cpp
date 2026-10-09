// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../token_event_cache.h"

#include <cassert>

using namespace flutter::winrt::plugins;
namespace {
struct Store {
  firebase_core::StateBytes bytes;
  bool fail = false;
  unsigned commits = 0;
  Store() {
    const std::string original =
        R"({"version":1,"installations":[{"opaque":"SDK-owned"}],"coreSettings":{"resourceManagement":true}})";
    bytes.assign(original.begin(), original.end());
  }
  firebase_core::StateTransaction Transaction() {
    return [this](const auto& update) {
      firebase_core::StateBytes next;
      if (!update(bytes, false, next)) return;
      if (fail) throw firebase_core::StateError("synthetic-write-failure");
      bytes = std::move(next);
      ++commits;
    };
  }
};
template <class Call> void Reject(Call call) {
  bool failed = false;
  try { call(); } catch (const firebase_core::StateError&) { failed = true; }
  assert(failed);
}
}  // namespace

int main() {
  Store store;
  const firebase_messaging::MessagingAppIdentity app{
      "[DEFAULT]", "project", "android-fis"};
  const firebase_messaging::MessagingAppIdentity other{
      "[DEFAULT]", "other-project", "android-fis"};
  firebase_messaging::TokenEventCache cache(store.Transaction());
  assert(!cache.Read(app));
  std::string borrowed = "synthetic-token1";
  cache.Save(app, {1, borrowed});
  borrowed.assign(borrowed.size(), '!');
  assert(cache.Read(app)->token == "synthetic-token1");
  assert(!cache.Read(other));
  // Native custody survives the engine/process owner and its borrowed buffer.
  firebase_messaging::TokenEventCache restarted(store.Transaction());
  assert(restarted.Read(app)->generation == 1);
  restarted.Save(app, {2, "synthetic-token2"});
  restarted.Complete(app, 1);  // Late Dart reply for an earlier event.
  assert(restarted.Read(app)->generation == 2);
  const auto committed = store.commits;
  restarted.Save(app, {1, "synthetic-token1"});
  assert(store.commits == committed);
  assert(restarted.Read(app)->token == "synthetic-token2");
  Reject([&] { restarted.Save(app, {2, "conflicting-token"}); });

  store.fail = true;
  const auto before = store.bytes;
  Reject([&] { restarted.Save(app, {3, "synthetic-token3"}); });
  assert(store.bytes == before && restarted.Read(app)->generation == 2);
  Reject([&] { restarted.Complete(app, 2); });
  assert(restarted.Read(app));
  store.fail = false;
  restarted.Complete(app, 2);
  assert(!restarted.Read(app));
  restarted.Save(app, {2, "synthetic-token2"});
  assert(!restarted.Read(app));  // Consumed generations never revive.
  restarted.Save(other, {2, "other-project-token"});
  assert(restarted.Read(other));
  assert(!restarted.Read(app));
  restarted.Save(app, {3, "synthetic-token3"});
  assert(restarted.Read(app)->generation == 3);

  const auto document = firebase_core::StateDocument::Parse(store.bytes, false);
  assert(document["installations"][0]["opaque"] == "SDK-owned");
  assert(document["coreSettings"]["resourceManagement"].GetBool());
  Reject([&] { restarted.Save(app, {0, "invalid"}); });
  Reject([&] { restarted.Save(app, {4, std::string("a\0b", 3)}); });
}
