#pragma once
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"
#include <flutter/standard_method_codec.h>
#include "gal_storage_original.h"
#include <memory>

namespace flutter::winrt::plugins {
namespace gallery {
struct Lifetime {};
using Value = flutter::EncodableValue;

inline ::winrt::fire_and_forget Dispatch(
    std::weak_ptr<Lifetime> lifetime, PlatformMessageContext context,
    ComPtr<ABI::Windows::ApplicationModel::Core::IFrameworkView> owner,
    const FlutterPlatformMessageResponseHandle* response,
    std::string method, Value arguments) {
  Value result;
  std::string error_code;
  std::string error_message;
  const bool access = method == "hasAccess" || method == "requestAccess";
  try {
    if (access) {
      // UWP grants this library through the package declaration. Query the real
      // broker instead of inheriting the desktop backend's unconditional true.
      co_await ::winrt::Windows::Storage::KnownFolders::PicturesLibrary().GetBasicPropertiesAsync();
      result = Value(true);
    } else if (method == "open") {
      co_await ::gal::Open();
    } else {
      const auto* map = std::get_if<flutter::EncodableMap>(&arguments);
      if (!map) throw std::invalid_argument("Expected gallery arguments");
      std::optional<std::string> album;
      auto item = map->find(Value("album"));
      if (item != map->end()) if (const auto* text = std::get_if<std::string>(&item->second)) album = *text;
      if (method == "putImage" || method == "putVideo") {
        co_await ::gal::PutMedia(std::get<std::string>(map->at(Value("path"))), album);
      } else {
        // These references live in this coroutine frame until the original
        // helper completes; its byte/name parameters are reference arguments.
        co_await ::gal::PutMediaBytes(std::get<std::vector<uint8_t>>(map->at(Value("bytes"))),
                                    album, std::get<std::string>(map->at(Value("name"))));
      }
    }
  } catch (const ::winrt::hresult_error& error) {
    if (access && error.code() == E_ACCESSDENIED) result = Value(false);
    else {
      error_code = error.code() == HRESULT_FROM_WIN32(ERROR_DISK_FULL) ? "NOT_ENOUGH_SPACE"
          : error.code() == ::gal::E_UNSUPPORTED_FORMAT ? "NOT_SUPPORTED_FORMAT" : "UNEXPECTED";
      error_message = ::winrt::to_string(error.message());
    }
  } catch (const std::exception& error) {
    error_code = "UNEXPECTED";
    error_message = error.what();
  }
  auto alive = lifetime.lock();
  if (!alive || !response) co_return;
  const auto& codec = flutter::StandardMethodCodec::GetInstance();
  auto bytes = error_code.empty() ? codec.EncodeSuccessEnvelope(&result)
                                 : codec.EncodeErrorEnvelope(error_code, error_message);
  context.api.SendPlatformMessageResponse(context.engine, response, bytes->data(), bytes->size());
}
}  // namespace gallery

inline void RegisterGallery(PlatformMessageHandlers& handlers) {
  auto lifetime = std::make_shared<gallery::Lifetime>();
  handlers.Register([lifetime](const FlutterPlatformMessage* message,
                               const PlatformMessageContext& context) {
    if (strcmp(message->channel, "gal") != 0) return false;
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    auto call = codec.DecodeMethodCall(message->message, message->message_size);
    const std::string method = call ? call->method_name() : std::string{};
    if (method == "putImage" || method == "putVideo" || method == "putImageBytes" ||
        method == "open" || method == "hasAccess" || method == "requestAccess") {
      ComPtr<ABI::Windows::ApplicationModel::Core::IFrameworkView> owner = context.owner;
      gallery::Dispatch(lifetime, context, std::move(owner), message->response_handle,
                       method, call->arguments() ? *call->arguments() : gallery::Value{});
    } else if (message->response_handle) {
      context.api.SendPlatformMessageResponse(context.engine, message->response_handle, nullptr, 0);
    }
    return true;
  });
}
}  // namespace flutter::winrt::plugins
