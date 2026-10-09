// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../settings_store.h"

#include <cassert>
#include <iostream>


using namespace flutter::winrt::plugins::firebase_core;

void VerifyInheritedDefaults() {
  StateBytes disk;
  bool missing = true, inherited = false, fail = false;
  SettingsStore store([&](const StateUpdate& update) {
    StateBytes replacement;
    if (update(disk, missing, replacement)) {
      if (fail) throw StateError("storage-error");
      disk = std::move(replacement);
      missing = false;
    }
  });
  auto commit = [&](const std::string& name, const std::optional<App>& app,
                    AppRegistry::Change)
      -> std::optional<AppRegistry::Error> {
    try {
      if (app) store.Save(name, *app);
      return std::nullopt;
    } catch (const StateError& error) {
      return AppRegistry::Error(error.what());
    }
  };
  auto defaults = [&](const std::string& name, App& app)
      -> std::optional<AppRegistry::Error> {
    // The resolved SDK value is an input, not a policy recomputed by Core.
    app.automatic_data_collection = inherited;
    store.Load(name, app);
    return std::nullopt;
  };
  auto constants = [](const std::string&) { return flutter::EncodableMap{}; };
  AppRegistry::Options options("synthetic-api", "synthetic-app", "sender", "project");
  auto create = [&](AppRegistry& registry) {
    registry.InitializeApp("[DEFAULT]", options,
                           [](auto reply) { assert(!reply.has_error()); });
  };
  AppRegistry first(commit, constants, std::nullopt, defaults);
  create(first);
  assert(!first.Find("[DEFAULT]")->automatic_data_collection);
  assert(first.Find("[DEFAULT]")->data_collection_source == DataCollectionSource::kInherited);
  first.SetAutomaticResourceManagementEnabled("[DEFAULT]", true,
                                             [](auto error) { assert(!error); });
  inherited = true;
  AppRegistry next(commit, constants, std::nullopt, defaults);
  create(next);
  assert(next.Find("[DEFAULT]")->automatic_data_collection);
  assert(next.Find("[DEFAULT]")->data_collection_source == DataCollectionSource::kInherited);
  const auto before = disk;
  fail = true;
  next.SetAutomaticDataCollectionEnabled("[DEFAULT]", true,
                                        [](auto error) { assert(error); });
  assert(next.Find("[DEFAULT]")->data_collection_source == DataCollectionSource::kInherited && disk == before);
  fail = false;
  // A same-value setter still establishes an explicit override.
  next.SetAutomaticDataCollectionEnabled("[DEFAULT]", true,
                                        [](auto error) { assert(!error); });
  inherited = false;
  AppRegistry explicit_value(commit, constants, std::nullopt, defaults);
  create(explicit_value);
  assert(explicit_value.Find("[DEFAULT]")->automatic_data_collection);
  assert(explicit_value.Find("[DEFAULT]")->data_collection_source == DataCollectionSource::kExplicitSetting);
  auto legacy = StateDocument::Parse(disk, false);
  legacy["coreSettings"][0].RemoveMember("dataCollectionExplicit");
  disk = StateDocument::Encode(legacy);
  App restored{options};
  restored.automatic_data_collection = false;
  store.Load("[DEFAULT]", restored);
  assert(restored.automatic_data_collection && restored.data_collection_source == DataCollectionSource::kLegacySaved);
  store.Save("[DEFAULT]", restored);
  assert(!StateDocument::Parse(disk, false)["coreSettings"][0].HasMember("dataCollectionExplicit"));
  legacy["coreSettings"][0].AddMember("dataCollectionExplicit", "bad",
                                    legacy.GetAllocator());
  disk = StateDocument::Encode(legacy);
  try {
    store.Load("[DEFAULT]", restored);
    assert(false);
  } catch (const StateError& error) {
    assert(std::string(error.what()) == "invalid-local-state");
  }
}

