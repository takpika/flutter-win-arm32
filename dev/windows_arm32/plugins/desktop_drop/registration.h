#pragma once
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"
#include <flutter/standard_method_codec.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.DragDrop.Core.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Storage.h>
#include <atomic>
#include <memory>

namespace flutter::winrt::plugins::desktop_drop {
using namespace ::winrt::Windows::ApplicationModel::DataTransfer;
using namespace ::winrt::Windows::ApplicationModel::DataTransfer::DragDrop::Core;
using Value = flutter::EncodableValue;
struct State {
  FlutterEngine engine;
  FlutterEngineProcTable api;
  ::winrt::apartment_context ui;
  std::atomic<bool> active{true};
  explicit State(const PlatformMessageContext& context) : engine(context.engine), api(context.api) {}
  void Emit(const char* method, Value value) {
    if (!active) return;
    auto bytes = flutter::StandardMethodCodec::GetInstance().EncodeMethodCall(
        flutter::MethodCall<Value>(method, std::make_unique<Value>(std::move(value))));
    FlutterPlatformMessage message{};
    message.struct_size = sizeof(message); message.channel = "desktop_drop";
    message.message = bytes->data(); message.message_size = bytes->size();
    if (api.SendPlatformMessage(engine, &message) != kSuccess) ::winrt::throw_hresult(E_FAIL);
  }
  void Position(const char* method, CoreDragInfo const& info) {
    auto point = info.Position();
    // WinUI's DropOperationTarget forwards this point directly to XAML root
    // hit testing. Convert those client DIPs to the original Windows package's
    // physical pixels; its unchanged Dart DropTarget divides by devicePixelRatio.
    double ratio = ::winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel();
    Emit(method, Value(flutter::EncodableList{Value(point.X * ratio), Value(point.Y * ratio)}));
  }
};

inline bool Accepts(CoreDragInfo const& info) {
  if (!info.Data().Contains(StandardDataFormats::StorageItems())) return false;
  auto extended = info.try_as<ICoreDragInfo2>();
  return !extended || (static_cast<unsigned>(extended.AllowedOperations()) &
                      static_cast<unsigned>(DataPackageOperation::Copy));
}

inline void Remember(::winrt::Windows::Storage::IStorageItem const& item) {
  using namespace ::winrt::Windows::Storage;
  const bool folder = item.IsOfType(StorageItemTypes::Folder);
  HMODULE library = LoadPackagedLibrary(L"flutter_winrt_compat.dll", 0);
  if (!library) ::winrt::throw_hresult(HRESULT_FROM_WIN32(GetLastError()));
  using Entry = HRESULT(WINAPI*)(void*);
  auto entry = reinterpret_cast<Entry>(GetProcAddress(library,
      folder ? "FlutterWinRTRememberStorageFolder" : "FlutterWinRTRememberStorageFile"));
  HRESULT result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
  try {
    if (entry) result = folder ? entry(::winrt::get_abi(item.as<StorageFolder>()))
                              : entry(::winrt::get_abi(item.as<StorageFile>()));
  } catch (...) { FreeLibrary(library); throw; }
  FreeLibrary(library); ::winrt::check_hresult(result);
}

struct Target : ::winrt::implements<Target, ICoreDropOperationTarget> {
  std::weak_ptr<State> state;
  explicit Target(std::weak_ptr<State> value) : state(std::move(value)) {}
  ::winrt::Windows::Foundation::IAsyncOperation<DataPackageOperation> EnterAsync(
      CoreDragInfo info, CoreDragUIOverride) {
    auto current = state.lock(); if (!current) co_return DataPackageOperation::None;
    co_await current->ui;
    if (!current->active || !Accepts(info)) co_return DataPackageOperation::None;
    current->Position("entered", info); co_return DataPackageOperation::Copy;
  }
  ::winrt::Windows::Foundation::IAsyncOperation<DataPackageOperation> OverAsync(
      CoreDragInfo info, CoreDragUIOverride) {
    auto current = state.lock(); if (!current) co_return DataPackageOperation::None;
    co_await current->ui;
    if (!current->active || !Accepts(info)) co_return DataPackageOperation::None;
    current->Position("updated", info); co_return DataPackageOperation::Copy;
  }
  ::winrt::Windows::Foundation::IAsyncAction LeaveAsync(CoreDragInfo) {
    auto current = state.lock(); if (!current) co_return;
    co_await current->ui;
    if (current->active) current->Emit("exited", Value{});
  }
  ::winrt::Windows::Foundation::IAsyncOperation<DataPackageOperation> DropAsync(CoreDragInfo info) {
    auto current = state.lock(); if (!current) co_return DataPackageOperation::None;
    co_await current->ui;
    if (!current->active || !Accepts(info)) co_return DataPackageOperation::None;
    auto items = co_await info.Data().GetStorageItemsAsync();
    if (!current->active) co_return DataPackageOperation::None;
    flutter::EncodableList paths;
    for (auto const& item : items) {
      Remember(item);
      auto path = ::winrt::to_string(item.Path());
      if (path.empty()) ::winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
      paths.emplace_back(std::move(path));
    }
    current->Emit("performOperation", Value(std::move(paths)));
    co_return DataPackageOperation::Copy;
  }
};

struct Subscription {
  std::shared_ptr<State> state;
  CoreDragDropManager manager{nullptr};
  ::winrt::event_token token{};
  bool registered = false;
  HRESULT Start(const PlatformMessageContext& context) noexcept {
    try {
      state = std::make_shared<State>(context);
      manager = CoreDragDropManager::GetForCurrentView();
      token = manager.TargetRequested([weak = std::weak_ptr<State>(state)](auto const&, auto const& args) {
        if (auto current = weak.lock(); current && current->active)
          args.SetTarget(::winrt::make<Target>(weak));
      });
      registered = true; return S_OK;
    } catch (...) { return ::winrt::to_hresult(); }
  }
  ~Subscription() {
    if (state) state->active = false;
    if (registered) manager.TargetRequested(token);
  }
};
}  // namespace flutter::winrt::plugins::desktop_drop

namespace flutter::winrt::plugins {
inline void RegisterDesktopDrop(PlatformMessageHandlers& handlers) {
  auto subscription = std::make_shared<desktop_drop::Subscription>();
  handlers.RegisterInitializer([subscription](const PlatformMessageContext& context) { return subscription->Start(context); });
  handlers.Register([](const FlutterPlatformMessage* message, const PlatformMessageContext& context) {
    if (strcmp(message->channel, "desktop_drop") != 0) return false;
    // The upstream Windows backend implements only native-to-Dart events.
    if (message->response_handle) context.api.SendPlatformMessageResponse(context.engine, message->response_handle, nullptr, 0);
    return true;
  });
}
}
