// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../sdk_message_handler.h"

#include <atomic>
#include <cassert>
#include <future>
#include <thread>

using namespace flutter::winrt::plugins;
using firebase_messaging::SdkMessageHandler;

namespace {
int foreground = 1;
HRESULT visibility_result = S_OK;
}
extern "C" HRESULT fcm_sdk_get_foreground_state(int* value) {
  *value = foreground;
  return visibility_result;
}

int main() {
  firebase_core::StateBytes bytes{'{', '"', 'v', 'e', 'r', 's', 'i', 'o', 'n',
                                 '"', ':', '1', '}'};
  auto transaction = [&bytes](const firebase_core::StateUpdate& update) {
    firebase_core::StateBytes next;
    if (update(bytes, false, next)) bytes = std::move(next);
  };
  const firebase_messaging::MessagingAppIdentity app{
      "[DEFAULT]", "project", "android-fis"};
  firebase_messaging::InitialMessageCache cache(transaction);
  const std::string json = R"({"messageId":"synthetic","data":{"value":"日本語"}})";
  auto data = reinterpret_cast<const uint8_t*>(json.data());
  bool canceled = false;
  unsigned emitted = 0, background = 0;
  SdkMessageHandler::Completion pending;
  std::string method;
  SdkMessageHandler handler(app, cache,
      [&](const std::string& name, flutter::EncodableValue value, auto reply) {
        ++emitted;
        method = name;
        assert(std::get<flutter::EncodableMap>(value).count(
            flutter::EncodableValue("messageId")));
        reply(true);
        return true;
      },
      [&](const auto& message, const auto& stop) {
        assert(!stop() && message.count(flutter::EncodableValue("messageId")));
        ++background;
        return S_OK;
      });
  assert(handler.Dispatch(FCM_SDK_MESSAGE, 1, data, json.size(),
                          [&] { return canceled; }) == S_OK);
  assert(emitted == 1 && method == "Messaging#onMessage");
  assert(handler.Dispatch(FCM_SDK_BACKGROUND_OBSERVER, 1, data, json.size(),
                          [&] { return canceled; }) == S_OK);
  assert(background == 1 && emitted == 1);
  foreground = 0;
  assert(handler.Dispatch(FCM_SDK_MESSAGE, 1, data, json.size(),
                          [&] { return canceled; }) == S_OK);
  assert(background == 2 && emitted == 1);
  visibility_result = HRESULT_FROM_WIN32(ERROR_NOT_READY);
  assert(handler.Dispatch(FCM_SDK_MESSAGE, 1, data, json.size(),
                          [&] { return canceled; }) == visibility_result);
  assert(background == 2 && emitted == 1);
  visibility_result = S_OK;
  foreground = 1;
  cache.Save(app, 2, json);
  assert(handler.Dispatch(FCM_SDK_NOTIFICATION_OPENED, 2, data, json.size(),
                          [&] { return canceled; }) == S_OK);
  assert(emitted == 1 && cache.Take(app, 2));
  assert(handler.Dispatch(FCM_SDK_NOTIFICATION_OPENED, 3, data, json.size(),
                          [&] { return canceled; }) == S_OK);
  assert(method == "Messaging#onMessageOpenedApp" && emitted == 2);
  canceled = true;
  assert(handler.Dispatch(FCM_SDK_MESSAGE, 1, data, json.size(),
                          [&] { return canceled; }) == E_ABORT);
  assert(emitted == 2);
  canceled = false;
  assert(handler.Dispatch(99, 1, data, json.size(),
                          [&] { return canceled; }) == E_INVALIDARG);

  // The SDK worker can wait for a reply handled by the independent serialized
  // plugin worker. Synchronize the test explicitly rather than guessing timing.
  std::promise<void> submitted;
  auto queued = submitted.get_future();
  std::atomic<bool> stop{false};
  SdkMessageHandler asynchronous(app, cache,
      [&](const auto&, auto, auto reply) {
        pending = std::move(reply);
        submitted.set_value();
        return true;
      }, {});
  auto dispatch = std::async(std::launch::async, [&] {
    return asynchronous.Dispatch(FCM_SDK_MESSAGE, 4, data, json.size(),
                                  [&] { return stop.load(); });
  });
  queued.get();
  pending(true);  // Simulates the serialized worker receiving Dart's reply.
  assert(dispatch.get() == S_OK);

  std::promise<void> submitted_again;
  auto queued_again = submitted_again.get_future();
  SdkMessageHandler::Completion late;
  {
    SdkMessageHandler canceled_handler(app, cache,
        [&](const auto&, auto, auto reply) {
          late = std::move(reply);
          submitted_again.set_value();
          return true;
        }, {});
    auto interrupted = std::async(std::launch::async, [&] {
      return canceled_handler.Dispatch(FCM_SDK_MESSAGE, 5, data, json.size(),
                                         [&] { return stop.load(); });
    });
    queued_again.get();
    stop = true;
    assert(interrupted.get() == E_ABORT);
  }
  // The engine reply may arrive after SDK scope cancellation/context release.
  late(true);
  late(false);
}
