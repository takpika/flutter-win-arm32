#include "registration.h"
#include "shell_images.h"
#include "window_title.h"
#include <flutter/standard_method_codec.h>
#include <array>
#include <new>

namespace flutter::winrt::plugins::taskbar {
using Value = flutter::EncodableValue;
using Map = flutter::EncodableMap;

class NativeTaskbar {
 public:
  explicit NativeTaskbar(std::shared_ptr<NativeWindowSession> session)
      : session_(std::move(session)) {}

  bool Handle(const FlutterPlatformMessage* message,
              const PlatformMessageContext& context) {
    if (strcmp(message->channel, kChannel) != 0) return false;
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    auto call = codec.DecodeMethodCall(message->message, message->message_size);
    Value value;
    HRESULT result = E_INVALIDARG;
    bool implemented = call && KnownMethod(call->method_name());
    if (implemented) {
      try {
        result = Apply(call->method_name(), std::get_if<Map>(call->arguments()), &value);
      } catch (const std::bad_alloc&) {
        result = E_OUTOFMEMORY;
      }
    }
    std::unique_ptr<std::vector<uint8_t>> response;
    if (call && !implemented) {
      context.api.SendPlatformMessageResponse(context.engine, message->response_handle, nullptr, 0);
      return true;
    }
    if (SUCCEEDED(result)) {
      response = codec.EncodeSuccessEnvelope(&value);
    } else {
      flutter::winrt::Diagnostic("taskbar native method", result);
      Value details(Map{{Value("hresult"), Value(static_cast<int64_t>(result))}});
      response = codec.EncodeErrorEnvelope("-1", "ERROR: WindowsTaskbar::" +
          (call ? call->method_name() : std::string("InvalidMethodCall")), &details);
    }
    context.api.SendPlatformMessageResponse(context.engine, message->response_handle,
                                             response->data(), response->size());
    return true;
  }

  std::optional<LRESULT> HandleNative(HWND, UINT message, WPARAM wparam,
                                     LPARAM, const PlatformMessageContext& context) {
    UINT id = static_cast<UINT>(wparam & 0xffff);
    if (!context.engine || message != 0x0111 || id < 40001 || id >= 40008 ||
        ((wparam >> 16) & 0xffff) != 0x1800) return std::nullopt;
    Value index(static_cast<int32_t>(id - 40001));
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    auto bytes = codec.EncodeMethodCall(flutter::MethodCall<Value>("WM_COMMAND",
                                                                 std::make_unique<Value>(index)));
    FlutterPlatformMessage event{};
    event.struct_size = sizeof(event);
    event.channel = kChannel;
    event.message = bytes->data();
    event.message_size = bytes->size();
    auto result = context.api.SendPlatformMessage(context.engine, &event);
    flutter::winrt::Diagnostic("taskbar thumbnail callback", result == kSuccess ? S_OK : E_FAIL);
    return 0;
  }

