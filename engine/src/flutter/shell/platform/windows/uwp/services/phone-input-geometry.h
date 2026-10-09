#pragma once

// Microsoft SDK windows.graphics.display.h, IDisplayInformation2 (contract 1).
// Read-only diagnostics: do not change pointer coordinates from an unmeasured
// offset or assume this raw scale must equal the render target's scale.
struct __declspec(uuid("4dcd0021-fad1-4b8e-8edf-775887b8bf19")) PhoneRawDisplayScale : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_RawPixelsPerViewPixel(DOUBLE*)=0;
};
__CRT_UUID_DECL(PhoneRawDisplayScale,0x4dcd0021,0xfad1,0x4b8e,0x8e,0xdf,0x77,0x58,0x87,0xb8,0xbf,0x19)

static HRESULT PhoneReadVisibleBounds(ABI::Windows::Foundation::Rect* bounds) {
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

static HRESULT PhoneUseCoreWindowBounds() {
    using namespace ABI::Windows::UI::ViewManagement;
    HSTRING cls=nullptr;const wchar_t* name=L"Windows.UI.ViewManagement.ApplicationView";
    HRESULT hr=WindowsCreateString(name,(UINT32)wcslen(name),&cls);
    ComPtr<IApplicationViewStatics2> factory;
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(cls,IID_PPV_ARGS(&factory));
    WindowsDeleteString(cls);
    ComPtr<IApplicationView> view;ComPtr<IApplicationView2> view2;
    if(SUCCEEDED(hr))hr=factory->GetForCurrentView(&view);
    if(SUCCEEDED(hr))hr=view.As(&view2);
    boolean applied=false;
    if(SUCCEEDED(hr))hr=view2->SetDesiredBoundsMode(ApplicationViewBoundsMode_UseCoreWindow,&applied);
    if(SUCCEEDED(hr)&&!applied)hr=E_FAIL;
    Diagnostic("Phone OS CoreWindow content bounds mode",hr);
    return hr;
}

// Mobile StatusBar ABI prefix from the original Microsoft SDK.
struct PhoneStatusColor {BYTE A,R,G,B;};
struct __declspec(uuid("ab8e5d11-b0c1-5a21-95ae-f16bf3a37624")) PhoneStatusColorReference : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Value(PhoneStatusColor*)=0;
};
__CRT_UUID_DECL(PhoneStatusColorReference,0xab8e5d11,0xb0c1,0x5a21,0x95,0xae,0xf1,0x6b,0xf3,0xa3,0x76,0x24)
class PhoneBoxedStatusColor : public Ref<PhoneStatusColorReference> {
    PhoneStatusColor color;
public:
    explicit PhoneBoxedStatusColor(bool dark):color{255,(BYTE)(dark?0:255),(BYTE)(dark?0:255),(BYTE)(dark?0:255)}{}
    HRESULT STDMETHODCALLTYPE get_Value(PhoneStatusColor* value) override {if(!value)return E_POINTER;*value=color;return S_OK;}
};
struct __declspec(uuid("0ffcc5bf-98d0-4864-b1e8-b3f4020be8b4")) PhoneStatusBar : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE ShowAsync(ABI::Windows::Foundation::IAsyncAction**)=0;
    virtual HRESULT STDMETHODCALLTYPE HideAsync(ABI::Windows::Foundation::IAsyncAction**)=0;
    virtual HRESULT STDMETHODCALLTYPE get_BackgroundOpacity(DOUBLE*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_BackgroundOpacity(DOUBLE)=0;
    virtual HRESULT STDMETHODCALLTYPE get_ForegroundColor(PhoneStatusColorReference**)=0;
    virtual HRESULT STDMETHODCALLTYPE put_ForegroundColor(PhoneStatusColorReference*)=0;
};
__CRT_UUID_DECL(PhoneStatusBar,0x0ffcc5bf,0x98d0,0x4864,0xb1,0xe8,0xb3,0xf4,0x02,0x0b,0xe8,0xb4)
struct __declspec(uuid("8b463fdf-422f-4561-8806-fb1289cadfb7")) PhoneStatusBarStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetForCurrentView(PhoneStatusBar**)=0;
};
__CRT_UUID_DECL(PhoneStatusBarStatics,0x8b463fdf,0x422f,0x4561,0x88,0x06,0xfb,0x12,0x89,0xca,0xdf,0xb7)

static HRESULT PhoneShowStatusBar() {
    HSTRING cls=nullptr;const wchar_t* name=L"Windows.UI.ViewManagement.StatusBar";
    HRESULT hr=WindowsCreateString(name,(UINT32)wcslen(name),&cls);
    ComPtr<PhoneStatusBarStatics> factory;
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(cls,IID_PPV_ARGS(&factory));
    WindowsDeleteString(cls);
    ComPtr<PhoneStatusBar> bar;
    if(SUCCEEDED(hr))hr=factory->GetForCurrentView(&bar);
    if(SUCCEEDED(hr))hr=bar->put_BackgroundOpacity(0.0);
    ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
    if(SUCCEEDED(hr))hr=bar->ShowAsync(&action);
    Diagnostic("Phone OS status bar show requested",hr);
    return hr;
}

static HRESULT PhoneSetStatusBarStyle(bool darkIcons) {
    HSTRING cls=nullptr;const wchar_t* name=L"Windows.UI.ViewManagement.StatusBar";
    HRESULT hr=WindowsCreateString(name,(UINT32)wcslen(name),&cls);
    ComPtr<PhoneStatusBarStatics> factory;
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(cls,IID_PPV_ARGS(&factory));WindowsDeleteString(cls);
    ComPtr<PhoneStatusBar> bar;if(SUCCEEDED(hr))hr=factory->GetForCurrentView(&bar);
    ComPtr<PhoneStatusColorReference> color;color.Attach(new PhoneBoxedStatusColor(darkIcons));
    if(SUCCEEDED(hr))hr=bar->put_ForegroundColor(color.Get());
    Diagnostic(darkIcons?"Phone status bar dark icons":"Phone status bar light icons",hr);
    return hr;
}
