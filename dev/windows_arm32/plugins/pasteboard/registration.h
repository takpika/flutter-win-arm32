#pragma once
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"
#include <flutter/standard_method_codec.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <memory>

namespace flutter::winrt::plugins {
namespace pasteboard {
struct Lifetime {};
using Value = flutter::EncodableValue;
using namespace ::winrt::Windows::Storage;
using namespace ::winrt::Windows::Storage::Streams;
using namespace ::winrt::Windows::Graphics::Imaging;
using namespace ::winrt::Windows::ApplicationModel::DataTransfer;

inline ::winrt::fire_and_forget Dispatch(
    std::weak_ptr<Lifetime> lifetime, PlatformMessageContext context,
    ComPtr<ABI::Windows::ApplicationModel::Core::IFrameworkView> owner,
    const FlutterPlatformMessageResponseHandle* response,
    std::string method, Value arguments) {
  Value result;
  std::string error;
  StorageFile temporary{nullptr};
  try {
    if (method == "image") {
      auto content = Clipboard::GetContent();
      if (content.Contains(StandardDataFormats::Bitmap())) {
        auto reference = co_await content.GetBitmapAsync();
        auto input = co_await reference.OpenReadAsync();
        auto decoder = co_await BitmapDecoder::CreateAsync(input);
        auto bitmap = co_await decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore);
        input.Close();
        if (lifetime.expired()) co_return;
        temporary = co_await ApplicationData::Current().TemporaryFolder().CreateFileAsync(
            L"pasteboard.bmp", CreationCollisionOption::GenerateUniqueName);
        auto output = co_await temporary.OpenAsync(FileAccessMode::ReadWrite);
        auto encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::BmpEncoderId(), output);
        encoder.SetSoftwareBitmap(bitmap);
        co_await encoder.FlushAsync();
        output.Close();bitmap.Close();
        result = Value(::winrt::to_string(temporary.Path()));
      }
    } else if (method == "files") {
      auto content = Clipboard::GetContent();
      if (content.Contains(StandardDataFormats::StorageItems())) {
        auto items = co_await content.GetStorageItemsAsync();
        flutter::EncodableList paths;
        for (const auto& item : items) paths.emplace_back(::winrt::to_string(item.Path()));
        result = Value(std::move(paths));
      }
    } else if (method == "html") {
      auto content = Clipboard::GetContent();
      if (content.Contains(StandardDataFormats::Html())) {
        auto html = co_await content.GetHtmlFormatAsync();
        result = Value(::winrt::to_string(html));
      }
    } else if (method == "writeFiles") {
      const auto* paths = std::get_if<flutter::EncodableList>(&arguments);
      auto items = ::winrt::single_threaded_vector<IStorageItem>();
      if (paths) for (const auto& value : *paths) {
        const auto* path = std::get_if<std::string>(&value);
        if (!path) continue;
        IStorageItem item{nullptr};
        bool file = true;
        try { item = co_await StorageFile::GetFileFromPathAsync(::winrt::to_hstring(*path)); }
        catch (const ::winrt::hresult_error&) { file = false; }
        if (!file) item = co_await StorageFolder::GetFolderFromPathAsync(::winrt::to_hstring(*path));
        items.Append(item);
      }
      if (!items.Size()) error = "files is empty";
      else if (!lifetime.expired()) {
        DataPackage package;
        package.SetStorageItems(items);
        Clipboard::SetContent(package);
        Clipboard::Flush();
      }
    } else if (method == "writeImage") {
      std::string path;
      const auto* map = std::get_if<flutter::EncodableMap>(&arguments);
      if (map) {
        auto found = map->find(Value("fileName"));
        if (found != map->end()) if (const auto* text = std::get_if<std::string>(&found->second)) path = *text;
      }
      if (path.empty()) error = "File name is empty";
      else {
        auto file = co_await StorageFile::GetFileFromPathAsync(::winrt::to_hstring(path));
        auto input = co_await file.OpenReadAsync();
        auto decoder = co_await BitmapDecoder::CreateAsync(input);
        auto bitmap = co_await decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore);
        input.Close();
        InMemoryRandomAccessStream memory;
        auto encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::BmpEncoderId(), memory);
        encoder.SetSoftwareBitmap(bitmap);
        co_await encoder.FlushAsync();bitmap.Close();memory.Seek(0);
        if (!lifetime.expired()) {
          DataPackage package;
          package.SetBitmap(RandomAccessStreamReference::CreateFromStream(memory));
          // The package retains the stream. The original Dart caller deletes
          // its input file as soon as this method returns.
          Clipboard::SetContent(package);
        Clipboard::Flush();
        }
      }
    }
  } catch (const ::winrt::hresult_error& failure) {
    error = "WinRT clipboard failed: " + std::to_string(static_cast<int32_t>(failure.code()));
  } catch (const std::bad_alloc&) {
    error = "Clipboard allocation failed";
  }
  if (temporary && (!error.empty() || lifetime.expired())) {
    try { co_await temporary.DeleteAsync(StorageDeleteOption::PermanentDelete); }
    catch (const ::winrt::hresult_error&) {}
  }
  auto alive = lifetime.lock();
  if (!alive || !response) co_return;
  const auto& codec = flutter::StandardMethodCodec::GetInstance();
  auto bytes = error.empty() ? codec.EncodeSuccessEnvelope(&result) : codec.EncodeErrorEnvelope("0", error);
  context.api.SendPlatformMessageResponse(context.engine, response, bytes->data(), bytes->size());
}
}  // namespace pasteboard

inline void RegisterPasteboard(PlatformMessageHandlers& handlers) {
  auto lifetime = std::make_shared<pasteboard::Lifetime>();
  handlers.Register([lifetime](const FlutterPlatformMessage* message,
                               const PlatformMessageContext& context) {
    if (strcmp(message->channel, "pasteboard") != 0) return false;
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    auto call = codec.DecodeMethodCall(message->message, message->message_size);
    if (call && (call->method_name() == "image" || call->method_name() == "files" ||
                 call->method_name() == "html" || call->method_name() == "writeFiles" ||
                 call->method_name() == "writeImage")) {
      ComPtr<ABI::Windows::ApplicationModel::Core::IFrameworkView> owner = context.owner;
      pasteboard::Dispatch(lifetime, context, std::move(owner), message->response_handle,
          call->method_name(), call->arguments() ? *call->arguments() : flutter::EncodableValue{});
    } else if (message->response_handle) {
      context.api.SendPlatformMessageResponse(context.engine, message->response_handle, nullptr, 0);
    }
    return true;
  });
}
}  // namespace flutter::winrt::plugins
