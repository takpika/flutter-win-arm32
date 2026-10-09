// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../notification_activation_store.h"

#include <cassert>
#include <cstdlib>
using namespace flutter::winrt::plugins;
namespace fm = firebase_messaging;
namespace fc = firebase_core;
namespace {
std::string payload =
    R"({"messageId":"synthetic","data":{"日本語":"通知"},"notification":{"title":"開く"},"future":[1,true]})";
bool processed = false, queued = false, fail_queue = false;
int lookups = 0, frees = 0;
int saved_opens = 0, saved_closes = 0;
HRESULT saved_result = S_OK;
void Copy(fcm_sdk_buffer* out) {
  out->size = payload.size();
  out->data = static_cast<uint8_t*>(malloc(out->size));
  memcpy(out->data, payload.data(), out->size);
}
}  // namespace
extern "C" {
const char* fcm_sdk_error_name(HRESULT) { return "synthetic-error"; }
HRESULT fcm_sdk_open_saved(const char* name, fcm_sdk_handle* client) {
  assert(std::string(name) == "[DEFAULT]");
  ++saved_opens;
  *client = saved_result == S_OK ? 7 : 0;
  return saved_result;
}
HRESULT fcm_sdk_close(fcm_sdk_handle client) {
  assert(client == 7);
  ++saved_closes;
  return S_OK;
}
HRESULT fcm_sdk_queue_activation(fcm_sdk_handle client, const char* launch,
                                 int64_t* receipt) {
  assert(client == 7 && std::string(launch) == "opaque-OS-launch");
  if (fail_queue) return E_FAIL;
  if (processed) return 1;
  queued = true;
  *receipt = 41;
  return S_OK;
}
HRESULT fcm_sdk_lookup(fcm_sdk_handle client, int64_t receipt,
                       fcm_sdk_buffer* out) {
  assert(client == 7 && receipt == 41 && queued);
  ++lookups;
  Copy(out);
  return S_OK;
}
HRESULT fcm_sdk_lookup_activation(fcm_sdk_handle client, const char* launch,
                                  int64_t* receipt, fcm_sdk_buffer* out) {
  assert(client == 7 && std::string(launch) == "opaque-OS-launch");
  ++lookups;
  if (!queued) return 1;
  *receipt = 41;
  Copy(out);
  return S_OK;
}
void fcm_sdk_buffer_free(fcm_sdk_buffer* value) {
  if (value->data) {
    ++frees;
    free(value->data);
  }
  *value = {};
}
}
int main() {
  fc::StateBytes disk;
  bool missing = true;
  int commits = 0, fail_at = -1;
  fc::StateTransaction transaction = [&](const fc::StateUpdate& update) {
    fc::StateBytes next;
    if (update(disk, missing, next)) {
      if (++commits == fail_at) throw fc::StateError("synthetic-write-failure");
      disk = std::move(next);
      missing = false;
    }
  };
  auto fails = [&](auto action) {
    bool caught = false;
    try {
      action();
    } catch (const fc::StateError&) {
      caught = true;
    }
    assert(caught);
  };
  const fm::MessagingAppIdentity app{"[DEFAULT]", "project", "android-app"};
  fm::NotificationActivationStore activations(transaction);
  fm::InitialMessageCache initial(transaction);
  // Readable-but-already-processed SDK payload must never become initial again.
  processed = true;
  activations.Observe("opaque-OS-launch", true);
  activations.Queue(7, app);
  assert(initial.Pending(app).empty() && lookups == 0);
  // SDK queue failure keeps the original OS intent; it cannot fabricate data.
  processed = false;
  fail_queue = true;
  activations.Observe("opaque-OS-launch", true);
  fails([&] { activations.Queue(7, app); });
  assert(initial.Pending(app).empty());
  fail_queue = false;
  // Simulate process death after SDK queue acceptance, before Flutter's full
  // payload commit. Recovery can correlate the saved OS intent using only the
  // public SDK lookup, and persists the complete callback before accepting it.
  fail_at = commits + 1;
  fails([&] { activations.Queue(7, app); });
  assert(queued && initial.Pending(app).empty());
  fail_at = -1;
  fm::NotificationActivationStore restarted(transaction);
  assert(restarted.CaptureOpened(
      7, app, 41, reinterpret_cast<const uint8_t*>(payload.data()),
      payload.size()));
  assert(initial.Pending(app) == std::vector<int64_t>{41});
  assert(initial.Take(app, 41) ==
         fm::DecodeSdkRemoteMessage(
             reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
  // Crash after callback cache commit, before SDK opened ACK, is idempotent
  // even when getInitialMessage already consumed it. SDK replay never
  // resurrects it.
  assert(restarted.CaptureOpened(
      7, app, 41, reinterpret_cast<const uint8_t*>(payload.data()),
      payload.size()));
  assert(initial.Pending(app).empty() && !initial.Take(app, 41));
  processed = true;
  restarted.Observe("opaque-OS-launch", true);
  restarted.Queue(7, app);
  assert(initial.Pending(app).empty());
  // Warm taps cannot enter cold getInitialMessage custody.
  disk.clear();
  missing = true;
  processed = false;
  queued = false;
  restarted.Observe("opaque-OS-launch", false);
  restarted.Queue(7, app);
  assert(!restarted.CaptureOpened(
      7, app, 41, reinterpret_cast<const uint8_t*>(payload.data()),
      payload.size()));
  assert(initial.Pending(app).empty());
  // Normal cold queue commits the public payload before an immediate Dart
  // getInitialMessage RPC, without waiting for asynchronous SDK drain
  // callbacks.
  disk.clear();
  missing = true;
  queued = false;
  restarted.Observe("opaque-OS-launch", true);
  restarted.Queue(7, app);
  assert(initial.Pending(app) == std::vector<int64_t>{41});
  assert(initial.Take(app, 41) && initial.Pending(app).empty());
  // The real OS tap protects the saved SDK receipt before any Core/Dart app
  // exists. Full message custody and consumption still wait for Core identity.
  disk.clear();
  missing = true;
  queued = false;
  const int before_lookup = lookups;
  restarted.ProtectSaved("[DEFAULT]");
  assert(saved_opens == 0);
  restarted.Observe("opaque-OS-launch", true);
  saved_result = 1;  // First launch has no SDK descriptor yet.
  restarted.ProtectSaved("[DEFAULT]");
  assert(!queued && saved_closes == 0 && lookups == before_lookup);
  saved_result = E_FAIL;
  fails([&] { restarted.ProtectSaved("[DEFAULT]"); });
  assert(!queued && saved_closes == 0);
  saved_result = S_OK;
  fail_queue = true;
  fails([&] { restarted.ProtectSaved("[DEFAULT]"); });
  assert(saved_closes == 1 && !queued);
  fail_queue = false;
  restarted.ProtectSaved("[DEFAULT]");
  assert(queued && saved_closes == 2 && lookups == before_lookup &&
         initial.Pending(app).empty());
  restarted.ProtectSaved("[DEFAULT]");
  assert(saved_closes == 3 && initial.Pending(app).empty());
  restarted.Queue(7, app);
  assert(initial.Pending(app) == std::vector<int64_t>{41});
  assert(initial.Take(app, 41));
  processed = true;
  restarted.Observe("opaque-OS-launch", true);
  restarted.ProtectSaved("[DEFAULT]");
  const int prior_open = saved_opens;
  restarted.ProtectSaved("[DEFAULT]");
  assert(saved_opens == prior_open && initial.Pending(app).empty());
  assert(frees == lookups);
}
