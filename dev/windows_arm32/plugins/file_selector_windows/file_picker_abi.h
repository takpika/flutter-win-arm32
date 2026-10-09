#pragma once
// Windows.Storage.Pickers ABI declarations previously checked against the Microsoft SDK.
struct PhoneStringVector : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32,HSTRING*)=0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetView(IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(HSTRING,UINT32*,boolean*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetAt(UINT32,HSTRING)=0;
    virtual HRESULT STDMETHODCALLTYPE InsertAt(UINT32,HSTRING)=0;
    virtual HRESULT STDMETHODCALLTYPE RemoveAt(UINT32)=0;
    virtual HRESULT STDMETHODCALLTYPE Append(HSTRING)=0;
};
using PhoneFiles=ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::Storage::StorageFile*>;
using PhonePickOperation=ABI::Windows::Foundation::IAsyncOperation<PhoneFiles*>;
using PhoneCopyOperation=ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFile*>;
struct __declspec(uuid("2ca8278a-12c5-4c5f-8977-94547793c241")) PhoneFileOpenPicker : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_ViewMode(INT*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_ViewMode(INT)=0;
    virtual HRESULT STDMETHODCALLTYPE get_SettingsIdentifier(HSTRING*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_SettingsIdentifier(HSTRING)=0;
    virtual HRESULT STDMETHODCALLTYPE get_SuggestedStartLocation(INT*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_SuggestedStartLocation(INT)=0;
    virtual HRESULT STDMETHODCALLTYPE get_CommitButtonText(HSTRING*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_CommitButtonText(HSTRING)=0;
    virtual HRESULT STDMETHODCALLTYPE get_FileTypeFilter(PhoneStringVector**)=0;
    virtual HRESULT STDMETHODCALLTYPE PickSingleFileAsync(PhoneCopyOperation**)=0;
    virtual HRESULT STDMETHODCALLTYPE PickMultipleFilesAsync(PhonePickOperation**)=0;
};
__CRT_UUID_DECL(PhoneFileOpenPicker,0x2ca8278a,0x12c5,0x4c5f,0x89,0x77,0x94,0x54,0x77,0x93,0xc2,0x41)

using PhoneFolderOperation=ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFolder*>;
struct __declspec(uuid("084f7799-f3fb-400a-99b1-7b4a772fd60d")) PhoneFolderPicker : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_ViewMode(INT*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_ViewMode(INT)=0;
    virtual HRESULT STDMETHODCALLTYPE get_SettingsIdentifier(HSTRING*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_SettingsIdentifier(HSTRING)=0;
    virtual HRESULT STDMETHODCALLTYPE get_SuggestedStartLocation(INT*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_SuggestedStartLocation(INT)=0;
    virtual HRESULT STDMETHODCALLTYPE get_CommitButtonText(HSTRING*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_CommitButtonText(HSTRING)=0;
    virtual HRESULT STDMETHODCALLTYPE get_FileTypeFilter(PhoneStringVector**)=0;
    virtual HRESULT STDMETHODCALLTYPE PickSingleFolderAsync(PhoneFolderOperation**)=0;
};
__CRT_UUID_DECL(PhoneFolderPicker,0x084f7799,0xf3fb,0x400a,0x99,0xb1,0x7b,0x4a,0x77,0x2f,0xd6,0x0d)
