#include "registration.h"
#include "native_menu.h"
#include "shell_api.h"
#include <flutter/standard_method_codec.h>
#include <algorithm>
#include <cmath>
#include <new>

namespace flutter::winrt::plugins::tray {
class NativeTray {
 public:
  explicit NativeTray(std::shared_ptr<NativeWindowSession> session)
      : session_(std::move(session)) {}
  ~NativeTray() {
    HRESULT result = Destroy();
    if (FAILED(result)) flutter::winrt::Diagnostic("tray cleanup", result);
  }

  bool Handle(const FlutterPlatformMessage* message,
              const PlatformMessageContext& context) {
    if (strcmp(message->channel, "tray_manager") != 0) return false;
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    auto call = codec.DecodeMethodCall(message->message, message->message_size);
    Value value(true);
    HRESULT result = E_INVALIDARG;
    bool known = false;
    if (call) {
      const auto& method = call->method_name();
      known = method == "destroy" || method == "setIcon" || method == "setToolTip" ||
              method == "setContextMenu" || method == "popUpContextMenu" || method == "getBounds";
      if (known) {
        try {
          result = Apply(method, std::get_if<Map>(call->arguments()), &value, context);
        } catch (const std::bad_alloc&) { result = E_OUTOFMEMORY; }
      }
    }
    if (call && !known) {
      context.api.SendPlatformMessageResponse(context.engine, message->response_handle, nullptr, 0);
      return true;
    }
    std::unique_ptr<std::vector<uint8_t>> response;
    if (SUCCEEDED(result)) response = codec.EncodeSuccessEnvelope(&value);
    else {
      flutter::winrt::Diagnostic("tray native method", result);
      Value details(Map{{Value("hresult"), Value(static_cast<int64_t>(result))}});
      response = codec.EncodeErrorEnvelope("TRAY_MANAGER_ERROR",
          call ? call->method_name() : "InvalidMethodCall", &details);
    }
    context.api.SendPlatformMessageResponse(context.engine, message->response_handle,
                                             response->data(), response->size());
    return true;
  }

  std::optional<LRESULT> HandleNative(HWND, UINT message, WPARAM wparam, LPARAM lparam,
                                     const PlatformMessageContext& context) {
    if (message == 0x0002) {
      HRESULT result = Destroy();
      if (FAILED(result)) flutter::winrt::Diagnostic("tray window destruction", result);
      return std::nullopt;
    }
    if (!context.engine) return std::nullopt;
    if (message == 0x0111 && ((wparam >> 16) & 0xffff) == 0 &&
        menu_.Contains(static_cast<UINT>(wparam & 0xffff))) {
      Emit("onTrayMenuItemClick", Value(Map{{Value("id"), Value(static_cast<int32_t>(wparam & 0xffff))}}), context);
    } else if (message == 0x0401 && added_ && wparam == data_.id) {
      if (lparam == 0x0202) Emit("onTrayIconMouseDown", Value(), context);
      else if (lparam == 0x0205) Emit("onTrayIconRightMouseDown", Value(), context);
    }
    return std::nullopt;
  }

