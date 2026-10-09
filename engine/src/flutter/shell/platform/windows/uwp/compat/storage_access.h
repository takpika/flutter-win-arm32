#pragma once
#include "downloads_folder.h"
#include <bcrypt.h>
#include <vector>
#include <string>

namespace flutter::winrt {
// ABI prefixes from the retained Windows SDK AccessCache metadata. The list
// belongs to this package; we never clear it or alter another component's token.
struct StorageAccessList : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Add(IInspectable*,HSTRING*)=0;
    virtual HRESULT STDMETHODCALLTYPE AddWithMetadata(IInspectable*,HSTRING,HSTRING*)=0;
    virtual HRESULT STDMETHODCALLTYPE AddOrReplace(HSTRING,IInspectable*)=0;
    virtual HRESULT STDMETHODCALLTYPE AddOrReplaceWithMetadata(HSTRING,IInspectable*,HSTRING)=0;
    virtual HRESULT STDMETHODCALLTYPE GetItemAsync(HSTRING,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE GetFileAsync(HSTRING,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE GetFolderAsync(HSTRING,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE GetItemWithOptionsAsync(HSTRING,UINT32,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE GetFileWithOptionsAsync(HSTRING,UINT32,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE GetFolderWithOptionsAsync(HSTRING,UINT32,IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE Remove(HSTRING)=0;
    virtual HRESULT STDMETHODCALLTYPE ContainsItem(HSTRING,boolean*)=0;
};
struct StoragePermissionsStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_FutureAccessList(StorageAccessList**)=0;
    virtual HRESULT STDMETHODCALLTYPE get_MostRecentlyUsedList(IInspectable**)=0;
};

inline HRESULT GetFutureAccessList(StorageAccessList** list) {
    if(!list)return E_POINTER;*list=nullptr;
    HSTRING name=nullptr;const wchar_t* cls=L"Windows.Storage.AccessCache.StorageApplicationPermissions";
    HRESULT hr=WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&name);
    const GUID iid={0x4391dfaa,0xd033,0x48f9,{0x80,0x60,0x3e,0xc8,0x47,0xd2,0xe3,0xf1}};
    Microsoft::WRL::ComPtr<StoragePermissionsStatics> factory;
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(name,iid,reinterpret_cast<void**>(factory.GetAddressOf()));
    WindowsDeleteString(name);
    return SUCCEEDED(hr)?factory->get_FutureAccessList(list):hr;
}

inline HRESULT RestoreStorageFolder(StorageAccessList* list,HSTRING token,
    ABI::Windows::Storage::IStorageFolder** folder) {
    if(!folder)return E_POINTER;*folder=nullptr;if(!list)return E_INVALIDARG;
    Microsoft::WRL::ComPtr<IInspectable> pending;
    HRESULT hr=list->GetFolderAsync(token,&pending);if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<IAsyncInfo> info;hr=pending.As(&info);if(FAILED(hr))return hr;
    ULONGLONG deadline=GetTickCount64()+15000;AsyncStatus status{};
    do {
        hr=info->get_Status(&status);if(FAILED(hr))return hr;
        if(status!=Started)break;
        if(GetTickCount64()>=deadline){info->Cancel();return HRESULT_FROM_WIN32(ERROR_TIMEOUT);}
        Sleep(8);
    }while(true);
    if(status!=Completed){HRESULT error=S_OK;hr=info->get_ErrorCode(&error);return FAILED(hr)?hr:FAILED(error)?error:HRESULT_FROM_WIN32(ERROR_CANCELLED);}
    Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFolder*>> operation;
    hr=pending.As(&operation);return SUCCEEDED(hr)?operation->GetResults(folder):hr;
}

// Stable, SDK-owned tokens retain exactly the items granted by an OS picker.
inline HRESULT StorageFileToken(LPCWSTR path,HSTRING* token,const wchar_t* prefix=L"flutter.sdk.file.") {
    if(!token)return E_POINTER;*token=nullptr;if(!path)return E_INVALIDARG;
    int length=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,path,-1,nullptr,0,nullptr,nullptr,0);
    if(!length)return HRESULT_FROM_WIN32(GetLastError());
    std::wstring canonical(length,L'\0');
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,path,-1,canonical.data(),length,nullptr,nullptr,0))return HRESULT_FROM_WIN32(GetLastError());
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    NTSTATUS status=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0);
    DWORD size=0,written=0;std::vector<UCHAR> object;UCHAR digest[32]{};
    if(status>=0)status=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&written,0);
    if(status>=0){object.resize(size);status=BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0);}
    if(status>=0)status=BCryptHashData(hash,reinterpret_cast<PUCHAR>(canonical.data()),(length-1)*sizeof(wchar_t),0);
    if(status>=0)status=BCryptFinishHash(hash,digest,sizeof(digest),0);
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)return HRESULT_FROM_NT(status);
    std::wstring value=prefix;const wchar_t* hex=L"0123456789abcdef";
    for(UCHAR byte:digest){value+=hex[byte>>4];value+=hex[byte&15];}
    return WindowsCreateString(value.c_str(),static_cast<UINT32>(value.size()),token);
}
inline HRESULT RememberStorageFile(ABI::Windows::Storage::IStorageFile* file) {
    if(!file)return E_INVALIDARG;
    Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageItem> item;
    HRESULT hr=file->QueryInterface(IID_PPV_ARGS(&item));HSTRING path=nullptr,token=nullptr;
    if(SUCCEEDED(hr))hr=item->get_Path(&path);
    if(SUCCEEDED(hr))hr=StorageFileToken(WindowsGetStringRawBuffer(path,nullptr),&token);
    Microsoft::WRL::ComPtr<StorageAccessList> list;
    if(SUCCEEDED(hr))hr=GetFutureAccessList(&list);
    if(SUCCEEDED(hr))hr=list->AddOrReplace(token,item.Get());
    WindowsDeleteString(path);WindowsDeleteString(token);return hr;
}
inline HRESULT RememberStorageFolder(ABI::Windows::Storage::IStorageFolder* folder) {
    if(!folder)return E_INVALIDARG;
    Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageItem> item;
    HRESULT hr=folder->QueryInterface(IID_PPV_ARGS(&item));HSTRING path=nullptr,token=nullptr;
    if(SUCCEEDED(hr))hr=item->get_Path(&path);
    if(SUCCEEDED(hr))hr=StorageFileToken(WindowsGetStringRawBuffer(path,nullptr),&token,L"flutter.sdk.folder.");
    Microsoft::WRL::ComPtr<StorageAccessList> list;
    if(SUCCEEDED(hr))hr=GetFutureAccessList(&list);
    if(SUCCEEDED(hr))hr=list->AddOrReplace(token,item.Get());
    WindowsDeleteString(path);WindowsDeleteString(token);return hr;
}
inline HRESULT RestoreStorageFolderPath(LPCWSTR path,ABI::Windows::Storage::IStorageFolder** folder) {
    if(!folder)return E_POINTER;*folder=nullptr;if(!path)return E_INVALIDARG;
    Microsoft::WRL::ComPtr<StorageAccessList> list;
    HRESULT hr=GetFutureAccessList(&list);if(FAILED(hr))return hr;
    std::wstring candidate=path;
    while(!candidate.empty()) {
        HSTRING token=nullptr;hr=StorageFileToken(candidate.c_str(),&token,L"flutter.sdk.folder.");
        boolean exists=false;if(SUCCEEDED(hr))hr=list->ContainsItem(token,&exists);
        Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageFolder> root;
        if(SUCCEEDED(hr)&&exists)hr=RestoreStorageFolder(list.Get(),token,&root);
        WindowsDeleteString(token);if(FAILED(hr))return hr;
        if(exists) {
            Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageItem> item;
            hr=root.As(&item);HSTRING actual=nullptr;
            if(SUCCEEDED(hr))hr=item->get_Path(&actual);
            if(SUCCEEDED(hr)&&CompareStringOrdinal(candidate.c_str(),-1,WindowsGetStringRawBuffer(actual,nullptr),-1,TRUE)!=CSTR_EQUAL)
                hr=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            WindowsDeleteString(actual);if(FAILED(hr))return hr;
            if(candidate.size()==wcslen(path))return root.CopyTo(folder);
            HSTRING relative=nullptr;
            hr=WindowsCreateString(path+candidate.size()+1,static_cast<UINT32>(wcslen(path)-candidate.size()-1),&relative);
            Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFolder*>> operation;
            if(SUCCEEDED(hr))hr=root->GetFolderAsync(relative,&operation);
            WindowsDeleteString(relative);if(FAILED(hr))return hr;
            Microsoft::WRL::ComPtr<IAsyncInfo> info;hr=operation.As(&info);if(FAILED(hr))return hr;
            ULONGLONG deadline=GetTickCount64()+15000;AsyncStatus status{};
            do {hr=info->get_Status(&status);if(FAILED(hr))return hr;if(status!=Started)break;
                if(GetTickCount64()>=deadline){info->Cancel();return HRESULT_FROM_WIN32(ERROR_TIMEOUT);}Sleep(8);
            }while(true);
            if(status!=Completed){HRESULT error=S_OK;hr=info->get_ErrorCode(&error);return FAILED(hr)?hr:FAILED(error)?error:HRESULT_FROM_WIN32(ERROR_CANCELLED);}
            return operation->GetResults(folder);
        }
        auto slash=candidate.find_last_of(L'\\');
        if(slash==std::wstring::npos)break;
        candidate.resize(slash);
    }
    return E_ACCESSDENIED;
}
inline HRESULT RestoreStorageFile(LPCWSTR path,ABI::Windows::Storage::IStorageFile** file) {
    if(!file)return E_POINTER;*file=nullptr;HSTRING token=nullptr;
    HRESULT hr=StorageFileToken(path,&token);Microsoft::WRL::ComPtr<StorageAccessList> list;
    if(SUCCEEDED(hr))hr=GetFutureAccessList(&list);
    boolean exists=false;if(SUCCEEDED(hr))hr=list->ContainsItem(token,&exists);
    if(SUCCEEDED(hr)&&!exists)hr=E_ACCESSDENIED;
    Microsoft::WRL::ComPtr<IInspectable> pending;
    if(SUCCEEDED(hr))hr=list->GetFileAsync(token,&pending);
    WindowsDeleteString(token);if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<IAsyncInfo> info;hr=pending.As(&info);if(FAILED(hr))return hr;
    ULONGLONG deadline=GetTickCount64()+15000;AsyncStatus status{};
    do {hr=info->get_Status(&status);if(FAILED(hr))return hr;if(status!=Started)break;
        if(GetTickCount64()>=deadline){info->Cancel();return HRESULT_FROM_WIN32(ERROR_TIMEOUT);}Sleep(8);
    }while(true);
    if(status!=Completed){HRESULT error=S_OK;hr=info->get_ErrorCode(&error);return FAILED(hr)?hr:FAILED(error)?error:HRESULT_FROM_WIN32(ERROR_CANCELLED);}
    Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFile*>> operation;
    hr=pending.As(&operation);Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageFile> restored;
    if(SUCCEEDED(hr))hr=operation->GetResults(&restored);
    Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageItem> item;
    if(SUCCEEDED(hr))hr=restored.As(&item);HSTRING actual=nullptr;
    if(SUCCEEDED(hr))hr=item->get_Path(&actual);
    if(SUCCEEDED(hr)&&CompareStringOrdinal(path,-1,WindowsGetStringRawBuffer(actual,nullptr),-1,TRUE)!=CSTR_EQUAL)hr=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    WindowsDeleteString(actual);return SUCCEEDED(hr)?restored.CopyTo(file):hr;
}

inline HRESULT GetDownloadsDirectory(HSTRING token,HSTRING desiredName,
    ABI::Windows::Storage::IStorageFolder** folder,bool* restored=nullptr) {
    if(!folder)return E_POINTER;*folder=nullptr;if(restored)*restored=false;
    Microsoft::WRL::ComPtr<StorageAccessList> list;
    HRESULT hr=GetFutureAccessList(&list);if(FAILED(hr))return hr;
    boolean exists=false;hr=list->ContainsItem(token,&exists);if(FAILED(hr))return hr;
    if(exists){hr=RestoreStorageFolder(list.Get(),token,folder);if(SUCCEEDED(hr)&&restored)*restored=true;return hr;}
    Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageFolder> created;
    hr=CreateDownloadsDirectory(desiredName,&created);if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageItem> item;
    hr=created.As(&item);if(SUCCEEDED(hr))hr=list->AddOrReplace(token,item.Get());
    return SUCCEEDED(hr)?created.CopyTo(folder):hr;
}
}
