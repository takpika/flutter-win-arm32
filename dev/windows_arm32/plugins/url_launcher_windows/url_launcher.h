#pragma once
#include "flutter/shell/platform/windows/uwp/runtime_support.h"
#include <vector>
#include <memory>
#include <flutter/standard_message_codec.h>

namespace flutter::winrt::plugins {
using ABI::Windows::ApplicationModel::Core::IFrameworkView;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::Foundation;
void Diagnostic(const char* message, HRESULT result);

// Windows.System.Launcher ABI prefixes, checked against Microsoft C++/WinRT.
template<class T> struct FlutterWinRTLaunchOperation : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown*)=0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown**)=0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(T*)=0;
};
struct FlutterWinRTLauncher : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE LaunchFileAsync(IInspectable*,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE LaunchFileWithOptionsAsync(IInspectable*,IInspectable*,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE LaunchUriAsync(IUriRuntimeClass*,FlutterWinRTLaunchOperation<boolean>**)=0;
    virtual HRESULT STDMETHODCALLTYPE LaunchUriWithOptionsAsync(IUriRuntimeClass*,IInspectable*,IInspectable**)=0;
};
struct FlutterWinRTLauncher2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE LaunchUriForResultsAsync(IInspectable*,IInspectable*,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE LaunchUriForResultsWithDataAsync(IInspectable*,IInspectable*,IInspectable*,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE LaunchUriWithDataAsync(IInspectable*,IInspectable*,IInspectable*,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE QueryUriSupportAsync(IUriRuntimeClass*,INT,FlutterWinRTLaunchOperation<INT>**)=0;
};

class FlutterWinRTUrlLauncher {
    using Value=flutter::EncodableValue;
    using List=flutter::EncodableList;
    struct Lifetime {};
    std::shared_ptr<Lifetime> lifetime_=std::make_shared<Lifetime>();
    struct Request : Ref<IDispatchedHandler> {
        std::weak_ptr<Lifetime> lifetime;
        ComPtr<IFrameworkView> owner;
        ComPtr<ICoreDispatcher> dispatcher;
        ComPtr<FlutterWinRTLaunchOperation<boolean>> launch;
        ComPtr<FlutterWinRTLaunchOperation<INT>> query;
        FlutterEngine engine;
        FlutterEngineProcTable api;
        const FlutterPlatformMessageResponseHandle* response;
        std::vector<uint8_t> bytes;
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
            if(id==__uuidof(IAgileObject)){if(!out)return E_POINTER;*out=static_cast<IDispatchedHandler*>(this);AddRef();return S_OK;}
            return Ref<IDispatchedHandler>::QueryInterface(id,out);
        }
        void Finish(HRESULT hr,bool result=false) {
            if(lifetime.expired())return;
            Value value=FAILED(hr)?Value(List{Value("WINRT_LAUNCHER"),Value("Windows.System.Launcher failed: "+std::to_string(static_cast<unsigned long>(hr))),Value()}):Value(List{Value(result)});
            bytes=*flutter::StandardMessageCodec::GetInstance().EncodeMessage(value);
            ComPtr<IAsyncAction> action;
            Diagnostic("WinRT URL launcher response",dispatcher->RunAsync(CoreDispatcherPriority_Normal,this,&action));
        }
        HRESULT STDMETHODCALLTYPE Invoke() override {
            auto alive=lifetime.lock();
            if(alive&&engine&&response)api.SendPlatformMessageResponse(engine,response,bytes.data(),bytes.size());
            return S_OK;
        }
        static DWORD WINAPI Wait(void* context) {
            ComPtr<Request> request;request.Attach(static_cast<Request*>(context));
            HRESULT initialized=RoInitialize(RO_INIT_MULTITHREADED),hr=initialized;
            ComPtr<IAsyncInfo> info;
            if(SUCCEEDED(hr))hr=request->launch?request->launch.As(&info):request->query.As(&info);
            AsyncStatus status=Started;
            while(SUCCEEDED(hr)&&status==Started&&!request->lifetime.expired()){hr=info->get_Status(&status);if(SUCCEEDED(hr)&&status==Started)Sleep(8);}
            bool result=false;
            if(SUCCEEDED(hr)&&status==Completed) {
                if(request->launch){boolean launched=false;hr=request->launch->GetResults(&launched);result=launched;}
                else {INT support=4;hr=request->query->GetResults(&support);result=support==0;}
            } else if(SUCCEEDED(hr)) {
                if(status==AsyncStatus::Error){HRESULT error=S_OK;hr=info->get_ErrorCode(&error);if(SUCCEEDED(hr))hr=FAILED(error)?error:E_FAIL;}
                else hr=HRESULT_FROM_WIN32(ERROR_CANCELLED);
            }
            request->Finish(hr,result);
            if(SUCCEEDED(initialized))RoUninitialize();return 0;
        }
    };
    static HRESULT Factory(const wchar_t* name,REFIID iid,void** output) {
        HSTRING text=nullptr;HRESULT hr=WindowsCreateString(name,static_cast<UINT32>(wcslen(name)),&text);
        if(SUCCEEDED(hr))hr=RoGetActivationFactory(text,iid,output);
        WindowsDeleteString(text);return hr;
    }
public:
    bool Handle(const FlutterPlatformMessage* message,ICoreDispatcher* dispatcher,IFrameworkView* owner,
                FlutterEngine engine,const FlutterEngineProcTable& api) {
        bool query=strcmp(message->channel,"dev.flutter.pigeon.url_launcher_windows.UrlLauncherApi.canLaunchUrl")==0;
        if(!query&&strcmp(message->channel,"dev.flutter.pigeon.url_launcher_windows.UrlLauncherApi.launchUrl")!=0)return false;
        ComPtr<Request> request;request.Attach(new Request());request->owner=owner;request->dispatcher=dispatcher;
        request->lifetime=lifetime_;
        request->engine=engine;request->api=api;request->response=message->response_handle;
        auto decoded=flutter::StandardMessageCodec::GetInstance().DecodeMessage(message->message,message->message_size);
        const auto* args=decoded?std::get_if<List>(decoded.get()):nullptr;
        const auto* url=args&&args->size()==1?std::get_if<std::string>(&(*args)[0]):nullptr;
        HRESULT hr=url?S_OK:E_INVALIDARG;HSTRING text=nullptr;
        if(url){int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url->data(),static_cast<int>(url->size()),nullptr,0);
            if(!size)hr=E_INVALIDARG;
            else {std::wstring wide(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url->data(),static_cast<int>(url->size()),wide.data(),size);hr=WindowsCreateString(wide.data(),size,&text);}}
        ComPtr<IUriRuntimeClassFactory> factory;ComPtr<IUriRuntimeClass> uri;
        if(SUCCEEDED(hr))hr=Factory(L"Windows.Foundation.Uri",IID_PPV_ARGS(&factory));
        if(SUCCEEDED(hr))hr=factory->CreateUri(text,&uri);WindowsDeleteString(text);
        if(SUCCEEDED(hr)&&query){ComPtr<FlutterWinRTLauncher2> launcher;
            const GUID iid={0x59ba2fbb,0x24cb,0x4c02,{0xa4,0xc4,0x82,0x94,0x56,0x9d,0x54,0xf1}};
            hr=Factory(L"Windows.System.Launcher",iid,reinterpret_cast<void**>(launcher.GetAddressOf()));
            if(SUCCEEDED(hr))hr=launcher->QueryUriSupportAsync(uri.Get(),0,&request->query);
        } else if(SUCCEEDED(hr)){ComPtr<FlutterWinRTLauncher> launcher;
            const GUID iid={0x277151c3,0x9e3e,0x42f6,{0x91,0xa4,0x5d,0xfd,0xeb,0x23,0x24,0x51}};
            hr=Factory(L"Windows.System.Launcher",iid,reinterpret_cast<void**>(launcher.GetAddressOf()));
            if(SUCCEEDED(hr))hr=launcher->LaunchUriAsync(uri.Get(),&request->launch);
        }
        if(SUCCEEDED(hr)){request->AddRef();HANDLE worker=CreateThread(nullptr,0,Request::Wait,request.Get(),0,nullptr);
            if(worker)CloseHandle(worker);else {hr=HRESULT_FROM_WIN32(GetLastError());request->Release();}}
        if(FAILED(hr))request->Finish(hr);
        Diagnostic("WinRT URL launcher start",hr);return true;
    }
};

}  // namespace flutter::winrt::plugins
