#pragma once
#include "flutter/shell/platform/windows/uwp/runtime_support.h"
#include <vector>
#include <memory>
#include "file_picker_abi.h"
#include "messages.g.h"

namespace flutter::winrt::plugins {
using ABI::Windows::ApplicationModel::Core::IFrameworkView;
using ABI::Windows::UI::Core::ICoreDispatcher;
using ABI::Windows::UI::Core::IDispatchedHandler;
using ABI::Windows::UI::Core::CoreDispatcherPriority_Normal;
using ABI::Windows::Foundation::IAsyncAction;
void Diagnostic(const char* message, HRESULT result);
// Native implementation of the upstream file_selector_windows Pigeon contract.
// Return the selected item's own path. Brokered I/O remains a separate concern;
// do not silently substitute a cached copy or an application-specific token.
class FlutterWinRTFileSelector {
    using Value=flutter::EncodableValue;
    using List=flutter::EncodableList;
    struct Lifetime {};
    std::shared_ptr<Lifetime> lifetime_=std::make_shared<Lifetime>();
    struct Request;
    class ReplyTask : public Ref<IDispatchedHandler> {
        std::shared_ptr<Request> request;
        std::vector<uint8_t> bytes;
    public:
        ReplyTask(std::shared_ptr<Request> value,std::vector<uint8_t> payload)
            :request(std::move(value)),bytes(std::move(payload)){}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
            if(id==__uuidof(IAgileObject)) {if(!out)return E_POINTER;*out=static_cast<IDispatchedHandler*>(this);AddRef();return S_OK;}
            return Ref<IDispatchedHandler>::QueryInterface(id,out);
        }
        HRESULT STDMETHODCALLTYPE Invoke() override;
    };
    struct Request : std::enable_shared_from_this<Request> {
        std::weak_ptr<Lifetime> lifetime;
        ComPtr<IFrameworkView> owner;
        ComPtr<ICoreDispatcher> dispatcher;
        ComPtr<PhoneFileOpenPicker> picker;
        ComPtr<PhoneFolderPicker> folderPicker;
        ComPtr<IInspectable> pending;
        FlutterEngine engine;
        FlutterEngineProcTable api;
        const FlutterPlatformMessageResponseHandle* response;
        bool completed=false;
        void Finish(Value value) {
            if(completed)return;
            completed=true;pending.Reset();picker.Reset();folderPicker.Reset();
            if(lifetime.expired())return;
            auto bytes=file_selector_windows::FileSelectorApi::GetCodec().EncodeMessage(value);
            ComPtr<IDispatchedHandler> task;task.Attach(new ReplyTask(shared_from_this(),std::move(*bytes)));
            ComPtr<IAsyncAction> action;
            HRESULT hr=dispatcher->RunAsync(CoreDispatcherPriority_Normal,task.Get(),&action);
            Diagnostic("WinRT file selector response dispatch",hr);
        }
        void Fail(HRESULT hr) {
            char text[80];snprintf(text,sizeof(text),"WinRT picker failed: 0x%08lx",static_cast<unsigned long>(hr));
            Diagnostic("WinRT file selector failure",hr);
            Finish(file_selector_windows::FileSelectorApi::WrapError(file_selector_windows::FlutterError("WINRT_PICKER",text)));
        }
        void Success(List paths) {
            file_selector_windows::FileDialogResult result(paths);
            Finish(Value(List{Value(flutter::CustomEncodableValue(result))}));
        }
        template<class StorageItem> static HRESULT AddPath(StorageItem* file,List& paths,const char* exportName="FlutterWinRTRememberStorageFile") {
            ComPtr<ABI::Windows::Storage::IStorageItem> item;
            HRESULT hr=file->QueryInterface(IID_PPV_ARGS(&item));
            HSTRING path=nullptr;if(SUCCEEDED(hr))hr=item->get_Path(&path);
            if(SUCCEEDED(hr)) {
                // Preserve the OS picker grant in the SDK before returning its path.
                HMODULE sdk=LoadPackagedLibrary(L"flutter_winrt_compat.dll",0);
                if(!sdk)hr=HRESULT_FROM_WIN32(GetLastError());
                else {
                    using Remember=HRESULT(WINAPI*)(StorageItem*);
                    auto remember=reinterpret_cast<Remember>(GetProcAddress(sdk,exportName));
                    hr=remember?remember(file):HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
                    FreeLibrary(sdk);
                }
                Diagnostic("WinRT file selector SDK authorization",hr);
            }
            if(SUCCEEDED(hr)) {
                auto text=PhoneUtf8(path);
                if(text.empty())hr=HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
                else paths.emplace_back(text);
            }
            WindowsDeleteString(path);return hr;
        }
    };
    static HRESULT SetText(const std::string& value,HSTRING* text) {
        int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
        if(!value.empty()&&!length)return E_INVALIDARG;
        std::vector<wchar_t> wide(length);
        if(length&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),wide.data(),length))return E_INVALIDARG;
        return WindowsCreateString(length?wide.data():L"",length,text);
    }
