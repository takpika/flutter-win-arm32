// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../service.h"

#include <cassert>
#include <deque>
#include <set>
using namespace flutter;
using namespace flutter::winrt::plugins;
namespace fm = firebase_messaging;
namespace fc = firebase_core;
namespace {
struct Reply {
  int count = 0;
  std::string error;
  EncodableValue value;
  bool missing = false;
};
class Sink : public MethodResult<EncodableValue> {
 public:
  explicit Sink(std::shared_ptr<Reply> r) : reply(std::move(r)) {}

 private:
  void SuccessInternal(const EncodableValue* value) override {
    ++reply->count;
    if (value) reply->value = *value;
  }
  void ErrorInternal(const std::string& code, const std::string&,
                     const EncodableValue*) override {
    ++reply->count;
    reply->error = code;
  }
  void NotImplementedInternal() override {
    ++reply->count;
    reply->missing = true;
  }
  std::shared_ptr<Reply> reply;
};
std::set<fcm_sdk_handle> handles;
std::map<fcm_sdk_token_subscription, fcm_sdk_token_callbacks> subscriptions;
struct Pending {
  fcm_sdk_handle client;
  fcm_sdk_operation_callbacks callbacks;
};
std::map<fcm_sdk_operation, Pending> operations;
std::map<fcm_sdk_operation, fcm_sdk_background_callbacks> drains;
std::vector<fcm_sdk_operation> canceled;
uint64_t sequence = 0;
int network_opens = 0, core_gets = 0, core_sets = 0, auto_enabled = 0,
    metrics_enabled = 0, saved_opens = 0;
bool fail_start = false, immediate = false;
HRESULT rejected_start = E_FAIL;
std::string last_topic;
bool last_subscribe = false;
std::shared_ptr<Reply> Call(fm::Service& service, const char* name,
                            EncodableMap args = {}) {
  auto reply = std::make_shared<Reply>();
  args.try_emplace(EncodableValue("appName"), EncodableValue("[DEFAULT]"));
  service.Handle(MethodCall<EncodableValue>(
                     std::string("Messaging#") + name,
                     std::make_unique<EncodableValue>(std::move(args))),
                 std::make_unique<Sink>(reply));
  return reply;
}
void Complete(fcm_sdk_operation id, HRESULT result,
              const std::string& token = {}) {
  auto pending = operations.at(id);
  operations.erase(id);
  const fcm_sdk_buffer buffer{
      reinterpret_cast<uint8_t*>(const_cast<char*>(token.data())),
      token.size()};
  pending.callbacks.completed(pending.callbacks.context, result, &buffer);
  pending.callbacks.release(pending.callbacks.context);
}
void FinishDrain(fcm_sdk_operation id, fcm_sdk_drain_result result = {}) {
  auto callbacks = drains.at(id);
  drains.erase(id);
  callbacks.completed(callbacks.callbacks.context, S_OK, &result);
  callbacks.release(callbacks.callbacks.context);
}
void Token(fcm_sdk_token_subscription id, int64_t generation,
           const char* token) {
  auto callback = subscriptions.at(id);
  assert(callback.received(callback.context, id, generation,
                           reinterpret_cast<const uint8_t*>(token),
                           strlen(token)) == S_OK);
}
}  // namespace
extern "C" {
HRESULT fcm_sdk_get_foreground_state(int* value) {
  *value = 1;
  return S_OK;
}
HRESULT fcm_sdk_open_saved(const char*, fcm_sdk_handle* handle) {
  ++saved_opens;
  *handle = 0;
  return 1;
}
HRESULT fcm_sdk_queue_activation(fcm_sdk_handle, const char*, int64_t*) {
  return 1;
}
HRESULT fcm_sdk_lookup(fcm_sdk_handle, int64_t, fcm_sdk_buffer*) {
  return E_FAIL;
}
HRESULT fcm_sdk_lookup_activation(fcm_sdk_handle, const char*, int64_t*,
                                  fcm_sdk_buffer*) {
  return E_FAIL;
}
void fcm_sdk_buffer_free(fcm_sdk_buffer*) {}
uint32_t fcm_sdk_abi_version() { return 22; }
const char* fcm_sdk_error_name(HRESULT) { return "synthetic-error"; }
HRESULT fcm_sdk_open_with_core_identity(
    const fcm_sdk_configuration* config, const char* core,
    const fcm_sdk_messaging_options* options, fcm_sdk_handle* out) {
  assert(std::string(config->app_id) == "android-app" &&
         std::string(core) == "windows-app");
  if (options) ++network_opens;
  *out = ++sequence;
  handles.insert(*out);
  return S_OK;
}
HRESULT fcm_sdk_close(fcm_sdk_handle client) {
  assert(handles.erase(client) == 1);
  return S_OK;
}
HRESULT fcm_sdk_get_data_collection_default_enabled(fcm_sdk_handle client,
                                                    int* enabled) {
  assert(handles.count(client));
  ++core_gets;
  *enabled = 0;
  return S_OK;
}
HRESULT fcm_sdk_import_data_collection_default_enabled(fcm_sdk_handle, int,
                                                       int* enabled) {
  *enabled = 0;
  return 1;
}
HRESULT fcm_sdk_set_data_collection_default_enabled(fcm_sdk_handle,
                                                    int enabled) {
  assert(enabled == 0);
  ++core_sets;
  return S_OK;
}
HRESULT fcm_sdk_get_auto_init_enabled(fcm_sdk_handle, int* enabled) {
  *enabled = auto_enabled;
  return S_OK;
}
HRESULT fcm_sdk_set_auto_init_enabled(fcm_sdk_handle, int enabled) {
  auto_enabled = enabled;
  return S_OK;
}
HRESULT fcm_sdk_set_delivery_metrics_export_enabled(fcm_sdk_handle,
                                                    int enabled) {
  metrics_enabled = enabled;
  return S_OK;
}
HRESULT
fcm_sdk_subscribe_token_refresh(fcm_sdk_handle,
                                const fcm_sdk_token_callbacks* callbacks,
                                fcm_sdk_token_subscription* out) {
  *out = ++sequence;
  subscriptions.emplace(*out, *callbacks);
  return S_OK;
}
HRESULT fcm_sdk_unsubscribe_token_refresh(fcm_sdk_token_subscription id) {
  auto callbacks = subscriptions.at(id);
  subscriptions.erase(id);
  callbacks.release(callbacks.context);
  return S_OK;
}
HRESULT fcm_sdk_start_operation(fcm_sdk_handle client, int kind,
                                const fcm_sdk_operation_callbacks* callbacks,
                                fcm_sdk_operation* out) {
  assert(handles.count(client));
  if (fail_start) return rejected_start;
  *out = ++sequence;
  operations.emplace(*out, Pending{client, *callbacks});
  if (immediate)
    Complete(*out, S_OK, kind == FCM_SDK_GET_TOKEN ? "early-token" : "");
  return S_OK;
}
HRESULT fcm_sdk_start_topic_operation(
    fcm_sdk_handle client, const char* topic, int subscribe,
    const fcm_sdk_operation_callbacks* callbacks, fcm_sdk_operation* out) {
  last_topic = topic;
  last_subscribe = subscribe != 0;
  return fcm_sdk_start_operation(client, FCM_SDK_SYNC_TOPICS, callbacks, out);
}
HRESULT fcm_sdk_request_operation_cancellation(fcm_sdk_operation id) {
  canceled.push_back(id);
  return S_OK;
}
HRESULT fcm_sdk_start_drain(fcm_sdk_handle client,
                            const fcm_sdk_background_callbacks* callbacks,
                            uint32_t limit, fcm_sdk_operation* out) {
  assert(handles.count(client) && limit == 32);
  *out = ++sequence;
  drains.emplace(*out, *callbacks);
  return S_OK;
}
}
int main() {
  const std::string json =
      R"({"version":1,"backgroundDispatcher":{"libraryUri":"package:probe/main.dart","entrypoint":"dispatcher"},"apps":[{"appName":"[DEFAULT]","displayName":"Synthetic","coreOptions":{"apiKey":"public-key","appId":"windows-app","projectId":"project","messagingSenderId":"123"},"messaging":{"apiKey":"public-key","appId":"android-app","projectId":"project","senderId":"123","androidPackage":"org.example.synthetic","certificateSha1":"abababababababababababababababababababab"}}]})";
  auto configuration = fc::Configuration::Parse({json.begin(), json.end()});
  fc::StateBytes disk;
  bool missing = true;
  fc::StateTransaction transaction = [&](const fc::StateUpdate& update) {
    fc::StateBytes next;
    if (update(disk, missing, next)) {
      disk = std::move(next);
      missing = false;
    }
  };
  std::deque<std::function<void()>> tasks;
  auto flush = [&] {
    while (!tasks.empty()) {
      auto task = std::move(tasks.front());
      tasks.pop_front();
      task();
    }
  };
  fc::AppRegistry apps(
      [](const auto&, const auto&, auto) {
        return std::optional<fc::AppRegistry::Error>{};
      },
      [](const auto&) { return EncodableMap{}; });
  std::vector<fm::SdkMessageHandler::Completion> replies;
  std::vector<std::string> delivered;
  bool worker_open = true;
  int settings_queries = 0;
  fm::Service service(
      apps, configuration, transaction, true,
      [&](const auto*) {
        ++settings_queries;
        return EncodableMap{};
      },
      [&](const std::string& name) {
        return std::make_shared<fm::SdkMessageHandler>(
            fm::MessagingAppIdentity{name, "project", "android-app"},
            fm::InitialMessageCache(transaction), fm::SdkMessageHandler::Emit{},
            fm::SdkMessageHandler::Background{});
      });
  service.SetEmitter(
      [&](const auto& method, EncodableValue value, auto done) {
        assert(method == "Messaging#onTokenRefresh");
        delivered.push_back(std::get<std::string>(value));
        replies.push_back(std::move(done));
        return true;
      },
      [&](auto task, auto reject) {
        if (!worker_open) {
          if (reject) reject();
          return false;
        }
        tasks.push_back(std::move(task));
        return true;
      });
  assert(Call(service, "getToken")->error == "no-app");
  service.OpenNotification("early-real-OS-launch", true);
  assert(saved_opens == 1 && handles.empty() && network_opens == 0);
  assert(Call(service, "getNotificationSettings")->count == 1 &&
         settings_queries == 1);
  fc::App app{*configuration.DefaultOptions()};
  service.RestoreCore("[DEFAULT]", app);
  assert(!app.automatic_data_collection && core_gets == 1 &&
         network_opens == 0);
  apps.Restore("[DEFAULT]", app);
  service.ApplyCore("[DEFAULT]", app, fc::AppRegistry::Change::kInitialize);
  assert(network_opens == 1 && subscriptions.size() == 1);
  const auto first_subscription = subscriptions.begin()->first;
  Token(first_subscription, 1, "first");
  flush();
  assert(delivered.empty());
  EncodableMap dispatcher{
      {EncodableValue("dispatcherLibraryUri"),
       EncodableValue("package:probe/main.dart")},
      {EncodableValue("dispatcherEntrypoint"), EncodableValue("dispatcher")}};
  assert(Call(service, "initialized", dispatcher)->count == 1);
  assert(delivered == std::vector<std::string>{"first"});
  assert(drains.size() == 1);
  // No busy loop when the SDK reports pending work but made no progress.
  fcm_sdk_drain_result pending{};
  pending.more_pending = 1;
  FinishDrain(drains.begin()->first, pending);
  flush();
  assert(drains.empty());
  // One real resume can arrive before a hidden-view cancellation finishes.
  // Preserve that wake, coalesce duplicates, and stop after a pending result.
  service.Wake();
  assert(drains.size() == 1);
  const auto hidden_drain = drains.begin()->first;
  service.Wake();
  service.Wake();
  assert(drains.size() == 1 && drains.count(hidden_drain));
  pending.canceled = 1;
  FinishDrain(hidden_drain, pending);
  flush();
  assert(drains.size() == 1 && !drains.count(hidden_drain));
  FinishDrain(drains.begin()->first, pending);
  flush();
  assert(drains.empty());
  Token(first_subscription, 2, "newer");
  flush();
  assert(delivered.size() == 1);
  replies[0](true);
  flush();
  assert(delivered.back() == "newer" && delivered.size() == 2);
  replies[1](true);
  flush();
  assert(!fm::TokenEventCache(transaction)
              .Read({"[DEFAULT]", "project", "android-app"}));
  fail_start = true;
  auto failed = Call(service, "getToken");
  assert(failed->count == 1 && failed->error == "synthetic-error");
  rejected_start = 1;
  auto not_started = Call(service, "deleteToken");
  assert(not_started->count == 1 && not_started->error == "synthetic-error");
  fail_start = false;
  immediate = true;
  auto early = Call(service, "getToken");
  assert(
      early->count == 1 &&
      std::get<std::string>(
          std::get<EncodableMap>(early->value).at(EncodableValue("token"))) ==
          "early-token");
  immediate = false;
  auto topic = Call(service, "subscribeToTopic",
                    {{EncodableValue("topic"),
                      EncodableValue("/topics/SDK-normalizes-this")}});
  assert(topic->count == 0 && last_subscribe &&
         last_topic == "/topics/SDK-normalizes-this");
  const auto topic_id = operations.begin()->first;
  service.ApplyCore("[DEFAULT]", app, fc::AppRegistry::Change::kDataCollection);
  assert(core_sets == 1);
  assert(Call(service, "setAutoInitEnabled",
              {{EncodableValue("enabled"), EncodableValue(true)}})
                 ->count == 1 &&
         auto_enabled == 1);
  assert(Call(service, "setDeliveryMetricsExportToBigQuery",
              {{EncodableValue("enabled"), EncodableValue(true)}})
                 ->count == 1 &&
         metrics_enabled == 1);
  // Old Dart replies cannot consume a new app instance's pending generation.
  Token(first_subscription, 3, "old-instance");
  flush();
  assert(delivered.back() == "old-instance");
  service.ApplyCore("[DEFAULT]", std::nullopt,
                    fc::AppRegistry::Change::kDelete);
  assert(subscriptions.empty() && std::find(canceled.begin(), canceled.end(),
                                            topic_id) != canceled.end());
  service.RestoreCore("[DEFAULT]", app);
  service.ApplyCore("[DEFAULT]", app, fc::AppRegistry::Change::kInitialize);
  flush();
  Token(subscriptions.begin()->first, 4, "new-instance");
  flush();
  replies[2](true);
  flush();
  assert(fm::TokenEventCache(transaction)
             .Read({"[DEFAULT]", "project", "android-app"})
             ->generation == 4);
  // Close requests cancellation immediately even though an outstanding RPC owns
  // its result/worker state. A later SDK completion retains its client safely.
  service.Close();
  service.Close();
  service.Wake();
  worker_open = false;
  assert(subscriptions.empty() && handles.size() == 2 && topic->count == 0);
  Complete(topic_id, E_ABORT);
  assert(topic->count == 1 && topic->error == "synthetic-error");
  while (!drains.empty()) FinishDrain(drains.begin()->first);
  flush();
  assert(handles.empty());
  assert(Call(service, "unknown")->missing);
  // A headless host never subscribes to or consumes the UI token-refresh event.
  fc::AppRegistry headless_apps(
      [](const auto&, const auto&, auto) {
        return std::optional<fc::AppRegistry::Error>{};
      },
      [](const auto&) { return EncodableMap{}; });
  fm::Service headless(
      headless_apps, configuration, transaction, false,
      [](const auto*) { return EncodableMap{}; },
      [](const auto&) { return std::shared_ptr<fm::SdkMessageHandler>{}; });
  headless.RestoreCore("[DEFAULT]", app);
  headless_apps.Restore("[DEFAULT]", app);
  headless.ApplyCore("[DEFAULT]", app, fc::AppRegistry::Change::kInitialize);
  headless.Wake();
  assert(drains.empty());
  assert(subscriptions.empty());
  fm::InitialMessageCache(transaction)
      .Save({"[DEFAULT]", "project", "android-app"}, 77,
            R"({"messageId":"UI-only"})");
  assert(Call(headless, "getInitialMessage")->count == 1);
  assert(fm::InitialMessageCache(transaction)
             .Pending({"[DEFAULT]", "project", "android-app"}) ==
         std::vector<int64_t>{77});
  headless.Close();
  assert(handles.empty());
  worker_open = true;
  delivered.clear();
  replies.clear();
  fm::TokenEventCache(transaction)
      .Save({"[DEFAULT]", "project", "android-app"}, {5, "before-core"});
  fm::Service* live = nullptr;
  fc::AppRegistry registry(
      [&](const auto& name, const auto& value, auto change) {
        live->ApplyCore(name, value, change);
        return std::optional<fc::AppRegistry::Error>{};
      },
      [&](const auto& name) { return live->Constants(name); },
      configuration.DefaultOptions(),
      [&](const auto& name, auto& value) {
        live->RestoreCore(name, value);
        return std::optional<fc::AppRegistry::Error>{};
      });
  fm::Service initialized_before_core(
      registry, configuration, transaction, true,
      [](const auto*) { return EncodableMap{}; },
      [&](const std::string& name) {
        return std::make_shared<fm::SdkMessageHandler>(
            fm::MessagingAppIdentity{name, "project", "android-app"},
            fm::InitialMessageCache(transaction), fm::SdkMessageHandler::Emit{},
            fm::SdkMessageHandler::Background{});
      });
  live = &initialized_before_core;
  live->SetEmitter(
      [&](const auto&, EncodableValue value, auto reply) {
        delivered.push_back(std::get<std::string>(value));
        replies.push_back(std::move(reply));
        return true;
      },
      [&](auto task, auto) {
        tasks.push_back(std::move(task));
        return true;
      });
  assert(Call(*live, "initialized", dispatcher)->count == 1);
  bool initialized = false;
  registry.InitializeApp("[DEFAULT]", *configuration.DefaultOptions(),
                         [&](auto) { initialized = true; });
  assert(initialized && registry.Find("[DEFAULT]") && delivered.empty());
  flush();
  assert(delivered == std::vector<std::string>{"before-core"});
  replies.front()(true);
  flush();
  live->Close();
  while (!drains.empty()) FinishDrain(drains.begin()->first);
  flush();
  assert(handles.empty() && subscriptions.empty());
}
