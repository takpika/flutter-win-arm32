#pragma once
#include <vector>
#include <string>

// UniversalApiContract 1.0 ABI from the original RT SDK's
// windows.system.userprofile.h. The implementation remains in the Phone OS.
struct __declspec(uuid("01bf4326-ed37-4e96-b0e9-c1340d1ea158")) PhoneGlobalizationPreferences : IInspectable {
    using Languages=ABI::Windows::Foundation::Collections::IVectorView<HSTRING>;
    virtual HRESULT STDMETHODCALLTYPE get_Calendars(Languages**)=0;
    virtual HRESULT STDMETHODCALLTYPE get_Clocks(Languages**)=0;
    virtual HRESULT STDMETHODCALLTYPE get_Currencies(Languages**)=0;
    virtual HRESULT STDMETHODCALLTYPE get_Languages(Languages**)=0;
};
__CRT_UUID_DECL(PhoneGlobalizationPreferences,0x01bf4326,0xed37,0x4e96,0xb0,0xe9,0xc1,0x34,0x0d,0x1e,0xa1,0x58)

struct PhoneLocale {
    std::string language,region,script,variant;
    FlutterLocale locale{};
};

static HRESULT PhoneUpdateLocales(FlutterEngine engine,const FlutterEngineProcTable& api) {
    if(!engine || !api.UpdateLocales)return E_UNEXPECTED;
    HSTRING cls=nullptr;const wchar_t* name=L"Windows.System.UserProfile.GlobalizationPreferences";
    HRESULT hr=WindowsCreateString(name,(UINT32)wcslen(name),&cls);
    ComPtr<PhoneGlobalizationPreferences> preferences;
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(cls,IID_PPV_ARGS(&preferences));
    WindowsDeleteString(cls);
    ComPtr<PhoneGlobalizationPreferences::Languages> languages;
    if(SUCCEEDED(hr))hr=preferences->get_Languages(&languages);
    Diagnostic("Phone OS preferred languages",hr);if(FAILED(hr))return hr;
    UINT32 count=0;hr=languages->get_Size(&count);if(FAILED(hr))return hr;
    std::vector<PhoneLocale> records;records.reserve(count);
    for(UINT32 i=0;i<count;i++) {
        HSTRING value=nullptr;hr=languages->GetAt(i,&value);if(FAILED(hr))return hr;
        UINT32 length=0;const wchar_t* wide=WindowsGetStringRawBuffer(value,&length);
        int bytes=WideCharToMultiByte(CP_UTF8,0,wide,length,nullptr,0,nullptr,nullptr);
        std::string tag(bytes,'\0');
        if(bytes)WideCharToMultiByte(CP_UTF8,0,wide,length,&tag[0],bytes,nullptr,nullptr);
        WindowsDeleteString(value);
        if(tag.empty())continue;
        Diagnostic((std::string("Phone OS language ")+tag).c_str(),S_OK);
        PhoneLocale record;size_t position=0;
        while(position<tag.size()) {
            size_t end=tag.find('-',position);if(end==std::string::npos)end=tag.size();
            std::string part=tag.substr(position,end-position);
            if(position==0)record.language=part;
            else if(part.size()==1)break; // BCP-47 extensions are not locale fields.
            else if(record.script.empty()&&part.size()==4&&isalpha((unsigned char)part[0]))record.script=part;
            else if(record.region.empty()&&(part.size()==2||(part.size()==3&&isdigit((unsigned char)part[0]))))record.region=part;
            else {if(!record.variant.empty())record.variant+='-';record.variant+=part;}
            position=end+1;
        }
        if(!record.language.empty())records.push_back(std::move(record));
    }
    if(records.empty())return E_FAIL;
    std::vector<const FlutterLocale*> locales;locales.reserve(records.size());
    for(auto& record:records) {
        record.locale.struct_size=sizeof(FlutterLocale);
        record.locale.language_code=record.language.c_str();
        record.locale.country_code=record.region.empty()?nullptr:record.region.c_str();
        record.locale.script_code=record.script.empty()?nullptr:record.script.c_str();
        record.locale.variant_code=record.variant.empty()?nullptr:record.variant.c_str();
        locales.push_back(&record.locale);
    }
    auto result=api.UpdateLocales(engine,locales.data(),locales.size());
    Diagnostic("Phone Flutter OS locales updated",static_cast<HRESULT>(result));
    return result==kSuccess?S_OK:E_FAIL;
}
