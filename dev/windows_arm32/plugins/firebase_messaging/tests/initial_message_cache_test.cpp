// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../initial_message_cache.h"
#include <cassert>
#include <cstdio>

using namespace flutter::winrt::plugins;
using firebase_messaging::MessagingAppIdentity;
using firebase_messaging::InitialMessageCache;

struct State {
  firebase_core::StateBytes bytes;
  bool missing = false, fail_commit = false;
  unsigned commits = 0;
  State() {
    const std::string initial = R"({"version":1,"unrelated":{"callback":23,"keep":"日本語"}})";
    bytes.assign(initial.begin(), initial.end());
  }
  firebase_core::StateTransaction Transaction() {
    return [this](const firebase_core::StateUpdate& update) {
      firebase_core::StateBytes next;
      if (!update(bytes, missing, next)) return;
      if (fail_commit) throw firebase_core::StateError("synthetic-write-failure");
      bytes = std::move(next); missing = false; ++commits;
    };
  }
};

template <typename Task> void Reject(const char* label, Task task) {
  bool failed = false;
  try { task(); } catch (const std::exception&) { failed = true; }
  if (!failed) std::fprintf(stderr, "Expected rejection: %s\n", label);
  assert(failed);
}

int main() {
  State state;
  const MessagingAppIdentity app{"[DEFAULT]", "project", "1:123:android:abc"};
  const MessagingAppIdentity other{"[DEFAULT]", "other", "1:123:android:abc"};
  std::string borrowed = R"({"messageId":"full","sentTime":9223372036854775807,"data":{"日本語":"通知","nul":"a\u0000b"},"notification":{"title":"開く"},"futureExtension":[null,true,1.25]})";
  const auto original = firebase_messaging::DecodeSdkRemoteMessage(
      reinterpret_cast<const uint8_t*>(borrowed.data()), borrowed.size());
  const auto json = borrowed;
  InitialMessageCache cache(state.Transaction());
  cache.Save(app, 41, borrowed);
  borrowed.assign(borrowed.size(), '!');
  assert(cache.Pending(app) == std::vector<int64_t>{41});
  assert(cache.Pending(other).empty());
  assert(cache.Contains(app, 41) && !cache.Contains(other, 41));
  // Reconstructing the adapter needs no SDK Inbox or original callback buffer.
  InitialMessageCache restarted(state.Transaction());
  assert(restarted.Pending(app) == std::vector<int64_t>{41});
  auto message = restarted.Take(app, 41);
  assert(message && *message == original && !restarted.Take(app, 41));
  assert(restarted.Pending(app).empty());
  const auto committed = state.commits;
  restarted.Save(app, 41, json);
  assert(state.commits == committed && !restarted.Take(app, 41));
  restarted.Save(other, 41, json);
  assert(restarted.Take(other, 41) == original);

  state.fail_commit = true;
  Reject("save commit failure", [&] { restarted.Save(app, 42, json); });
  assert(!restarted.Contains(app, 42));
  state.fail_commit = false;
  restarted.Save(app, 42, json);
  const auto before_replay = state.commits;
  restarted.Save(app, 42, " \n" + json + " \n");
  assert(state.commits == before_replay);
  Reject("conflicting pending receipt", [&] { restarted.Save(app, 42, R"({"messageId":"different"})"); });
  state.fail_commit = true;
  Reject("consume commit failure", [&] { restarted.Take(app, 42); });
  state.fail_commit = false;
  assert(restarted.Take(app, 42) == original && !restarted.Take(app, 42));

  const auto document = firebase_core::StateDocument::Parse(state.bytes, false);
  assert(document["unrelated"]["callback"].GetInt() == 23);
  const auto valid = state.bytes;
  state.bytes.push_back(0);state.bytes.push_back('x');
  Reject("raw NUL in persisted state", [&] { restarted.Contains(app, 41); });
  state.bytes = valid;
  Reject("duplicate SDK JSON fields", [&] { restarted.Save(app, 43, R"({"data":{"x":1,"x":2}})"); });
  Reject("invalid receipt", [&] { restarted.Save(app, 0, json); });
  assert(!restarted.Contains(app, 43));
}
