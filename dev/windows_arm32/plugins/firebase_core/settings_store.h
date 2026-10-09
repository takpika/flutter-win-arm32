// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <tuple>
#include <utility>

#include "app_registry.h"
#include "state_document.h"

namespace flutter::winrt::plugins::firebase_core {
// Preferences persist across native App deletion/recreation. Merely reading
// saved preferences never creates a Firebase app or starts a Firebase service.
class SettingsStore {
 public:
  explicit SettingsStore(StateTransaction transaction)
      : transaction_(std::move(transaction)) {}
  void Load(const std::string& name, App& app) const {
    transaction_([&](const auto& bytes, bool missing, auto&) {
      auto document = StateDocument::Parse(bytes, missing);
      if (auto* record = Find(document, name, app)) {
        // Legacy records cannot distinguish a setter from initialization;
        // retain their saved value. New inherited values follow SDK defaults.
        app.data_collection_source = !record->HasMember("dataCollectionExplicit")
            ? DataCollectionSource::kLegacySaved
            : ((*record)["dataCollectionExplicit"].GetBool()
                   ? DataCollectionSource::kExplicitSetting
                   : DataCollectionSource::kInherited);
        if (app.data_collection_source != DataCollectionSource::kInherited)
          app.automatic_data_collection = (*record)["dataCollection"].GetBool();
        app.automatic_resource_management =
            (*record)["resourceManagement"].GetBool();
      }
      return false;
    });
  }
  void Save(const std::string& name, const App& app) const {
    transaction_([&](const auto& bytes, bool missing, auto& replacement) {
      auto document = StateDocument::Parse(bytes, missing);
      auto* record = Find(document, name, app);
      if (!document.HasMember("coreSettings"))
        document.AddMember("coreSettings",
                           rapidjson::Value(rapidjson::kArrayType),
                           document.GetAllocator());
      if (!record) {
        rapidjson::Value created(rapidjson::kObjectType);
        Put(created, "appName", name, document);
        Put(created, "projectId", app.options.project_id(), document);
        Put(created, "appId", app.options.app_id(), document);
        created.AddMember("dataCollection", true, document.GetAllocator());
        created.AddMember("resourceManagement", false, document.GetAllocator());
        auto& entries = document["coreSettings"];
        entries.PushBack(created, document.GetAllocator());
        record = &entries[entries.Size() - 1];
      }
      (*record)["dataCollection"].SetBool(app.automatic_data_collection);
      if (app.data_collection_source == DataCollectionSource::kLegacySaved) {
        record->RemoveMember("dataCollectionExplicit");
      } else {
        if (!record->HasMember("dataCollectionExplicit"))
          record->AddMember("dataCollectionExplicit", false,
                            document.GetAllocator());
        (*record)["dataCollectionExplicit"].SetBool(
            app.data_collection_source == DataCollectionSource::kExplicitSetting);
      }
      (*record)["resourceManagement"].SetBool(
          app.automatic_resource_management);
      replacement = StateDocument::Encode(document);
      return true;
    });
  }

 private:
  static std::string Text(const rapidjson::Value& object, const char* name) {
    if (!object.HasMember(name) || !object[name].IsString())
      throw StateError("invalid-local-state");
    return {object[name].GetString(), object[name].GetStringLength()};
  }
  static rapidjson::Value* Find(rapidjson::Document& document,
                                const std::string& name, const App& app) {
    if (!document.HasMember("coreSettings")) return nullptr;
    if (!document["coreSettings"].IsArray())
      throw StateError("invalid-local-state");
    std::set<std::tuple<std::string, std::string, std::string>> identities;
    rapidjson::Value* found = nullptr;
    for (auto& entry : document["coreSettings"].GetArray()) {
      StateDocument::UniqueFields(entry);
      const auto entry_name = Text(entry, "appName");
      const auto project = Text(entry, "projectId"),
                 app_id = Text(entry, "appId");
      if (!identities.emplace(entry_name, project, app_id).second ||
          !entry.HasMember("dataCollection") ||
          !entry["dataCollection"].IsBool() ||
          (entry.HasMember("dataCollectionExplicit") &&
           !entry["dataCollectionExplicit"].IsBool()) ||
          !entry.HasMember("resourceManagement") ||
          !entry["resourceManagement"].IsBool())
        throw StateError("invalid-local-state");
      if (entry_name == name && project == app.options.project_id() &&
          app_id == app.options.app_id())
        found = &entry;
    }
    return found;
  }
  static void Put(rapidjson::Value& object, const char* name,
                  const std::string& text, rapidjson::Document& document) {
    rapidjson::Value value;
    value.SetString(text.data(), static_cast<rapidjson::SizeType>(text.size()),
                    document.GetAllocator());
    object.AddMember(rapidjson::StringRef(name), value,
                     document.GetAllocator());
  }
  StateTransaction transaction_;
};
}  // namespace flutter::winrt::plugins::firebase_core
