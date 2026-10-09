// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once
#include <flutter/method_call.h>
#include <flutter/method_result.h>

#include <algorithm>
#include <atomic>
#include <map>

#include "background_state.h"
#include "notification_activation_store.h"
#include "sdk_client.h"
#include "sdk_execution.h"
#include "sdk_operation.h"
#include "sdk_topic_operation.h"
#include "token_event_cache.h"

namespace flutter::winrt::plugins::firebase_messaging {
class Service {
 public:
  using Result = MethodResult<EncodableValue>;
  using Completion = SdkMessageHandler::Completion;
  using Emit = SdkMessageHandler::Emit;
  using Task = std::function<void()>;
  using Post = std::function<bool(Task, Task)>;
  using NotificationSettingsQuery =
      std::function<EncodableMap(const EncodableMap*)>;
  using HandlerFactory =
      std::function<std::shared_ptr<SdkMessageHandler>(const std::string&)>;
  Service(firebase_core::AppRegistry& apps,
          const firebase_core::Configuration& configuration,
          firebase_core::StateTransaction transaction, bool has_view,
          NotificationSettingsQuery settings, HandlerFactory handlers)
      : apps_(apps),
        configuration_(configuration),
        background_(transaction),
        tokens_(transaction),
        activations_(transaction),
        initial_(std::move(transaction)),
        has_view_(has_view),
        notification_settings_(std::move(settings)),
        handlers_(std::move(handlers)) {
    if (fcm_sdk_abi_version() < 22)
      throw firebase_core::StateError("sdk-version-unsupported");
  }
  ~Service() { Close(); }
  // Invoke from WorkerMessenger's cleanup hook, not just the destructor:
  // pending RPC replies may retain its owner after the engine has closed.
  void Close() {
    if (closing_->exchange(true)) return;
    for (auto& [name, state] : clients_) {
      (void)name;
      if (state.subscription)
        fcm_sdk_unsubscribe_token_refresh(state.subscription);
    }
    for (auto& weak : operations_)
      if (auto operation = weak.lock();
          operation && !operation->finished && operation->id)
        fcm_sdk_request_operation_cancellation(operation->id);
    clients_.clear();
  }
  void SetEmitter(Emit emit, Post post) {
    emit_ = std::move(emit);
    post_ = std::move(post);
  }
  // Called on the plugin worker after a real OS visibility/resume event.
  // The SDK independently determines the execution scope and receipt policy.
  void Wake() {
    if (!has_view_ || !ready_ || *closing_) return;
    for (const auto& [name, state] : clients_) {
      if (!state.published) continue;
      DrainTokens(name);
      RequestDrain(name);
    }
  }
  bool Deliver(const std::string& name, const std::string& method,
               EncodableValue value, Completion done) {
    const auto found = clients_.find(name);
    return ready_ && !*closing_ && apps_.Find(name) &&
           found != clients_.end() && found->second.published && emit_ &&
           emit_(method, std::move(value), std::move(done));
  }
  void RestoreCore(const std::string& name, firebase_core::App& app) {
    const auto* configured = configuration_.Find(name);
    if (!configured || !configured->messaging ||
        configured->messaging->project_id != app.options.project_id())
      return;
    Detach(name);
    auto client = std::make_shared<SdkClient>(
        *configured, app, background_.Read().callback.has_value());
    client->RestoreDataCollection(app);
    clients_.emplace(name, Client{std::move(client)});
  }
  bool OwnsCore(const std::string& name) const {
    return clients_.count(name) != 0;
  }
  void ApplyCore(const std::string& name,
                 const std::optional<firebase_core::App>& app,
                 firebase_core::AppRegistry::Change change) {
    if (!app) {
      Detach(name);
      return;
    }
    auto found = clients_.find(name);
    if (found == clients_.end()) return;
    auto& state = found->second;
    if (change == firebase_core::AppRegistry::Change::kDataCollection)
      state.client->SetDataCollection(app->automatic_data_collection);
    else if (change == firebase_core::AppRegistry::Change::kInitialize) {
      state.client->PublishNetworkOptions();
      state.published = true;
      if (has_view_ && name == "[DEFAULT]")
        activations_.Queue(state.client->handle(), Identity(name));
      if (has_view_) Subscribe(name, state);
      if (ready_) {
        // AppRegistry commits this change before publishing the live app.
        // Schedule channel work after that commit, never inside it.
        std::weak_ptr<SdkClient> expected = state.client;
        post_(
            [this, name, expected] {
              const auto current = clients_.find(name);
              const auto owner = expected.lock();
              if (!owner || current == clients_.end() ||
                  current->second.client != owner)
                return;
              DrainTokens(name);
              RequestDrain(name);
            },
            {});
      }
    }
  }
  EncodableMap Constants(const std::string& name) const {
    const auto found = clients_.find(name);
    if (found == clients_.end() || !apps_.Find(name)) return {};
    int enabled = 0;
    Check(fcm_sdk_get_auto_init_enabled(found->second.client->handle(),
                                        &enabled));
    return {{EncodableValue("plugins.flutter.io/firebase_messaging"),
             EncodableValue(EncodableMap{{EncodableValue("AUTO_INIT_ENABLED"),
                                          EncodableValue(enabled != 0)}})}};
  }
  void OpenNotification(const std::string& launch, bool initial) {
    activations_.Observe(launch, initial);
    const auto found = clients_.find("[DEFAULT]");
    if (found != clients_.end() && found->second.published) {
      activations_.Queue(found->second.client->handle(), Identity("[DEFAULT]"));
      RequestDrain("[DEFAULT]");
    } else {
      activations_.ProtectSaved("[DEFAULT]");
    }
  }
  void Handle(const MethodCall<EncodableValue>& call,
              std::unique_ptr<Result> unique) {
    auto result = std::shared_ptr<Result>(std::move(unique));
    const auto& method = call.method_name();
    try {
      if (method == "Messaging#isSupported") {
        result->Success(EncodableValue(true));
        return;
      }
      if (method == "Messaging#setForegroundNotificationPresentationOptions") {
        result->Success();
        return;
      }
      const auto* args = call.arguments()
                             ? std::get_if<EncodableMap>(call.arguments())
                             : nullptr;
      if (!args) throw firebase_core::StateError("invalid-argument");
      if (method == "Messaging#initialized" ||
          method == "Messaging#startBackgroundIsolate") {
        const auto library = Text(*args, "dispatcherLibraryUri");
        const auto entrypoint = Text(*args, "dispatcherEntrypoint");
        const auto& packaged = configuration_.BackgroundDispatcher();
        if (!packaged || packaged->first != library ||
            packaged->second != entrypoint)
          throw firebase_core::StateError("background-dispatcher-mismatch");
        std::optional<int64_t> callback;
        if (method == "Messaging#startBackgroundIsolate") {
          const auto& value = Field(*args, "userCallbackHandle");
          if (const auto* v = std::get_if<int64_t>(&value))
            callback = *v;
          else if (const auto* v = std::get_if<int32_t>(&value))
            callback = *v;
          else
            throw firebase_core::StateError("invalid-background-callback");
        }
        background_.Configure(library, entrypoint, callback);
        if (callback)
          for (auto& [name, state] : clients_) {
            (void)name;
            state.client->SetBackgroundObserver(true);
          }
        if (method == "Messaging#initialized") {
          ready_ = true;
          for (const auto& [name, state] : clients_) {
            (void)state;
            DrainTokens(name);
            RequestDrain(name);
          }
        }
        result->Success();
        return;
      }
      if (method == "Messaging#getNotificationSettings" ||
          method == "Messaging#requestPermission") {
        const EncodableMap* permissions = nullptr;
        if (method == "Messaging#requestPermission") {
          permissions = std::get_if<EncodableMap>(&Field(*args, "permissions"));
          if (!permissions) throw firebase_core::StateError("invalid-argument");
        }
        result->Success(EncodableValue(notification_settings_(permissions)));
        return;
      }
      if (method != "Messaging#getToken" && method != "Messaging#deleteToken" &&
          method != "Messaging#subscribeToTopic" &&
          method != "Messaging#unsubscribeFromTopic" &&
          method != "Messaging#setAutoInitEnabled" &&
          method != "Messaging#getInitialMessage" &&
          method != "Messaging#setDeliveryMetricsExportToBigQuery") {
        result->NotImplemented();
        return;
      }
      const auto name = Text(*args, "appName");
      auto client = RequireClient(name);
      if (method == "Messaging#getInitialMessage") {
        if (!has_view_) { result->Success(); return; }
        auto pending = initial_.Pending(Identity(name));
        if (pending.empty())
          result->Success();
        else {
          auto message = initial_.Take(Identity(name), pending.front());
          if (message)
            result->Success(EncodableValue(std::move(*message)));
          else
            result->Success();
        }
        return;
      }
      if (method == "Messaging#setAutoInitEnabled") {
        Check(fcm_sdk_set_auto_init_enabled(client->handle(),
                                            Boolean(*args, "enabled") ? 1 : 0));
        int enabled = 0;
        Check(fcm_sdk_get_auto_init_enabled(client->handle(), &enabled));
        result->Success(
            EncodableValue(EncodableMap{{EncodableValue("isAutoInitEnabled"),
                                         EncodableValue(enabled != 0)}}));
        return;
      }
      if (method == "Messaging#setDeliveryMetricsExportToBigQuery") {
        Check(fcm_sdk_set_delivery_metrics_export_enabled(
            client->handle(), Boolean(*args, "enabled") ? 1 : 0));
        result->Success();
        return;
      }
      auto operation = NewOperation(std::move(client));
      auto reply = [operation, result, get = method == "Messaging#getToken"](
                       int32_t code, auto bytes) {
        operation->finished = true;
        if (code != S_OK) {
          result->Error(Error(code));
          return;
        }
        if (!get) {
          result->Success();
          return;
        }
        if (bytes.empty()) {
          result->Error("invalid-token-response");
          return;
        }
        result->Success(EncodableValue(
            EncodableMap{{EncodableValue("token"),
                          EncodableValue(std::string(
                              reinterpret_cast<const char*>(bytes.data()),
                              bytes.size()))}}));
      };
      if (method == "Messaging#subscribeToTopic" ||
          method == "Messaging#unsubscribeFromTopic")
        StartSdkTopicOperation(operation->client->handle(),
                               Text(*args, "topic"),
                               method == "Messaging#subscribeToTopic",
                               std::move(reply), operation, &operation->id);
      else
        StartSdkOperation(operation->client->handle(),
                          method == "Messaging#getToken" ? FCM_SDK_GET_TOKEN
                                                         : FCM_SDK_DELETE_TOKEN,
                          std::move(reply), operation, &operation->id);
    } catch (const firebase_core::StateError& error) {
      result->Error(error.what());
    }
  }

