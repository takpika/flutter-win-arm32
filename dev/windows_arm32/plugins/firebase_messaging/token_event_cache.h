// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <optional>

#include "../firebase_core/state_document.h"
#include "messaging_app_identity.h"

namespace flutter::winrt::plugins::firebase_messaging {
struct TokenEvent {
  int64_t generation;
  std::string token;
};

// Custody of an SDK token-refresh callback while the Dart channel is absent.
// This is not the registration/token store: getToken, deletion, generation and
// retry policy belong to the SDK. Accept only SDK events, preserving the latest
// generation until Dart consumes it. Never fill this from getToken results.
class TokenEventCache {
 public:
  explicit TokenEventCache(firebase_core::StateTransaction transaction)
      : transaction_(std::move(transaction)) {}

  void Save(const MessagingAppIdentity& app, const TokenEvent& event) const {
    Check(app);
    if (event.generation <= 0 || event.token.empty() ||
        event.token.find('\0') != std::string::npos)
      throw firebase_core::StateError("invalid-token-event");
    transaction_([&](const auto& bytes, bool missing, auto& next) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      auto* previous = Find(document, app);
      if (previous) {
        const auto generation = (*previous)["generation"].GetInt64();
        if (event.generation < generation) return false;
        if (event.generation == generation) {
          if (!(*previous)["consumed"].GetBool() &&
              Text((*previous)["token"]) != event.token)
            throw firebase_core::StateError("token-event-conflict");
          return false;  // SDK replay cannot revive a consumed generation.
        }
      }
      auto& allocator = document.GetAllocator();
      if (!document.HasMember(kModule))
        document.AddMember(rapidjson::StringRef(kModule),
                           rapidjson::Value(rapidjson::kArrayType), allocator);
      rapidjson::Value record(rapidjson::kObjectType);
      Put(record, "appName", app.name, document);
      Put(record, "projectId", app.project_id, document);
      Put(record, "appId", app.app_id, document);
      record.AddMember("generation", event.generation, allocator);
      record.AddMember("consumed", false, allocator);
      Put(record, "token", event.token, document);
      if (previous) *previous = std::move(record);
      else document[kModule].PushBack(record, allocator);
      next = firebase_core::StateDocument::Encode(document);
      return true;
    });
  }

  std::optional<TokenEvent> Read(const MessagingAppIdentity& app) const {
    Check(app);
    std::optional<TokenEvent> result;
    transaction_([&](const auto& bytes, bool missing, auto&) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      if (auto* record = Find(document, app);
          record && !(*record)["consumed"].GetBool())
        result = TokenEvent{(*record)["generation"].GetInt64(),
                            Text((*record)["token"])};
      return false;
    });
    return result;
  }

  void Complete(const MessagingAppIdentity& app, int64_t generation) const {
    Check(app);
    transaction_([&](const auto& bytes, bool missing, auto& next) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      auto* record = Find(document, app);
      // A late Dart reply for generation N cannot consume a newer N+1 event.
      if (!record || (*record)["generation"].GetInt64() != generation ||
          (*record)["consumed"].GetBool()) return false;
      (*record)["consumed"].SetBool(true);
      record->RemoveMember("token");
      next = firebase_core::StateDocument::Encode(document);
      return true;
    });
  }

 private:
  static constexpr const char* kModule = "flutterTokenEvents";
  static std::string Text(const rapidjson::Value& value) {
    if (!value.IsString())
      throw firebase_core::StateError("invalid-token-event-cache");
    return {value.GetString(), value.GetStringLength()};
  }
  static void Check(const MessagingAppIdentity& app) {
    for (const auto* text : {&app.name, &app.project_id, &app.app_id})
      if (text->empty() || text->find('\0') != std::string::npos)
        throw firebase_core::StateError("invalid-argument");
  }
  static rapidjson::Value* Find(rapidjson::Document& document,
                                const MessagingAppIdentity& app) {
    if (!document.HasMember(kModule)) return nullptr;
    auto& records = document[kModule];
    if (!records.IsArray())
      throw firebase_core::StateError("invalid-token-event-cache");
    rapidjson::Value* found = nullptr;
    for (auto& record : records.GetArray()) {
      firebase_core::StateDocument::UniqueFields(record);
      for (const auto* field : {"appName", "projectId", "appId"})
        if (!record.HasMember(field) || Text(record[field]).empty())
          throw firebase_core::StateError("invalid-token-event-cache");
      if (!record.HasMember("generation") || !record["generation"].IsInt64() ||
          record["generation"].GetInt64() <= 0 ||
          !record.HasMember("consumed") || !record["consumed"].IsBool())
        throw firebase_core::StateError("invalid-token-event-cache");
      if (!record["consumed"].GetBool() &&
          (!record.HasMember("token") || Text(record["token"]).empty()))
        throw firebase_core::StateError("invalid-token-event-cache");
      if (Text(record["appName"]) == app.name &&
          Text(record["projectId"]) == app.project_id &&
          Text(record["appId"]) == app.app_id) {
        if (found) throw firebase_core::StateError("invalid-token-event-cache");
        found = &record;
      }
    }
    return found;
  }
  static void Put(rapidjson::Value& object, const char* key,
                  const std::string& text, rapidjson::Document& document) {
    object.AddMember(rapidjson::StringRef(key),
                     rapidjson::Value(text.data(),
                                      static_cast<rapidjson::SizeType>(text.size()),
                                      document.GetAllocator()),
                     document.GetAllocator());
  }
  firebase_core::StateTransaction transaction_;
};
}  // namespace flutter::winrt::plugins::firebase_messaging
