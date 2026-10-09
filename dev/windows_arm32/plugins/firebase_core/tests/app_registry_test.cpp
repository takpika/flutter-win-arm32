// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../app_registry.h"

#include <cassert>
#include <iostream>

using firebase_core_windows::FirebaseAppHostApi;
using firebase_core_windows::FirebaseCoreHostApi;
using flutter::CustomEncodableValue;
using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::winrt::plugins::firebase_core::App;
using flutter::winrt::plugins::firebase_core::AppRegistry;

class Messenger : public flutter::BinaryMessenger {
 public:
  void Send(const std::string&, const uint8_t*, size_t,
            flutter::BinaryReply = nullptr) const override {
    assert(false);
  }
  void SetMessageHandler(const std::string& channel,
                         flutter::BinaryMessageHandler handler) override {
    if (handler)
      handlers.insert_or_assign(channel, std::move(handler));
    else
      handlers.erase(channel);
  }
  EncodableList Call(const std::string& method,
                     const EncodableValue& argument) {
    const auto& codec = FirebaseCoreHostApi::GetCodec();
    auto bytes = codec.EncodeMessage(argument);
    std::unique_ptr<EncodableValue> response;
    int replies = 0;
    const std::string prefix =
        "dev.flutter.pigeon.firebase_core_platform_interface.";
    handlers.at(prefix + method)(bytes->data(), bytes->size(),
                                 [&](const uint8_t* data, size_t size) {
                                   ++replies;
                                   response = codec.DecodeMessage(data, size);
                                 });
    assert(replies == 1 && response);
    return std::get<EncodableList>(*response);
  }
  std::map<std::string, flutter::BinaryMessageHandler> handlers;
};

int main() {
  bool fail_commit = false;
  int commits = 0;
  std::vector<AppRegistry::Change> changes;
  std::map<std::string, App> persisted;
  auto commit =
      [&](const std::string& name,
          const std::optional<App>& app,
          AppRegistry::Change change) -> std::optional<AppRegistry::Error> {
    ++commits;
    changes.push_back(change);
    if (fail_commit)
      return AppRegistry::Error("storage-error", "Synthetic failure");
    if (app)
      persisted.insert_or_assign(name, *app);
    else
      persisted.erase(name);
    return std::nullopt;
  };
  auto constants = [](const std::string&) {
    return EncodableMap{
        {EncodableValue("plugins.flutter.io/firebase_messaging"),
         EncodableValue(EncodableMap{
             {EncodableValue("AUTO_INIT_ENABLED"), EncodableValue(false)}})}};
  };
  AppRegistry registry(commit, constants);
  Messenger messenger;
  FirebaseCoreHostApi::SetUp(&messenger, &registry);
  FirebaseAppHostApi::SetUp(&messenger, &registry);
  auto core =
      messenger.Call("FirebaseCoreHostApi.initializeCore", EncodableValue());
  assert(core.size() == 1 && std::get<EncodableList>(core[0]).empty());
  assert(commits == 0);

  AppRegistry::Options options("synthetic-public-key", "1:123:android:abc",
                               "123", "synthetic-project");
  auto initialized =
      messenger.Call("FirebaseCoreHostApi.initializeApp",
                     EncodableValue(EncodableList{
                         EncodableValue("[DEFAULT]"),
                         EncodableValue(CustomEncodableValue(options))}));
  assert(initialized.size() == 1);
  auto response = std::any_cast<AppRegistry::Response>(
      std::get<CustomEncodableValue>(initialized[0]));
  assert(response.name() == "[DEFAULT]" && response.options() == options);
  assert(*response.is_automatic_data_collection_enabled());
  assert(response.plugin_constants() == constants("[DEFAULT]"));
  assert(commits == 1 && persisted.size() == 1);
  assert(changes.back() == AppRegistry::Change::kInitialize);

  auto duplicate =
      messenger.Call("FirebaseCoreHostApi.initializeApp",
                     EncodableValue(EncodableList{
                         EncodableValue("[DEFAULT]"),
                         EncodableValue(CustomEncodableValue(options))}));
  assert(duplicate.size() == 1 && commits == 1);
  auto changed = options;
  changed.set_project_id("other-project");
  auto conflict =
      messenger.Call("FirebaseCoreHostApi.initializeApp",
                     EncodableValue(EncodableList{
                         EncodableValue("[DEFAULT]"),
                         EncodableValue(CustomEncodableValue(changed))}));
  assert(conflict.size() == 3 &&
         std::get<std::string>(conflict[0]) == "duplicate-app");
  assert(registry.Find("[DEFAULT]")->options == options && commits == 1);

  auto update = [&](const char* method, bool enabled) {
    return messenger.Call(
        method, EncodableValue(EncodableList{EncodableValue("[DEFAULT]"),
                                             EncodableValue(enabled)}));
  };
  // An explicit setter is still an explicit operation at the same value;
  // initialization/resource changes must not accidentally pin SDK defaults.
  assert(update("FirebaseAppHostApi.setAutomaticDataCollectionEnabled", true)
             .size() == 1);
  assert(changes.back() == AppRegistry::Change::kDataCollection);
  assert(update("FirebaseAppHostApi.setAutomaticDataCollectionEnabled", false)
             .size() == 1);
  assert(changes.back() == AppRegistry::Change::kDataCollection);
  assert(!registry.Find("[DEFAULT]")->automatic_data_collection);
  assert(!persisted.at("[DEFAULT]").automatic_data_collection);
  fail_commit = true;
  auto rejected =
      update("FirebaseAppHostApi.setAutomaticResourceManagementEnabled", true);
  assert(rejected.size() == 3 &&
         std::get<std::string>(rejected[0]) == "storage-error");
  assert(!registry.Find("[DEFAULT]")->automatic_resource_management);
  fail_commit = false;
  assert(
      update("FirebaseAppHostApi.setAutomaticResourceManagementEnabled", true)
          .size() == 1);
  assert(registry.Find("[DEFAULT]")->automatic_resource_management);
  assert(changes.back() == AppRegistry::Change::kResourceManagement);
  auto removed = messenger.Call(
      "FirebaseAppHostApi.delete",
      EncodableValue(EncodableList{EncodableValue("[DEFAULT]")}));
  assert(removed.size() == 1 && !registry.Find("[DEFAULT]") &&
         persisted.empty());
  assert(changes.back() == AppRegistry::Change::kDelete);

  AppRegistry restored(commit, constants, options);
  bool restored_core = false;
  restored.InitializeCore([&](AppRegistry::Reply<EncodableList> value) {
    assert(!value.has_error() && value.value().empty());
    restored_core = true;
  });
  assert(restored_core && !restored.Find("[DEFAULT]"));
  restored.OptionsFromResource(
      [&](AppRegistry::Reply<AppRegistry::Options> value) {
        assert(!value.has_error() && value.value() == options);
      });
  restored.InitializeApp("[DEFAULT]", options,
                         [](auto value) { assert(!value.has_error()); });
  const int before_read = commits;
  restored.InitializeCore([](AppRegistry::Reply<EncodableList> value) {
    assert(!value.has_error() && value.value().size() == 1);
  });
  assert(commits == before_read);
  restored.Delete("[DEFAULT]", [](std::optional<AppRegistry::Error> error) {
    assert(!error);
  });
  restored.InitializeCore([](AppRegistry::Reply<EncodableList> value) {
    assert(!value.has_error() && value.value().empty());
  });
  assert(!restored.Find("[DEFAULT]"));
  std::cout << "Firebase Core original Pigeon codec, identity and state "
               "rollback passed\n";
}
