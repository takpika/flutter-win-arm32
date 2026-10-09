// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "background.h"

#include <flutter/method_result_functions.h>
#include <flutter/standard_method_codec.h>
#include <mutex>

#include "../../runner/headless_engine.h"
#include "../firebase_core/configuration_native.h"
#include "../firebase_core/private_state_file.h"
#include "../method_channel.h"
#include "background_state.h"

namespace flutter::winrt::plugins::firebase_messaging {
namespace {
class Apartment {
 public:
  Apartment() : result(RoInitialize(RO_INIT_MULTITHREADED)) {}
  ~Apartment() { if (SUCCEEDED(result)) RoUninitialize(); }
  HRESULT result;
};

std::mutex& BackgroundProcessing() {
  static std::mutex processing;
  return processing;
}

// Only the Flutter callback protocol belongs here. The SDK owns acceptance,
// notification presentation and retries when this helper serves its callback.
HRESULT InvokeBackgroundCallback(const firebase_core::Configuration& configuration,
                                 const EncodableMap& message,
                                 const std::function<bool()>& canceled) {
  const auto registration =
      BackgroundState(firebase_core::NativeStateTransaction()).Read();
  // Same public plugin contract as the original receiver: an app which has
  // not registered onBackgroundMessage has no Dart work to execute.
  if (!registration.callback) return S_OK;
  // A package upgrade can relocate the SDK library. The current AOT's
  // packaged URI is authoritative, never the previous registration's path.
  const auto& dispatcher = configuration.BackgroundDispatcher();
  if (!dispatcher) return E_INVALIDARG;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  bool initialized = false, completed = false, success = false;
  host::HeadlessEngine engine;
  constexpr char channel[] = "plugins.flutter.io/firebase_messaging_background";
  RegisterMethodHandler(engine.messenger(), channel,
      [&](const MethodCall<EncodableValue>& call, auto result) {
        if (call.method_name() != "MessagingBackground#initialized") {
          result->NotImplemented();
          return;
        }
        initialized = true;
        result->Success();
      });
  const HRESULT started = engine.Start({dispatcher->first, dispatcher->second});
  if (FAILED(started)) return started;
  if (!engine.PumpUntil([&] { return initialized; }, deadline, canceled))
    return canceled() ? E_ABORT : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
  const auto& codec = StandardMethodCodec::GetInstance();
  auto call = codec.EncodeMethodCall(MethodCall<EncodableValue>(
      "MessagingBackground#onMessage", std::make_unique<EncodableValue>(EncodableMap{
          {EncodableValue("userCallbackHandle"), EncodableValue(*registration.callback)},
          {EncodableValue("message"), EncodableValue(message)}})));
  engine.messenger().Send(channel, call->data(), call->size(),
      [&](const uint8_t* bytes, size_t size) {
        MethodResultFunctions<EncodableValue> result(
            [&](const EncodableValue*) { success = true; },
            [](const std::string&, const std::string&, const EncodableValue*) {},
            [] {});
        if (size) codec.DecodeAndProcessResponseEnvelope(bytes, size, &result);
        completed = true;
      });
  // The handshake above is our own protocol. Once the application's handler
  // starts, its execution lifetime follows the caller's real Windows scope and
  // cancellation, rather than sharing the engine-startup timeout.
  if (!engine.PumpUntil([&] { return completed; },
                        std::chrono::steady_clock::time_point::max(), canceled))
    return canceled() ? E_ABORT : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
  if (!success) return E_FAIL;
  return S_OK;
}
}  // namespace

HRESULT DeliverSdkBackground(const EncodableMap& message,
                             const std::function<bool()>& canceled) {
  std::unique_lock<std::mutex> lock(BackgroundProcessing(), std::try_to_lock);
  if (!lock.owns_lock()) return HRESULT_FROM_WIN32(ERROR_BUSY);
  if (!canceled) return E_INVALIDARG;
  try {
    if (canceled()) return E_ABORT;
    Apartment apartment;
    if (FAILED(apartment.result)) return apartment.result;
    return InvokeBackgroundCallback(firebase_core::LoadPackagedConfiguration(),
                                    message, canceled);
  } catch (...) {
    return E_FAIL;
  }
}
}  // namespace flutter::winrt::plugins::firebase_messaging
