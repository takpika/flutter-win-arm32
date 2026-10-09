// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

// Use the dependency's original Pigeon types and codec, not a parallel
// protocol.
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "messages.g.h"

namespace flutter::winrt::plugins::firebase_core {

enum class DataCollectionSource { kInherited, kExplicitSetting, kLegacySaved };

struct App {
  firebase_core_windows::CoreFirebaseOptions options;
  bool automatic_data_collection = true;
  bool automatic_resource_management = false;
  DataCollectionSource data_collection_source = DataCollectionSource::kInherited;
};

class AppRegistry final : public firebase_core_windows::FirebaseCoreHostApi,
                          public firebase_core_windows::FirebaseAppHostApi {
 public:
  using Error = firebase_core_windows::FlutterError;
  using Options = firebase_core_windows::CoreFirebaseOptions;
  using Response = firebase_core_windows::CoreInitializeResponse;
  enum class Change { kInitialize, kDataCollection, kResourceManagement, kDelete };
  template <class T>
  using Reply = firebase_core_windows::ErrorOr<T>;
  // The host persists an update and applies it to its registered Firebase
  // services before accepting it. Failure leaves the previous registry intact.
  using Commit = std::function<std::optional<Error>(const std::string&,
                                                    const std::optional<App>&,
                                                    Change)>;
  using Constants = std::function<EncodableMap(const std::string&)>;
  using Defaults =
      std::function<std::optional<Error>(const std::string&, App&)>;

  explicit AppRegistry(Commit commit, Constants constants,
                       std::optional<Options> resource_options = std::nullopt,
                       Defaults defaults = {})
      : commit_(std::move(commit)),
        constants_(std::move(constants)),
        resource_options_(std::move(resource_options)),
        defaults_(std::move(defaults)) {}

  std::vector<std::string> Names() const {
    std::vector<std::string> names;
    for (const auto& entry : apps_) names.push_back(entry.first);
    return names;
  }

  const App* Find(const std::string& name) const {
    const auto entry = apps_.find(name);
    return entry == apps_.end() ? nullptr : &entry->second;
  }

  // Called for native app instances already alive in this host. Merely saved
  // options must not resurrect secondary apps after process restart.
  void Restore(const std::string& name, App app) {
    apps_.insert_or_assign(name, std::move(app));
  }

  void InitializeApp(const std::string& name, const Options& options,
                     std::function<void(Reply<Response>)> result) override {
    if (const auto* existing = Find(name)) {
      if (existing->options != options) {
        result(Error("duplicate-app", "A Firebase app already uses this name"));
      } else {
        result(Describe(name, *existing));
      }
      return;
    }
    App app{options};
    if (defaults_) {
      if (const auto error = defaults_(name, app)) {
        result(*error);
        return;
      }
    }
    if (const auto error = commit_(name, app, Change::kInitialize)) {
      result(*error);
      return;
    }
    apps_.emplace(name, std::move(app));
    result(Describe(name, apps_.at(name)));
  }

  void InitializeCore(
      std::function<void(Reply<EncodableList>)> result) override {
    // As in the original Windows host, enumerate existing native apps. Resource
    // options are used only when Dart explicitly asks OptionsFromResource;
    // creating a default here could conflict with explicit Windows options.
    EncodableList responses;
    for (const auto& entry : apps_) {
      responses.emplace_back(
          CustomEncodableValue(Describe(entry.first, entry.second)));
    }
    result(std::move(responses));
  }

  void OptionsFromResource(
      std::function<void(Reply<Options>)> result) override {
    if (resource_options_) {
      result(*resource_options_);
    } else {
      result(
          Error("no-options",
                "No native Firebase application configuration is installed"));
    }
  }

  void SetAutomaticDataCollectionEnabled(
      const std::string& name, bool enabled,
      std::function<void(std::optional<Error>)> result) override {
    Update(
        name, Change::kDataCollection,
        [enabled](App& app) {
          app.automatic_data_collection = enabled;
          app.data_collection_source = DataCollectionSource::kExplicitSetting;
        },
        std::move(result));
  }

  void SetAutomaticResourceManagementEnabled(
      const std::string& name, bool enabled,
      std::function<void(std::optional<Error>)> result) override {
    Update(
        name, Change::kResourceManagement,
        [enabled](App& app) { app.automatic_resource_management = enabled; },
        std::move(result));
  }

  void Delete(const std::string& name,
              std::function<void(std::optional<Error>)> result) override {
    if (!Find(name)) {
      result(MissingApp());
      return;
    }
    if (const auto error = commit_(name, std::nullopt, Change::kDelete)) {
      result(error);
      return;
    }
    apps_.erase(name);
    result(std::nullopt);
  }

 private:
  Response Describe(const std::string& name, const App& app) const {
    return Response(name, app.options, &app.automatic_data_collection,
                    constants_(name));
  }

  static Error MissingApp() {
    return Error("no-app", "No Firebase app uses this name");
  }

  void Update(const std::string& name, Change change,
              const std::function<void(App&)>& update,
              std::function<void(std::optional<Error>)> result) {
    const auto* existing = Find(name);
    if (!existing) {
      result(MissingApp());
      return;
    }
    App replacement = *existing;
    update(replacement);
    if (const auto error = commit_(name, replacement, change)) {
      result(error);
      return;
    }
    apps_.insert_or_assign(name, std::move(replacement));
    result(std::nullopt);
  }

  Commit commit_;
  Constants constants_;
  std::optional<Options> resource_options_;
  std::map<std::string, App> apps_;
  Defaults defaults_;
};

}  // namespace flutter::winrt::plugins::firebase_core
