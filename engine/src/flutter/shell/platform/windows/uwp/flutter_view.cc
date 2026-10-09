#include "flutter/shell/platform/windows/uwp/flutter_view.h"
#include "flutter/shell/platform/windows/uwp/angle_surface.h"
#include "flutter/shell/platform/windows/uwp/diagnostics.h"
#include <windows.ui.input.h>
#include <windows.ui.viewmanagement.h>
#include <map>
#include "flutter/third_party/angle/src/libANGLE/renderer/d3d/d3d11/winrt/PhoneDisplayInformation.h"
using namespace flutter::winrt;
#define CHECK(expression) do { HRESULT result_ = (expression); if (FAILED(result_)) { Diagnostic(#expression, result_); return result_; } } while (0)
#include "services/phone-navigation.h"
#include "services/phone-text-input.h"
#include "services/phone-splash.h"
#include "services/phone-clipboard.h"
#include "services/phone-locales.h"
#include "services/phone-input-geometry.h"
#include "services/application_environment.h"
#include "services/phone-mouse-cursor.h"

class FlutterView : public AngleView {
    HMODULE engineLibrary=nullptr;
    FlutterEngineProcTable api{};
    FlutterEngine engine=nullptr;
    FlutterEngineAOTData aot=nullptr;
    EGLContext resourceContext=EGL_NO_CONTEXT;
    EGLSurface resourceSurface=EGL_NO_SURFACE;
    DWORD platformThread=0;
    double pixelRatio=1.0;
    ComPtr<rx::PhoneDisplayInformation> displayInformation;
    size_t metricsWidth=0,metricsHeight=0;
    bool metricsPending=true;
    ULONGLONG nextDpiCheck=0;
    flutter::winrt::PlatformMessageHandlers plugins;
    ComPtr<PhoneNavigation> navigation;
    EventRegistrationToken backToken{};
    bool backRegistered=false;
    PhoneTextInput textInput;
    double keyboardInset=0;
    bool activationReady=false;
    ComPtr<ABI::Windows::ApplicationModel::Activation::ISplashScreen> nativeSplash;
    ComPtr<ICoreApplication> lifecycleApplication;
    EventRegistrationToken suspendedToken{},resumedToken{},lifecycleVisibilityToken{},lifecycleFocusToken{};
    bool suspendedRegistered=false,resumedRegistered=false,lifecycleVisibilityRegistered=false,lifecycleFocusRegistered=false;
    bool focused=false;
    volatile LONG osSuspended=0,lifecyclePending=1,localesPending=0;
    std::string lastLifecycle;
    std::string lastSystemInsets;
    Rect contentBounds{};
    EGLint windowSurfaceAttributes[7]{};
    HRESULT ReadContentBounds(Rect* bounds) {
        // Render unscaled in CoreWindow coordinates. OS occlusion is reported
        // through Flutter view metrics; input uses the same coordinate space.
        return window->get_Bounds(bounds);
    }
    const EGLint* WindowSurfaceAttributes() override {
        Rect bounds{};FLOAT dpi=96;
        if(FAILED(ReadContentBounds(&bounds))||FAILED(displayInformation->get_LogicalDpi(&dpi)))return nullptr;
        windowSurfaceAttributes[0]=EGL_FIXED_SIZE_ANGLE;windowSurfaceAttributes[1]=EGL_TRUE;
        windowSurfaceAttributes[2]=EGL_WIDTH;windowSurfaceAttributes[3]=(EGLint)(bounds.Width*dpi/96.0+0.5);
        windowSurfaceAttributes[4]=EGL_HEIGHT;windowSurfaceAttributes[5]=(EGLint)(bounds.Height*dpi/96.0+0.5);
        windowSurfaceAttributes[6]=EGL_NONE;return windowSurfaceAttributes;
    }
    struct PointerState {bool added=false,down=false;};
    std::map<UINT32,PointerState> pointers;
    volatile LONG frameCount=0;
    struct Pending {FlutterTask task;uint64_t time;Pending* next;};
    Pending* tasks=nullptr;
    SRWLOCK tasksLock=SRWLOCK_INIT;
    FlutterTaskRunnerDescription platformRunner{};
    FlutterCustomTaskRunners runners{};
    static FlutterView* Self(void* data) {return static_cast<FlutterView*>(data);}
    static bool MakeCurrent(void* data) {
        auto self=Self(data);return self->makeCurrent(self->display,self->surface,self->surface,self->context)==EGL_TRUE;
    }
    static bool ClearCurrent(void* data) {
        auto self=Self(data);return self->makeCurrent(self->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT)==EGL_TRUE;
    }
    static bool MakeResourceCurrent(void* data) {
        auto self=Self(data);
        return self->makeCurrent(self->display,self->resourceSurface,self->resourceSurface,self->resourceContext)==EGL_TRUE;
    }
    static bool Present(void* data) {
        auto self=Self(data);
        bool result=self->swapBuffers(self->display,self->surface)==EGL_TRUE;
        if(result && InterlockedIncrement(&self->frameCount)==1)Diagnostic("Real Flutter hardware first Present",S_OK);
        if(!result)self->EglFailure("Real Flutter Present");
        return result;
    }
    static uint32_t Fbo(void*) {return 0;}
    static void* GlProc(void* data,const char* name) {
        auto self=Self(data);auto result=GetProcAddress(self->gles,name);
        if(result)return reinterpret_cast<void*>(result);
        auto resolver=reinterpret_cast<PFNEGLGETPROCADDRESSPROC>(GetProcAddress(self->egl,"eglGetProcAddress"));
        return resolver?reinterpret_cast<void*>(resolver(name)):nullptr;
    }
    static bool OnPlatformThread(void* data) {return GetCurrentThreadId()==Self(data)->platformThread;}
    static void PostTask(FlutterTask task,uint64_t time,void* data) {
        auto self=Self(data);auto pending=new Pending{task,time,nullptr};
        AcquireSRWLockExclusive(&self->tasksLock);
        pending->next=self->tasks;self->tasks=pending;
        ReleaseSRWLockExclusive(&self->tasksLock);
    }
    void DrainTasks() {
        Pending* ready=nullptr;auto now=api.GetCurrentTime();
        AcquireSRWLockExclusive(&tasksLock);
        auto position=&tasks;
        while(*position) {
            auto task=*position;
            if(task->time<=now) {*position=task->next;task->next=ready;ready=task;}
            else position=&task->next;
        }
        ReleaseSRWLockExclusive(&tasksLock);
        while(ready) {auto task=ready;ready=ready->next;api.RunTask(engine,&task->task);delete task;}
    }
    static void Log(const char* tag,const char* message,void*) {
        char label[4000]{};snprintf(label,sizeof(label),"Flutter %s: %s",tag?tag:"",message?message:"");Diagnostic(label,S_OK);
    }
    static void PlatformMessage(const FlutterPlatformMessage* message,void* data) {
        auto self=Self(data);
        flutter::winrt::PlatformMessageContext context{self->window.Get(), self->dispatcher.Get(),
            self, self->pixelRatio, self->focused, self->engine, self->api};
        if(PhoneHandleMouseCursor(message,context))return;
        if(self->plugins.Handle(message, context))return;
        if(strcmp(message->channel,"flutter/textinput")==0) {
            rapidjson::Document call;call.Parse(reinterpret_cast<const char*>(message->message),message->message_size);
            HRESULT hr=call.HasParseError()?E_INVALIDARG:self->textInput.Handle(call);
            if(FAILED(hr))self->ReplyServiceError(message->response_handle,hr);
            else ReplyJson(self,message->response_handle,"[null]",6);
            return;
        }
        if(strcmp(message->channel,"flutter/platform")==0) {
            rapidjson::Document decoded;decoded.Parse(reinterpret_cast<const char*>(message->message),message->message_size);
            if(!decoded.HasParseError() && decoded.IsObject() && decoded.HasMember("method") && decoded["method"].IsString() &&
               strncmp(decoded["method"].GetString(),"Clipboard.",10)==0) {
                HRESULT hr=PhoneHandleClipboard(decoded,self->dispatcher.Get(),self,ReplyJson,self,message->response_handle);
                if(FAILED(hr))self->ReplyServiceError(message->response_handle,hr);
                return;
            }
            if(!decoded.HasParseError() && decoded.IsObject() && decoded.HasMember("method") && decoded["method"].IsString() &&
               strcmp(decoded["method"].GetString(),"SystemChrome.setSystemUIOverlayStyle")==0) {
                if(!decoded.HasMember("args")) {self->ReplyServiceError(message->response_handle,E_INVALIDARG);return;}
                const auto& style=decoded["args"];
                HRESULT hr=S_OK;
                if(style.IsObject()&&style.HasMember("statusBarIconBrightness")&&style["statusBarIconBrightness"].IsString())
                    hr=PhoneSetStatusBarStyle(strstr(style["statusBarIconBrightness"].GetString(),"dark")!=nullptr);
                if(FAILED(hr))self->ReplyServiceError(message->response_handle,hr);else ReplyJson(self,message->response_handle,"[null]",6);return;
            }
            if(!decoded.HasParseError() && decoded.IsObject() && decoded.HasMember("method") && decoded["method"].IsString() &&
               strcmp(decoded["method"].GetString(),"SystemChrome.setPreferredOrientations")==0) {
                HRESULT hr=decoded.HasMember("args")?self->PreferredOrientations(decoded["args"]):E_INVALIDARG;
                if(FAILED(hr))self->ReplyServiceError(message->response_handle,hr);
                else ReplyJson(self,message->response_handle,"[null]",6);
                return;
            }
            std::string call(reinterpret_cast<const char*>(message->message),message->message_size);
            if(call.find("\"SystemNavigator.pop\"")!=std::string::npos) {
                ReplyJson(self,message->response_handle,"[null]",6);
                self->closed=true;Diagnostic("Phone SystemNavigator.pop",S_OK);return;
            }
        }

        char label[240]{};
        snprintf(label,sizeof(label),"Phone unhandled platform channel %s",message->channel);
        Diagnostic(label,E_NOTIMPL);
        // An empty response explicitly reports an unsupported plugin to Dart.
        // Every response handle must be completed, including unknown channels.
        if(message->response_handle && self->engine)
            self->api.SendPlatformMessageResponse(self->engine,message->response_handle,nullptr,0);
    }
    static void SendJson(void* data,const char* channel,const std::string& json) {
        auto self=Self(data);if(!self->engine)return;
        FlutterPlatformMessage message{};message.struct_size=sizeof(message);
        message.channel=channel;message.message=reinterpret_cast<const uint8_t*>(json.data());message.message_size=json.size();
        Diagnostic("Phone text event to Flutter",static_cast<HRESULT>(self->api.SendPlatformMessage(self->engine,&message)));
    }
    static void ReplyJson(void* data,const FlutterPlatformMessageResponseHandle* response,const char* json,size_t size) {
        auto self=Self(data);
        if(self->engine && response)self->api.SendPlatformMessageResponse(self->engine,response,reinterpret_cast<const uint8_t*>(json),size);
    }
    void ReplyServiceError(const FlutterPlatformMessageResponseHandle* response,HRESULT hr) {
        char reply[160];snprintf(reply,sizeof(reply),"[\"PHONE_SERVICE\",\"WinRT error 0x%08lx\",null]",static_cast<unsigned long>(hr));
        Diagnostic("Phone native service error",hr);ReplyJson(this,response,reply,strlen(reply));
    }
    void UpdateLifecycle(bool detached=false) {
        if(!engine)return;
        if(!detached && !InterlockedExchange(&lifecyclePending,0))return;
        const char* state=detached?"AppLifecycleState.detached":
            InterlockedCompareExchange(&osSuspended,0,0)?"AppLifecycleState.paused":
            !visible?"AppLifecycleState.hidden":focused?"AppLifecycleState.resumed":"AppLifecycleState.inactive";
        if(lastLifecycle==state)return;
        FlutterPlatformMessage message{};message.struct_size=sizeof(message);
        message.channel="flutter/lifecycle";message.message=reinterpret_cast<const uint8_t*>(state);message.message_size=strlen(state);
        auto result=api.SendPlatformMessage(engine,&message);
        char label[120]{};snprintf(label,sizeof(label),"Phone lifecycle %s",state);Diagnostic(label,static_cast<HRESULT>(result));
        if(result==kSuccess)lastLifecycle=state;
        else InterlockedExchange(&lifecyclePending,1);
    }
    HRESULT PreferredOrientations(const rapidjson::Value& orientations) {
        if(!orientations.IsArray())return E_INVALIDARG;
        INT flags=0;
        for(const auto& value:orientations.GetArray()) {
            if(!value.IsString())return E_INVALIDARG;
            const char* name=value.GetString();
            if(strcmp(name,"DeviceOrientation.landscapeLeft")==0)flags|=1;
            else if(strcmp(name,"DeviceOrientation.portraitUp")==0)flags|=2;
            else if(strcmp(name,"DeviceOrientation.landscapeRight")==0)flags|=4;
            else if(strcmp(name,"DeviceOrientation.portraitDown")==0)flags|=8;
            else return E_INVALIDARG;
        }
        HSTRING name=nullptr;const wchar_t* cls=L"Windows.Graphics.Display.DisplayInformation";
        CHECK(WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&name));
        ComPtr<PhoneRotationPreferences> factory;
        HRESULT hr=RoGetActivationFactory(name,IID_PPV_ARGS(&factory));WindowsDeleteString(name);
        if(SUCCEEDED(hr))hr=factory->put_AutoRotationPreferences(flags);
        char label[100]{};snprintf(label,sizeof(label),"Phone OS rotation preferences flags=%d",flags);Diagnostic(label,hr);
        metricsPending=true;return hr;
    }
    std::string SystemInsetsJson() {
        Rect bounds{},visible{};
        if(FAILED(ReadContentBounds(&bounds))||FAILED(PhoneReadVisibleBounds(&visible)))return "{\"top\":0,\"right\":0,\"bottom\":0,\"left\":0}";
        auto clamp=[](double value,double maximum){return std::max(0.0,std::min(value,maximum));};
        char json[240]{};
        snprintf(json,sizeof(json),"{\"top\":%.6f,\"right\":%.6f,\"bottom\":%.6f,\"left\":%.6f}",
            clamp(visible.Y-bounds.Y,bounds.Height),clamp(bounds.X+bounds.Width-visible.X-visible.Width,bounds.Width),
            clamp(bounds.Y+bounds.Height-visible.Y-visible.Height,bounds.Height),clamp(visible.X-bounds.X,bounds.Width));
        return json;
    }
    void TraceInputGeometry(const char* reason) {
        Rect bounds{};HRESULT boundsResult=window->get_Bounds(&bounds);
        Rect visible{};HRESULT visibleResult=PhoneReadVisibleBounds(&visible);
        DOUBLE rawScale=0;ComPtr<PhoneRawDisplayScale> rawDisplay;
        HRESULT rawResult=displayInformation.As(&rawDisplay);
        if(SUCCEEDED(rawResult))rawResult=rawDisplay->get_RawPixelsPerViewPixel(&rawScale);
        auto query=reinterpret_cast<PFNEGLQUERYSURFACEPROC>(GetProcAddress(egl,"eglQuerySurface"));
        EGLint width=0,height=0;bool surfaceRead=query&&query(display,surface,EGL_WIDTH,&width)&&query(display,surface,EGL_HEIGHT,&height);
        char label[420]{};
        snprintf(label,sizeof(label),"Phone input geometry %s bounds=(%.3f,%.3f %.3fx%.3f) boundsHR=%08lx rawScale=%.6f rawHR=%08lx flutter=%zux%zu ratio=%.6f EGL=%dx%d eglRead=%d inset=%.3f",
            reason,double(bounds.X),double(bounds.Y),double(bounds.Width),double(bounds.Height),
            (unsigned long)boundsResult,double(rawScale),(unsigned long)rawResult,
            metricsWidth,metricsHeight,pixelRatio,width,height,int(surfaceRead),keyboardInset);
        Diagnostic(label,S_OK);
        snprintf(label,sizeof(label),"Phone OS visible bounds %s (%.3f,%.3f %.3fx%.3f) HR=%08lx",
            reason,double(visible.X),double(visible.Y),double(visible.Width),double(visible.Height),(unsigned long)visibleResult);
        Diagnostic(label,visibleResult);
    }
    HRESULT Metrics() {
        // ANGLE's surface dimensions remain cached until its next swap. Read
        // CoreWindow and the OS DPI instead, after all resize handlers run.
        Rect bounds{};
        CHECK(ReadContentBounds(&bounds));
        FLOAT dpi=0;CHECK(displayInformation->get_LogicalDpi(&dpi));
        if(dpi<=0 || bounds.Width<=0 || bounds.Height<=0)return S_OK;
        double ratio=double(dpi)/96.0;
        size_t width=static_cast<size_t>(bounds.Width*ratio+0.5);
        size_t height=static_cast<size_t>(bounds.Height*ratio+0.5);
        double inset=0;
        contentBounds=bounds;
        if(width!=metricsWidth||height!=metricsHeight) {
            auto attribute=reinterpret_cast<PFNEGLSURFACEATTRIBPROC>(GetProcAddress(egl,"eglSurfaceAttrib"));
            if(!attribute||!attribute(display,surface,EGL_WIDTH,(EGLint)width)||!attribute(display,surface,EGL_HEIGHT,(EGLint)height))return EglFailure("Phone visible surface resize");
        }
        auto systemInsets=SystemInsetsJson();
        bool paddingChanged=systemInsets!=lastSystemInsets;
        inset=textInput.BottomInset(bounds)*ratio;
        if(width==metricsWidth && height==metricsHeight && ratio==pixelRatio && inset==keyboardInset&&!paddingChanged)return S_OK;
        FlutterWindowMetricsEvent event{};event.struct_size=sizeof(event);
        event.width=width;event.height=height;event.pixel_ratio=ratio;
        event.physical_view_inset_bottom=inset;
        rapidjson::Document padding;padding.Parse(systemInsets.data(),systemInsets.size());
        event.physical_view_padding_top=padding["top"].GetDouble()*ratio;
        event.physical_view_padding_right=padding["right"].GetDouble()*ratio;
        event.physical_view_padding_bottom=padding["bottom"].GetDouble()*ratio;
        event.physical_view_padding_left=padding["left"].GetDouble()*ratio;
        auto result=api.SendWindowMetricsEvent(engine,&event);Diagnostic("Flutter window metrics",static_cast<HRESULT>(result));
        if(result==kSuccess) {metricsWidth=width;metricsHeight=height;pixelRatio=ratio;}
        if(result==kSuccess)keyboardInset=inset;
        if(result==kSuccess)lastSystemInsets=systemInsets;
        Diagnostic((std::string("Phone engine view padding ")+systemInsets).c_str(),static_cast<HRESULT>(result));
        char dimensions[240]{};
        snprintf(dimensions,sizeof(dimensions),"Phone viewport %zux%zu dpi=%.2f bounds=(%.2f,%.2f %.2fx%.2f) keyboardInset=%.2f",width,height,double(dpi),double(bounds.X),double(bounds.Y),double(bounds.Width),double(bounds.Height),inset);
        Diagnostic(dimensions,static_cast<HRESULT>(result));
        if(result==kSuccess)TraceInputGeometry("metrics");
        return result==kSuccess?S_OK:E_FAIL;
    }
    void Pointer(IPointerEventArgs* args,FlutterPointerPhase phase,bool wheel=false) {
        if(!engine)return;
        using namespace ABI::Windows::UI::Input;
        ComPtr<IPointerPoint> point;if(FAILED(args->get_CurrentPoint(&point)))return;
        Point location{};UINT32 id=0;
        if(FAILED(point->get_Position(&location)) || FAILED(point->get_pointer_id(&id)))return;
        using namespace ABI::Windows::Devices::Input;
        ComPtr<IPointerDevice> device;PointerDeviceType kind;
        ComPtr<IPointerPointProperties> properties;
        if(FAILED(point->get_PointerDevice(&device)) || FAILED(device->get_PointerDeviceType(&kind)) ||
           FAILED(point->get_Properties(&properties)))return;
        FlutterPointerDeviceKind flutterKind=kind==PointerDeviceType_Mouse?kFlutterPointerDeviceKindMouse:
            kind==PointerDeviceType_Pen?kFlutterPointerDeviceKindStylus:kFlutterPointerDeviceKindTouch;
        // Match Win32's device-kind namespace, so mouse and touch IDs cannot
        // share a Flutter pointer state when both are present.
        UINT32 pointerId=(UINT32(flutterKind)<<28)|id;
        auto found=pointers.find(pointerId);
        if((phase==kRemove || phase==kCancel) && found==pointers.end())return;
        if(kind==PointerDeviceType_Touch && phase==kMove &&
           (found==pointers.end() || !found->second.down))return;
        auto& state=pointers[pointerId];
        bool newlyAdded=!state.added;
        if(phase==kCancel && !state.down)return;
        FlutterPointerEvent event{};event.struct_size=sizeof(event);event.phase=phase;
        event.timestamp=api.GetCurrentTime()/1000;event.x=location.X*pixelRatio;event.y=location.Y*pixelRatio;
        // PointerPoint.Position is already client DIP. The full CoreWindow EGL
        // canvas starts at client (0,0); system-window origin is not subtracted.
        event.device=INT32(pointerId);event.device_kind=flutterKind;
        boolean left=false,right=false,middle=false,back=false,forward=false,contact=false;
        if(kind==PointerDeviceType_Mouse) {
            if(FAILED(properties->get_IsLeftButtonPressed(&left)) || FAILED(properties->get_IsRightButtonPressed(&right)) ||
               FAILED(properties->get_IsMiddleButtonPressed(&middle)) || FAILED(properties->get_IsXButton1Pressed(&back)) ||
               FAILED(properties->get_IsXButton2Pressed(&forward)))return;
            event.buttons=(left?kFlutterPointerButtonMousePrimary:0)|(right?kFlutterPointerButtonMouseSecondary:0)|
                (middle?kFlutterPointerButtonMouseMiddle:0)|(back?kFlutterPointerButtonMouseBack:0)|
                (forward?kFlutterPointerButtonMouseForward:0);
        } else {
            if(FAILED(point->get_IsInContact(&contact)))return;
            if(kind==PointerDeviceType_Touch && !contact && !state.down &&
               phase!=kRemove && phase!=kCancel) {pointers.erase(pointerId);return;}
            event.buttons=contact?1:0;
            if(kind==PointerDeviceType_Pen && SUCCEEDED(properties->get_IsBarrelButtonPressed(&right)) && right)
                event.buttons|=2;
        }
        // As in the Win32 embedder, a button change while another is held is
        // a move, and releasing the final button is the only up transition.
        if(phase==kDown || phase==kMove || phase==kUp || phase==kHover)
            event.phase=event.buttons?(state.down?kMove:kDown):(state.down?kUp:kHover);
        if(phase==kRemove && state.down)return; // Keep captured drags alive.
        if(wheel) {
            INT32 delta=0;boolean horizontal=false;
            if(FAILED(properties->get_MouseWheelDelta(&delta)) ||
               FAILED(properties->get_IsHorizontalMouseWheel(&horizontal)))return;
            event.signal_kind=kFlutterPointerSignalKindScroll;
            // Match Flutter Win32's default 100 physical pixels per detent.
            double pixels=double(delta)*100.0/120.0;
            if(horizontal)event.scroll_delta_x=pixels;else event.scroll_delta_y=-pixels;
        }
        if(!state.added) {
            auto add=event;add.phase=kAdd;add.buttons=0;add.signal_kind=kFlutterPointerSignalKindNone;
            add.scroll_delta_x=add.scroll_delta_y=0;api.SendPointerEvent(engine,&add,1);state.added=true;
        }
        if(phase!=kMove || wheel || newlyAdded) {
            TraceInputGeometry("pointer");
            char pointer[240]{};
            snprintf(pointer,sizeof(pointer),"Phone pointer phase=%d kind=%d buttons=%lld DIP=(%.2f,%.2f) pixels=(%.2f,%.2f) scroll=(%.2f,%.2f)",
                int(event.phase),int(event.device_kind),(long long)event.buttons,double(location.X),double(location.Y),
                event.x,event.y,event.scroll_delta_x,event.scroll_delta_y);
            Diagnostic(pointer,S_OK);
        }
        api.SendPointerEvent(engine,&event,1);
        if(kind==PointerDeviceType_Mouse && phase==kDown && event.phase==kDown)
            Diagnostic("Phone mouse pointer capture",window->SetPointerCapture());
        if(event.phase==kDown)state.down=true;
        if(event.phase==kUp || event.phase==kCancel)state.down=false;
        if(kind==PointerDeviceType_Mouse && event.phase==kUp)
            Diagnostic("Phone mouse pointer release capture",window->ReleasePointerCapture());
        if(phase==kRemove || phase==kCancel || (event.phase==kUp && kind==PointerDeviceType_Touch)) {
            if(event.phase!=kRemove){event.phase=kRemove;event.buttons=0;api.SendPointerEvent(engine,&event,1);}
            pointers.erase(pointerId);
        }
    }
    HRESULT StartFlutter() {
        // FML errors before engine initialization have no embedder log callback.
        // Capture stderr in this app's LocalState using the OS CRT.
        FILE* redirected=nullptr;
        auto path=DiagnosticPath();
        if(path) {
            // GUI stderr has no descriptor for dup2 on this phone's CRT.
            // Keep CRT's exclusive stderr file separate from the live log.
            wchar_t stderrPath[1200]{};
            wcsncpy(stderrPath,path,1199);
            auto slash=wcsrchr(stderrPath,L'\\');
            if(!slash)return E_FAIL;
            wcscpy(slash+1,L"flutter-phone-engine-stderr.txt");
            auto error=_wfreopen_s(&redirected,stderrPath,L"a",stderr);
            Diagnostic("Engine stderr capture",static_cast<HRESULT>(error));
            if(!error)setvbuf(stderr,nullptr,_IONBF,0);
        }
        engineLibrary=LoadPackagedLibrary(L"flutter_engine.dll",0);
        if(!engineLibrary)return HRESULT_FROM_WIN32(GetLastError());
        auto getApi=reinterpret_cast<FlutterEngineResult (*)(FlutterEngineProcTable*)>(GetProcAddress(engineLibrary,"FlutterEngineGetProcAddresses"));
        api.struct_size=sizeof(api);
        if(!getApi || getApi(&api)!=kSuccess || !api.RunsAOTCompiledDartCode())return E_NOINTERFACE;
        wchar_t executable[1024]{};
        DWORD count=GetModuleFileNameW(nullptr,executable,1024);if(!count || count>=1024)return E_FAIL;
        auto slash=wcsrchr(executable,L'\\');if(!slash)return E_FAIL;*slash=0;
        char base[2048]{},image[2200]{},icu[2200]{},assets[2200]{};
        if(!WideCharToMultiByte(CP_UTF8,0,executable,-1,base,sizeof(base),nullptr,nullptr))return E_FAIL;
        snprintf(image,sizeof(image),"%s\\data\\app.so",base);
        Diagnostic(image,S_OK);
        snprintf(icu,sizeof(icu),"%s\\data\\icudtl.dat",base);
        snprintf(assets,sizeof(assets),"%s\\data\\flutter_assets",base);
        wchar_t assetDirectory[1200]{};
        MultiByteToWideChar(CP_UTF8,0,assets,-1,assetDirectory,1200);
        CREATEFILE2_EXTENDED_PARAMETERS directoryParameters{};
        directoryParameters.dwSize=sizeof(directoryParameters);
        directoryParameters.dwFileFlags=FILE_FLAG_BACKUP_SEMANTICS;
        HANDLE directoryHandle=CreateFile2(assetDirectory,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,OPEN_EXISTING,&directoryParameters);
        Diagnostic("Phone asset directory open",directoryHandle==INVALID_HANDLE_VALUE?HRESULT_FROM_WIN32(GetLastError()):S_OK);
        if(directoryHandle!=INVALID_HANDLE_VALUE) {
            const DWORD pathFlags[]={FILE_NAME_NORMALIZED,FILE_NAME_OPENED};
            for(DWORD flags : pathFlags) {
                wchar_t finalPath[1200]{};
                DWORD size=GetFinalPathNameByHandleW(directoryHandle,finalPath,1200,flags);
                Diagnostic(flags==FILE_NAME_NORMALIZED?"Phone normalized asset handle path":"Phone opened asset handle path",size?S_OK:HRESULT_FROM_WIN32(GetLastError()));
                if(size && size<1200) {
                    char pathUtf8[2400]{};
                    WideCharToMultiByte(CP_UTF8,0,finalPath,-1,pathUtf8,sizeof(pathUtf8),nullptr,nullptr);
                    Diagnostic(pathUtf8,S_OK);
                }
            }
            CloseHandle(directoryHandle);
        }
        FlutterEngineAOTDataSource source{};source.type=kFlutterEngineAOTDataSourceTypeElfPath;source.elf_path=image;
        auto result=api.CreateAOTData(&source,&aot);Diagnostic("Flutter CreateAOTData",static_cast<HRESULT>(result));
        if(result!=kSuccess)return E_FAIL;
        auto createContext=reinterpret_cast<PFNEGLCREATECONTEXTPROC>(GetProcAddress(egl,"eglCreateContext"));
        auto createPbuffer=reinterpret_cast<PFNEGLCREATEPBUFFERSURFACEPROC>(GetProcAddress(egl,"eglCreatePbufferSurface"));
        const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
        const EGLint surfaceAttributes[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
        if(!createContext || !createPbuffer)return E_NOINTERFACE;
        resourceContext=createContext(display,config,context,contextAttributes);
        resourceSurface=createPbuffer(display,config,surfaceAttributes);
        if(resourceContext==EGL_NO_CONTEXT || resourceSurface==EGL_NO_SURFACE)return EglFailure("Flutter shared resource context");
        Diagnostic("Flutter shared hardware resource context",S_OK);
        platformThread=GetCurrentThreadId();
        platformRunner.struct_size=sizeof(platformRunner);platformRunner.user_data=this;
        platformRunner.runs_task_on_current_thread_callback=OnPlatformThread;
        platformRunner.post_task_callback=PostTask;platformRunner.identifier=1;
        runners.struct_size=sizeof(runners);runners.platform_task_runner=&platformRunner;
        FlutterRendererConfig renderer{};renderer.type=kOpenGL;
        auto& gl=renderer.open_gl;gl.struct_size=sizeof(gl);gl.make_current=MakeCurrent;
        gl.clear_current=ClearCurrent;gl.present=Present;gl.fbo_callback=Fbo;
        gl.make_resource_current=MakeResourceCurrent;
        gl.gl_proc_resolver=GlProc;gl.fbo_reset_after_present=true;
        FlutterProjectArgs project{};project.struct_size=sizeof(project);
        CHECK(FlutterWinRTSetApplicationEnvironment());
        project.assets_path=assets;project.icu_data_path=icu;project.aot_data=aot;
        project.custom_task_runners=&runners;project.log_message_callback=Log;
        project.platform_message_callback=PlatformMessage;
        project.shutdown_dart_vm_when_done=true;

        ClearCurrent(this);
        // Obtain the engine handle and initialize native plugins before Dart
        // can call their channels or executable FFI exports.
        result=api.Initialize(FLUTTER_ENGINE_VERSION,&renderer,&project,this,&engine);
        Diagnostic("FlutterEngineInitialize",static_cast<HRESULT>(result));
        if(result!=kSuccess)return E_FAIL;
        flutter::winrt::PlatformMessageContext pluginContext{window.Get(), dispatcher.Get(),
            this, pixelRatio, focused, engine, api};
        CHECK(plugins.Initialize(pluginContext));
        result=api.RunInitialized(engine);
        Diagnostic("FlutterEngineRunInitialized",static_cast<HRESULT>(result));
        if(result!=kSuccess)return E_FAIL;
        CHECK(PhoneUpdateLocales(engine,api));
        CHECK(Metrics());
        result=api.ScheduleFrame(engine);
        Diagnostic("Phone initial frame",static_cast<HRESULT>(result));
        return result==kSuccess?S_OK:E_FAIL;
    }
public:
    explicit FlutterView(flutter::winrt::PlatformMessageHandlers handlers):plugins(std::move(handlers)) {}
    HRESULT STDMETHODCALLTYPE Initialize(ICoreApplicationView* view) override {
        auto activated=callback<ITypedEventHandler<CoreApplicationView*,IActivatedEventArgs*>,ICoreApplicationView,IActivatedEventArgs>(
            [this](ICoreApplicationView*,IActivatedEventArgs* args) -> HRESULT {
                activationReady=true;
                if(engine)return window->Activate();
                HRESULT hr=args->get_SplashScreen(&nativeSplash);
                Diagnostic("Phone OS splash object",hr);
                return S_OK;
            });
        EventRegistrationToken token;CHECK(view->add_Activated(activated.Get(),&token));
        HSTRING name=nullptr;const wchar_t* cls=L"Windows.ApplicationModel.Core.CoreApplication";
        CHECK(WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&name));
        HRESULT hr=RoGetActivationFactory(name,IID_PPV_ARGS(&lifecycleApplication));WindowsDeleteString(name);CHECK(hr);
        auto suspending=callback<IEventHandler<ABI::Windows::ApplicationModel::SuspendingEventArgs*>,IInspectable,ABI::Windows::ApplicationModel::ISuspendingEventArgs>(
            [this](IInspectable*,ABI::Windows::ApplicationModel::ISuspendingEventArgs*) -> HRESULT {
                InterlockedExchange(&osSuspended,1);InterlockedExchange(&lifecyclePending,1);
                Diagnostic("Phone OS suspending",S_OK);
                if(GetCurrentThreadId()==platformThread) {UpdateLifecycle();DrainTasks();}
                return S_OK;
            });
        CHECK(lifecycleApplication->add_Suspending(suspending.Get(),&suspendedToken));suspendedRegistered=true;
        auto resuming=callback<IEventHandler<IInspectable*>,IInspectable,IInspectable>(
            [this](IInspectable*,IInspectable*) -> HRESULT {
                InterlockedExchange(&osSuspended,0);InterlockedExchange(&lifecyclePending,1);InterlockedExchange(&localesPending,1);
                Diagnostic("Phone OS resuming",S_OK);return S_OK;
            });
        CHECK(lifecycleApplication->add_Resuming(resuming.Get(),&resumedToken));resumedRegistered=true;
        Diagnostic("Phone OS lifecycle registration",S_OK);return S_OK;
    }
    HRESULT DrawStartupSplash() {
        wchar_t asset[1200]{};if(!GetModuleFileNameW(nullptr,asset,1200))return HRESULT_FROM_WIN32(GetLastError());
        auto slash=wcsrchr(asset,L'\\');if(!slash)return E_FAIL;
        wcscpy_s(slash+1,1200-(slash+1-asset),L"Assets\\FlutterSplash.bin");
        PhoneSplashImage splash;CHECK(splash.Load(asset));
        Rect bounds{},image{};FLOAT dpi=0;
        CHECK(window->get_Bounds(&bounds));CHECK(displayInformation->get_LogicalDpi(&dpi));
        if(bounds.Width<=0 || bounds.Height<=0 || dpi<=0)return E_FAIL;
        HRESULT location=nativeSplash?nativeSplash->get_ImageLocation(&image):E_NOINTERFACE;
        Diagnostic("Phone OS splash ImageLocation",location);
        char reported[180]{};snprintf(reported,sizeof(reported),"Phone OS splash reported rect %.2f,%.2f %.2fx%.2f",double(image.X),double(image.Y),double(image.Width),double(image.Height));Diagnostic(reported,S_OK);
        // Mobile 14393 returns the full physical display. CoreWindow is in DIP.
        // Detect the physical rectangle from actual bounds and DPI, not a
        // hard-coded screen resolution. Other OS variants may already use DIP.
        const float ratio=dpi/96;
        if(fabs(image.Width-bounds.Width*ratio)<1 && fabs(image.Height-bounds.Height*ratio)<1) {
            image.X/=ratio;image.Y/=ratio;image.Width/=ratio;image.Height/=ratio;
        }
        if(FAILED(location) || image.Width<=0 || image.Height<=0) {
            image.Width=bounds.Width;image.Height=bounds.Height;
            image.X=bounds.X+(bounds.Width-image.Width)/2;image.Y=bounds.Y+(bounds.Height-image.Height)/2;
        }
        // Keep the logo circular across display aspect ratios and rotation.
        float fittedWidth=image.Height*splash.width/splash.height;
        if(fittedWidth<=image.Width) {image.X+=(image.Width-fittedWidth)/2;image.Width=fittedWidth;}
        else {float fittedHeight=image.Width*splash.height/splash.width;image.Y+=(image.Height-fittedHeight)/2;image.Height=fittedHeight;}
        char rectangle[180]{};snprintf(rectangle,sizeof(rectangle),"Phone splash DIP rect %.2f,%.2f %.2fx%.2f",double(image.X),double(image.Y),double(image.Width),double(image.Height));Diagnostic(rectangle,S_OK);
        HRESULT hr=PhoneDrawSplash(gles,splash,bounds,image,static_cast<GLsizei>(bounds.Width*dpi/96+.5f),static_cast<GLsizei>(bounds.Height*dpi/96+.5f));
        Diagnostic("Phone extended splash hardware draw",hr);CHECK(hr);
        if(swapBuffers(display,surface)!=EGL_TRUE)return EglFailure("Phone extended splash Present");
        Diagnostic("Phone extended splash hardware Present",S_OK);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetWindow(ICoreWindow* value) override {
        CHECK(AngleView::SetWindow(value));
        auto lifecycleVisibility=callback<ITypedEventHandler<CoreWindow*,VisibilityChangedEventArgs*>,ICoreWindow,IVisibilityChangedEventArgs>(
            [this](ICoreWindow*,IVisibilityChangedEventArgs*) -> HRESULT {InterlockedExchange(&lifecyclePending,1);return S_OK;});
        CHECK(window->add_VisibilityChanged(lifecycleVisibility.Get(),&lifecycleVisibilityToken));lifecycleVisibilityRegistered=true;
        auto lifecycleFocus=callback<ITypedEventHandler<CoreWindow*,WindowActivatedEventArgs*>,ICoreWindow,IWindowActivatedEventArgs>(
            [this](ICoreWindow*,IWindowActivatedEventArgs* args) -> HRESULT {
                CoreWindowActivationState state{};CHECK(args->get_WindowActivationState(&state));
                focused=state!=CoreWindowActivationState_Deactivated;
                InterlockedExchange(&lifecyclePending,1);return S_OK;
            });
        CHECK(window->add_Activated(lifecycleFocus.Get(),&lifecycleFocusToken));lifecycleFocusRegistered=true;
        const wchar_t* navigationClass=L"Windows.UI.Core.SystemNavigationManager";
        HSTRING navigationName=nullptr;
        CHECK(WindowsCreateString(navigationClass,static_cast<UINT32>(wcslen(navigationClass)),&navigationName));
        ComPtr<PhoneNavigationStatics> navigationFactory;
        HRESULT navigationResult=RoGetActivationFactory(navigationName,IID_PPV_ARGS(&navigationFactory));
        WindowsDeleteString(navigationName);
        if(SUCCEEDED(navigationResult))navigationResult=navigationFactory->GetForCurrentView(&navigation);
        if(SUCCEEDED(navigationResult)) {
            auto back=callback<PhoneBackHandler,IInspectable,PhoneBackArgs>(
                [this](IInspectable*,PhoneBackArgs* args) -> HRESULT {
                    if(!engine)return S_OK;
                    const char json[]="{\"method\":\"popRoute\",\"args\":null}";
                    FlutterPlatformMessage message{};message.struct_size=sizeof(message);
                    message.channel="flutter/navigation";message.message=reinterpret_cast<const uint8_t*>(json);
                    message.message_size=sizeof(json)-1;
                    auto result=api.SendPlatformMessage(engine,&message);
                    Diagnostic("Phone OS back to Flutter popRoute",static_cast<HRESULT>(result));
                    return result==kSuccess?args->put_Handled(true):S_OK;
                });
            navigationResult=navigation->add_BackRequested(back.Get(),&backToken);
            backRegistered=SUCCEEDED(navigationResult);
        }
        Diagnostic("Phone system back registration",navigationResult);
        CHECK(navigationResult);
        CHECK(textInput.Initialize(SendJson,this));
        HSTRING displayName=nullptr;
        const wchar_t* displayClass=L"Windows.Graphics.Display.DisplayInformation";
        CHECK(WindowsCreateString(displayClass,static_cast<UINT32>(wcslen(displayClass)),&displayName));
        ComPtr<rx::PhoneDisplayInformationStatics> displayFactory;
        HRESULT displayResult=RoGetActivationFactory(displayName,IID_PPV_ARGS(&displayFactory));
        WindowsDeleteString(displayName);CHECK(displayResult);
        CHECK(displayFactory->GetForCurrentView(&displayInformation));
        auto pressed=callback<ITypedEventHandler<CoreWindow*,PointerEventArgs*>,ICoreWindow,IPointerEventArgs>(
            [this](ICoreWindow*,IPointerEventArgs* args) -> HRESULT {Pointer(args,kDown);return S_OK;});
        auto moved=callback<ITypedEventHandler<CoreWindow*,PointerEventArgs*>,ICoreWindow,IPointerEventArgs>(
            [this](ICoreWindow*,IPointerEventArgs* args) -> HRESULT {Pointer(args,kMove);return S_OK;});
        auto released=callback<ITypedEventHandler<CoreWindow*,PointerEventArgs*>,ICoreWindow,IPointerEventArgs>(
            [this](ICoreWindow*,IPointerEventArgs* args) -> HRESULT {Pointer(args,kUp);return S_OK;});
        auto canceled=callback<ITypedEventHandler<CoreWindow*,PointerEventArgs*>,ICoreWindow,IPointerEventArgs>(
            [this](ICoreWindow*,IPointerEventArgs* args) -> HRESULT {Pointer(args,kCancel);return S_OK;});
        auto entered=callback<ITypedEventHandler<CoreWindow*,PointerEventArgs*>,ICoreWindow,IPointerEventArgs>(
            [this](ICoreWindow*,IPointerEventArgs* args) -> HRESULT {Pointer(args,kHover);return S_OK;});
        auto exited=callback<ITypedEventHandler<CoreWindow*,PointerEventArgs*>,ICoreWindow,IPointerEventArgs>(
            [this](ICoreWindow*,IPointerEventArgs* args) -> HRESULT {Pointer(args,kRemove);return S_OK;});
        auto wheel=callback<ITypedEventHandler<CoreWindow*,PointerEventArgs*>,ICoreWindow,IPointerEventArgs>(
            [this](ICoreWindow*,IPointerEventArgs* args) -> HRESULT {Pointer(args,kMove,true);return S_OK;});
        auto resized=callback<ITypedEventHandler<CoreWindow*,WindowSizeChangedEventArgs*>,ICoreWindow,IWindowSizeChangedEventArgs>(
            [this](ICoreWindow*,IWindowSizeChangedEventArgs*) -> HRESULT {metricsPending=true;return S_OK;});
        EventRegistrationToken token;CHECK(window->add_PointerPressed(pressed.Get(),&token));
        CHECK(window->add_PointerMoved(moved.Get(),&token));CHECK(window->add_PointerReleased(released.Get(),&token));
        CHECK(window->add_PointerEntered(entered.Get(),&token));CHECK(window->add_PointerExited(exited.Get(),&token));
        CHECK(window->add_PointerWheelChanged(wheel.Get(),&token));
        CHECK(window->add_SizeChanged(resized.Get(),&token));
        return window->add_PointerCaptureLost(canceled.Get(),&token);
    }
    HRESULT STDMETHODCALLTYPE Run() override {
        while(!closed) {
            // Activated may be delivered before Run. Do not wait for a second
            // OS event after activation has already authorized engine startup.
            CHECK(dispatcher->ProcessEvents((engine||activationReady)?CoreProcessEventsOption_ProcessAllIfPresent:CoreProcessEventsOption_ProcessOneAndAllPending));
            if(!engine && activationReady) {
                CHECK(PhoneUseCoreWindowBounds());
                CHECK(PhoneShowStatusBar());
                CHECK(PhoneSetStatusBarStyle(true));
                CHECK(InitializeAngle());
                CHECK(DrawStartupSplash());
                CHECK(window->Activate());CHECK(StartFlutter());
            }
            if(engine) {
                UpdateLifecycle();
                if(InterlockedExchange(&localesPending,0))PhoneUpdateLocales(engine,api);
                auto now=GetTickCount64();
                // Also observe DPI-only changes, which need not resize the window.
                if(metricsPending || now>=nextDpiCheck) {
                    CHECK(Metrics());metricsPending=false;nextDpiCheck=now+250;
                }
                DrainTasks();
                Sleep(8);
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Uninitialize() override {
        UpdateLifecycle(true);
        if(suspendedRegistered){lifecycleApplication->remove_Suspending(suspendedToken);suspendedRegistered=false;}
        if(resumedRegistered){lifecycleApplication->remove_Resuming(resumedToken);resumedRegistered=false;}
        if(lifecycleVisibilityRegistered){window->remove_VisibilityChanged(lifecycleVisibilityToken);lifecycleVisibilityRegistered=false;}
        if(lifecycleFocusRegistered){window->remove_Activated(lifecycleFocusToken);lifecycleFocusRegistered=false;}
        textInput.Close();
        if(backRegistered) {navigation->remove_BackRequested(backToken);backRegistered=false;}
        plugins.Clear();
        if(engine) {api.Shutdown(engine);engine=nullptr;}
        if(aot) {api.CollectAOTData(aot);aot=nullptr;}
        if(resourceSurface!=EGL_NO_SURFACE) {destroySurface(display,resourceSurface);resourceSurface=EGL_NO_SURFACE;}
        if(resourceContext!=EGL_NO_CONTEXT) {destroyContext(display,resourceContext);resourceContext=EGL_NO_CONTEXT;}
        while(tasks) {auto task=tasks;tasks=task->next;delete task;}
        if(engineLibrary) {FreeLibrary(engineLibrary);engineLibrary=nullptr;}
        return AngleView::Uninitialize();
    }
};

#undef CHECK
namespace flutter::winrt {
HRESULT CreateFlutterView(PlatformMessageHandlers handlers, IFrameworkView** result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    ComPtr<IFrameworkView> view;
    view.Attach(new FlutterView(std::move(handlers)));
    return view.CopyTo(result);
}
}
