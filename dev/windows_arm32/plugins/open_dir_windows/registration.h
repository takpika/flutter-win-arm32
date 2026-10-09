#pragma once
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"
#include <flutter/standard_method_codec.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <memory>

namespace flutter::winrt::plugins {
namespace open_directory {
struct Lifetime {};

inline ::winrt::fire_and_forget Launch(
    std::weak_ptr<Lifetime> lifetime, PlatformMessageContext context,
    ComPtr<ABI::Windows::ApplicationModel::Core::IFrameworkView> owner,
    const FlutterPlatformMessageResponseHandle* response,
    std::string path, std::string highlighted) {
  // C++/WinRT resumes the captured UI apartment after each asynchronous call.
  // Keep host storage alive, but never reply after its registrations are cleared.
  bool opened = false;
  try {
    auto folder = co_await ::winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(
        ::winrt::to_hstring(path));
    if (lifetime.expired()) co_return;
    if (highlighted.empty()) {
      opened = co_await ::winrt::Windows::System::Launcher::LaunchFolderAsync(folder);
    } else {
      auto item = co_await folder.GetItemAsync(::winrt::to_hstring(highlighted));
      if (lifetime.expired()) co_return;
      ::winrt::Windows::System::FolderLauncherOptions options;
      options.ItemsToSelect().Append(item);
      opened = co_await ::winrt::Windows::System::Launcher::LaunchFolderAsync(folder, options);
    }
  } catch (const ::winrt::hresult_error&) {
    // The original ShellExecute backend reports operation failures as false.
    opened = false;
  } catch (const std::bad_alloc&) {
    opened = false;
  }
  auto alive = lifetime.lock();
  if (!alive || !response) co_return;
  const flutter::EncodableValue value(opened);
  auto bytes = flutter::StandardMethodCodec::GetInstance().EncodeSuccessEnvelope(&value);
  context.api.SendPlatformMessageResponse(context.engine, response, bytes->data(), bytes->size());
}

inline std::string Argument(const flutter::EncodableValue* value, const char* key) {
  const auto* map = value ? std::get_if<flutter::EncodableMap>(value) : nullptr;
  if (!map) return {};
  auto item = map->find(flutter::EncodableValue(key));
  if (item == map->end()) return {};
  const auto* text = std::get_if<std::string>(&item->second);
  return text ? *text : std::string{};
}
}  // namespace open_directory

inline void RegisterOpenDirectory(PlatformMessageHandlers& handlers) {
  auto lifetime = std::make_shared<open_directory::Lifetime>();
  handlers.Register([lifetime](const FlutterPlatformMessage* message,
                               const PlatformMessageContext& context) {
    if (strcmp(message->channel, "com.flutter/open-dir-windows") != 0) return false;
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    auto call = codec.DecodeMethodCall(message->message, message->message_size);
    if (call && call->method_name() == "openNativeDir") {
      ComPtr<ABI::Windows::ApplicationModel::Core::IFrameworkView> owner = context.owner;
      open_directory::Launch(lifetime, context, std::move(owner), message->response_handle,
          open_directory::Argument(call->arguments(), "path"),
          open_directory::Argument(call->arguments(), "highlightedFileName"));
    } else if (message->response_handle) {
      context.api.SendPlatformMessageResponse(context.engine, message->response_handle, nullptr, 0);
    }
    return true;
  });
}
}  // namespace flutter::winrt::plugins
