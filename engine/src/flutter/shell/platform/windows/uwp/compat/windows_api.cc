#include "storage_access.h"
#include "storage_handle.h"
#include "directory_listing.h"
#include <windows.applicationmodel.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <cstdlib>

using Microsoft::WRL::ComPtr;
using namespace ABI::Windows::Storage;

namespace {
template<class F> HRESULT OnMta(F work) {
    HRESULT initialized=RoInitialize(RO_INIT_MULTITHREADED);
    if(SUCCEEDED(initialized)){HRESULT result=work();RoUninitialize();return result;}
    if(initialized!=RPC_E_CHANGED_MODE)return initialized;
    // A UWP UI apartment cannot be changed. Run storage work on our own MTA;
    // never reinterpret an initialization error as successful initialization.
    struct Context {F* work;HRESULT result;};Context context{&work,E_FAIL};
    HANDLE worker=CreateThread(nullptr,0,[](void* data)->DWORD {
        auto* c=static_cast<Context*>(data);HRESULT hr=RoInitialize(RO_INIT_MULTITHREADED);
        c->result=SUCCEEDED(hr)?(*c->work)():hr;if(SUCCEEDED(hr))RoUninitialize();return 0;
    },&context,0,nullptr);
    if(!worker)return HRESULT_FROM_WIN32(GetLastError());
    DWORD waited=WaitForSingleObject(worker,INFINITE);CloseHandle(worker);
    return waited==WAIT_OBJECT_0?context.result:HRESULT_FROM_WIN32(GetLastError());
}
HRESULT FolderPath(IStorageFolder* folder,wchar_t** output) {
    ComPtr<IStorageItem> item;HRESULT hr=folder->QueryInterface(IID_PPV_ARGS(&item));
    HSTRING path=nullptr;if(SUCCEEDED(hr))hr=item->get_Path(&path);
    if(SUCCEEDED(hr)) {
        UINT32 size=0;const wchar_t* value=WindowsGetStringRawBuffer(path,&size);
        *output=static_cast<wchar_t*>(CoTaskMemAlloc((size+1)*sizeof(wchar_t)));
        if(!*output)hr=E_OUTOFMEMORY;
        else memcpy(*output,value,(size+1)*sizeof(wchar_t));
    }
    WindowsDeleteString(path);return hr;
}
HRESULT PackageDisplayName(HSTRING* output) {
    using namespace ABI::Windows::ApplicationModel;
    HSTRING name=nullptr;const wchar_t* cls=L"Windows.ApplicationModel.Package";
    HRESULT hr=WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&name);
    ComPtr<IPackageStatics> factory;
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(name,IID_PPV_ARGS(&factory));
    WindowsDeleteString(name);ComPtr<IPackage> package;ComPtr<IPackage2> details;
    if(SUCCEEDED(hr))hr=factory->get_Current(&package);
    if(SUCCEEDED(hr))hr=package.As(&details);
    HSTRING display=nullptr;if(SUCCEEDED(hr))hr=details->get_DisplayName(&display);
    if(SUCCEEDED(hr)) {
        UINT32 size=0;const wchar_t* text=WindowsGetStringRawBuffer(display,&size);
        std::wstring safe(text,size);
        for(auto& ch:safe)if(ch<32||wcschr(L"<>:\"/\\|?*",ch))ch=L'_';
        while(!safe.empty()&&(safe.back()==L'.'||safe.back()==L' '))safe.pop_back();
        hr=safe.empty()?HRESULT_FROM_WIN32(ERROR_INVALID_NAME):WindowsCreateString(safe.c_str(),static_cast<UINT32>(safe.size()),output);
    }
    WindowsDeleteString(display);return hr;
}
HRESULT Downloads(IStorageFolder** output) {
    HSTRING token=nullptr,name=nullptr;const wchar_t* key=L"flutter.sdk.downloads";
    HRESULT hr=WindowsCreateString(key,static_cast<UINT32>(wcslen(key)),&token);
    if(SUCCEEDED(hr))hr=PackageDisplayName(&name);
    if(SUCCEEDED(hr))hr=flutter::winrt::GetDownloadsDirectory(token,name,output);
    WindowsDeleteString(name);WindowsDeleteString(token);return hr;
}
HRESULT AppData(bool roaming,IStorageFolder** output) {
    HSTRING name=nullptr;const wchar_t* cls=L"Windows.Storage.ApplicationData";
    HRESULT hr=WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&name);
    ComPtr<IApplicationDataStatics> factory;
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(name,IID_PPV_ARGS(&factory));
    WindowsDeleteString(name);ComPtr<IApplicationData> data;
    if(SUCCEEDED(hr))hr=factory->get_Current(&data);
    return FAILED(hr)?hr:roaming?data->get_RoamingFolder(output):data->get_LocalFolder(output);
}
std::wstring NormalPath(LPCWSTR path) {
    std::wstring normalized(path);
    if(normalized.rfind(L"\\\\?\\UNC\\",0)==0)normalized=L"\\\\"+normalized.substr(8);
    else if(normalized.rfind(L"\\\\?\\",0)==0)normalized.erase(0,4);
    while(normalized.size()>3&&normalized.back()==L'\\')normalized.pop_back();
    return normalized;
}
HRESULT WaitForStorageOperation(IInspectable* operation) {
    if(!operation)return E_POINTER;
    ComPtr<IAsyncInfo> info;HRESULT hr=operation->QueryInterface(IID_PPV_ARGS(&info));
    if(FAILED(hr))return hr;
    AsyncStatus status{};
    do {hr=info->get_Status(&status);if(FAILED(hr))return hr;
        if(status!=Started)break;Sleep(8);
    }while(true);
    if(status==Completed)return S_OK;
    HRESULT error=S_OK;hr=info->get_ErrorCode(&error);
    return FAILED(hr)?hr:FAILED(error)?error:HRESULT_FROM_WIN32(ERROR_CANCELLED);
}
HRESULT DeleteStorageFilePath(LPCWSTR path) {
    auto normalized=NormalPath(path);auto slash=normalized.find_last_of(L'\\');
    if(slash==std::wstring::npos||slash==0||slash+1==normalized.size())
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    std::wstring parent=normalized.substr(0,slash),leaf=normalized.substr(slash+1);
    ComPtr<IStorageFolder> folder;ComPtr<IStorageFile> file;
    HRESULT hr=flutter::winrt::GetStorageFolderByPath(parent.c_str(),&folder);
    if(hr==E_ACCESSDENIED)hr=flutter::winrt::RestoreStorageFile(normalized.c_str(),&file);
    else if(SUCCEEDED(hr)) {
        HSTRING name=nullptr;hr=WindowsCreateString(leaf.c_str(),static_cast<UINT32>(leaf.size()),&name);
        ComPtr<ABI::Windows::Foundation::IAsyncOperation<StorageFile*>> operation;
        if(SUCCEEDED(hr))hr=folder->GetFileAsync(name,&operation);
        WindowsDeleteString(name);
        if(SUCCEEDED(hr))hr=WaitForStorageOperation(operation.Get());
        if(SUCCEEDED(hr))hr=operation->GetResults(&file);
    }
    if(FAILED(hr))return hr;
    // GetFileAsync requires a file. Use the OS item deletion operation rather
    // than deleting an opened handle, which can follow a reparse point.
    ComPtr<IStorageItem> item;hr=file.As(&item);
    FileAttributes attributes{};if(SUCCEEDED(hr))hr=item->get_Attributes(&attributes);
    if(SUCCEEDED(hr)&&(attributes&FileAttributes_ReadOnly))return E_ACCESSDENIED;
    ComPtr<ABI::Windows::Foundation::IAsyncAction> operation;
    if(SUCCEEDED(hr))hr=item->DeleteAsync(StorageDeleteOption_PermanentDelete,&operation);
    if(SUCCEEDED(hr))hr=WaitForStorageOperation(operation.Get());
    if(SUCCEEDED(hr))hr=operation->GetResults();
    return hr;
}
}

