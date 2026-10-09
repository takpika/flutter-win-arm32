#include "flutter/shell/platform/windows/uwp/diagnostics.h"
#include "flutter/shell/platform/windows/uwp/runtime_support.h"

namespace flutter::winrt {
const wchar_t* DiagnosticPath() {
    static wchar_t path[1024]{};
    if(!path[0]) {
    using namespace ABI::Windows::Storage;
    HSTRING name=nullptr; const wchar_t* cls=L"Windows.Storage.ApplicationData";
    if(FAILED(WindowsCreateString(cls,31,&name))) return nullptr;
    ComPtr<IApplicationDataStatics> statics;
    HRESULT result=RoGetActivationFactory(name,IID_PPV_ARGS(&statics)); WindowsDeleteString(name);
    if(FAILED(result)) return nullptr;
    ComPtr<IApplicationData> data; ComPtr<IStorageFolder> folder; ComPtr<IStorageItem> item;
    if(FAILED(statics->get_Current(&data)) || FAILED(data->get_LocalFolder(&folder)) || FAILED(folder.As(&item))) return nullptr;
    HSTRING folderPath=nullptr; if(FAILED(item->get_Path(&folderPath))) return nullptr;
    wcscpy_s(path,WindowsGetStringRawBuffer(folderPath,nullptr)); WindowsDeleteString(folderPath);
    wcscat_s(path,L"\\flutter-engine-diagnostic.txt");
    }
    return path;
}
void Diagnostic(const char* expression,HRESULT hr) {
    const wchar_t* path=DiagnosticPath();if(!path)return;
    HANDLE file=CreateFile2(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,OPEN_ALWAYS,nullptr);
    if(file==INVALID_HANDLE_VALUE) return;
    char line[4096]; int count=snprintf(line,sizeof(line),"%s: %08lx\r\n",expression,(unsigned long)hr);
    if(count<0) {CloseHandle(file);return;}
    if(count>=static_cast<int>(sizeof(line)))count=sizeof(line)-1;
    DWORD written; WriteFile(file,line,count,&written,nullptr); CloseHandle(file);
}
}
