// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstring>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace flutter::winrt::plugins::firebase_core {
using StateBytes = std::vector<uint8_t>;
using StateUpdate =
    std::function<bool(const StateBytes&, bool missing, StateBytes&)>;
using StateTransaction = std::function<void(const StateUpdate&)>;

class StateError : public std::runtime_error {
 public:
  explicit StateError(const char* code) : std::runtime_error(code) {}
};

// Shared envelope only. Each Firebase adapter validates its own module; other
// modules' fields survive an atomic update without being interpreted here.
class StateDocument {
 public:
  static rapidjson::Document Parse(const std::vector<uint8_t>& bytes,
                                   bool missing) {
    rapidjson::Document document;
    if (missing) {
      document.SetObject();
      document.AddMember("version", 1, document.GetAllocator());
    } else {
      if (bytes.empty() || std::memchr(bytes.data(), 0, bytes.size()))
        throw StateError("invalid-local-state");
      document.Parse<rapidjson::kParseValidateEncodingFlag |
                     rapidjson::kParseIterativeFlag>(
          reinterpret_cast<const char*>(bytes.data()), bytes.size());
      if (document.HasParseError()) throw StateError("invalid-local-state");
    }
    Validate(document);
    return document;
  }
  static std::vector<uint8_t> Encode(const rapidjson::Document& document) {
    Validate(document);
    rapidjson::StringBuffer bytes;
    rapidjson::Writer<rapidjson::StringBuffer> writer(bytes);
    if (!document.Accept(writer)) throw StateError("invalid-local-state");
    const auto* begin = reinterpret_cast<const uint8_t*>(bytes.GetString());
    return {begin, begin + bytes.GetSize()};
  }
  static void UniqueFields(const rapidjson::Value& object) {
    if (!object.IsObject()) throw StateError("invalid-local-state");
    std::set<std::string> keys;
    for (const auto& field : object.GetObject()) {
      if (!keys.emplace(field.name.GetString(), field.name.GetStringLength())
               .second)
        throw StateError("invalid-local-state");
    }
  }
  static void Validate(const rapidjson::Value& document) {
    UniqueFields(document);
    if (!document.HasMember("version") || !document["version"].IsInt() ||
        document["version"].GetInt() != 1)
      throw StateError("invalid-local-state");
  }
};
}  // namespace flutter::winrt::plugins::firebase_core