// The Phone runtime binds its existing CRT import to this SDK adapter. This
// DLL retains the real CRT import: ordinary paths and invalid-input handling
// still go through the original _wremove, without a recursive import slot.
extern "C" __declspec(dllexport) int __cdecl FlutterWinRTWremove(const wchar_t* path) {
    int result=_wremove(path);
    if(result==0||errno!=EACCES||!path)return result;
    const int savedErrno=errno;const unsigned long savedDosErrno=_doserrno;
    const DWORD savedError=GetLastError();
    HRESULT hr=OnMta([&]()->HRESULT {return DeleteStorageFilePath(path);});
    if(hr==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)||hr==HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
        errno=ENOENT;_doserrno=HRESULT_CODE(hr);SetLastError(HRESULT_CODE(hr));return -1;
    }
    errno=savedErrno;_doserrno=savedDosErrno;SetLastError(savedError);
    return SUCCEEDED(hr)?0:result;
}

// ABI consumed by the unchanged Windows path-provider FFI. The implementation
// belongs to the Phone SDK's OS adapter, not to application or VM logic.
extern "C" __declspec(dllexport) HRESULT WINAPI SHGetKnownFolderPath(
    const GUID& id,DWORD flags,HANDLE user,wchar_t** output) {
    if(!output)return E_POINTER;*output=nullptr;
    if(flags||user)return E_NOTIMPL;
    return OnMta([&]()->HRESULT {
    const GUID downloads={0x374de290,0x123f,0x4565,{0x91,0x64,0x39,0xc4,0x92,0x5e,0x46,0x7b}};
    const GUID roaming={0x3eb685db,0x65f9,0x4cf6,{0xa0,0x3a,0xe3,0xef,0x65,0x72,0x9f,0x3d}};
    const GUID local={0xf1b32785,0x6fba,0x4fcf,{0x9d,0x55,0x7b,0x8e,0x7f,0x15,0x70,0x91}};
    ComPtr<IStorageFolder> folder;HRESULT hr=E_NOTIMPL;
    if(IsEqualGUID(id,downloads))hr=Downloads(&folder);
    else if(IsEqualGUID(id,roaming))hr=AppData(true,&folder);
    else if(IsEqualGUID(id,local))hr=AppData(false,&folder);
    return SUCCEEDED(hr)?FolderPath(folder.Get(),output):hr;
    });
}