public:
    bool Handle(const FlutterPlatformMessage* message,ICoreDispatcher* dispatcher,IFrameworkView* owner,
                FlutterEngine engine,const FlutterEngineProcTable& api) {
        if(strcmp(message->channel,"dev.flutter.pigeon.file_selector_windows.FileSelectorApi.showOpenDialog")!=0)return false;
        auto request=std::make_shared<Request>();request->owner=owner;request->dispatcher=dispatcher;
        request->lifetime=lifetime_;
        request->engine=engine;request->api=api;request->response=message->response_handle;
        auto decoded=file_selector_windows::FileSelectorApi::GetCodec().DecodeMessage(message->message,message->message_size);
        const auto* args=decoded?std::get_if<List>(decoded.get()):nullptr;
        const auto* custom=args&&args->size()==3?std::get_if<flutter::CustomEncodableValue>(&(*args)[0]):nullptr;
        const auto* options=custom?std::any_cast<file_selector_windows::SelectionOptions>(&static_cast<const std::any&>(*custom)):nullptr;
        if(!options) {request->Fail(E_INVALIDARG);return true;}
        if(options->select_folders()) {
            ComPtr<IInspectable> object;
            HRESULT hr=PhoneActivate(L"Windows.Storage.Pickers.FolderPicker",&object);
            if(SUCCEEDED(hr))hr=object.As(&request->folderPicker);
            ComPtr<PhoneStringVector> filters;
            if(SUCCEEDED(hr))hr=request->folderPicker->get_FileTypeFilter(&filters);
            HSTRING text=nullptr;
            if(SUCCEEDED(hr))hr=WindowsCreateString(L"*",1,&text);
            if(SUCCEEDED(hr))hr=filters->Append(text);
            WindowsDeleteString(text);text=nullptr;
            if(SUCCEEDED(hr)&&!(*args)[2].IsNull()) {
                const auto* label=std::get_if<std::string>(&(*args)[2]);
                hr=label?SetText(*label,&text):E_INVALIDARG;
                if(SUCCEEDED(hr))hr=request->folderPicker->put_CommitButtonText(text);
                WindowsDeleteString(text);
            }
            ComPtr<PhoneFolderOperation> operation;
            if(SUCCEEDED(hr))hr=request->folderPicker->PickSingleFolderAsync(&operation);
            if(SUCCEEDED(hr)) {
                request->pending=operation;
                auto handler=PhoneCompleted<ABI::Windows::Foundation::IAsyncOperationCompletedHandler<ABI::Windows::Storage::StorageFolder*>,PhoneFolderOperation>(
                    [request](PhoneFolderOperation* operation,AsyncStatus status)->HRESULT {
                        if(request->lifetime.expired()){request->Finish(Value());return S_OK;}
                        if(status==AsyncStatus::Canceled){request->Success({});return S_OK;}
                        ComPtr<ABI::Windows::Storage::IStorageFolder> folder;
                        HRESULT hr=operation->GetResults(&folder);List paths;
                        if(SUCCEEDED(hr)&&folder)hr=Request::AddPath(folder.Get(),paths,"FlutterWinRTRememberStorageFolder");
                        if(FAILED(hr))request->Fail(hr);else request->Success(std::move(paths));return S_OK;
                    });
                hr=operation->put_Completed(handler.Get());
            }
            if(FAILED(hr))request->Fail(hr);
            Diagnostic("WinRT file selector folder open",hr);return true;
        }
        ComPtr<IInspectable> object;HRESULT hr=PhoneActivate(L"Windows.Storage.Pickers.FileOpenPicker",&object);
        if(SUCCEEDED(hr))hr=object.As(&request->picker);
        ComPtr<PhoneStringVector> filters;if(SUCCEEDED(hr))hr=request->picker->get_FileTypeFilter(&filters);
        if(SUCCEEDED(hr)&&options->allowed_types().empty()) {
            HSTRING text=nullptr;hr=WindowsCreateString(L"*",1,&text);
            if(SUCCEEDED(hr))hr=filters->Append(text);WindowsDeleteString(text);
        }
        for(const auto& group:options->allowed_types()) {
            const auto* customGroup=std::get_if<flutter::CustomEncodableValue>(&group);
            const auto* type=customGroup?std::any_cast<file_selector_windows::TypeGroup>(&static_cast<const std::any&>(*customGroup)):nullptr;
            if(!type) {hr=E_INVALIDARG;break;}
            // Upstream treats a type group without extensions as all files.
            if(type->extensions().empty()) {
                HSTRING text=nullptr;if(SUCCEEDED(hr))hr=WindowsCreateString(L"*",1,&text);
                if(SUCCEEDED(hr))hr=filters->Append(text);WindowsDeleteString(text);
                continue;
            }
            for(const auto& extension:type->extensions()) {
                const auto* ext=std::get_if<std::string>(&extension);
                if(!ext||ext->empty()) {hr=E_INVALIDARG;break;}
                HSTRING text=nullptr;if(SUCCEEDED(hr))hr=SetText("."+*ext,&text);
                if(SUCCEEDED(hr))hr=filters->Append(text);WindowsDeleteString(text);
            }
        }
        if(SUCCEEDED(hr)&&!(*args)[2].IsNull()) {
            const auto* label=std::get_if<std::string>(&(*args)[2]);
            if(!label)hr=E_INVALIDARG;
            else {HSTRING text=nullptr;hr=SetText(*label,&text);if(SUCCEEDED(hr))hr=request->picker->put_CommitButtonText(text);WindowsDeleteString(text);}
        }
        // Initial directory has no equivalent on the Mobile picker; leave the
        // OS picker at its normal remembered location rather than inventing one.
        if(FAILED(hr)) {request->Fail(hr);return true;}
        if(options->allow_multiple()) {
            ComPtr<PhonePickOperation> operation;hr=request->picker->PickMultipleFilesAsync(&operation);
            if(SUCCEEDED(hr)) {
                request->pending=operation;
                auto handler=PhoneCompleted<ABI::Windows::Foundation::IAsyncOperationCompletedHandler<PhoneFiles*>,PhonePickOperation>(
                    [request](PhonePickOperation* operation,AsyncStatus status)->HRESULT {
                        if(request->lifetime.expired()){request->Finish(Value());return S_OK;}
                        if(status==AsyncStatus::Canceled) {request->Success({});return S_OK;}
                        ComPtr<PhoneFiles> files;HRESULT hr=operation->GetResults(&files);List paths;UINT32 count=0;
                        if(SUCCEEDED(hr)&&files)hr=files->get_Size(&count);
                        for(UINT32 i=0;SUCCEEDED(hr)&&i<count;++i) {
                            ComPtr<ABI::Windows::Storage::IStorageFile> file;hr=files->GetAt(i,&file);
                            if(SUCCEEDED(hr))hr=Request::AddPath(file.Get(),paths);
                        }
                        if(FAILED(hr))request->Fail(hr);else request->Success(std::move(paths));return S_OK;
                    });
                hr=operation->put_Completed(handler.Get());
            }
        } else {
            ComPtr<PhoneCopyOperation> operation;hr=request->picker->PickSingleFileAsync(&operation);
            if(SUCCEEDED(hr)) {
                request->pending=operation;
                auto handler=PhoneCompleted<ABI::Windows::Foundation::IAsyncOperationCompletedHandler<ABI::Windows::Storage::StorageFile*>,PhoneCopyOperation>(
                    [request](PhoneCopyOperation* operation,AsyncStatus status)->HRESULT {
                        if(request->lifetime.expired()){request->Finish(Value());return S_OK;}
                        if(status==AsyncStatus::Canceled) {request->Success({});return S_OK;}
                        ComPtr<ABI::Windows::Storage::IStorageFile> file;HRESULT hr=operation->GetResults(&file);List paths;
                        if(SUCCEEDED(hr)&&file)hr=Request::AddPath(file.Get(),paths);
                        if(FAILED(hr))request->Fail(hr);else request->Success(std::move(paths));return S_OK;
                    });
                hr=operation->put_Completed(handler.Get());
            }
        }
        if(FAILED(hr))request->Fail(hr);
        Diagnostic("WinRT file selector open",hr);return true;
    }
};
HRESULT FlutterWinRTFileSelector::ReplyTask::Invoke() {
    auto alive=request->lifetime.lock();
    if(alive&&request->engine&&request->response)request->api.SendPlatformMessageResponse(request->engine,request->response,bytes.data(),bytes.size());
    return S_OK;
}
}  // namespace flutter::winrt::plugins