 private:
  void Emit(const char* method, Value value, const PlatformMessageContext& context) {
    if (!context.engine) return;
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    auto bytes = codec.EncodeMethodCall(flutter::MethodCall<Value>(method, std::make_unique<Value>(std::move(value))));
    FlutterPlatformMessage event{};
    event.struct_size = sizeof(event);
    event.channel = "tray_manager";
    event.message = bytes->data();
    event.message_size = bytes->size();
    auto result = context.api.SendPlatformMessage(context.engine, &event);
    flutter::winrt::Diagnostic("tray native callback", result == kSuccess ? S_OK : E_FAIL);
  }
  HRESULT Destroy() {
    if (added_) {
      HRESULT result = shell_.Notify(2, &data_);
      if (FAILED(result)) return result;
      added_ = false;
    }
    icon_.reset();
    menu_.Reset();
    return S_OK;
  }
  HRESULT Apply(const std::string& method, const Map* arguments, Value* value,
                const PlatformMessageContext& context) {
    if (method == "destroy") return Destroy();
    HWND window = session_->window();
    if (!window) return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    if (method == "setIcon") {
      auto path = Argument<std::string>(arguments, "iconPath");
      if (!path) return E_INVALIDARG;
      OwnedNativeHandle staged(nullptr, NativeHandleDeleter{});
      HRESULT result = icons_.Load(*path, 0x0010, false, &staged);
      if (FAILED(result)) return result;
      auto replacement = data_;
      replacement.size = sizeof(replacement);
      replacement.window = window;
      replacement.flags = 1 | 2;  // NIF_MESSAGE | NIF_ICON
      replacement.callback = 0x0401;
      replacement.icon = staged.get();
      result = shell_.Notify(added_ ? 1 : 0, &replacement);
      if (FAILED(result)) return result;
      data_ = replacement;
      icon_ = std::move(staged);
      added_ = true;
      return S_OK;
    }
    if (method == "setToolTip") {
      auto tooltip = Argument<std::string>(arguments, "toolTip");
      if (!tooltip) return E_INVALIDARG;
      if (!added_) return E_UNEXPECTED;
      std::wstring text;
      HRESULT result = NativeUtf16(*tooltip, &text);
      if (FAILED(result)) return result;
      auto replacement = data_;
      replacement.flags = 1 | 2 | 4;
      std::fill(std::begin(replacement.tooltip), std::end(replacement.tooltip), L'\0');
      std::copy_n(text.data(), std::min<size_t>(127, text.size()), replacement.tooltip);
      result = shell_.Notify(1, &replacement);
      if (SUCCEEDED(result)) data_ = replacement;
      return result;
    }
    if (method == "setContextMenu") return menu_.Set(Argument<Map>(arguments, "menu"));
    if (method == "popUpContextMenu") {
      UINT command = 0;
      HRESULT result = menu_.Popup(window, &command);
      if (SUCCEEDED(result) && command)
        Emit("onTrayMenuItemClick", Value(Map{{Value("id"), Value(static_cast<int32_t>(command))}}), context);
      return result;
    }
    if (!added_) { *value = Value(); return S_OK; }
    auto ratio = Argument<double>(arguments, "devicePixelRatio");
    if (!ratio || !std::isfinite(*ratio) || *ratio <= 0) return E_INVALIDARG;
    Identifier identifier{sizeof(Identifier), data_.window, data_.id, {}};
    RECT rectangle{};
    HRESULT result = shell_.Bounds(identifier, &rectangle);
    if (FAILED(result)) return result;
    *value = Value(Map{{Value("x"), Value(rectangle.left / *ratio)},
                      {Value("y"), Value(rectangle.top / *ratio)},
                      {Value("width"), Value((rectangle.right - rectangle.left) / *ratio)},
                      {Value("height"), Value((rectangle.bottom - rectangle.top) / *ratio)}});
    return S_OK;
  }
  std::shared_ptr<NativeWindowSession> session_;
  ShellApi shell_;
  NativeIconLoader icons_;
  NativeMenu menu_;
  OwnedNativeHandle icon_{nullptr, NativeHandleDeleter{}};
  Notification data_{};
  bool added_ = false;
};
}

namespace flutter::winrt::plugins {
void RegisterTrayManager(PlatformMessageHandlers& handlers,
                        const std::shared_ptr<NativeWindowSession>& session) {
  auto plugin = std::make_shared<tray::NativeTray>(session);
  session->Register([plugin](HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                             const PlatformMessageContext& context) {
    return plugin->HandleNative(window, message, wparam, lparam, context);
  });
  handlers.Register([plugin](const FlutterPlatformMessage* message,
                             const PlatformMessageContext& context) {
    auto lifetime = plugin;  // Modal OS calls may reenter host teardown.
    return lifetime->Handle(message, context);
  });
}
}
