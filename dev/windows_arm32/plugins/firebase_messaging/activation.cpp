// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.AppService.h>
#include <winrt/Windows.ApplicationModel.Background.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <wpnotify.h>

#include "../firebase_core/configuration_native.h"
#include "../firebase_core/private_state_file.h"
#include "../firebase_core/registration.h"
#include "background.h"
#include "delivery.h"
#include "flutter/shell/platform/windows/uwp/diagnostics.h"
#include "notification_activation_store.h"
#include "notification_open.h"
#include "sdk_execution.h"

namespace flutter::winrt::plugins {
namespace {
using ::winrt::Windows::ApplicationModel::Activation::
    BackgroundActivatedEventArgs;
using ::winrt::Windows::ApplicationModel::AppService::AppServiceTriggerDetails;
using ::winrt::Windows::ApplicationModel::Core::CoreApplication;

class FirebaseApplication final
    : public std::enable_shared_from_this<FirebaseApplication> {
 public:
  void Register() {
    std::weak_ptr<FirebaseApplication> weak = shared_from_this();
    activation_ = CoreApplication::BackgroundActivated(
        ::winrt::auto_revoke,
        [weak](const auto&, const BackgroundActivatedEventArgs& args) {
          if (auto self = weak.lock()) self->Activated(args);
        });
  }

 private:
  void Activated(const BackgroundActivatedEventArgs& args) noexcept {
    try {
      const auto task = args.TaskInstance();
      const auto details =
          task.TriggerDetails().try_as<AppServiceTriggerDetails>();
      // The public Messaging API in the original package supports the default
      // app. Core secondary apps do not create another Messaging receiver.
      // The SDK rejects unrelated recovery task names without consuming them.
      const int kind =
          details && details.Name() == ::winrt::to_hstring(WPN_DELIVERY_SERVICE)
              ? FCM_SDK_DELIVERY
              : FCM_SDK_RECOVERY;
      const HRESULT started = firebase_messaging::SdkExecution::BackgroundSaved(
          "[DEFAULT]", ::winrt::get_abi(task), kind, 32,
          [] {
            const auto configuration =
                firebase_core::LoadPackagedConfiguration();
            const auto* configured = configuration.Find("[DEFAULT]");
            if (!configured || !configured->messaging)
              throw firebase_core::StateError("missing-configuration");
            const firebase_messaging::MessagingAppIdentity app{
                configured->name, configured->messaging->project_id,
                configured->messaging->app_id};
            const auto transaction = firebase_core::NativeStateTransaction();
            return std::make_shared<firebase_messaging::SdkMessageHandler>(
                app, firebase_messaging::InitialMessageCache(transaction),
                [name = app.name](const auto& method, auto value, auto done) {
                  return firebase_messaging::ForegroundReceiver::Deliver(
                      name, method, std::move(value), std::move(done));
                },
                firebase_messaging::DeliverSdkBackground,
                [app, store = firebase_messaging::NotificationActivationStore(
                          transaction)](fcm_sdk_handle client, int64_t receipt,
                                        const uint8_t* json, size_t size) {
                  return store.CaptureOpened(client, app, receipt, json, size);
                });
          },
          shared_from_this(),
          [](HRESULT result, const fcm_sdk_drain_result&) {
            Diagnostic("Firebase SDK background completion", result);
          });
      if (FAILED(started))
        Diagnostic("Firebase SDK background activation", started);
    } catch (const ::winrt::hresult_error& error) {
      Diagnostic("Firebase SDK background activation", error.code());
    } catch (...) {
      Diagnostic("Firebase SDK background activation", E_FAIL);
    }
  }
  CoreApplication::BackgroundActivated_revoker activation_;
};
}  // namespace
void HandleFirebaseNotificationActivation(
    ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs* args,
    bool initial) {
  try {
    ::winrt::Windows::ApplicationModel::Activation::IActivatedEventArgs value{
        nullptr};
    ::winrt::copy_from_abi(value, args);
    const auto toast =
        value.try_as<::winrt::Windows::ApplicationModel::Activation::
                         ToastNotificationActivatedEventArgs>();
    if (toast)
      firebase_messaging::NotificationOpen::Submit(
          {::winrt::to_string(toast.Argument()), initial});
  } catch (...) {
    Diagnostic("Firebase notification activation arguments", E_INVALIDARG);
  }
}
HRESULT RegisterFirebaseApplicationServices(std::shared_ptr<void>* lifetime) {
  if (!lifetime) return E_POINTER;
  try {
    auto application = std::make_shared<FirebaseApplication>();
    application->Register();
    *lifetime = std::move(application);
    return S_OK;
  } catch (const ::winrt::hresult_error& error) {
    return error.code();
  } catch (...) {
    return E_FAIL;
  }
}
}  // namespace flutter::winrt::plugins
