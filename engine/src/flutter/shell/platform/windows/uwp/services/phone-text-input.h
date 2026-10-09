#pragma once
#include "phone-text-abi.h"
#include <rapidjson/document.h>
#include <algorithm>

// CoreText keeps offsets in UTF-16, matching Flutter's TextEditingValue.
class PhoneTextInput {
    ComPtr<PhoneCoreTextEditContext> edit;
    ComPtr<ABI::Windows::UI::ViewManagement::IInputPane> pane;
    ComPtr<ABI::Windows::UI::ViewManagement::IInputPane2> paneControl;
    EventRegistrationToken tokens[9]{};
    unsigned registered=0;
    std::wstring text;
    INT32 base=0,extent=0,compositionBase=-1,compositionExtent=-1;
    INT64 client=0;
    bool focused=false;
    Rect control{},caret{};
    double transform[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    using Send=void (*)(void*,const char*,const std::string&);
    Send send=nullptr;
    void* user=nullptr;
    PhoneCoreTextRange Selection() const {return {std::min(base,extent),std::max(base,extent)};}
    static HRESULT Factory(const wchar_t* name,REFIID iid,void** result) {
        HSTRING cls=nullptr;HRESULT hr=WindowsCreateString(name,static_cast<UINT32>(wcslen(name)),&cls);
        if(SUCCEEDED(hr))hr=RoGetActivationFactory(cls,iid,result);
        WindowsDeleteString(cls);return hr;
    }
    static std::wstring Wide(const char* data,size_t length) {
        int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data,static_cast<int>(length),nullptr,0);
        std::wstring value(count,L'\0');
        if(count)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data,static_cast<int>(length),value.data(),count);
        return value;
    }
    void Changed() {
        if(!client || !send)return;
        HSTRING value=nullptr;WindowsCreateString(text.data(),static_cast<UINT32>(text.size()),&value);
        auto quoted=PhoneJsonQuote(PhoneUtf8(value));WindowsDeleteString(value);
        std::string json="{\"method\":\"TextInputClient.updateEditingState\",\"args\":["+std::to_string(client)+
            ",{\"text\":"+quoted+",\"selectionBase\":"+std::to_string(base)+",\"selectionExtent\":"+std::to_string(extent)+
            ",\"selectionAffinity\":\"TextAffinity.downstream\",\"selectionIsDirectional\":false,\"composingBase\":"+
            std::to_string(compositionBase)+",\"composingExtent\":"+std::to_string(compositionExtent)+"}]}";
        send(user,"flutter/textinput",json);
    }
    Rect MapRect(Rect value) const {
        // Flutter supplies the editable-to-window matrix in logical pixels.
        double x=value.X,y=value.Y,width=value.Width,height=value.Height;
        double w=transform[3]*x+transform[7]*y+transform[15];if(w==0)w=1;
        value.X=static_cast<FLOAT>((transform[0]*x+transform[4]*y+transform[12])/w);
        value.Y=static_cast<FLOAT>((transform[1]*x+transform[5]*y+transform[13])/w);
        value.Width=static_cast<FLOAT>(std::abs(transform[0])*width+std::abs(transform[4])*height);
        value.Height=static_cast<FLOAT>(std::abs(transform[1])*width+std::abs(transform[5])*height);
        return value;
    }
