#pragma once
#include <windows.applicationmodel.datatransfer.h>
#include <memory>
#include <string>
#include <vector>

// Marshal async service replies to Flutter's platform thread. The dispatcher
// delegate is agile; the owned response and view are only used by Invoke.
class PhoneServiceReplyTask : public Ref<IDispatchedHandler> {
    ComPtr<IFrameworkView> owner;
    PhoneReply reply;
    void* userData;
    const FlutterPlatformMessageResponseHandle* response;
    std::string envelope;
public:
    PhoneServiceReplyTask(IFrameworkView* view,PhoneReply fn,void* user,
                         const FlutterPlatformMessageResponseHandle* handle,std::string value)
        :owner(view),reply(fn),userData(user),response(handle),envelope(std::move(value)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(id==__uuidof(IAgileObject)) {
            if(!out)return E_POINTER;
            *out=static_cast<IDispatchedHandler*>(this);AddRef();return S_OK;
        }
        return Ref<IDispatchedHandler>::QueryInterface(id,out);
    }
    HRESULT STDMETHODCALLTYPE Invoke() override {
        reply(userData,response,envelope.data(),envelope.size());return S_OK;
    }
};
static HRESULT PhoneDispatchReply(ICoreDispatcher* dispatcher,IFrameworkView* owner,
                                  PhoneReply reply,void* user,
                                  const FlutterPlatformMessageResponseHandle* response,
                                  std::string envelope) {
    ComPtr<IDispatchedHandler> task;task.Attach(new PhoneServiceReplyTask(owner,reply,user,response,std::move(envelope)));
    ComPtr<IAsyncAction> action;
    HRESULT hr=dispatcher->RunAsync(CoreDispatcherPriority_Normal,task.Get(),&action);
    Diagnostic("Phone native service dispatch response",hr);return hr;
}
static HRESULT PhoneClipboardFactory(ABI::Windows::ApplicationModel::DataTransfer::IClipboardStatics** result) {
    HSTRING name=nullptr;const wchar_t* cls=L"Windows.ApplicationModel.DataTransfer.Clipboard";
    HRESULT hr=WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&name);
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(name,__uuidof(ABI::Windows::ApplicationModel::DataTransfer::IClipboardStatics),reinterpret_cast<void**>(result));
    WindowsDeleteString(name);return hr;
}
struct PhoneClipboardReadState {
    ComPtr<IFrameworkView> owner;
    ComPtr<ICoreDispatcher> dispatcher;
    ComPtr<ABI::Windows::ApplicationModel::DataTransfer::IDataPackageView> view;
    ComPtr<IAsyncOperation<HSTRING>> pending;
    PhoneReply reply=nullptr;
    void* user=nullptr;
    const FlutterPlatformMessageResponseHandle* response=nullptr;
    HRESULT Finish(std::string envelope) {
        return PhoneDispatchReply(dispatcher.Get(),owner.Get(),reply,user,response,std::move(envelope));
    }
};
static HRESULT PhoneHandleClipboard(const rapidjson::Value& call,ICoreDispatcher* dispatcher,
                                     IFrameworkView* owner,PhoneReply reply,void* user,
                                     const FlutterPlatformMessageResponseHandle* response) {
    using namespace ABI::Windows::ApplicationModel::DataTransfer;
    if(!call.HasMember("args"))return E_INVALIDARG;
    const auto& args=call["args"];
    const char* method=call["method"].GetString();
    ComPtr<IClipboardStatics> clipboard;HRESULT hr=PhoneClipboardFactory(&clipboard);
    Diagnostic("Phone OS Clipboard factory",hr);if(FAILED(hr))return hr;
    if(strcmp(method,"Clipboard.setData")==0) {
        if(!args.IsObject() || !args.HasMember("text") || !args["text"].IsString())return E_INVALIDARG;
        const auto& value=args["text"];
        int units=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.GetString(),value.GetStringLength(),nullptr,0);
        if(value.GetStringLength() && !units)return HRESULT_FROM_WIN32(GetLastError());
        std::vector<wchar_t> text(units);
        if(units && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.GetString(),value.GetStringLength(),text.data(),units))return HRESULT_FROM_WIN32(GetLastError());
        HSTRING string=nullptr;hr=WindowsCreateString(units?text.data():L"",units,&string);
        ComPtr<IInspectable> object;ComPtr<IDataPackage> package;
        if(SUCCEEDED(hr))hr=PhoneActivate(L"Windows.ApplicationModel.DataTransfer.DataPackage",&object);
        if(SUCCEEDED(hr))hr=object.As(&package);
        if(SUCCEEDED(hr))hr=package->SetText(string);WindowsDeleteString(string);
        if(SUCCEEDED(hr))hr=clipboard->SetContent(package.Get());
        Diagnostic("Phone OS Clipboard set text",hr);
        if(SUCCEEDED(hr)) {hr=clipboard->Flush();Diagnostic("Phone OS Clipboard flush",hr);}
        if(SUCCEEDED(hr))reply(user,response,"[null]",6);
        return hr;
    }
    bool hasStrings=strcmp(method,"Clipboard.hasStrings")==0;
    if(!hasStrings && strcmp(method,"Clipboard.getData")!=0)return E_NOTIMPL;
    if(!args.IsString() || strcmp(args.GetString(),"text/plain")!=0) {
        const char* empty=hasStrings?"[{\"value\":false}]":"[null]";
        reply(user,response,empty,strlen(empty));return S_OK;
    }
    ComPtr<IDataPackageView> view;hr=clipboard->GetContent(&view);
    boolean contains=false;HSTRING textFormat=nullptr;
    if(SUCCEEDED(hr))hr=WindowsCreateString(L"Text",4,&textFormat);
    if(SUCCEEDED(hr) && view)hr=view->Contains(textFormat,&contains);
    WindowsDeleteString(textFormat);Diagnostic("Phone OS Clipboard contains text",hr);
    if(FAILED(hr))return hr;
    if(hasStrings || !contains) {
        const char* envelope=hasStrings?(contains?"[{\"value\":true}]":"[{\"value\":false}]"):"[null]";
        reply(user,response,envelope,strlen(envelope));return S_OK;
    }
    auto state=std::make_shared<PhoneClipboardReadState>();
    state->owner=owner;state->dispatcher=dispatcher;state->view=view;
    state->reply=reply;state->user=user;state->response=response;
    hr=view->GetTextAsync(&state->pending);if(FAILED(hr))return hr;
    auto completed=PhoneCompleted<IAsyncOperationCompletedHandler<HSTRING>,IAsyncOperation<HSTRING>>(
        [state](IAsyncOperation<HSTRING>* operation,AsyncStatus) -> HRESULT {
            HSTRING text=nullptr;HRESULT result=operation->GetResults(&text);
            state->pending.Reset();state->view.Reset();
            Diagnostic("Phone OS Clipboard get text",result);
            std::string envelope=SUCCEEDED(result)?"[{\"text\":"+PhoneJsonQuote(PhoneUtf8(text))+"}]":"[\"PHONE_CLIPBOARD\",\"WinRT clipboard read failed\",null]";
            WindowsDeleteString(text);return state->Finish(std::move(envelope));
        });
    hr=state->pending->put_Completed(completed.Get());
    if(FAILED(hr))state->pending.Reset();return hr;
}
