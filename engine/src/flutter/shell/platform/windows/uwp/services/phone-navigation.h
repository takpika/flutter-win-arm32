#pragma once
// UniversalApiContract 1.0 ABI, from the preserved Microsoft RT SDK.
struct __declspec(uuid("d603d28a-e411-4a4e-ba41-6a327a8675bc")) PhoneBackArgs : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Handled(boolean*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_Handled(boolean)=0;
};
__CRT_UUID_DECL(PhoneBackArgs,0xd603d28a,0xe411,0x4a4e,0xba,0x41,0x6a,0x32,0x7a,0x86,0x75,0xbc)
struct __declspec(uuid("ca821060-002b-526d-8122-982630d7cdbe")) PhoneBackHandler : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(IInspectable*,PhoneBackArgs*)=0;
};
__CRT_UUID_DECL(PhoneBackHandler,0xca821060,0x002b,0x526d,0x81,0x22,0x98,0x26,0x30,0xd7,0xcd,0xbe)
struct __declspec(uuid("93023118-cf50-42a6-9706-69107fa122e1")) PhoneNavigation : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE add_BackRequested(PhoneBackHandler*,EventRegistrationToken*)=0;
    virtual HRESULT STDMETHODCALLTYPE remove_BackRequested(EventRegistrationToken)=0;
};
__CRT_UUID_DECL(PhoneNavigation,0x93023118,0xcf50,0x42a6,0x97,0x06,0x69,0x10,0x7f,0xa1,0x22,0xe1)
struct __declspec(uuid("dc52b5ce-bee0-4305-8c54-68228ed683b5")) PhoneNavigationStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetForCurrentView(PhoneNavigation**)=0;
};
__CRT_UUID_DECL(PhoneNavigationStatics,0xdc52b5ce,0xbee0,0x4305,0x8c,0x54,0x68,0x22,0x8e,0xd6,0x83,0xb5)
// DisplayInformation's static ABI prefix; orientation preferences are per app.
struct __declspec(uuid("c6a02a6c-d452-44dc-ba07-96f3c6adf9d1")) PhoneRotationPreferences : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetForCurrentView(IInspectable**)=0;
    virtual HRESULT STDMETHODCALLTYPE get_AutoRotationPreferences(INT*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_AutoRotationPreferences(INT)=0;
};
__CRT_UUID_DECL(PhoneRotationPreferences,0xc6a02a6c,0xd452,0x44dc,0xba,0x07,0x96,0xf3,0xc6,0xad,0xf9,0xd1)
