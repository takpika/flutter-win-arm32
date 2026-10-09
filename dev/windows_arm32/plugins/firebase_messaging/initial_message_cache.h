// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <optional>
#include "../firebase_core/state_document.h"
#include "messaging_app_identity.h"
#include "sdk_remote_message.h"

namespace flutter::winrt::plugins::firebase_messaging {
// Flutter owns getInitialMessage consumption, independently of SDK delivery.
// A cold opened callback may return S_OK only after Save commits the complete
// public message. No SDK lookup, receipt retention or notification policy is
// needed to read this cache after process death. Never use it for warm taps.
class InitialMessageCache {
 public:
  explicit InitialMessageCache(firebase_core::StateTransaction transaction)
      : transaction_(std::move(transaction)) {}

  void Save(const MessagingAppIdentity& app, int64_t receipt,
            const std::string& json) const {
    Check(app, receipt);
    const auto message = Decode(json);
    transaction_([&](const auto& bytes, bool missing, auto& next) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      if (auto* previous = Find(document, app, receipt)) {
        if (!(*previous)["consumed"].GetBool() &&
            Decode(Text((*previous)["message"])) != message)
          throw firebase_core::StateError("initial-message-conflict");
        return false;  // SDK replay cannot revive a consumed initial message.
      }
      auto& allocator = document.GetAllocator();
      if (!document.HasMember(kModule))
        document.AddMember(rapidjson::StringRef(kModule),
                           rapidjson::Value(rapidjson::kArrayType), allocator);
      rapidjson::Value record(rapidjson::kObjectType);
      Put(record, "appName", app.name, document);
      Put(record, "projectId", app.project_id, document);
      Put(record, "appId", app.app_id, document);
      record.AddMember("receipt", receipt, allocator);
      record.AddMember("consumed", false, allocator);
      Put(record, "message", json, document);
      document[kModule].PushBack(record, allocator);
      next = firebase_core::StateDocument::Encode(document);
      return true;
    });
  }

  std::optional<EncodableMap> Take(const MessagingAppIdentity& app,
                                 int64_t receipt) const {
    Check(app, receipt);
    std::optional<EncodableMap> result;
    transaction_([&](const auto& bytes, bool missing, auto& next) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      auto* record = Find(document, app, receipt);
      if (!record || (*record)["consumed"].GetBool()) return false;
      result = Decode(Text((*record)["message"]));
      (*record)["consumed"].SetBool(true);
      record->RemoveMember("message");
      next = firebase_core::StateDocument::Encode(document);
      return true;
    });
    // A failed atomic commit throws, so no message escapes before consumption.
    return result;
  }

  std::vector<int64_t> Pending(const MessagingAppIdentity& app) const {
    Check(app);
    std::vector<int64_t> result;
    transaction_([&](const auto& bytes, bool missing, auto&) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      if (auto* entries = Records(document))
        for (const auto& record : entries->GetArray())
          if (Matches(record, app) && !record["consumed"].GetBool())
            result.push_back(record["receipt"].GetInt64());
      return false;
    });
    return result;
  }

  bool Contains(const MessagingAppIdentity& app, int64_t receipt) const {
    Check(app, receipt);
    bool result = false;
    transaction_([&](const auto& bytes, bool missing, auto&) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      result = Find(document, app, receipt) != nullptr;
      return false;
    });
    return result;
  }

 private:
  static constexpr const char* kModule = "flutterInitialMessages";
  static std::string Text(const rapidjson::Value& value) {
    if (!value.IsString()) throw firebase_core::StateError("invalid-local-state");
    return {value.GetString(), value.GetStringLength()};
  }
  static void Check(const MessagingAppIdentity& app) {
    for (const auto* text : {&app.name, &app.project_id, &app.app_id})
      if (text->empty() || text->find('\0') != std::string::npos)
        throw firebase_core::StateError("invalid-argument");
  }
  static void Check(const MessagingAppIdentity& app, int64_t receipt) {
    Check(app);
    if (receipt <= 0) throw firebase_core::StateError("invalid-argument");
  }
  static EncodableMap Decode(const std::string& json) {
    return DecodeSdkRemoteMessage(reinterpret_cast<const uint8_t*>(json.data()),
                                  json.size());
  }
  static rapidjson::Value* Records(rapidjson::Document& document) {
    if (!document.HasMember(kModule)) return nullptr;
    auto& entries = document[kModule];
    if (!entries.IsArray()) throw firebase_core::StateError("invalid-local-state");
    for (auto& record : entries.GetArray()) {
      firebase_core::StateDocument::UniqueFields(record);
      for (const auto* key : {"appName", "projectId", "appId"})
        if (!record.HasMember(key) || Text(record[key]).empty() ||
            Text(record[key]).find('\0') != std::string::npos)
          throw firebase_core::StateError("invalid-local-state");
      if (!record.HasMember("receipt") || !record["receipt"].IsInt64() ||
          record["receipt"].GetInt64() <= 0 || !record.HasMember("consumed") ||
          !record["consumed"].IsBool() ||
          (!record["consumed"].GetBool() &&
           (!record.HasMember("message") || !record["message"].IsString())))
        throw firebase_core::StateError("invalid-local-state");
    }
    return &entries;
  }
  static bool Matches(const rapidjson::Value& record, const MessagingAppIdentity& app) {
    return Text(record["appName"]) == app.name &&
           Text(record["projectId"]) == app.project_id &&
           Text(record["appId"]) == app.app_id;
  }
  static rapidjson::Value* Find(rapidjson::Document& document,
                               const MessagingAppIdentity& app, int64_t receipt) {
    auto* entries = Records(document);
    if (!entries) return nullptr;
    rapidjson::Value* found = nullptr;
    for (auto& record : entries->GetArray()) {
      if (Matches(record, app) && record["receipt"].GetInt64() == receipt) {
        if (found) throw firebase_core::StateError("invalid-local-state");
        found = &record;
      }
    }
    return found;
  }
  static void Put(rapidjson::Value& record, const char* key,
                  const std::string& text, rapidjson::Document& document) {
    rapidjson::Value value;
    value.SetString(text.data(), static_cast<rapidjson::SizeType>(text.size()),
                    document.GetAllocator());
    record.AddMember(rapidjson::StringRef(key), value, document.GetAllocator());
  }
  firebase_core::StateTransaction transaction_;
};
}  // namespace flutter::winrt::plugins::firebase_messaging
