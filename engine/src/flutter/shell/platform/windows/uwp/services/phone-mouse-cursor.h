#pragma once
#include <flutter/standard_method_codec.h>

namespace flutter::winrt {
// Flutter owns cursor selection; CoreWindow owns the native cursor display.
static bool PhoneHandleMouseCursor(const FlutterPlatformMessage* message,
                                  const PlatformMessageContext& context) {
    if(strcmp(message->channel,"flutter/mousecursor")!=0)return false;
    const auto& codec=flutter::StandardMethodCodec::GetInstance();
    auto call=codec.DecodeMethodCall(message->message,message->message_size);
    std::unique_ptr<std::vector<uint8_t>> reply;
    if(!call)reply=codec.EncodeErrorEnvelope("Argument error","Invalid cursor method call");
    else if(call->method_name()=="activateSystemCursor") {
        const auto* args=call->arguments()?std::get_if<flutter::EncodableMap>(call->arguments()):nullptr;
        const std::string* kind=nullptr;
        if(args) {
            auto found=args->find(flutter::EncodableValue("kind"));
            if(found!=args->end())kind=std::get_if<std::string>(&found->second);
        }
        if(!kind)reply=codec.EncodeErrorEnvelope("Argument error","Missing cursor kind");
        else {
            using namespace ABI::Windows::UI::Core;
            static const std::map<std::string,CoreCursorType> cursors={
                {"basic",CoreCursorType_Arrow},{"click",CoreCursorType_Hand},
                {"text",CoreCursorType_IBeam},{"verticalText",CoreCursorType_IBeam},
                {"forbidden",CoreCursorType_UniversalNo},{"noDrop",CoreCursorType_UniversalNo},
                {"wait",CoreCursorType_Wait},{"progress",CoreCursorType_Wait},
                {"crosshair",CoreCursorType_Cross},{"precise",CoreCursorType_Cross},
                {"help",CoreCursorType_Help},{"move",CoreCursorType_SizeAll},
                {"allScroll",CoreCursorType_SizeAll},{"grab",CoreCursorType_Hand},
                {"grabbing",CoreCursorType_Hand},{"resizeLeftRight",CoreCursorType_SizeWestEast},
                {"resizeLeft",CoreCursorType_SizeWestEast},{"resizeRight",CoreCursorType_SizeWestEast},
                {"resizeUpDown",CoreCursorType_SizeNorthSouth},{"resizeUp",CoreCursorType_SizeNorthSouth},
                {"resizeDown",CoreCursorType_SizeNorthSouth},{"resizeRow",CoreCursorType_SizeNorthSouth},
                {"resizeColumn",CoreCursorType_SizeWestEast},
                {"resizeUpLeftDownRight",CoreCursorType_SizeNorthwestSoutheast},
                {"resizeUpLeft",CoreCursorType_SizeNorthwestSoutheast},
                {"resizeDownRight",CoreCursorType_SizeNorthwestSoutheast},
                {"resizeUpRightDownLeft",CoreCursorType_SizeNortheastSouthwest},
                {"resizeUpRight",CoreCursorType_SizeNortheastSouthwest},
                {"resizeDownLeft",CoreCursorType_SizeNortheastSouthwest}};
            ComPtr<ICoreCursor> cursor;HRESULT hr=S_OK;
            if(*kind!="none") {
                HSTRING name=nullptr;const wchar_t* cls=L"Windows.UI.Core.CoreCursor";
                hr=WindowsCreateString(cls,UINT32(wcslen(cls)),&name);
                ComPtr<ICoreCursorFactory> factory;
                if(SUCCEEDED(hr))hr=RoGetActivationFactory(name,IID_PPV_ARGS(&factory));
                WindowsDeleteString(name);
                auto found=cursors.find(*kind);
                if(SUCCEEDED(hr))hr=factory->CreateCursor(found==cursors.end()?CoreCursorType_Arrow:found->second,0,&cursor);
            }
            if(SUCCEEDED(hr))hr=context.window->put_PointerCursor(cursor.Get());
            Diagnostic(("Phone system cursor "+*kind).c_str(),hr);
            reply=SUCCEEDED(hr)?codec.EncodeSuccessEnvelope():
                codec.EncodeErrorEnvelope("Cursor error","CoreWindow cursor activation failed");
        }
    }
    if(message->response_handle)context.api.SendPlatformMessageResponse(context.engine,
        message->response_handle,reply?reply->data():nullptr,reply?reply->size():0);
    return true;
}
} // namespace flutter::winrt