 private:
  struct Client {
    std::shared_ptr<SdkClient> client;
    fcm_sdk_token_subscription subscription = 0;
    bool published = false, draining = false, drain_requested = false;
  };
  struct Operation {
    std::shared_ptr<SdkClient> client;
    fcm_sdk_operation id = 0;
    std::atomic<bool> finished{false};
  };
  struct TokenContext {
    TokenEventCache cache;
    MessagingAppIdentity app;
    Post post;
    Service* service;
  };
  static std::string Error(HRESULT result) {
    if (const char* name = fcm_sdk_error_name(result)) return name;
    char text[32]{};
    snprintf(text, sizeof(text), "windows-%08x", static_cast<unsigned>(result));
    return text;
  }
  static void Check(HRESULT result) {
    if (FAILED(result)) throw firebase_core::StateError(Error(result).c_str());
  }
  static const EncodableValue& Field(const EncodableMap& args,
                                     const char* name) {
    const auto found = args.find(EncodableValue(name));
    if (found == args.end())
      throw firebase_core::StateError("invalid-argument");
    return found->second;
  }
  static std::string Text(const EncodableMap& args, const char* name) {
    const auto* value = std::get_if<std::string>(&Field(args, name));
    if (!value || value->empty() || value->find('\0') != std::string::npos)
      throw firebase_core::StateError("invalid-argument");
    return *value;
  }
  static bool Boolean(const EncodableMap& args, const char* name) {
    const auto* value = std::get_if<bool>(&Field(args, name));
    if (!value) throw firebase_core::StateError("invalid-argument");
    return *value;
  }
  MessagingAppIdentity Identity(const std::string& name) const {
    const auto* app = configuration_.Find(name);
    if (!app || !app->messaging)
      throw firebase_core::StateError("missing-configuration");
    return {name, app->messaging->project_id, app->messaging->app_id};
  }
  std::shared_ptr<SdkClient> RequireClient(const std::string& name) const {
    if (!apps_.Find(name)) throw firebase_core::StateError("no-app");
    const auto found = clients_.find(name);
    if (found == clients_.end() || !found->second.published)
      throw firebase_core::StateError("missing-configuration");
    return found->second.client;
  }
  std::shared_ptr<Operation> NewOperation(std::shared_ptr<SdkClient> client) {
    std::erase_if(operations_, [](const auto& weak) { return weak.expired(); });
    auto operation = std::make_shared<Operation>();
    operation->client = std::move(client);
    operations_.push_back(operation);
    return operation;
  }
  void Detach(const std::string& name) {
    const auto found = clients_.find(name);
    if (found == clients_.end()) return;
    if (found->second.subscription)
      fcm_sdk_unsubscribe_token_refresh(found->second.subscription);
    for (auto& weak : operations_)
      if (auto op = weak.lock();
          op && op->client == found->second.client && !op->finished && op->id)
        fcm_sdk_request_operation_cancellation(op->id);
    clients_.erase(found);
    token_delivering_.erase(name);
  }
  void Subscribe(const std::string& name, Client& state) {
    auto* context = new TokenContext{tokens_, Identity(name), post_, this};
    fcm_sdk_token_callbacks callbacks{};
    callbacks.size = sizeof(callbacks);
    callbacks.context = context;
    callbacks.received = [](void* raw, fcm_sdk_token_subscription,
                            int64_t generation, const uint8_t* token,
                            size_t size) noexcept -> HRESULT {
      auto& value = *static_cast<TokenContext*>(raw);
      if (!token || !size) return E_INVALIDARG;
      try {
        value.cache.Save(
            value.app,
            {generation,
             std::string(reinterpret_cast<const char*>(token), size)});
        // The post's active worker retains its FirebaseState owner. A closed
        // worker never invokes this task, so no raw service escapes its
        // lifetime.
        value.post([service = value.service,
                    name = value.app.name] { service->DrainTokens(name); },
                   {});
        return S_OK;
      } catch (...) {
        return E_FAIL;
      }
    };
    callbacks.release = [](void* raw) noexcept {
      delete static_cast<TokenContext*>(raw);
    };
    const HRESULT result = fcm_sdk_subscribe_token_refresh(
        state.client->handle(), &callbacks, &state.subscription);
    if (result != S_OK) {
      delete context;
      Check(result);
      throw firebase_core::StateError("token-subscription-not-started");
    }
  }
  void DrainTokens(const std::string& name) {
    if (!has_view_ || !ready_ || *closing_ || !clients_.count(name) ||
        !apps_.Find(name) || token_delivering_.count(name) || !emit_)
      return;
    const auto app = Identity(name);
    const auto event = tokens_.Read(app);
    if (!event) return;
    token_delivering_[name] = event->generation;
    std::weak_ptr<SdkClient> expected = clients_.at(name).client;
    auto finish = [this, post = post_, name, app,
                   generation = event->generation, expected](bool success) {
      post(
          [this, name, app, generation, success, expected] {
            const auto current = clients_.find(name);
            const auto owner = expected.lock();
            const auto flight = token_delivering_.find(name);
            if (!owner || current == clients_.end() ||
                current->second.client != owner ||
                flight == token_delivering_.end() ||
                flight->second != generation)
              return;
            token_delivering_.erase(flight);
            try {
              if (success) tokens_.Complete(app, generation);
              const auto next = tokens_.Read(app);
              if (next && (success || next->generation != generation))
                DrainTokens(name);
            } catch (const firebase_core::StateError&) {
            }
          },
          {});
    };
    if (!emit_("Messaging#onTokenRefresh", EncodableValue(event->token),
               finish))
      finish(false);
  }
  void RequestDrain(const std::string& name) {
    const auto found = clients_.find(name);
    if (!has_view_ || !ready_ || *closing_ || found == clients_.end() ||
        !found->second.published)
      return;
    if (found->second.draining) {
      // A resume can precede completion of a canceled hidden-view drain.
      found->second.drain_requested = true;
      return;
    }
    found->second.drain_requested = false;
    found->second.draining = true;
    auto operation = NewOperation(found->second.client);
    SdkExecution::Drain(
        operation->client->handle(), 32, handlers_(name), closing_, operation,
        [this, post = post_, name, operation](HRESULT result,
                                              fcm_sdk_drain_result drain) {
          operation->finished = true;
          std::weak_ptr<SdkClient> expected = operation->client;
          post(
              [this, name, result, drain, expected] {
                const auto current = clients_.find(name);
                const auto owner = expected.lock();
                if (!owner || current == clients_.end() ||
                    current->second.client != owner)
                  return;
                current->second.draining = false;
                const bool requested =
                    std::exchange(current->second.drain_requested, false);
                // Continue successful bounded pagination only. SDK recovery
                // owns retries of busy/canceled/failed work and its
                // scheduling/backoff.
                if (requested || (SUCCEEDED(result) && drain.visited >= 32 &&
                    drain.completed_stages > 0 && drain.more_pending &&
                    !drain.busy && !drain.canceled && !drain.failed_stages))
                  RequestDrain(name);
              },
              {});
        },
        &operation->id);
  }
  firebase_core::AppRegistry& apps_;
  const firebase_core::Configuration& configuration_;
  BackgroundState background_;
  TokenEventCache tokens_;
  NotificationActivationStore activations_;
  InitialMessageCache initial_;
  bool has_view_, ready_ = false;
  std::shared_ptr<std::atomic<bool>> closing_ =
      std::make_shared<std::atomic<bool>>(false);
  NotificationSettingsQuery notification_settings_;
  HandlerFactory handlers_;
  Emit emit_;
  Post post_;
  std::map<std::string, Client> clients_;
  std::map<std::string, int64_t> token_delivering_;
  std::vector<std::weak_ptr<Operation>> operations_;
};
}  // namespace flutter::winrt::plugins::firebase_messaging
