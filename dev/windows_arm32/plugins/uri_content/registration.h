#pragma once
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"
#include <flutter/standard_method_codec.h>
#include <cerrno>
#include <cstdlib>

namespace flutter::winrt::plugins {

// AnalyticsInfo ABI declarations match the installed Microsoft projection.
struct __declspec(uuid("1D5EE066-188D-5BA9-4387-ACAEB0E7E305")) UriAnalyticsStatics : IInspectable {
  virtual HRESULT STDMETHODCALLTYPE get_VersionInfo(IInspectable**) = 0;
  virtual HRESULT STDMETHODCALLTYPE get_DeviceForm(HSTRING*) = 0;
};
struct __declspec(uuid("926130B8-9955-4C74-BDC1-7CD0DECF9B03")) UriAnalyticsVersionInfo : IInspectable {
  virtual HRESULT STDMETHODCALLTYPE get_DeviceFamily(HSTRING*) = 0;
  virtual HRESULT STDMETHODCALLTYPE get_DeviceFamilyVersion(HSTRING*) = 0;
};

inline HRESULT UriReadPlatformVersion(std::string* version) {
  if (!version) return E_POINTER;
  constexpr wchar_t name[] = L"Windows.System.Profile.AnalyticsInfo";
  HSTRING text = nullptr;
  HRESULT hr = WindowsCreateString(name, ARRAYSIZE(name) - 1, &text);
  ComPtr<UriAnalyticsStatics> statics;
  if (SUCCEEDED(hr)) hr = RoGetActivationFactory(text, IID_PPV_ARGS(&statics));
  WindowsDeleteString(text);
  ComPtr<IInspectable> instance;
  if (SUCCEEDED(hr)) hr = statics->get_VersionInfo(&instance);
  ComPtr<UriAnalyticsVersionInfo> info;
  if (SUCCEEDED(hr)) hr = instance.As(&info);
  text = nullptr;
  if (SUCCEEDED(hr)) hr = info->get_DeviceFamilyVersion(&text);
  if (SUCCEEDED(hr)) {
    UINT32 count = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(text, &count);
    wchar_t* end = nullptr;
    errno = 0;
    const auto encoded = wcstoull(raw, &end, 10);
    if (!count || end != raw + count || errno == ERANGE) {
      hr = E_INVALIDARG;
    } else {
      const auto major = encoded >> 48;
      *version = major >= 10 ? "Windows 10+" : major >= 8 ? "Windows 8" :
                 major >= 7 ? "Windows 7" : "Windows ";
    }
  }
  WindowsDeleteString(text);
  return hr;
}

inline void RegisterUriContent(PlatformMessageHandlers& handlers) {
  handlers.Register([](const FlutterPlatformMessage* message,
                       const PlatformMessageContext& context) {
    if (strcmp(message->channel, "uri_content") != 0) return false;
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    auto call = codec.DecodeMethodCall(message->message, message->message_size);
    std::unique_ptr<std::vector<uint8_t>> response;
    if (!call) {
      response = codec.EncodeErrorEnvelope("INVALID_ARGUMENT", "Invalid uri_content method call");
    } else if (call->method_name() == "getPlatformVersion") {
      std::string version;
      const HRESULT hr = UriReadPlatformVersion(&version);
      if (SUCCEEDED(hr)) {
        const flutter::EncodableValue value(version);
        response = codec.EncodeSuccessEnvelope(&value);
      } else {
        response = codec.EncodeErrorEnvelope("WINRT_VERSION", "AnalyticsInfo.VersionInfo failed: " +
                       std::to_string(static_cast<unsigned long>(hr)));
      }
    }
    // Other methods preserve the original Windows package's unimplemented reply.
    if (message->response_handle) {
      context.api.SendPlatformMessageResponse(context.engine, message->response_handle,
          response ? response->data() : nullptr, response ? response->size() : 0);
    }
    return true;
  });
}
}  // namespace flutter::winrt::plugins
