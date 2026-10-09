// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <algorithm>
#include <optional>
#include <utility>

#include "app_registry.h"
#include "state_document.h"

namespace flutter::winrt::plugins::firebase_core {
struct MessagingConfiguration {
  std::string api_key, app_id, project_id, sender_id, android_package,
      certificate_sha1;
};
struct ConfiguredApp {
  std::string name, display_name;
  AppRegistry::Options core_options;
  std::optional<MessagingConfiguration> messaging;
};

class Configuration {
 public:
  static Configuration Parse(const StateBytes& bytes) {
    const auto document = StateDocument::Parse(bytes, false);
    if (!document.HasMember("apps") || !document["apps"].IsArray() ||
        document["apps"].Empty())
      throw StateError("invalid-firebase-configuration");
    Configuration result;
    if (document.HasMember("backgroundDispatcher")) {
      const auto& dispatcher = document["backgroundDispatcher"];
      StateDocument::UniqueFields(dispatcher);
      result.background_dispatcher_ = std::make_pair(
          Text(dispatcher, "libraryUri"), Text(dispatcher, "entrypoint"));
    }
    for (const auto& value : document["apps"].GetArray()) {
      StateDocument::UniqueFields(value);
      if (!value.HasMember("coreOptions"))
        throw StateError("invalid-firebase-configuration");
      ConfiguredApp app{Text(value, "appName"), Text(value, "displayName"),
                        ParseOptions(value["coreOptions"]), std::nullopt};
      if (result.Find(app.name))
        throw StateError("invalid-firebase-configuration");
      if (value.HasMember("messaging")) {
        const auto& source = value["messaging"];
        StateDocument::UniqueFields(source);
        MessagingConfiguration configuration{
            Text(source, "apiKey"),         Text(source, "appId"),
            Text(source, "projectId"),      Text(source, "senderId"),
            Text(source, "androidPackage"), Text(source, "certificateSha1")};
        if (configuration.project_id != app.core_options.project_id() ||
            configuration.certificate_sha1.size() != 40 ||
            !std::all_of(
                configuration.certificate_sha1.begin(),
                configuration.certificate_sha1.end(), [](unsigned char c) {
                  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
                }))
          throw StateError("invalid-firebase-configuration");
        app.messaging = std::move(configuration);
      }
      result.apps_.push_back(std::move(app));
    }
    return result;
  }
  const ConfiguredApp* Find(const std::string& name) const {
    const auto found =
        std::find_if(apps_.begin(), apps_.end(),
                     [&](const auto& app) { return app.name == name; });
    return found == apps_.end() ? nullptr : &*found;
  }
  const std::vector<ConfiguredApp>& Apps() const { return apps_; }
  std::optional<AppRegistry::Options> DefaultOptions() const {
    const auto* app = Find("[DEFAULT]");
    return app ? std::make_optional(app->core_options) : std::nullopt;
  }
  const std::optional<std::pair<std::string, std::string>>&
  BackgroundDispatcher() const {
    return background_dispatcher_;
  }
  static AppRegistry::Options ParseOptions(const rapidjson::Value& source) {
    StateDocument::UniqueFields(source);
    AppRegistry::Options options(Text(source, "apiKey"), Text(source, "appId"),
                                 Text(source, "messagingSenderId"),
                                 Text(source, "projectId"));
    auto optional = [&](const char* key, auto setter) {
      if (source.HasMember(key) && !source[key].IsNull())
        setter(Text(source, key));
    };
    optional("authDomain", [&](auto value) { options.set_auth_domain(value); });
    optional("databaseURL",
             [&](auto value) { options.set_database_u_r_l(value); });
    optional("storageBucket",
             [&](auto value) { options.set_storage_bucket(value); });
    optional("measurementId",
             [&](auto value) { options.set_measurement_id(value); });
    optional("trackingId", [&](auto value) { options.set_tracking_id(value); });
    optional("deepLinkURLScheme",
             [&](auto value) { options.set_deep_link_u_r_l_scheme(value); });
    optional("androidClientId",
             [&](auto value) { options.set_android_client_id(value); });
    optional("iosClientId",
             [&](auto value) { options.set_ios_client_id(value); });
    optional("iosBundleId",
             [&](auto value) { options.set_ios_bundle_id(value); });
    optional("appGroupId",
             [&](auto value) { options.set_app_group_id(value); });
    return options;
  }

 private:
  static std::string Text(const rapidjson::Value& object, const char* key) {
    if (!object.HasMember(key) || !object[key].IsString() ||
        !object[key].GetStringLength())
      throw StateError("invalid-firebase-configuration");
    std::string value(object[key].GetString(), object[key].GetStringLength());
    if (std::any_of(value.begin(), value.end(),
                    [](unsigned char c) { return c < 32; }))
      throw StateError("invalid-firebase-configuration");
    return value;
  }
  std::vector<ConfiguredApp> apps_;
  std::optional<std::pair<std::string, std::string>> background_dispatcher_;
};
}  // namespace flutter::winrt::plugins::firebase_core
