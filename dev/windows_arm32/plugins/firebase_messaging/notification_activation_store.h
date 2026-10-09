// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once
#include <firebase_compat/sdk.h>

#include "initial_message_cache.h"

namespace flutter::winrt::plugins::firebase_messaging {
// Framework custody of real OS activation classification, never SDK receipt
// state. Store before queueing so process death cannot turn a cold tap warm.
class NotificationActivationStore {
 public:
  explicit NotificationActivationStore(
      firebase_core::StateTransaction transaction)
      : transaction_(transaction), initial_(std::move(transaction)) {}
  void Observe(const std::string& launch, bool initial) const {
    if (launch.empty() || launch.find('\0') != std::string::npos)
      throw firebase_core::StateError("invalid-notification-activation");
    transaction_([&](const auto& bytes, bool missing, auto& next) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      auto* records = Records(document);
      if (records)
        for (const auto& record : records->GetArray())
          if (Text(record["launch"]) == launch) return false;
      auto& allocator = document.GetAllocator();
      if (!records) {
        document.AddMember(rapidjson::StringRef(kModule),
                           rapidjson::Value(rapidjson::kArrayType), allocator);
        records = &document[kModule];
      }
      rapidjson::Value record(rapidjson::kObjectType), value;
      value.SetString(launch.data(),
                      static_cast<rapidjson::SizeType>(launch.size()),
                      allocator);
      record.AddMember("launch", value, allocator);
      record.AddMember("initial", initial, allocator);
      records->PushBack(record, allocator);
      next = firebase_core::StateDocument::Encode(document);
      return true;
    });
  }
  // Protect the real OS tap before Dart/Core initialization. The saved SDK
  // descriptor owns its identity and settings; do not invent a new Core app.
  // Retain the raw intent until full initial-message custody can be committed.
  void ProtectSaved(const char* app_name) const {
    const auto events = Read();
    if (events.empty()) return;
    struct SavedClient {
      fcm_sdk_handle value = 0;
      ~SavedClient() { if (value) fcm_sdk_close(value); }
    } client;
    const HRESULT opened = fcm_sdk_open_saved(app_name, &client.value);
    if (opened == 1) return;  // No previous descriptor; await normal Core init.
    Check(opened);
    for (const auto& event : events) {
      int64_t receipt = 0;
      const HRESULT queued = fcm_sdk_queue_activation(
          client.value, event.launch.c_str(), &receipt);
      if (queued == 1) {
        Remove(event.launch);
        continue;
      }
      Check(queued);
      if (receipt <= 0)
        throw firebase_core::StateError("invalid-notification-receipt");
    }
  }
  void Queue(fcm_sdk_handle client, const MessagingAppIdentity& app) const {
    for (const auto& event : Read()) {
      int64_t receipt = 0;
      const HRESULT queued =
          fcm_sdk_queue_activation(client, event.launch.c_str(), &receipt);
      if (queued == 1) {
        Remove(event.launch);
        continue;
      }  // Unknown/already processed.
      Check(queued);
      if (receipt <= 0)
        throw firebase_core::StateError("invalid-notification-receipt");
      // Only an accepted real OS tap may create a new initial message. This
      // read merely copies public payload after the queue's durable acceptance;
      // the SDK still waits for its OPENED callback before marking it opened.
      if (event.initial) {
        Buffer payload;
        Check(fcm_sdk_lookup(client, receipt, &payload.value));
        initial_.Save(app, receipt, payload.Text());
      }
      Remove(event.launch);
    }
  }
  bool CaptureOpened(fcm_sdk_handle client, const MessagingAppIdentity& app,
                     int64_t receipt, const uint8_t* json, size_t size) const {
    if (initial_.Contains(app, receipt))
      return true;  // Includes consumed tombstone.
    for (const auto& event : Read()) {
      Buffer unused;
      int64_t expected = 0;
      const HRESULT found = fcm_sdk_lookup_activation(
          client, event.launch.c_str(), &expected, &unused.value);
      if (found == 1) continue;
      Check(found);
      if (expected != receipt) continue;
      if (event.initial) {
        if (!json || !size)
          throw firebase_core::StateError("invalid-sdk-remote-message");
        initial_.Save(app, receipt,
                      std::string(reinterpret_cast<const char*>(json), size));
      }
      Remove(event.launch);
      return event.initial;
    }
    return false;  // Warm processing still waits for the actual Dart reply.
  }

 private:
  struct Event {
    std::string launch;
    bool initial;
  };
  struct Buffer {
    fcm_sdk_buffer value{};
    ~Buffer() { fcm_sdk_buffer_free(&value); }
    std::string Text() const {
      if (!value.data || !value.size)
        throw firebase_core::StateError("invalid-sdk-remote-message");
      return {reinterpret_cast<const char*>(value.data), value.size};
    }
  };
  static constexpr const char* kModule = "flutterNotificationActivations";
  static void Check(HRESULT result) {
    if (result == S_OK) return;
    const char* error = fcm_sdk_error_name(result);
    throw firebase_core::StateError(error ? error
                                          : "notification-activation-failed");
  }
  static std::string Text(const rapidjson::Value& value) {
    if (!value.IsString())
      throw firebase_core::StateError("invalid-local-state");
    return {value.GetString(), value.GetStringLength()};
  }
  static rapidjson::Value* Records(rapidjson::Document& document) {
    if (!document.HasMember(kModule)) return nullptr;
    auto& records = document[kModule];
    if (!records.IsArray())
      throw firebase_core::StateError("invalid-local-state");
    std::set<std::string> launches;
    for (const auto& record : records.GetArray()) {
      firebase_core::StateDocument::UniqueFields(record);
      if (!record.HasMember("launch") || !record.HasMember("initial") ||
          !record["initial"].IsBool())
        throw firebase_core::StateError("invalid-local-state");
      const auto launch = Text(record["launch"]);
      if (launch.empty() || launch.find('\0') != std::string::npos ||
          !launches.insert(launch).second)
        throw firebase_core::StateError("invalid-local-state");
    }
    return &records;
  }
  std::vector<Event> Read() const {
    std::vector<Event> result;
    transaction_([&](const auto& bytes, bool missing, auto&) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      if (const auto* records = Records(document))
        for (const auto& record : records->GetArray())
          result.push_back(
              {Text(record["launch"]), record["initial"].GetBool()});
      return false;
    });
    return result;
  }
  void Remove(const std::string& launch) const {
    transaction_([&](const auto& bytes, bool missing, auto& next) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      auto* records = Records(document);
      if (!records) return false;
      for (auto i = records->Begin(); i != records->End(); ++i)
        if (Text((*i)["launch"]) == launch) {
          records->Erase(i);
          next = firebase_core::StateDocument::Encode(document);
          return true;
        }
      return false;
    });
  }
  firebase_core::StateTransaction transaction_;
  InitialMessageCache initial_;
};
}  // namespace flutter::winrt::plugins::firebase_messaging
