#pragma once
#include <windows.h>
#include <roapi.h>
#include <windows.storage.h>
#include <wrl.h>

namespace flutter::winrt {
// ABI from the SDK's IDownloadsFolderStatics metadata. MinGW's generated
// storage header omits this interface; preserve the original vtable order.
struct DownloadsStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateFileAsync(HSTRING,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE CreateFolderAsync(HSTRING,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE CreateFileWithCollisionOptionAsync(HSTRING,INT32,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE CreateFolderWithCollisionOptionAsync(HSTRING,INT32,IInspectable**)=0;
};

// Must run on an initialized MTA. DownloadsFolder grants access to folders
// created by this app; no hardcoded public path or filesystem permission
// bypass is used. The caller owns the returned StorageFolder reference.
inline HRESULT CreateDownloadsDirectory(HSTRING name,
    ABI::Windows::Storage::IStorageFolder** folder,
    void (*observe)(const char*,HRESULT)=nullptr) {
    if(!folder)return E_POINTER;
    *folder=nullptr;
    HSTRING runtimeClass=nullptr;
    const wchar_t* cls=L"Windows.Storage.DownloadsFolder";
    HRESULT hr=WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&runtimeClass);
    Microsoft::WRL::ComPtr<DownloadsStatics> factory;
    const GUID iid={0x27862ed0,0x404e,0x47df,{0xa1,0xe2,0xe3,0x73,0x08,0xbe,0x7b,0x37}};
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(runtimeClass,iid,reinterpret_cast<void**>(factory.GetAddressOf()));
    if(observe)observe("DownloadsFolder activation",hr);
    WindowsDeleteString(runtimeClass);
    if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<IInspectable> pending;
    hr=factory->CreateFolderWithCollisionOptionAsync(name,
        ABI::Windows::Storage::CreationCollisionOption_GenerateUniqueName,&pending);
    if(observe)observe("DownloadsFolder CreateFolderWithCollisionOptionAsync",hr);
    if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<IAsyncInfo> info;
    hr=pending.As(&info);
    if(observe)observe("DownloadsFolder async info",hr);
    if(FAILED(hr))return hr;
    ULONGLONG deadline=GetTickCount64()+15000;
    AsyncStatus status{};
    do {
        hr=info->get_Status(&status);
        if(FAILED(hr))return hr;
        if(status!=Started)break;
        if(GetTickCount64()>=deadline){info->Cancel();return HRESULT_FROM_WIN32(ERROR_TIMEOUT);}
        Sleep(8);
    }while(true);
    if(status!=Completed) {
        HRESULT error=S_OK;hr=info->get_ErrorCode(&error);
        if(observe)observe("DownloadsFolder async completion error",FAILED(hr)?hr:error);
        return FAILED(hr)?hr:FAILED(error)?error:HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFolder*>> operation;
    hr=pending.As(&operation);
    if(observe)observe("DownloadsFolder typed async result",hr);
    if(SUCCEEDED(hr))hr=operation->GetResults(folder);
    if(observe)observe("DownloadsFolder GetResults",hr);
    return hr;
}
}