int main() {
  VerifyInheritedDefaults();
  bool missing = true, fail = false;
  StateBytes disk;
  SettingsStore store([&](const StateUpdate& update) {
    StateBytes replacement;
    if (update(disk, missing, replacement)) {
      if (fail) throw StateError("storage-error");
      disk = std::move(replacement);
      missing = false;
    }
  });
  auto commit =
      [&](const std::string& name,
          const std::optional<App>& app,
          AppRegistry::Change) -> std::optional<AppRegistry::Error> {
    try {
      if (app) store.Save(name, *app);
      return std::nullopt;
    } catch (const StateError& error) {
      return AppRegistry::Error(error.what());
    }
  };
  auto defaults = [&](const std::string& name,
                      App& app) -> std::optional<AppRegistry::Error> {
    try {
      store.Load(name, app);
      return std::nullopt;
    } catch (const StateError& error) {
      return AppRegistry::Error(error.what());
    }
  };
  AppRegistry::Options options("synthetic-api", "synthetic-app", "sender",
                               "project");
  auto constants = [](const std::string&) { return flutter::EncodableMap{}; };
  AppRegistry first(commit, constants, std::nullopt, defaults);
  first.InitializeApp("[DEFAULT]", options,
                      [](auto reply) { assert(!reply.has_error()); });
  assert(!missing && first.Find("[DEFAULT]")->automatic_data_collection);
  first.SetAutomaticDataCollectionEnabled("[DEFAULT]", false,
                                          [](auto error) { assert(!error); });
  first.SetAutomaticResourceManagementEnabled(
      "[DEFAULT]", true, [](auto error) { assert(!error); });
  const auto saved = disk;
  fail = true;
  first.SetAutomaticDataCollectionEnabled("[DEFAULT]", true, [](auto error) {
    assert(error && error->code() == "storage-error");
  });
  assert(!first.Find("[DEFAULT]")->automatic_data_collection && disk == saved);
  fail = false;
  first.Delete("[DEFAULT]", [](auto error) { assert(!error); });
  assert(disk == saved);
  AppRegistry restarted(commit, constants, std::nullopt, defaults);
  restarted.InitializeCore([](auto reply) { assert(reply.value().empty()); });
  assert(!restarted.Find("[DEFAULT]"));
  restarted.InitializeApp("[DEFAULT]", options,
                          [](auto reply) { assert(!reply.has_error()); });
  assert(!restarted.Find("[DEFAULT]")->automatic_data_collection);
  assert(restarted.Find("[DEFAULT]")->automatic_resource_management);
  auto other = options;
  other.set_project_id("different-project");
  AppRegistry different(commit, constants, std::nullopt, defaults);
  different.InitializeApp("[DEFAULT]", other,
                          [](auto reply) { assert(!reply.has_error()); });
  assert(different.Find("[DEFAULT]")->automatic_data_collection);
  assert(!different.Find("[DEFAULT]")->automatic_resource_management);

  // Core treats SDK-owned modules as opaque, preserving even future fields.
  auto document = StateDocument::Parse(disk, false);
  rapidjson::Document foreign;
  foreign.Parse(R"({"nested":{"future":[null,true,17,"日本語"]},"revision":23})");
  for (const auto* module : {"installations", "messaging", "inbox", "metrics", "topics"}) {
    rapidjson::Value value;
    value.CopyFrom(foreign, document.GetAllocator());
    document.AddMember(rapidjson::StringRef(module), value, document.GetAllocator());
  }
  disk = StateDocument::Encode(document);
  App changed{options};
  changed.automatic_data_collection = false;
  store.Save("[DEFAULT]", changed);
  document = StateDocument::Parse(disk, false);
  for (const auto* module : {"installations", "messaging", "inbox", "metrics", "topics"})
    assert(document[module] == foreign);

  document["coreSettings"][0]["dataCollection"].SetString("bad");
  disk = StateDocument::Encode(document);
  const auto damaged = disk;
  AppRegistry corrupt(commit, constants, std::nullopt, defaults);
  corrupt.InitializeApp("[DEFAULT]", options, [](auto reply) {
    assert(reply.has_error() && reply.error().code() == "invalid-local-state");
  });
  assert(!corrupt.Find("[DEFAULT]") && disk == damaged);
  std::cout << "Core persisted preferences: explicit app creation, project "
               "isolation, rollback, no resurrection and FIS module "
               "preservation PASS\n";
}
