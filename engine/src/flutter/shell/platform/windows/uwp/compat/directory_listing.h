#pragma once
#include "storage_handle.h"
#include <cstring>
#include <new>

namespace flutter::winrt {
using DirectoryItems=ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::Storage::IStorageItem*>;
struct DirectoryListing {
    Microsoft::WRL::ComPtr<DirectoryItems> items;
    UINT32 index=0,count=0;
};

inline HRESULT OpenDirectoryListing(LPCWSTR path,DirectoryListing** listing) {
    if(!listing)return E_POINTER;*listing=nullptr;
    Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageFolder> folder;
    HRESULT hr=GetStorageFolderByPath(path,&folder);if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<DirectoryItems*>> operation;
    hr=folder->GetItemsAsyncOverloadDefaultStartAndCount(&operation);if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<IAsyncInfo> info;hr=operation.As(&info);if(FAILED(hr))return hr;
    ULONGLONG deadline=GetTickCount64()+15000;AsyncStatus status{};
    do {hr=info->get_Status(&status);if(FAILED(hr))return hr;if(status!=Started)break;
        if(GetTickCount64()>=deadline){info->Cancel();return HRESULT_FROM_WIN32(ERROR_TIMEOUT);}Sleep(8);
    }while(true);
    if(status!=Completed){HRESULT error=S_OK;hr=info->get_ErrorCode(&error);return FAILED(hr)?hr:FAILED(error)?error:HRESULT_FROM_WIN32(ERROR_CANCELLED);}
    Microsoft::WRL::ComPtr<DirectoryItems> items;hr=operation->GetResults(&items);if(FAILED(hr))return hr;
    UINT32 count=0;hr=items->get_Size(&count);if(FAILED(hr))return hr;
    auto result=new(std::nothrow) DirectoryListing();if(!result)return E_OUTOFMEMORY;
    result->items=items;result->count=count;*listing=result;return S_OK;
}

// A directory cursor exposes exactly the name and attributes supplied by
// Windows.Storage. No cached copies, synthetic filesystem entries or guessed
// timestamps are returned. The caller serializes use and closes the cursor.
inline HRESULT NextDirectoryEntry(DirectoryListing* listing,wchar_t* name,
    UINT32 capacity,DWORD* attributes,BOOL* found) {
    if(!listing||!name||!capacity||!attributes||!found)return E_INVALIDARG;
    *found=FALSE;name[0]=L'\0';*attributes=0;
    if(listing->index==listing->count)return S_OK;
    Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageItem> item;
    HRESULT hr=listing->items->GetAt(listing->index,&item);HSTRING text=nullptr;
    if(SUCCEEDED(hr))hr=item->get_Name(&text);
    UINT32 length=0;const wchar_t* raw=WindowsGetStringRawBuffer(text,&length);
    if(SUCCEEDED(hr)&&length>=capacity)hr=HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    ABI::Windows::Storage::FileAttributes value{};
    if(SUCCEEDED(hr))hr=item->get_Attributes(&value);
    if(SUCCEEDED(hr)) {
        memcpy(name,raw,length*sizeof(wchar_t));name[length]=L'\0';
        *attributes=static_cast<DWORD>(value);*found=TRUE;++listing->index;
    }
    WindowsDeleteString(text);return hr;
}
}