extern "C" __declspec(dllexport) HRESULT WINAPI FlutterWinRTRememberStorageFile(IStorageFile* file) {
    if(!file)return E_INVALIDARG;
    return OnMta([&]()->HRESULT {return flutter::winrt::RememberStorageFile(file);});
}
extern "C" __declspec(dllexport) HRESULT WINAPI FlutterWinRTRememberStorageFolder(IStorageFolder* folder) {
    if(!folder)return E_INVALIDARG;
    return OnMta([&]()->HRESULT {return flutter::winrt::RememberStorageFolder(folder);});
}
extern "C" __declspec(dllexport) HRESULT WINAPI FlutterWinRTDirectoryOpen(LPCWSTR path,void** cursor) {
    if(!cursor)return E_POINTER;*cursor=nullptr;if(!path)return E_INVALIDARG;
    return OnMta([&]()->HRESULT {
        auto normalized=NormalPath(path);
        while(normalized.size()>3&&normalized.back()==L'\\')normalized.pop_back();
        flutter::winrt::DirectoryListing* listing=nullptr;
        HRESULT hr=flutter::winrt::OpenDirectoryListing(normalized.c_str(),&listing);
        if(SUCCEEDED(hr))*cursor=listing;return hr;
    });
}
extern "C" __declspec(dllexport) HRESULT WINAPI FlutterWinRTDirectoryNext(void* cursor,
    wchar_t* name,UINT32 capacity,DWORD* attributes,BOOL* found) {
    return OnMta([&]()->HRESULT {return flutter::winrt::NextDirectoryEntry(
        static_cast<flutter::winrt::DirectoryListing*>(cursor),name,capacity,attributes,found);});
}
extern "C" __declspec(dllexport) HRESULT WINAPI FlutterWinRTDirectoryClose(void* cursor) {
    if(!cursor)return E_INVALIDARG;
    return OnMta([&]()->HRESULT {delete static_cast<flutter::winrt::DirectoryListing*>(cursor);return S_OK;});
}

// Return a real OS HANDLE; the caller retains its own CRT descriptor table.
extern "C" __declspec(dllexport) HRESULT WINAPI FlutterWinRTOpenFile(
    LPCWSTR path,DWORD creation,DWORD access,DWORD sharing,DWORD options,HANDLE* handle) {
    if(!handle)return E_POINTER;*handle=INVALID_HANDLE_VALUE;
    if(!path)return E_INVALIDARG;
    return OnMta([&]()->HRESULT {
    std::wstring normalized=NormalPath(path);
    return flutter::winrt::OpenStoragePathFile(normalized.c_str(),creation,access,sharing,options,handle);
    });
}

extern "C" __declspec(dllexport) HRESULT WINAPI FlutterWinRTDirectoryAttributes(LPCWSTR path,DWORD* attributes) {
    if(!attributes)return E_POINTER;*attributes=INVALID_FILE_ATTRIBUTES;if(!path)return E_INVALIDARG;
    return OnMta([&]()->HRESULT {
        auto normalized=NormalPath(path);ComPtr<IStorageFolder> folder;
        HRESULT hr=flutter::winrt::GetStorageFolderByPath(normalized.c_str(),&folder);
        ComPtr<IStorageItem> item;if(SUCCEEDED(hr))hr=folder.As(&item);
        FileAttributes value{};if(SUCCEEDED(hr))hr=item->get_Attributes(&value);
        if(SUCCEEDED(hr))*attributes=static_cast<DWORD>(value);
        return hr;
    });
}

