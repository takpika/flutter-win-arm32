#pragma once
#include <windows.h>
#include <windows.storage.h>
#include <wrl.h>
#include <string>
#include "storage_access.h"

namespace flutter::winrt {
// WindowsStorageCOM ABI. Availability on Mobile must be established by QI;
// desktop documentation alone is not evidence that a Phone supports it.
struct StorageFolderHandleAccess : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Create(LPCWSTR,UINT32,UINT32,UINT32,
                                             UINT32,IUnknown*,HANDLE*)=0;
};
struct StorageItemHandleAccess : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Create(UINT32,UINT32,UINT32,IUnknown*,HANDLE*)=0;
};
struct StorageFileStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetFileFromPathAsync(HSTRING,
        ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFile*>**)=0;
};
struct StorageFolderStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetFolderFromPathAsync(HSTRING,
        ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFolder*>**)=0;
};
inline HRESULT OpenStorageFolderFile(ABI::Windows::Storage::IStorageFolder* folder,
    LPCWSTR name,DWORD creation,DWORD access,DWORD sharing,DWORD options,HANDLE* handle) {
    if(!handle)return E_POINTER;
    *handle=INVALID_HANDLE_VALUE;
    if(!folder||!name)return E_INVALIDARG;
    const GUID iid={0xdf19938f,0x5462,0x48a0,{0xbe,0x65,0xd2,0xa3,0x27,0x1a,0x08,0xd6}};
    Microsoft::WRL::ComPtr<StorageFolderHandleAccess> handles;
    HRESULT hr=folder->QueryInterface(iid,reinterpret_cast<void**>(handles.GetAddressOf()));
    return SUCCEEDED(hr)?handles->Create(name,creation,access,sharing,options,nullptr,handle):hr;
}

