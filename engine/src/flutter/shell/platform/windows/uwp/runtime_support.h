#pragma once
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.applicationmodel.core.h>
#include <windows.ui.core.h>
#include <windows.storage.h>
#include <wrl/client.h>
#include <type_traits>
#include <utility>
#include <cstdio>
#include <memory>
#include <string>
#include "flutter/shell/platform/embedder/embedder.h"

namespace flutter::winrt {
using Microsoft::WRL::ComPtr;
using ::AsyncStatus;
template<class I, bool Inspectable = std::is_base_of<IInspectable, I>::value>
class RefInterface : public I {};
template<class I> class RefInterface<I, true> : public I {
public:
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count,IID** ids) override {
        if(!count || !ids) return E_POINTER;
        *count=1; *ids=(IID*)CoTaskMemAlloc(sizeof(IID));
        if(!*ids) return E_OUTOFMEMORY; **ids=__uuidof(I); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override { return WindowsCreateString(L"",0,name); }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* level) override { if(!level) return E_POINTER; *level=BaseTrust; return S_OK; }
};
template<class I> class Ref : public RefInterface<I> {
    volatile LONG refs=1;
public:
    virtual ~Ref()=default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out) return E_POINTER;
        *out=nullptr;
        if(id==IID_IUnknown || id==__uuidof(I)) *out=static_cast<I*>(this);
        if constexpr(std::is_base_of<IInspectable,I>::value) {
            if(id==__uuidof(IInspectable)) *out=static_cast<I*>(this);
        }
        if(!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=InterlockedDecrement(&refs); if(!n) delete this; return n; }

};
template<class I,class S,class A,class F> class Handler : public Ref<I> {
    F fn;
public:
    explicit Handler(F f):fn(f){}
    HRESULT STDMETHODCALLTYPE Invoke(S* sender,A* args) override { return fn(sender,args); }
};
template<class I,class S,class A,class F> ComPtr<I> callback(F f) {
    ComPtr<I> ptr; ptr.Attach(new Handler<I,S,A,F>(f)); return ptr;
}

// Shared WinRT activation, string encoding and asynchronous reply primitives.
static HRESULT PhoneActivate(const wchar_t* name,IInspectable** value) {
    HSTRING text=nullptr;HRESULT hr=WindowsCreateString(name,static_cast<UINT32>(wcslen(name)),&text);
    if(SUCCEEDED(hr))hr=RoActivateInstance(text,value);
    WindowsDeleteString(text);return hr;
}
static std::string PhoneUtf8(HSTRING text) {
    UINT32 count=0;const wchar_t* raw=WindowsGetStringRawBuffer(text,&count);
    int bytes=WideCharToMultiByte(CP_UTF8,0,raw,count,nullptr,0,nullptr,nullptr);
    std::string result(bytes,'\0');
    if(bytes)WideCharToMultiByte(CP_UTF8,0,raw,count,result.data(),bytes,nullptr,nullptr);
    return result;
}
static std::string PhoneJsonQuote(const std::string& text) {
    std::string result="\"";
    for(unsigned char character:text) {
        if(character=='\\' || character=='"') {result+='\\';result+=character;}
        else if(character<32) {char escaped[8];snprintf(escaped,sizeof(escaped),"\\u%04x",unsigned(character));result+=escaped;}
        else result+=character;
    }
    return result+'"';
}
using PhoneReply=void (*)(void*,const FlutterPlatformMessageResponseHandle*,const char*,size_t);
template<class I,class O,class F> class PhoneAsyncHandler : public Ref<I> {
    F fn;
public:
    explicit PhoneAsyncHandler(F value):fn(std::move(value)){}
    HRESULT STDMETHODCALLTYPE Invoke(O* operation,AsyncStatus status) override {return fn(operation,status);}
};
template<class I,class O,class F> static ComPtr<I> PhoneCompleted(F fn) {
    ComPtr<I> handler;handler.Attach(new PhoneAsyncHandler<I,O,F>(std::move(fn)));return handler;
}

}  // namespace flutter::winrt
