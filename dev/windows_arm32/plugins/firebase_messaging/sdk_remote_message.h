// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <flutter/encodable_value.h>
#include <rapidjson/document.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace flutter::winrt::plugins::firebase_messaging {
namespace sdk_message_detail {
inline EncodableValue CopyJson(const rapidjson::Value& value, unsigned depth) {
  if (depth > 32) throw std::invalid_argument("invalid-sdk-remote-message");
  if (value.IsNull()) return EncodableValue();
  if (value.IsBool()) return EncodableValue(value.GetBool());
  if (value.IsInt()) return EncodableValue(value.GetInt());
  if (value.IsInt64()) return EncodableValue(value.GetInt64());
  if (value.IsDouble() && std::isfinite(value.GetDouble()))
    return EncodableValue(value.GetDouble());
  if (value.IsString())
    return EncodableValue(
        std::string(value.GetString(), value.GetStringLength()));
  if (value.IsObject()) {
    EncodableMap result;
    for (auto field = value.MemberBegin(); field != value.MemberEnd();
         ++field) {
      auto inserted = result.emplace(
          EncodableValue(std::string(field->name.GetString(),
                                     field->name.GetStringLength())),
          CopyJson(field->value, depth + 1));
      if (!inserted.second)
        throw std::invalid_argument("invalid-sdk-remote-message");
    }
    return EncodableValue(std::move(result));
  }
  if (value.IsArray()) {
    EncodableList result;
    result.reserve(value.Size());
    for (const auto& field : value.GetArray())
      result.push_back(CopyJson(field, depth + 1));
    return EncodableValue(std::move(result));
  }
  throw std::invalid_argument("invalid-sdk-remote-message");
}
}  // namespace sdk_message_detail

// The Framework owns FCM interpretation. Copy its public JSON values as-is
// before returning from the borrowed-buffer callback or releasing an SDK
// buffer.
inline EncodableMap DecodeSdkRemoteMessage(const uint8_t* bytes,
                                           size_t length) {
  if (!bytes || !length || std::memchr(bytes, 0, length))
    throw std::invalid_argument("invalid-sdk-remote-message");
  rapidjson::Document document;
  document.Parse<rapidjson::kParseValidateEncodingFlag |
                 rapidjson::kParseIterativeFlag>(
      reinterpret_cast<const char*>(bytes), length);
  if (document.HasParseError() || !document.IsObject())
    throw std::invalid_argument("invalid-sdk-remote-message");
  return std::get<EncodableMap>(sdk_message_detail::CopyJson(document, 0));
}
}  // namespace flutter::winrt::plugins::firebase_messaging
