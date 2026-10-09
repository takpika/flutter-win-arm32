#pragma once

// Establish an AppContainer's conventional Windows paths from the OS, before
// starting Dart or native plugins. This is process-local, not a registry edit.
static HRESULT FlutterWinRTSetApplicationEnvironment() {
    using namespace ABI::Windows::Storage;
    HSTRING name=nullptr;
    const wchar_t* cls=L"Windows.Storage.ApplicationData";
    HRESULT hr=WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&name);
    Microsoft::WRL::ComPtr<IApplicationDataStatics> factory;
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(name,IID_PPV_ARGS(&factory));
    WindowsDeleteString(name);
    if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<IApplicationData> data;
    hr=factory->get_Current(&data);
    if(FAILED(hr))return hr;
    Microsoft::WRL::ComPtr<IStorageFolder> local,roaming,temporary;
    hr=data->get_LocalFolder(&local);
    if(SUCCEEDED(hr))hr=data->get_RoamingFolder(&roaming);
    if(SUCCEEDED(hr))hr=data->get_TemporaryFolder(&temporary);
    if(FAILED(hr))return hr;
    auto set=[](const wchar_t* variable,IStorageFolder* folder)->HRESULT {
        Microsoft::WRL::ComPtr<IStorageItem> item;
        HRESULT result=folder->QueryInterface(IID_PPV_ARGS(&item));
        HSTRING path=nullptr;
        if(SUCCEEDED(result))result=item->get_Path(&path);
        if(SUCCEEDED(result)&&!SetEnvironmentVariableW(variable,WindowsGetStringRawBuffer(path,nullptr)))
            result=HRESULT_FROM_WIN32(GetLastError());
        WindowsDeleteString(path);
        return result;
    };
    hr=set(L"APPDATA",roaming.Get());
    if(SUCCEEDED(hr))hr=set(L"LOCALAPPDATA",local.Get());
    if(SUCCEEDED(hr))hr=set(L"TEMP",temporary.Get());
    if(SUCCEEDED(hr))hr=set(L"TMP",temporary.Get());
    return hr;
}