public:
    HRESULT Initialize(Send callback,void* data) {
        send=callback;user=data;
        ComPtr<PhoneCoreTextServicesManagerStatics> factory;
        CHECK(Factory(L"Windows.UI.Text.Core.CoreTextServicesManager",__uuidof(PhoneCoreTextServicesManagerStatics),reinterpret_cast<void**>(factory.GetAddressOf())));
        ComPtr<PhoneCoreTextServicesManager> manager;CHECK(factory->GetForCurrentView(&manager));CHECK(manager->CreateEditContext(&edit));
        CHECK(edit->put_InputPaneDisplayPolicy(1)); // Manual; Flutter controls show/hide.
        ComPtr<ABI::Windows::UI::ViewManagement::IInputPaneStatics> paneFactory;
        CHECK(Factory(L"Windows.UI.ViewManagement.InputPane",__uuidof(ABI::Windows::UI::ViewManagement::IInputPaneStatics),reinterpret_cast<void**>(paneFactory.GetAddressOf())));
        CHECK(paneFactory->GetForCurrentView(&pane));CHECK(pane.As(&paneControl));
        auto requested=callbackText<PhoneCoreTextTextRequestedEventArgsHandler,PhoneCoreTextTextRequestedEventArgs>(
            [this](PhoneCoreTextEditContext*,PhoneCoreTextTextRequestedEventArgs* args) -> HRESULT {
                ComPtr<PhoneCoreTextTextRequest> request;CHECK(args->get_Request(&request));PhoneCoreTextRange range{};CHECK(request->get_Range(&range));
                INT32 start=std::clamp(range.StartCaretPosition,0,static_cast<INT32>(text.size()));
                INT32 end=std::clamp(range.EndCaretPosition,start,static_cast<INT32>(text.size()));
                HSTRING value=nullptr;CHECK(WindowsCreateString(text.data()+start,end-start,&value));
                HRESULT hr=request->put_Text(value);WindowsDeleteString(value);return hr;
            });
        CHECK(edit->add_TextRequested(requested.Get(),&tokens[registered]));++registered;
        auto selection=callbackText<PhoneCoreTextSelectionRequestedEventArgsHandler,PhoneCoreTextSelectionRequestedEventArgs>(
            [this](PhoneCoreTextEditContext*,PhoneCoreTextSelectionRequestedEventArgs* args) -> HRESULT {
                ComPtr<PhoneCoreTextSelectionRequest> request;CHECK(args->get_Request(&request));return request->put_Selection(Selection());
            });
        CHECK(edit->add_SelectionRequested(selection.Get(),&tokens[registered]));++registered;
        auto layout=callbackText<PhoneCoreTextLayoutRequestedEventArgsHandler,PhoneCoreTextLayoutRequestedEventArgs>(
            [this](PhoneCoreTextEditContext*,PhoneCoreTextLayoutRequestedEventArgs* args) -> HRESULT {
                ComPtr<PhoneCoreTextLayoutRequest> request;ComPtr<PhoneCoreTextLayoutBounds> bounds;
                CHECK(args->get_Request(&request));CHECK(request->get_LayoutBounds(&bounds));
                CHECK(bounds->put_ControlBounds(MapRect(control)));return bounds->put_TextBounds(MapRect(caret));
            });
        CHECK(edit->add_LayoutRequested(layout.Get(),&tokens[registered]));++registered;
        auto updating=callbackText<PhoneCoreTextTextUpdatingEventArgsHandler,PhoneCoreTextTextUpdatingEventArgs>(
            [this](PhoneCoreTextEditContext*,PhoneCoreTextTextUpdatingEventArgs* args) -> HRESULT {
                PhoneCoreTextRange range{},next{};HSTRING value=nullptr;
                CHECK(args->get_Range(&range));CHECK(args->get_NewSelection(&next));CHECK(args->get_Text(&value));
                UINT32 length=0;const wchar_t* raw=WindowsGetStringRawBuffer(value,&length);
                if(range.StartCaretPosition<0 || range.EndCaretPosition<range.StartCaretPosition || range.EndCaretPosition>static_cast<INT32>(text.size())) {
                    WindowsDeleteString(value);return args->put_Result(1);
                }
                text.replace(range.StartCaretPosition,range.EndCaretPosition-range.StartCaretPosition,raw,length);WindowsDeleteString(value);
                base=next.StartCaretPosition;extent=next.EndCaretPosition;
                char details[180]{};snprintf(details,sizeof(details),"Phone IME edit range=(%ld,%ld) length=%u selection=(%ld,%ld)",static_cast<long>(range.StartCaretPosition),static_cast<long>(range.EndCaretPosition),length,static_cast<long>(base),static_cast<long>(extent));Diagnostic(details,S_OK);
                if(compositionBase>=0)compositionExtent=range.StartCaretPosition+static_cast<INT32>(length);
                Changed();Diagnostic("Phone CoreText editing update",S_OK);return args->put_Result(0);
            });
        CHECK(edit->add_TextUpdating(updating.Get(),&tokens[registered]));++registered;
        auto selectionUpdate=callbackText<PhoneCoreTextSelectionUpdatingEventArgsHandler,PhoneCoreTextSelectionUpdatingEventArgs>(
            [this](PhoneCoreTextEditContext*,PhoneCoreTextSelectionUpdatingEventArgs* args) -> HRESULT {
                PhoneCoreTextRange next{};CHECK(args->get_Selection(&next));base=next.StartCaretPosition;extent=next.EndCaretPosition;
                Changed();return args->put_Result(0);
            });
        CHECK(edit->add_SelectionUpdating(selectionUpdate.Get(),&tokens[registered]));++registered;
        auto compositionStart=callbackText<PhoneCoreTextCompositionStartedEventArgsHandler,IInspectable>(
            [this](PhoneCoreTextEditContext*,IInspectable*) -> HRESULT {compositionBase=Selection().StartCaretPosition;compositionExtent=Selection().EndCaretPosition;Diagnostic("Phone IME composition started",S_OK);return S_OK;});
        CHECK(edit->add_CompositionStarted(compositionStart.Get(),&tokens[registered]));++registered;
        auto compositionEnd=callbackText<PhoneCoreTextCompositionCompletedEventArgsHandler,IInspectable>(
            [this](PhoneCoreTextEditContext*,IInspectable*) -> HRESULT {compositionBase=compositionExtent=-1;Changed();Diagnostic("Phone IME composition completed",S_OK);return S_OK;});
        CHECK(edit->add_CompositionCompleted(compositionEnd.Get(),&tokens[registered]));++registered;
        auto removed=callbackText<PhoneCoreTextFocusRemovedHandler,IInspectable>(
            [this](PhoneCoreTextEditContext*,IInspectable*) -> HRESULT {focused=false;return S_OK;});
        CHECK(edit->add_FocusRemoved(removed.Get(),&tokens[registered]));++registered;
        auto format=callbackText<PhoneCoreTextFormatUpdatingEventArgsHandler,IInspectable>(
            [](PhoneCoreTextEditContext*,IInspectable*) -> HRESULT {return S_OK;});
        CHECK(edit->add_FormatUpdating(format.Get(),&tokens[registered]));++registered;
        Diagnostic("Phone CoreText initialized",S_OK);return S_OK;
    }
    template<class I,class A,class F> static ComPtr<I> callbackText(F fn) {
        return callback<I,PhoneCoreTextEditContext,A>(fn);
    }
    HRESULT Handle(const rapidjson::Value& message) {
        if(!message.IsObject() || !message.HasMember("method") || !message["method"].IsString() || !message.HasMember("args"))return E_INVALIDARG;
        const char* method=message["method"].GetString();const auto& args=message["args"];
        if(strcmp(method,"TextInput.setClient")==0) {
            if(!args.IsArray() || args.Size()!=2 || !args[0].IsInt64() || !args[1].IsObject())return E_INVALIDARG;
            if(focused) {CHECK(edit->NotifyFocusLeave());focused=false;}
            client=args[0].GetInt64();text.clear();base=extent=0;compositionBase=compositionExtent=-1;
            INT32 scope=57; // CoreTextInputScope.Text, as in Microsoft's custom-edit sample.
            if(args[1].HasMember("obscureText") && args[1]["obscureText"].IsBool() && args[1]["obscureText"].GetBool())scope=31;
            else if(args[1].HasMember("inputType") && args[1]["inputType"].IsObject()) {
                const auto& type=args[1]["inputType"];
                if(type.HasMember("name") && type["name"].IsString() && strcmp(type["name"].GetString(),"TextInputType.number")==0)scope=29;
            }
            char label[80]{};snprintf(label,sizeof(label),"Phone CoreText input scope %ld",static_cast<long>(scope));
            HRESULT hr=edit->put_InputScope(scope);Diagnostic(label,hr);return hr;
        }
        if(strcmp(method,"TextInput.setEditingState")==0) {
            if(!args.IsObject() || !args.HasMember("text") || !args["text"].IsString() || !args.HasMember("selectionBase") || !args["selectionBase"].IsInt() || !args.HasMember("selectionExtent") || !args["selectionExtent"].IsInt())return E_INVALIDARG;
            auto next=Wide(args["text"].GetString(),args["text"].GetStringLength());
            INT32 oldLength=static_cast<INT32>(text.size());bool changed=text!=next;
            INT32 oldBase=base,oldExtent=extent;text=std::move(next);
            base=std::clamp(args["selectionBase"].GetInt(),0,static_cast<int>(text.size()));extent=std::clamp(args["selectionExtent"].GetInt(),0,static_cast<int>(text.size()));
            compositionBase=args.HasMember("composingBase") && args["composingBase"].IsInt()?args["composingBase"].GetInt():-1;
            compositionExtent=args.HasMember("composingExtent") && args["composingExtent"].IsInt()?args["composingExtent"].GetInt():-1;
            // An echo of an OS edit must not notify TSF again: doing so commits
            // the IME's current composition between successive key presses.
            if(changed)return edit->NotifyTextChanged({0,oldLength},static_cast<INT32>(text.size()),Selection());
            if(base!=oldBase || extent!=oldExtent)return edit->NotifySelectionChanged(Selection());
            return S_OK;
        }
        if(strcmp(method,"TextInput.show")==0) {
            if(!client)return E_UNEXPECTED;
            if(!focused) {CHECK(edit->NotifyFocusEnter());focused=true;}
            boolean shown=false;HRESULT hr=paneControl->TryShow(&shown);Diagnostic(shown?"Phone keyboard shown":"Phone keyboard show not accepted",hr);return hr;
        }
        if(strcmp(method,"TextInput.hide")==0 || strcmp(method,"TextInput.clearClient")==0) {
            boolean hidden=false;CHECK(paneControl->TryHide(&hidden));
            if(strcmp(method,"TextInput.clearClient")==0) {if(focused)CHECK(edit->NotifyFocusLeave());focused=false;client=0;}
            return S_OK;
        }
        if(strcmp(method,"TextInput.setEditableSizeAndTransform")==0) {
            if(!args.IsObject() || !args.HasMember("width") || !args["width"].IsNumber() || !args.HasMember("height") || !args["height"].IsNumber() || !args.HasMember("transform") || !args["transform"].IsArray() || args["transform"].Size()!=16)return E_INVALIDARG;
            control={0,0,static_cast<FLOAT>(args["width"].GetDouble()),static_cast<FLOAT>(args["height"].GetDouble())};
            for(unsigned i=0;i<16;++i) {if(!args["transform"][i].IsNumber())return E_INVALIDARG;transform[i]=args["transform"][i].GetDouble();}
            return edit->NotifyLayoutChanged();
        }
        if(strcmp(method,"TextInput.setCaretRect")==0) {
            if(!args.IsObject())return E_INVALIDARG;
            for(const char* key:{"x","y","width","height"})if(!args.HasMember(key) || !args[key].IsNumber())return E_INVALIDARG;
            caret={static_cast<FLOAT>(args["x"].GetDouble()),static_cast<FLOAT>(args["y"].GetDouble()),static_cast<FLOAT>(args["width"].GetDouble()),static_cast<FLOAT>(args["height"].GetDouble())};return edit->NotifyLayoutChanged();
        }
        // Presentation details are rendered by Flutter, rather than the OS.
        if(strcmp(method,"TextInput.setStyle")==0 || strcmp(method,"TextInput.setMarkedTextRect")==0 || strcmp(method,"TextInput.setSelectionRects")==0 || strcmp(method,"TextInput.finishAutofillContext")==0)return S_OK;
        return E_NOTIMPL;
    }
    double BottomInset(Rect bounds) const {
        Rect occluded{};if(!pane || FAILED(pane->get_OccludedRect(&occluded)) || occluded.Height<=0 || occluded.Y<=0)return 0;
        // InputPane.OccludedRect is in client DIP, not the system-window
        // coordinate space containing CoreWindow.Bounds.X/Y.
        return std::clamp(double(bounds.Height-occluded.Y),0.0,double(bounds.Height));
    }
    void Close() {
        if(!edit)return;
        if(focused)edit->NotifyFocusLeave();focused=false;
        if(registered>0)edit->remove_TextRequested(tokens[0]);
        if(registered>1)edit->remove_SelectionRequested(tokens[1]);
        if(registered>2)edit->remove_LayoutRequested(tokens[2]);
        if(registered>3)edit->remove_TextUpdating(tokens[3]);
        if(registered>4)edit->remove_SelectionUpdating(tokens[4]);
        if(registered>5)edit->remove_CompositionStarted(tokens[5]);
        if(registered>6)edit->remove_CompositionCompleted(tokens[6]);
        if(registered>7)edit->remove_FocusRemoved(tokens[7]);
        if(registered>8)edit->remove_FormatUpdating(tokens[8]);
        registered=0;edit.Reset();paneControl.Reset();pane.Reset();
    }
};