extern "C" __declspec(dllexport) HRESULT WINAPI FlutterWinRTCreateDirectory(LPCWSTR path) {
    if(!path)return E_INVALIDARG;
    return OnMta([&]()->HRESULT {
        auto normalized=NormalPath(path);auto slash=normalized.find_last_of(L'\\');
        if(slash==std::wstring::npos||slash==0||slash+1==normalized.size())return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
        std::wstring parent=normalized.substr(0,slash),leaf=normalized.substr(slash+1);
        ComPtr<IStorageFolder> folder;HRESULT hr=flutter::winrt::GetStorageFolderByPath(parent.c_str(),&folder);
        HSTRING name=nullptr;if(SUCCEEDED(hr))hr=WindowsCreateString(leaf.c_str(),static_cast<UINT32>(leaf.size()),&name);
        ComPtr<ABI::Windows::Foundation::IAsyncOperation<StorageFolder*>> operation;
        if(SUCCEEDED(hr))hr=folder->CreateFolderAsync(name,CreationCollisionOption_FailIfExists,&operation);
        WindowsDeleteString(name);if(FAILED(hr))return hr;
        ComPtr<IAsyncInfo> info;hr=operation.As(&info);if(FAILED(hr))return hr;
        ULONGLONG deadline=GetTickCount64()+15000;AsyncStatus status{};
        do {hr=info->get_Status(&status);if(FAILED(hr))return hr;if(status!=Started)break;
            if(GetTickCount64()>=deadline){info->Cancel();return HRESULT_FROM_WIN32(ERROR_TIMEOUT);}Sleep(8);
        }while(true);
        if(status!=Completed){HRESULT error=S_OK;hr=info->get_ErrorCode(&error);return FAILED(hr)?hr:FAILED(error)?error:HRESULT_FROM_WIN32(ERROR_CANCELLED);}
        ComPtr<IStorageFolder> created;return operation->GetResults(&created);
    });
}

// Link-time resolver for the Phone runtime's imported loader slot. Real OS
// loading always runs first. Only a bare legacy facade name is redirected to
// this SDK-owned module; absolute/relative paths retain OS loader behavior.
extern "C" __declspec(dllexport) HMODULE WINAPI FlutterWinRTLoadPackagedLibrary(LPCWSTR name,DWORD reserved) {
    HMODULE module=LoadPackagedLibrary(name,reserved);
    if(module||reserved||!name)return module;
    // These OS libraries are not package dependencies. Acquire a balanced
    // reference to the exact bare system module, retaining its real exports
    // and permission checks rather than substituting SDK implementations.
    if(_wcsicmp(name,L"ntdll.dll")==0 || _wcsicmp(name,L"wlanapi.dll")==0)
        return LoadLibraryExW(name,nullptr,0);
    if(_wcsicmp(name,L"shell32.dll")!=0&&_wcsicmp(name,L"kernel32.dll")!=0&&
       _wcsicmp(name,L"user32.dll")!=0&&_wcsicmp(name,L"ole32.dll")!=0)return nullptr;
    return LoadPackagedLibrary(L"flutter_winrt_compat.dll",0);
}

// Legacy facade exports forward to the Phone's actual OS API contracts.
// Distinct C names keep the imported OS functions separate from our exports.
extern "C" BOOL WINAPI FlutterWinRTIsZoomed(HWND window) {
    // Mobile lacks IsZoomed. Query the real window's WS_MAXIMIZE style rather
    // than substituting a fixed state or confusing it with UWP fullscreen.
    using GetWindowLong=LONG_PTR(WINAPI*)(HWND,int);
    HMODULE facade=LoadPackagedLibrary(L"flutter_winrt_compat.dll",0);
    auto get=facade?reinterpret_cast<GetWindowLong>(GetProcAddress(facade,"GetWindowLongPtrW")):nullptr;
    LONG_PTR style=get?get(window,-16):0;
    DWORD error=GetLastError();
    if(facade)FreeLibrary(facade);
    SetLastError(error);
    return get&&(style&0x01000000L)!=0;
}

extern "C" DWORD WINAPI FlutterWinRTGetTempPathW(DWORD count,LPWSTR path) {
    return GetTempPathW(count,path);
}
extern "C" DWORD WINAPI FlutterWinRTGetLastError() { return GetLastError(); }
extern "C" DWORD WINAPI FlutterWinRTGetModuleFileNameW(HMODULE module,LPWSTR path,DWORD count) {
    return GetModuleFileNameW(module,path,count);
}
