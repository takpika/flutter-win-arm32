#pragma once
#include <windows.ui.viewmanagement.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include "flutter/shell/platform/windows/uwp/runtime_support.h"
#include "flutter/shell/platform/windows/uwp/diagnostics.h"

inline HRESULT PhoneCreateSettings(
    ABI::Windows::UI::ViewManagement::IUISettings3** settings) {
    HSTRING name=nullptr;
    const wchar_t* type=L"Windows.UI.ViewManagement.UISettings";
    HRESULT hr=WindowsCreateString(type,static_cast<UINT32>(wcslen(type)),&name);
    flutter::winrt::ComPtr<IInspectable> instance;
    if(SUCCEEDED(hr))hr=RoActivateInstance(name,&instance);
    WindowsDeleteString(name);
    return SUCCEEDED(hr)?instance->QueryInterface(IID_PPV_ARGS(settings)):hr;
}
inline HRESULT PhoneSendSettings(
    ABI::Windows::UI::ViewManagement::IUISettings3* settings,
    FlutterEngine engine,const FlutterEngineProcTable& api) {
    if(!settings || !engine)return E_POINTER;
    ABI::Windows::UI::Color foreground{};
    HRESULT hr=settings->GetColorValue(
        ABI::Windows::UI::ViewManagement::UIColorType_Foreground,&foreground);
    if(FAILED(hr))return hr;
    flutter::winrt::ComPtr<ABI::Windows::UI::ViewManagement::IUISettings2> textSettings;
    hr=settings->QueryInterface(IID_PPV_ARGS(&textSettings));
    DOUBLE textScale=0;
    if(SUCCEEDED(hr))hr=textSettings->get_TextScaleFactor(&textScale);
    if(FAILED(hr))return hr;
    // Match the desktop settings plugin's query of the user's time pattern.
    wchar_t timeFormat[100]{};
    if(!GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT,LOCALE_STIMEFORMAT,timeFormat,100))
        return HRESULT_FROM_WIN32(GetLastError());
    bool use24Hour=wcsstr(timeFormat,L"H")!=nullptr;
    // Microsoft's documented theme detection uses the foreground color.
    bool dark=(5*foreground.G+2*foreground.R+foreground.B)>8*128;
    // Dart's settings decoder requires all three fields in every update.
    rapidjson::StringBuffer json;
    rapidjson::Writer<rapidjson::StringBuffer> writer(json);
    writer.StartObject();
    writer.Key("textScaleFactor");writer.Double(textScale);
    writer.Key("alwaysUse24HourFormat");writer.Bool(use24Hour);
    writer.Key("platformBrightness");writer.String(dark?"dark":"light");
    writer.EndObject();
    FlutterPlatformMessage message{};message.struct_size=sizeof(message);
    message.channel="flutter/settings";
    message.message=reinterpret_cast<const uint8_t*>(json.GetString());message.message_size=json.GetSize();
    auto result=api.SendPlatformMessage(engine,&message);
    flutter::winrt::Diagnostic(dark?"Phone Flutter OS theme dark":"Phone Flutter OS theme light",
        static_cast<HRESULT>(result));
    return result==kSuccess?S_OK:E_FAIL;
}
