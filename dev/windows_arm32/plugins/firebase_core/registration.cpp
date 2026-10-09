// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "registration.h"

#include "../native_dispatch.h"
#ifdef FLUTTER_FIREBASE_MESSAGING
#include <firebase_compat/sdk.h>
#include <flutter/standard_method_codec.h>

#include "../firebase_messaging/background.h"
#include "../firebase_messaging/notifications.h"
#include "../method_channel.h"
#include "../firebase_messaging/delivery.h"
#include "../firebase_messaging/notification_open.h"
#include "../firebase_messaging/native_lifecycle.h"
#include "../firebase_messaging/service.h"
#endif
#include "app_registry.h"
#include "configuration_native.h"
#include "private_state_file.h"
#include "settings_store.h"

namespace flutter::winrt::plugins {
namespace {
struct FirebaseState {
  firebase_core::SettingsStore settings{
      firebase_core::NativeStateTransaction()};
  firebase_core::Configuration configuration;
  std::unique_ptr<firebase_core::AppRegistry> apps;
#ifdef FLUTTER_FIREBASE_MESSAGING
  std::unique_ptr<firebase_messaging::Service> messaging;
#endif
  EncodableMap Constants(const std::string& name) const {
#ifdef FLUTTER_FIREBASE_MESSAGING
    if (messaging) return messaging->Constants(name);
#else
    (void)name;
#endif
    return {};
  }
  void Initialize(bool has_view) {
    (void)has_view;
    configuration = firebase_core::LoadPackagedConfiguration();
    apps = std::make_unique<firebase_core::AppRegistry>(
        [this](const std::string& name,
               const std::optional<firebase_core::App>& app,
               [[maybe_unused]] firebase_core::AppRegistry::Change change)
            -> std::optional<firebase_core::AppRegistry::Error> {
          try {
        // The SDK owns Messaging's shared Core default. Do not commit a
        // second independent shadow store after its canonical setter.
#ifdef FLUTTER_FIREBASE_MESSAGING
            if (messaging && messaging->OwnsCore(name) &&
                change == firebase_core::AppRegistry::Change::kDataCollection) {
              messaging->ApplyCore(name, app, change);
              return std::nullopt;
            }
#endif
            // App.delete removes a live instance, preserving service
            // preferences and the installation. It is not remote token
            // deletion.
            if (app) settings.Save(name, *app);
#ifdef FLUTTER_FIREBASE_MESSAGING
            if (messaging) messaging->ApplyCore(name, app, change);
#endif
            return std::nullopt;
          } catch (const firebase_core::StateError& error) {
            return firebase_core::AppRegistry::Error(error.what());
          }
        },
        [this](const std::string& name) { return Constants(name); },
        configuration.DefaultOptions(),
        [this](const std::string& name, firebase_core::App& app)
            -> std::optional<firebase_core::AppRegistry::Error> {
          try {
            settings.Load(name, app);
#ifdef FLUTTER_FIREBASE_MESSAGING
            if (messaging) messaging->RestoreCore(name, app);
#endif
            return std::nullopt;
          } catch (const firebase_core::StateError& error) {
            return firebase_core::AppRegistry::Error(error.what());
          }
        });
#ifdef FLUTTER_FIREBASE_MESSAGING
    messaging = std::make_unique<firebase_messaging::Service>(
        *apps, configuration, firebase_core::NativeStateTransaction(), has_view,
        firebase_messaging::NativeSdkNotificationSettings,
        [this](const std::string& name) {
          const auto* configured = configuration.Find(name);
          if (!configured || !configured->messaging)
            throw firebase_core::StateError("missing-configuration");
          const firebase_messaging::MessagingAppIdentity app{
              name, configured->messaging->project_id,
              configured->messaging->app_id};
          const auto transaction = firebase_core::NativeStateTransaction();
          return std::make_shared<firebase_messaging::SdkMessageHandler>(
              app, firebase_messaging::InitialMessageCache(transaction),
              [name](const auto& method, auto value, auto done) {
                return firebase_messaging::ForegroundReceiver::Deliver(
                    name, method, std::move(value), std::move(done));
              },
              firebase_messaging::DeliverSdkBackground,
              [app, store = firebase_messaging::NotificationActivationStore(
                        transaction)](fcm_sdk_handle client, int64_t receipt,
                                      const uint8_t* json, size_t size) {
                return store.CaptureOpened(client, app, receipt, json, size);
              });
        });
#endif
  }
};
struct FirebaseRegistration {
  std::shared_ptr<FirebaseState> state = std::make_shared<FirebaseState>();
  std::unique_ptr<WorkerMessenger> messenger;
#ifdef FLUTTER_FIREBASE_MESSAGING
  std::shared_ptr<firebase_messaging::ForegroundReceiver> receiver;
  std::shared_ptr<firebase_messaging::NotificationOpen> notification_open;
  bool activation_received = false;
  std::unique_ptr<firebase_messaging::NativeLifecycle> lifecycle;
#endif
};
}  // namespace

void RegisterFirebase(PlatformMessageHandlers& handlers) {
  auto registration = std::make_shared<FirebaseRegistration>();
#ifdef FLUTTER_FIREBASE_MESSAGING
  handlers.RegisterActivationHandler([registration](auto* args) {
    const bool initial =
        !std::exchange(registration->activation_received, true);
    HandleFirebaseNotificationActivation(args, initial);
  });
#endif
  handlers.RegisterInitializer([registration](
                                   const PlatformMessageContext& context)
                                   -> HRESULT {
    auto platform_post = context.post_task;
    if (!platform_post && context.dispatcher)
      platform_post = PostToDispatcher(context.dispatcher);
    if (!context.messenger || !platform_post) return E_NOINTERFACE;
#ifdef FLUTTER_FIREBASE_MESSAGING
    // The SDK observes each real UI view on its own thread. A headless engine
    // must not attach or create a CoreWindow merely to dispatch Dart work.
    if (context.window) {
      const HRESULT attached = fcm_sdk_attach_current_view();
      if (FAILED(attached)) {
        Diagnostic("Firebase SDK UI view attachment", attached);
        return attached;
      }
    }
    auto lifecycle_post = platform_post;
#endif
    try {
      registration->messenger = std::make_unique<WorkerMessenger>(
          context.messenger, std::move(platform_post), registration->state,
          [state = registration->state, has_view = context.window != nullptr] {
            const HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
            if (FAILED(hr)) {
              Diagnostic("Firebase worker WinRT initialization", hr);
              return false;
            }
            try {
              state->Initialize(has_view);
              return true;
            } catch (...) {
              Diagnostic("Firebase resource initialization failed",
                         E_INVALIDARG);
              RoUninitialize();
              return false;
            }
          },
          [state = registration->state] {
#ifdef FLUTTER_FIREBASE_MESSAGING
            if (state->messaging) state->messaging->Close();
#endif
            RoUninitialize();
          });
      auto* messenger = registration->messenger.get();
      auto* apps = registration->state->apps.get();
      firebase_core_windows::FirebaseCoreHostApi::SetUp(messenger, apps);
      firebase_core_windows::FirebaseAppHostApi::SetUp(messenger, apps);
#ifdef FLUTTER_FIREBASE_MESSAGING
      auto* messaging = registration->state->messaging.get();
      auto send = messenger->BindSender();
      auto post = messenger->BindTaskPoster();
      messaging->SetEmitter(
          [send](const std::string& method, EncodableValue arguments,
                 firebase_messaging::Service::Completion done) {
            const auto& codec = StandardMethodCodec::GetInstance();
            auto encoded = codec.EncodeMethodCall(MethodCall<EncodableValue>(
                method,
                std::make_unique<EncodableValue>(std::move(arguments))));
            BinaryReply reply;
            if (done) {
              reply = [done](const uint8_t* bytes, size_t size) {
                bool success = false;
                MethodResultFunctions<EncodableValue> result(
                    [&](const EncodableValue*) { success = true; },
                    [](const std::string&, const std::string&,
                       const EncodableValue*) {},
                    [] {});
                // An empty reply means MissingPlugin, never successful
                // delivery.
                if (size)
                  StandardMethodCodec::GetInstance()
                      .DecodeAndProcessResponseEnvelope(bytes, size, &result);
                done(success);
              };
            }
            return send("plugins.flutter.io/firebase_messaging", *encoded,
                        std::move(reply));
          },
          post);
      RegisterMethodHandler(*messenger, "plugins.flutter.io/firebase_messaging",
                            [messaging](const auto& call, auto result) {
                              messaging->Handle(call, std::move(result));
                            });
      if (context.window) {
        registration->lifecycle =
            std::make_unique<firebase_messaging::NativeLifecycle>();
        const HRESULT attached = registration->lifecycle->Attach(
            context.window, std::move(lifecycle_post),
            [post, messaging] { post([messaging] { messaging->Wake(); }, {}); });
        if (FAILED(attached)) return attached;
        registration->notification_open =
            firebase_messaging::NotificationOpen::Attach(
                [post,
                 messaging](firebase_messaging::NotificationOpen::Event event) {
                  return post(
                      [messaging, event] {
                        try {
                          messaging->OpenNotification(event.launch,
                                                      event.initial);
                        } catch (const firebase_core::StateError&) {
                          firebase_messaging::NotificationOpen::Requeue(event);
                          Diagnostic("Firebase notification activation storage",
                                     E_FAIL);
                        }
                      },
                      [event] {
                        firebase_messaging::NotificationOpen::Requeue(event);
                      });
                });
        registration->receiver = firebase_messaging::ForegroundReceiver::Attach(
            [post, messaging](
                std::string name, std::string method, EncodableValue message,
                firebase_messaging::ForegroundReceiver::Complete done) {
              return post(
                  [messaging, name = std::move(name),
                   method = std::move(method), message = std::move(message),
                   done]() mutable {
                    if (!messaging->Deliver(name, method, std::move(message),
                                            done))
                      done(false);
                  },
                  [done] { done(false); });
            });
      }
#endif
      return S_OK;
    } catch (...) {
      Diagnostic("Firebase native registration failed", E_FAIL);
      return E_FAIL;
    }
  });
}
}  // namespace flutter::winrt::plugins
