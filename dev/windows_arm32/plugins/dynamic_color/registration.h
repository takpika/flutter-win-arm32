#pragma once
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"
#include <flutter/standard_method_codec.h>
#include <windows.ui.viewmanagement.h>

namespace flutter::winrt::plugins {

// The package's Windows contract returns an unsigned ARGB color in an int64.
// UWP obtains the user's accent from UISettings rather than desktop DWM.
inline HRESULT ReadAccentColor(int64_t* argb) {
  if (!argb) return E_POINTER;
  ComPtr<IInspectable> instance;
  HRESULT result = PhoneActivate(L"Windows.UI.ViewManagement.UISettings", &instance);
  ComPtr<ABI::Windows::UI::ViewManagement::IUISettings3> settings;
  if (SUCCEEDED(result)) result = instance.As(&settings);
  ABI::Windows::UI::Color color{};
  if (SUCCEEDED(result)) {
    result = settings->GetColorValue(
        ABI::Windows::UI::ViewManagement::UIColorType_Accent, &color);
  }
  if (SUCCEEDED(result)) {
    *argb = (static_cast<int64_t>(color.A) << 24) |
            (static_cast<int64_t>(color.R) << 16) |
            (static_cast<int64_t>(color.G) << 8) | color.B;
  }
  return result;
}

inline void RegisterDynamicColor(PlatformMessageHandlers& handlers) {
  handlers.Register([](const FlutterPlatformMessage* message,
                       const PlatformMessageContext& context) {
    if (strcmp(message->channel, "io.material.plugins/dynamic_color") != 0) {
      return false;
    }
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    auto call = codec.DecodeMethodCall(message->message, message->message_size);
    std::unique_ptr<std::vector<uint8_t>> response;
    if (!call) {
      response = codec.EncodeErrorEnvelope("INVALID_ARGUMENT", "Invalid dynamic_color method call");
    } else if (call->method_name() == "getAccentColor") {
      int64_t argb = 0;
      HRESULT result = ReadAccentColor(&argb);
      if (SUCCEEDED(result)) {
        const flutter::EncodableValue value(argb);
        response = codec.EncodeSuccessEnvelope(&value);
      } else {
        response = codec.EncodeErrorEnvelope(
            "WINRT_COLOR", "UISettings.GetColorValue failed: " +
                               std::to_string(static_cast<unsigned long>(result)));
      }
    }
    // Other methods keep the Windows package's unimplemented response.
    if (message->response_handle) {
      context.api.SendPlatformMessageResponse(
          context.engine, message->response_handle,
          response ? response->data() : nullptr, response ? response->size() : 0);
    }
    return true;
  });
}

}  // namespace flutter::winrt::plugins