 private:
  static constexpr const char* kChannel = "com.alexmercerind/windows_taskbar";
  static bool KnownMethod(const std::string& method) {
    constexpr std::array<const char*, 12> methods = {
        "SetProgressMode", "SetProgress", "SetThumbnailToolbar", "ResetThumbnailToolbar",
        "SetThumbnailTooltip", "SetFlashTaskbarAppIcon", "ResetFlashTaskbarAppIcon",
        "SetOverlayIcon", "ResetOverlayIcon", "SetWindowTitle", "ResetWindowTitle",
        "IsTaskbarVisible"};
    return std::any_of(methods.begin(), methods.end(), [&](const char* name) { return method == name; });
  }
  template<class T> static const T* Argument(const Map* arguments, const char* key) {
    if (!arguments) return nullptr;
    auto found = arguments->find(Value(key));
    return found == arguments->end() ? nullptr : std::get_if<T>(&found->second);
  }
  HRESULT IsVisible(Value* value) {
    using Find = HWND(WINAPI*)(LPCWSTR, LPCWSTR);
    HRESULT result = user_.Open(L"MinUser.dll");
    Find find = nullptr;
    if (SUCCEEDED(result)) result = user_.Resolve("FindWindowW", &find);
    if (FAILED(result)) return result;
    HWND window = find(L"Shell_TrayWnd", nullptr);
    if (!window) { *value = Value(false); return S_OK; }
    const auto& native = session_->native();
    NativeWindowApi::MonitorInformation monitor{};
    monitor.size = sizeof(monitor);
    HANDLE handle = native.monitor_from_window(window, 2);
    RECT bounds{};
    if (!handle || !native.get_monitor_info(handle, &monitor) ||
        !native.get_rectangle(window, &bounds)) return OsModule::Error();
    *value = Value(!(bounds.top >= monitor.monitor.bottom - 4 || bounds.right <= 2 ||
                     bounds.bottom <= 4 || bounds.left >= monitor.monitor.right - 2));
    return S_OK;
  }
  HRESULT Flash(HWND window, DWORD mode, UINT count, DWORD timeout) {
    struct FlashInformation { UINT size; HWND window; DWORD flags; UINT count; DWORD timeout; };
    static_assert(sizeof(FlashInformation) == 20);
    using Function = BOOL(WINAPI*)(FlashInformation*);
    HRESULT result = user_.Open(L"MinUser.dll");
    Function flash = nullptr;
    if (SUCCEEDED(result)) result = user_.Resolve("FlashWindowEx", &flash);
    if (FAILED(result)) return result;
    FlashInformation information{sizeof(information), window, mode, count, timeout};
    // The BOOL is the previous active state, not a success/error result.
    flash(&information);
    return S_OK;
  }
  HRESULT Apply(const std::string& method, const Map* arguments, Value* value) {
    HWND window = session_->window();
    if (!window) return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    if (method == "IsTaskbarVisible") return IsVisible(value);
    const auto& native = session_->native();
    if (!native.is_visible(window)) return E_FAIL;
    if (method == "SetWindowTitle") {
      auto title = Argument<std::string>(arguments, "title");
      return title ? title_.Set(native, window, Utf16FromUtf8(*title)) : E_INVALIDARG;
    }
    if (method == "ResetWindowTitle") return title_.Reset(native, window);
    if (method == "ResetFlashTaskbarAppIcon") return Flash(window, 0, 0, 0);
    if (method == "SetFlashTaskbarAppIcon") {
      auto mode = Argument<int32_t>(arguments, "mode");
      auto count = Argument<int32_t>(arguments, "flashCount");
      auto timeout = Argument<int32_t>(arguments, "timeout");
      return mode && count && timeout ? Flash(window, *mode, *count, *timeout) : E_INVALIDARG;
    }
    HRESULT result = api_.Open();
    if (FAILED(result)) return result;
    auto* taskbar = api_.get();
    if (method == "SetProgressMode") {
      auto mode = Argument<int32_t>(arguments, "mode");
      return mode ? taskbar->SetProgressState(window, *mode) : E_INVALIDARG;
    }
    if (method == "SetProgress") {
      auto completed = Argument<int32_t>(arguments, "completed");
      auto total = Argument<int32_t>(arguments, "total");
      return completed && total ? taskbar->SetProgressValue(window, *completed, *total) : E_INVALIDARG;
    }
    if (method == "SetThumbnailTooltip") {
      auto tooltip = Argument<std::string>(arguments, "tooltip");
      if (!tooltip) return E_INVALIDARG;
      auto text = Utf16FromUtf8(*tooltip);
      return taskbar->SetThumbnailTooltip(window, text.c_str());
    }
    if (method == "ResetOverlayIcon") return taskbar->SetOverlayIcon(window, nullptr, L"");
    if (method == "SetOverlayIcon") {
      auto icon = Argument<std::string>(arguments, "icon");
      auto tooltip = Argument<std::string>(arguments, "tooltip");
      if (!icon || !tooltip) return E_INVALIDARG;
      OwnedHandle handle(nullptr, HandleDeleter{});
      result = images_.LoadIcon(*icon, &handle);
      if (FAILED(result)) return result;
      auto text = Utf16FromUtf8(*tooltip);
      return taskbar->SetOverlayIcon(window, handle.get(), text.c_str());
    }
    std::vector<ToolbarButton> buttons;
    if (method == "SetThumbnailToolbar") {
      auto values = Argument<flutter::EncodableList>(arguments, "buttons");
      if (!values || values->size() > 7) return E_INVALIDARG;
      for (const auto& item : *values) {
        auto map = std::get_if<Map>(&item);
        auto icon = Argument<std::string>(map, "icon");
        auto tooltip = Argument<std::string>(map, "tooltip");
        auto mode = Argument<int32_t>(map, "mode");
        if (!icon || !tooltip || !mode) return E_INVALIDARG;
        buttons.push_back({*icon, *tooltip, static_cast<DWORD>(*mode)});
      }
    }
    return images_.SetToolbar(taskbar, window, buttons, &buttons_added_);
  }
  std::shared_ptr<NativeWindowSession> session_;
  TaskbarApi api_;
  WindowTitle title_;
  ShellImages images_;
  OsModule user_;
  bool buttons_added_ = false;
};
}

namespace flutter::winrt::plugins {
void RegisterWindowsTaskbar(PlatformMessageHandlers& handlers,
                           const std::shared_ptr<NativeWindowSession>& session) {
  auto plugin = std::make_shared<taskbar::NativeTaskbar>(session);
  session->Register([plugin](HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                             const PlatformMessageContext& context) {
    return plugin->HandleNative(window, message, wparam, lparam, context);
  });
  handlers.Register([plugin](const FlutterPlatformMessage* message,
                             const PlatformMessageContext& context) {
    auto lifetime = plugin;
    return lifetime->Handle(message, context);
  });
}
}
