#pragma once
#include "flutter/shell/platform/windows/uwp/runtime_support.h"
#include <windows.ui.viewmanagement.h>
#include <flutter/standard_method_codec.h>
#include <cmath>
#include <atomic>
#include <optional>

namespace flutter::winrt::plugins {
using ABI::Windows::UI::Core::ICoreWindow;
void Diagnostic(const char* message, HRESULT result);

// Native backend for the existing window_manager and screen_retriever APIs.
// Window geometry always comes from CoreWindow/ApplicationView in DIP.
class FlutterWinRTWindowPlugins {
    using Value=flutter::EncodableValue;
    using Map=flutter::EncodableMap;
    using List=flutter::EncodableList;
    using View=ABI::Windows::UI::ViewManagement::IApplicationView;
    using View3=ABI::Windows::UI::ViewManagement::IApplicationView3;
    Microsoft::WRL::ComPtr<View> view;
    Microsoft::WRL::ComPtr<View3> view3;
    std::atomic<bool> prevent_close_{false};
    static double Number(const Map* arguments,const char* key,double fallback) {
        if(!arguments)return fallback;
        auto it=arguments->find(Value(key));if(it==arguments->end())return fallback;
        if(auto number=std::get_if<double>(&it->second))return *number;
        if(auto number=std::get_if<int32_t>(&it->second))return *number;
        if(auto number=std::get_if<int64_t>(&it->second))return static_cast<double>(*number);
        return fallback;
    }
static HRESULT ReadVisibleBounds(ABI::Windows::Foundation::Rect* bounds) {
    using namespace ABI::Windows::UI::ViewManagement;
    HSTRING cls=nullptr;const wchar_t* name=L"Windows.UI.ViewManagement.ApplicationView";
    HRESULT hr=WindowsCreateString(name,(UINT32)wcslen(name),&cls);
    ComPtr<IApplicationViewStatics2> factory;
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(cls,IID_PPV_ARGS(&factory));
    WindowsDeleteString(cls);
    ComPtr<IApplicationView> view;ComPtr<IApplicationView2> visible;
    if(SUCCEEDED(hr))hr=factory->GetForCurrentView(&view);
    if(SUCCEEDED(hr))hr=view.As(&visible);
    if(SUCCEEDED(hr))hr=visible->get_VisibleBounds(bounds);
    return hr;
}

