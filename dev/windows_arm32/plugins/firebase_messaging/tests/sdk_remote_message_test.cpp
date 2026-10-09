// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../sdk_remote_message.h"
#include <flutter/standard_message_codec.h>

#include <cassert>
#include <limits>

using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::winrt::plugins::firebase_messaging::DecodeSdkRemoteMessage;

EncodableMap Decode(const std::string& json) {
  return DecodeSdkRemoteMessage(reinterpret_cast<const uint8_t*>(json.data()), json.size());
}

const EncodableValue& At(const EncodableMap& map, const char* name) {
  return map.at(EncodableValue(name));
}

int main() {
  std::string borrowed = R"({"messageId":"owned","sentTime":9223372036854775807,
    "data":{"text":"日本語","nul":"a\u0000b","google.synthetic":"SDK-owned"},
    "notification":{"title":"通知","android":{"channelId":null}},
    "extension":[true,-9223372036854775808,1.25]})";
  auto owned = Decode(borrowed);
  borrowed.assign(borrowed.size(), '!');
  assert(std::get<std::string>(At(owned, "messageId")) == "owned");
  assert(std::get<int64_t>(At(owned, "sentTime")) == std::numeric_limits<int64_t>::max());
  const auto& data = std::get<EncodableMap>(At(owned, "data"));
  assert(std::get<std::string>(At(data, "text")) == "日本語");
  assert(std::get<std::string>(At(data, "nul")) == std::string("a\0b", 3));
  assert(std::get<std::string>(At(data, "google.synthetic")) == "SDK-owned");
  const auto& extension = std::get<EncodableList>(At(owned, "extension"));
  assert(std::get<bool>(extension[0]));
  assert(std::get<int64_t>(extension[1]) == std::numeric_limits<int64_t>::min());
  assert(std::get<double>(extension[2]) == 1.25);
  auto& codec = flutter::StandardMessageCodec::GetInstance();
  auto encoded = codec.EncodeMessage(EncodableValue(owned));
  auto decoded = codec.DecodeMessage(*encoded);
  assert(decoded && *decoded == EncodableValue(owned));
  for (const auto& invalid : {
           std::string(), std::string("[]"), std::string("{} trailing"),
           std::string("{}\0ignored", 10),
           std::string("{\"data\":{\"same\":1,\"same\":2}}"),
           std::string("{\"same\":1,\"s\\u0061me\":2}"),
           std::string("{\"number\":18446744073709551615}"),
           std::string("{\"text\":\"\xff\"}"), std::string("{\"number\":1e400}")}) {
    bool rejected = false;
    try { Decode(invalid); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
  }
  bool rejected = false;
  try { DecodeSdkRemoteMessage(nullptr, 1); }
  catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected);
}
