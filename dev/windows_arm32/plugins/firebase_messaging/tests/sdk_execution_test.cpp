// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../sdk_execution.h"

#include <cassert>
using namespace flutter::winrt::plugins;
using firebase_messaging::SdkExecution;
using firebase_messaging::SdkMessageHandler;
extern "C" HRESULT fcm_sdk_get_foreground_state(int* value) {
  *value = 1;
  return S_OK;
}
namespace {
fcm_sdk_background_callbacks held{};
std::weak_ptr<void> owner;
bool fail = false, immediate = false, sdk_canceled = false;
unsigned completions = 0;
int Canceled(const void*) {
  return sdk_canceled || (held.callbacks.canceled &&
                          held.callbacks.canceled(held.callbacks.context));
}
}  // namespace
extern "C" HRESULT fcm_sdk_start_drain(
    fcm_sdk_handle client, const fcm_sdk_background_callbacks* callbacks,
    uint32_t limit, fcm_sdk_operation* operation) {
  assert(client == 7 && limit == 32 && callbacks->size == sizeof(*callbacks));
  if (fail) return E_FAIL;
  held = *callbacks;
  *operation = 17;
  const fcm_sdk_background_cancellation cancellation{nullptr, Canceled};
  held.started(held.callbacks.context, &cancellation);
  if (immediate) {
    fcm_sdk_drain_result result{};
    held.completed(held.callbacks.context, S_OK, &result);
    held.release(held.callbacks.context);
    assert(!owner.expired());
  }
  return S_OK;
}
namespace {
int saved_result = S_OK;
void* expected_task = nullptr;
unsigned saved_calls = 0;
}  // namespace
extern "C" HRESULT fcm_sdk_start_background_saved(
    const char* app, void* task, int kind,
    const fcm_sdk_background_callbacks* callbacks, uint32_t limit) {
  assert(std::string(app) == "[DEFAULT]" && task == expected_task &&
         limit == 32);
  assert(kind == FCM_SDK_DELIVERY || kind == FCM_SDK_RECOVERY);
  ++saved_calls;
  if (saved_result != S_OK) return saved_result;
  held = *callbacks;
  const fcm_sdk_background_cancellation cancellation{nullptr, Canceled};
  held.started(held.callbacks.context, &cancellation);
  return S_OK;
}
int main() {
  firebase_core::StateBytes bytes{'{', '"', 'v', 'e', 'r', 's', 'i',
                                  'o', 'n', '"', ':', '1', '}'};
  auto transaction = [&](const firebase_core::StateUpdate& update) {
    firebase_core::StateBytes next;
    if (update(bytes, false, next)) bytes = std::move(next);
  };
  bool captured_opened = false;
  auto handler = std::make_shared<SdkMessageHandler>(
      firebase_messaging::MessagingAppIdentity{"[DEFAULT]", "project",
                                               "android"},
      firebase_messaging::InitialMessageCache(transaction),
      [](const auto&, auto, auto reply) {
        reply(true);
        return true;
      },
      SdkMessageHandler::Background{},
      [&](fcm_sdk_handle borrowed, int64_t receipt, const uint8_t* json,
          size_t size) {
        assert(borrowed == 999 && receipt == 42);
        captured_opened = true;
        firebase_messaging::InitialMessageCache(transaction)
            .Save({"[DEFAULT]", "project", "android"}, receipt,
                  std::string(reinterpret_cast<const char*>(json), size));
        return true;
      });
  auto closing = std::make_shared<std::atomic<bool>>(false);
  auto object = std::make_shared<int>(42);
  owner = object;
  fcm_sdk_operation operation = 0;
  auto complete = [](HRESULT, fcm_sdk_drain_result) { ++completions; };
  fail = true;
  assert(SdkExecution::Drain(7, 32, handler, closing, object, complete,
                             &operation) == E_FAIL);
  assert(completions == 1 && object.use_count() == 1);
  fail = false;
  immediate = true;
  assert(SdkExecution::Drain(7, 32, handler, closing, object, complete,
                             &operation) == S_OK);
  assert(completions == 2 && object.use_count() == 1);
  immediate = false;
  assert(SdkExecution::Drain(7, 32, handler, closing, object, complete,
                             &operation) == S_OK);
  object.reset();
  assert(!owner.expired());
  const std::string json = R"({"messageId":"synthetic"})";
  auto data = reinterpret_cast<const uint8_t*>(json.data());
  assert(held.callbacks.message(held.callbacks.context, 999, FCM_SDK_MESSAGE, 1,
                                data, json.size()) == S_OK);
  assert(held.callbacks.message(held.callbacks.context, 999,
                                FCM_SDK_NOTIFICATION_OPENED, 42, data,
                                json.size()) == S_OK);
  assert(captured_opened);
  captured_opened = false;
  assert(held.callbacks.message(held.callbacks.context, 999,
                                FCM_SDK_NOTIFICATION_OPENED, 42, data,
                                json.size()) == S_OK);
  assert(!captured_opened);  // Durable custody prevents duplicate handling.
  sdk_canceled = true;
  assert(held.callbacks.message(held.callbacks.context, 999, FCM_SDK_MESSAGE, 1,
                                data, json.size()) == E_ABORT);
  sdk_canceled = false;
  *closing = true;
  assert(held.callbacks.canceled(held.callbacks.context) == 1);
  fcm_sdk_drain_result result{};
  held.completed(held.callbacks.context, E_ABORT, &result);
  held.completed(held.callbacks.context, E_ABORT, &result);
  assert(completions == 3);
  held.release(held.callbacks.context);
  assert(owner.expired());
  // The framework does not resolve the real task, acquire another deferral,
  // spin a worker, or construct the handler on the activation caller thread.
  expected_task = reinterpret_cast<void*>(uintptr_t{0x1234});
  unsigned factories = 0;
  auto factory = [&] {
    ++factories;
    return handler;
  };
  auto lifetime = std::make_shared<int>(43);
  owner = lifetime;
  saved_result = 1;
  assert(SdkExecution::BackgroundSaved("[DEFAULT]", expected_task,
                                       FCM_SDK_RECOVERY, 32, factory, lifetime,
                                       complete) == 1);
  assert(factories == 0 && lifetime.use_count() == 1 && completions == 4);
  saved_result = S_OK;
  assert(SdkExecution::BackgroundSaved("[DEFAULT]", expected_task,
                                       FCM_SDK_DELIVERY, 32, factory, lifetime,
                                       complete) == S_OK);
  assert(factories == 0 && saved_calls == 2);
  lifetime.reset();
  assert(!owner.expired());
  sdk_canceled = false;
  assert(held.callbacks.message(held.callbacks.context, 999, FCM_SDK_MESSAGE, 1,
                                data, json.size()) == S_OK);
  assert(factories == 1);
  sdk_canceled = true;
  assert(held.callbacks.message(held.callbacks.context, 999, FCM_SDK_MESSAGE, 1,
                                data, json.size()) == E_ABORT);
  assert(factories == 1 &&
         held.callbacks.canceled(held.callbacks.context) == 0);
  held.completed(held.callbacks.context, E_ABORT, &result);
  held.release(held.callbacks.context);
  assert(owner.expired() && completions == 5);
}
