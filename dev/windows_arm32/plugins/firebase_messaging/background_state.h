// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <optional>

#include "../firebase_core/state_document.h"

namespace flutter::winrt::plugins::firebase_messaging {
struct BackgroundRegistration {
  std::string library_uri, entrypoint;
  std::optional<int64_t> callback;
};
class BackgroundState {
 public:
  explicit BackgroundState(firebase_core::StateTransaction transaction)
      : transaction_(std::move(transaction)) {}
  BackgroundRegistration Read() const {
    BackgroundRegistration result;
    transaction_([&](const auto& bytes, bool missing, auto&) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      if (document.HasMember("messagingBackground"))
        result = Decode(document["messagingBackground"]);
      return false;
    });
    return result;
  }
  void Configure(std::string library, std::string entrypoint,
                 std::optional<int64_t> callback = std::nullopt) const {
    transaction_([&](const auto& bytes, bool missing, auto& next) {
      auto document = firebase_core::StateDocument::Parse(bytes, missing);
      BackgroundRegistration previous;
      if (document.HasMember("messagingBackground"))
        previous = Decode(document["messagingBackground"]);
      if (previous.library_uri == library &&
          previous.entrypoint == entrypoint &&
          (!callback || previous.callback == callback))
        return false;
      if (!document.HasMember("messagingBackground"))
        document.AddMember("messagingBackground",
                           rapidjson::Value(rapidjson::kObjectType),
                           document.GetAllocator());
      auto& record = document["messagingBackground"];
      Put(record, "libraryUri", library, document);
      Put(record, "entrypoint", entrypoint, document);
      if (callback) {
        if (record.HasMember("userCallbackHandle"))
          record["userCallbackHandle"].SetInt64(*callback);
        else
          record.AddMember("userCallbackHandle", *callback,
                           document.GetAllocator());
      }
      next = firebase_core::StateDocument::Encode(document);
      return true;
    });
  }

 private:
  static BackgroundRegistration Decode(const rapidjson::Value& value) {
    firebase_core::StateDocument::UniqueFields(value);
    for (const auto* name : {"libraryUri", "entrypoint"})
      if (!value.HasMember(name) || !value[name].IsString() ||
          value[name].GetStringLength() == 0)
        throw firebase_core::StateError("invalid-local-state");
    BackgroundRegistration result{{value["libraryUri"].GetString(),
                                   value["libraryUri"].GetStringLength()},
                                  {value["entrypoint"].GetString(),
                                   value["entrypoint"].GetStringLength()},
                                  std::nullopt};
    if (result.library_uri.find('\0') != std::string::npos ||
        result.entrypoint.find('\0') != std::string::npos)
      throw firebase_core::StateError("invalid-local-state");
    if (value.HasMember("userCallbackHandle")) {
      if (!value["userCallbackHandle"].IsInt64())
        throw firebase_core::StateError("invalid-local-state");
      result.callback = value["userCallbackHandle"].GetInt64();
    }
    return result;
  }
  static void Put(rapidjson::Value& object, const char* key,
                  const std::string& text, rapidjson::Document& document) {
    rapidjson::Value value;
    value.SetString(text.data(), static_cast<rapidjson::SizeType>(text.size()),
                    document.GetAllocator());
    if (object.HasMember(key))
      object[key] = std::move(value);
    else
      object.AddMember(rapidjson::StringRef(key), value,
                       document.GetAllocator());
  }
  firebase_core::StateTransaction transaction_;
};
}  // namespace flutter::winrt::plugins::firebase_messaging