// Reacquire the authorized folder through the OS instead of treating a path
// as a permission grant. Run on an initialized MTA, outside a loader callback.
inline HRESULT GetStorageFolderByPath(LPCWSTR path,ABI::Windows::Storage::IStorageFolder** folder) {
    if(!folder)return E_POINTER;*folder=nullptr;if(!path)return E_INVALIDARG;
    HSTRING directory=nullptr,runtimeClass=nullptr;
    HRESULT hr=WindowsCreateString(path,static_cast<UINT32>(wcslen(path)),&directory);
    const wchar_t* cls=L"Windows.Storage.StorageFolder";
    if(SUCCEEDED(hr))hr=WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&runtimeClass);
    Microsoft::WRL::ComPtr<StorageFolderStatics> factory;
    const GUID iid={0x08f327ff,0x85d5,0x48b9,{0xae,0xe9,0x28,0x51,0x1e,0x33,0x9f,0x9f}};
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(runtimeClass,iid,reinterpret_cast<void**>(factory.GetAddressOf()));
    WindowsDeleteString(runtimeClass);
    Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFolder*>> operation;
    if(SUCCEEDED(hr))hr=factory->GetFolderFromPathAsync(directory,&operation);
    WindowsDeleteString(directory);
    if(hr==E_ACCESSDENIED)return RestoreStorageFolderPath(path,folder);
    if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<IAsyncInfo> info;hr=operation.As(&info);
    if(FAILED(hr))return hr;
    ULONGLONG deadline=GetTickCount64()+15000;AsyncStatus status{};
    do {
        hr=info->get_Status(&status);if(FAILED(hr))return hr;
        if(status!=Started)break;
        if(GetTickCount64()>=deadline){info->Cancel();return HRESULT_FROM_WIN32(ERROR_TIMEOUT);}
        Sleep(8);
    }while(true);
    if(status!=Completed){HRESULT error=S_OK;hr=info->get_ErrorCode(&error);
        if(SUCCEEDED(hr)&&error==E_ACCESSDENIED)return RestoreStorageFolderPath(path,folder);
        return FAILED(hr)?hr:FAILED(error)?error:HRESULT_FROM_WIN32(ERROR_CANCELLED);}
    hr=operation->GetResults(folder);
    if(hr==E_ACCESSDENIED)return RestoreStorageFolderPath(path,folder);
    return hr;
}
inline HRESULT OpenStorageItemFile(ABI::Windows::Storage::IStorageFile* file,
    DWORD access,DWORD sharing,DWORD options,HANDLE* handle) {
    if(!handle)return E_POINTER;*handle=INVALID_HANDLE_VALUE;if(!file)return E_INVALIDARG;
    const GUID iid={0x5ca296b2,0x2c25,0x4d22,{0xb7,0x85,0xb8,0x85,0xc8,0x20,0x1e,0x6a}};
    Microsoft::WRL::ComPtr<StorageItemHandleAccess> handles;
    HRESULT hr=file->QueryInterface(iid,reinterpret_cast<void**>(handles.GetAddressOf()));
    return SUCCEEDED(hr)?handles->Create(access,sharing,options,nullptr,handle):hr;
}
// Picker access to one file does not grant access to its parent folder.
// Reacquire that exact existing file through the OS, then request a real handle.
inline HRESULT OpenStorageItemPathFile(LPCWSTR path,DWORD access,DWORD sharing,
    DWORD options,HANDLE* handle) {
    if(!handle)return E_POINTER;*handle=INVALID_HANDLE_VALUE;if(!path)return E_INVALIDARG;
    HSTRING name=nullptr,runtimeClass=nullptr;
    HRESULT hr=WindowsCreateString(path,static_cast<UINT32>(wcslen(path)),&name);
    const wchar_t* cls=L"Windows.Storage.StorageFile";
    if(SUCCEEDED(hr))hr=WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&runtimeClass);
    Microsoft::WRL::ComPtr<StorageFileStatics> factory;
    const GUID staticsIid={0x5984c710,0xdaf2,0x43c8,{0x8b,0xb4,0xa4,0xd3,0xea,0xcf,0xd0,0x3f}};
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(runtimeClass,staticsIid,reinterpret_cast<void**>(factory.GetAddressOf()));
    WindowsDeleteString(runtimeClass);
    Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFile*>> operation;
    if(SUCCEEDED(hr))hr=factory->GetFileFromPathAsync(name,&operation);
    WindowsDeleteString(name);
    if(hr==E_ACCESSDENIED) {
        Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageFile> remembered;
        hr=RestoreStorageFile(path,&remembered);
        return SUCCEEDED(hr)?OpenStorageItemFile(remembered.Get(),access,sharing,options,handle):hr;
    }
    if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<IAsyncInfo> info;hr=operation.As(&info);if(FAILED(hr))return hr;
    ULONGLONG deadline=GetTickCount64()+15000;AsyncStatus status{};
    do {hr=info->get_Status(&status);if(FAILED(hr))return hr;if(status!=Started)break;
        if(GetTickCount64()>=deadline){info->Cancel();return HRESULT_FROM_WIN32(ERROR_TIMEOUT);}Sleep(8);
    }while(true);
    Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageFile> file;
    if(status==Completed)hr=operation->GetResults(&file);
    else {HRESULT error=S_OK;hr=info->get_ErrorCode(&error);hr=FAILED(hr)?hr:FAILED(error)?error:HRESULT_FROM_WIN32(ERROR_CANCELLED);}
    if(hr==E_ACCESSDENIED)hr=RestoreStorageFile(path,&file);
    if(FAILED(hr))return hr;
    return OpenStorageItemFile(file.Get(),access,sharing,options,handle);
}
inline HRESULT OpenStoragePathFile(LPCWSTR path,DWORD creation,DWORD access,
    DWORD sharing,DWORD options,HANDLE* handle) {
    if(!handle)return E_POINTER;*handle=INVALID_HANDLE_VALUE;
    if(!path)return E_INVALIDARG;
    const wchar_t* slash=wcsrchr(path,L'\\');
    if(!slash||slash==path||!slash[1])return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    std::wstring directory(path,slash-path);
    Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageFolder> folder;
    HRESULT hr=GetStorageFolderByPath(directory.c_str(),&folder);
    if(SUCCEEDED(hr))return OpenStorageFolderFile(folder.Get(),slash+1,creation,access,sharing,options,handle);
    if(hr==E_ACCESSDENIED && creation==OPEN_EXISTING)
        return OpenStorageItemPathFile(path,access,sharing,options,handle);
    return hr;
}

}