    HRESULT Initialize() {
        if(view3)return S_OK;
        HSTRING name=nullptr;const wchar_t* cls=L"Windows.UI.ViewManagement.ApplicationView";
        HRESULT hr=WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&name);
        Microsoft::WRL::ComPtr<ABI::Windows::UI::ViewManagement::IApplicationViewStatics2> factory;
        if(SUCCEEDED(hr))hr=RoGetActivationFactory(name,IID_PPV_ARGS(&factory));
        WindowsDeleteString(name);
        if(SUCCEEDED(hr))hr=factory->GetForCurrentView(&view);
        if(SUCCEEDED(hr))hr=view.As(&view3);
        return hr;
    }
public:
    std::optional<LRESULT> HandleNative(UINT message, const PlatformMessageContext& context) {
        // Match window_manager's cancellable WM_CLOSE contract. OS suspension
        // and process termination are separate UWP lifecycle operations.
        if(message!=0x0010 || !context.engine)return std::nullopt;
        const auto& codec=flutter::StandardMethodCodec::GetInstance();
        auto arguments=std::make_unique<Value>(Map{{Value("eventName"),Value("close")}});
        auto bytes=codec.EncodeMethodCall(flutter::MethodCall<Value>("onEvent",std::move(arguments)));
        FlutterPlatformMessage event{};
        event.struct_size=sizeof(event);
        event.channel="window_manager";
        event.message=bytes->data();event.message_size=bytes->size();
        auto result=context.api.SendPlatformMessage(context.engine,&event);
        Diagnostic("WinRT window close event",result==kSuccess?S_OK:E_FAIL);
        if(prevent_close_.load())return LRESULT(-1);
        return std::nullopt;
    }
    HRESULT SetFullScreen(bool enabled) {
        HRESULT hr=Initialize();
        if(FAILED(hr))return hr;
        boolean applied=false;
        if(enabled)hr=view3->TryEnterFullScreenMode(&applied);
        else hr=view3->ExitFullScreenMode();
        Diagnostic(enabled?(applied?"WinRT fullscreen applied":"WinRT fullscreen not applied"):"WinRT fullscreen exit",hr);
        // Microsoft's documented compatibility property still affects Mobile.
        // Use it when Mobile refuses the newer fullscreen request, and clear
        // it when leaving fullscreen. Geometry remains OS-reported throughout.
        if(SUCCEEDED(hr)&&(!enabled||!applied)) {
            Microsoft::WRL::ComPtr<ABI::Windows::UI::ViewManagement::IApplicationView2> overlays;
            hr=view.As(&overlays);
            if(SUCCEEDED(hr))hr=overlays->put_SuppressSystemOverlays(enabled);
            Diagnostic(enabled?"WinRT mobile overlays suppressed":"WinRT mobile overlays restored",hr);
        }
        return hr;
    }
    bool Handle(const FlutterPlatformMessage* message,ICoreWindow* window,double scale,bool focused,
                FlutterEngine engine,const FlutterEngineProcTable& api) {
        const bool manager=strcmp(message->channel,"window_manager")==0;
        const bool screen=strcmp(message->channel,"dev.leanflutter.plugins/screen_retriever")==0;
        if(!manager&&!screen)return false;
        const auto& codec=flutter::StandardMethodCodec::GetInstance();
        auto call=codec.DecodeMethodCall(message->message,message->message_size);
        Value result;HRESULT hr=call?S_OK:E_INVALIDARG;bool implemented=true;
        ABI::Windows::Foundation::Rect bounds{},visible{};
        if(SUCCEEDED(hr))hr=window->get_Bounds(&bounds);
        const Map* args=call?std::get_if<Map>(call->arguments()):nullptr;
        if(SUCCEEDED(hr)) {
            const auto& method=call->method_name();
            if(manager) {
                hr=Initialize();
                if(SUCCEEDED(hr)) {
                    if(method=="ensureInitialized"||method=="waitUntilReadyToShow") {}
                    else if(method=="isPreventClose")result=Value(prevent_close_.load());
                    else if(method=="setPreventClose") {
                        auto value=args?args->find(Value("isPreventClose")):Map::const_iterator{};
                        if(!args||value==args->end()||!std::get_if<bool>(&value->second))hr=E_INVALIDARG;
                        else {prevent_close_.store(std::get<bool>(value->second));result=Value(true);}
                    }
                    else if(method=="show"||method=="focus"||method=="restore")hr=window->Activate();
                    else if(method=="getBounds")result=Map{{Value("x"),Value(double(bounds.X))},{Value("y"),Value(double(bounds.Y))},
                        {Value("width"),Value(double(bounds.Width))},{Value("height"),Value(double(bounds.Height))}};
                    else if(method=="isVisible") {boolean value=false;hr=window->get_Visible(&value);result=Value(bool(value));}
                    else if(method=="isFocused")result=Value(focused);
                    else if(method=="isFullScreen") {boolean value=false;hr=view3->get_IsFullScreenMode(&value);result=Value(bool(value));}
                    else if(method=="setFullScreen") {
                        auto value=args?args->find(Value("isFullScreen")):Map::const_iterator{};
                        if(!args||value==args->end()||!std::get_if<bool>(&value->second))hr=E_INVALIDARG;
                        else hr=SetFullScreen(std::get<bool>(value->second));
                    }
                    else if(method=="setMinimumSize") {
                        double width=Number(args,"width",0),height=Number(args,"height",0);
                        if(!std::isfinite(width)||!std::isfinite(height)||width<0||height<0)hr=E_INVALIDARG;
                        else hr=view3->SetPreferredMinSize({float(width),float(height)});
                    } else if(method=="setBounds") {
                        double width=Number(args,"width",bounds.Width),height=Number(args,"height",bounds.Height);
                        if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)hr=E_INVALIDARG;
                        else {
                            boolean applied=false;hr=view3->TryResizeView({float(width),float(height)},&applied);
                            // A refused size request is a normal OS constraint, not a
                            // fabricated geometry update. Future getBounds reads the OS.
                            Diagnostic(applied?"WinRT window resize applied":"WinRT window resize refused",hr);
                            // UWP positions views itself. setBounds cannot move the
                            // fixed Mobile view, just as Tablet mode constrains resize.
                        }
                    } else if(method=="isResizable"||method=="isMinimizable"||method=="isMaximizable")result=Value(false);
                    else if(method=="isMinimized"||method=="isMaximized")result=Value(false);
                    else implemented=false;
                }
            } else if(method=="getCursorScreenPoint") {
                ABI::Windows::Foundation::Point point{};
                hr=window->get_PointerPosition(&point);
                if(SUCCEEDED(hr))result=Map{{Value("dx"),Value(double(bounds.X+point.X))},{Value("dy"),Value(double(bounds.Y+point.Y))}};
            } else if(method=="getPrimaryDisplay"||method=="getAllDisplays") {
                hr=ReadVisibleBounds(&visible);
                if(SUCCEEDED(hr)) {
                    Value display(Map{{Value("id"),Value("CoreWindow")},{Value("name"),Value("WinRT display")},
                        {Value("size"),Value(Map{{Value("width"),Value(double(bounds.Width))},{Value("height"),Value(double(bounds.Height))}})},
                        {Value("visiblePosition"),Value(Map{{Value("dx"),Value(double(visible.X))},{Value("dy"),Value(double(visible.Y))}})},
                        {Value("visibleSize"),Value(Map{{Value("width"),Value(double(visible.Width))},{Value("height"),Value(double(visible.Height))}})},
                        {Value("scaleFactor"),Value(scale)}});
                    result=method=="getPrimaryDisplay"?display:Value(Map{{Value("displays"),Value(List{display})}});
                }
            } else implemented=false;
        }
        if(message->response_handle) {
            if(!implemented)api.SendPlatformMessageResponse(engine,message->response_handle,nullptr,0);
            else {
                auto reply=SUCCEEDED(hr)?codec.EncodeSuccessEnvelope(&result):
                    codec.EncodeErrorEnvelope("WINRT_WINDOW","OS window operation failed",nullptr);
                api.SendPlatformMessageResponse(engine,message->response_handle,reply->data(),reply->size());
            }
        }
        if(call)Diagnostic((std::string("WinRT plugin ")+message->channel+"."+call->method_name()).c_str(),implemented?hr:E_NOTIMPL);
        return true;
    }
};

}  // namespace flutter::winrt::plugins
