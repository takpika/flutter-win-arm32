#pragma once
#include "flutter/shell/platform/windows/uwp/runtime_support.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <map>
#include <mutex>

namespace flutter::winrt {
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::Foundation;
using ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs;
void Diagnostic(const char* expression, HRESULT result);
#define FLUTTER_WINRT_CHECK(expression) do { HRESULT result_ = (expression); if (FAILED(result_)) { Diagnostic(#expression, result_); return result_; } } while (0)

// Shared hardware ANGLE resources for CoreWindow and view-independent hosts.
// EGL displays outlive every engine/surface using them, including concurrent
// foreground and background engines in the same application process.
template<class T> T Proc(HMODULE module,const char* name) {
    auto result=reinterpret_cast<T>(GetProcAddress(module,name));
    Diagnostic(name,result?S_OK:HRESULT_FROM_WIN32(GetLastError()));
    return result;
}
class AngleSurface {
protected:
    HMODULE egl=nullptr,gles=nullptr;
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLSurface surface=EGL_NO_SURFACE;
    EGLContext context=EGL_NO_CONTEXT;
    EGLContext resourceContext=EGL_NO_CONTEXT;
    EGLSurface resourceSurface=EGL_NO_SURFACE;
    bool displayAcquired=false;
    inline static std::mutex displaysMutex;
    inline static std::map<EGLDisplay,size_t> displays;
    EGLConfig config=nullptr;
    PFNEGLMAKECURRENTPROC makeCurrent=nullptr;
    PFNEGLSWAPBUFFERSPROC swapBuffers=nullptr;
    PFNEGLGETERRORPROC eglError=nullptr;
    PFNEGLDESTROYSURFACEPROC destroySurface=nullptr;
    PFNEGLDESTROYCONTEXTPROC destroyContext=nullptr;
    PFNEGLTERMINATEPROC terminate=nullptr;
    PFNGLCLEARCOLORPROC clearColor=nullptr;
    PFNGLCLEARPROC clear=nullptr;
    PFNGLGETERRORPROC glError=nullptr;
    HRESULT EglFailure(const char* label) {
        Diagnostic(label,static_cast<HRESULT>(eglError?eglError():EGL_NOT_INITIALIZED));
        return E_FAIL;
    }
    HRESULT InitializeAngle(ICoreWindow* nativeWindow, const EGLint* windowAttributes) {
        if(egl || gles || displayAcquired)return E_UNEXPECTED;
        egl=LoadPackagedLibrary(L"libEGL.dll",0);
        gles=LoadPackagedLibrary(L"libGLESv2.dll",0);
        if(!egl || !gles)return HRESULT_FROM_WIN32(GetLastError());
#define FLUTTER_WINRT_EGL_PROC(type,var,name) auto var=Proc<type>(egl,name); if(!var)return E_NOINTERFACE
        FLUTTER_WINRT_EGL_PROC(PFNEGLGETPLATFORMDISPLAYEXTPROC,getPlatform,"eglGetPlatformDisplayEXT");
        FLUTTER_WINRT_EGL_PROC(PFNEGLINITIALIZEPROC,initialize,"eglInitialize");
        FLUTTER_WINRT_EGL_PROC(PFNEGLCHOOSECONFIGPROC,choose,"eglChooseConfig");
        FLUTTER_WINRT_EGL_PROC(PFNEGLCREATEWINDOWSURFACEPROC,createSurface,"eglCreateWindowSurface");
        FLUTTER_WINRT_EGL_PROC(PFNEGLCREATEPBUFFERSURFACEPROC,createPbuffer,"eglCreatePbufferSurface");
        FLUTTER_WINRT_EGL_PROC(PFNEGLCREATECONTEXTPROC,createContext,"eglCreateContext");
        FLUTTER_WINRT_EGL_PROC(PFNEGLQUERYDISPLAYATTRIBEXTPROC,queryDisplay,"eglQueryDisplayAttribEXT");
        FLUTTER_WINRT_EGL_PROC(PFNEGLQUERYDEVICEATTRIBEXTPROC,queryDevice,"eglQueryDeviceAttribEXT");
#undef FLUTTER_WINRT_EGL_PROC
        makeCurrent=Proc<PFNEGLMAKECURRENTPROC>(egl,"eglMakeCurrent");
        swapBuffers=Proc<PFNEGLSWAPBUFFERSPROC>(egl,"eglSwapBuffers");
        eglError=Proc<PFNEGLGETERRORPROC>(egl,"eglGetError");
        destroySurface=Proc<PFNEGLDESTROYSURFACEPROC>(egl,"eglDestroySurface");
        destroyContext=Proc<PFNEGLDESTROYCONTEXTPROC>(egl,"eglDestroyContext");
        terminate=Proc<PFNEGLTERMINATEPROC>(egl,"eglTerminate");
        clearColor=Proc<PFNGLCLEARCOLORPROC>(gles,"glClearColor");
        clear=Proc<PFNGLCLEARPROC>(gles,"glClear");
        glError=Proc<PFNGLGETERRORPROC>(gles,"glGetError");
        auto getString=Proc<PFNGLGETSTRINGPROC>(gles,"glGetString");
        if(!makeCurrent || !swapBuffers || !eglError || !destroySurface || !destroyContext || !terminate || !clearColor || !clear || !glError || !getString)return E_NOINTERFACE;
        // Let ANGLE choose the highest supported hardware feature level.
        const EGLint attributes[]={EGL_PLATFORM_ANGLE_TYPE_ANGLE,EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE,EGL_NONE};
        {
            std::lock_guard<std::mutex> lock(displaysMutex);
            display=getPlatform(EGL_PLATFORM_ANGLE_ANGLE,EGL_DEFAULT_DISPLAY,attributes);
            if(display==EGL_NO_DISPLAY)return EglFailure("ANGLE hardware display");
            auto lease=displays.try_emplace(display,0).first;
            if(lease->second==0) {
                EGLint major=0,minor=0;
                if(!initialize(display,&major,&minor)) {
                    displays.erase(lease);return EglFailure("ANGLE eglInitialize");
                }
            }
            ++lease->second;displayAcquired=true;
        }
        Diagnostic("ANGLE eglInitialize",S_OK);
        EGLAttrib deviceValue=0,d3dValue=0;
        if(!queryDisplay(display,EGL_DEVICE_EXT,&deviceValue) ||
           !queryDevice(reinterpret_cast<EGLDeviceEXT>(deviceValue),EGL_D3D11_DEVICE_ANGLE,&d3dValue) || !d3dValue)
            return EglFailure("ANGLE actual D3D11 device query");
        auto d3d=reinterpret_cast<ID3D11Device*>(d3dValue);
        ComPtr<IDXGIDevice> dxgi;FLUTTER_WINRT_CHECK(d3d->QueryInterface(IID_PPV_ARGS(&dxgi)));
        ComPtr<IDXGIAdapter> adapter;FLUTTER_WINRT_CHECK(dxgi->GetAdapter(&adapter));
        ComPtr<IDXGIAdapter2> adapter2;FLUTTER_WINRT_CHECK(adapter.As(&adapter2));
        DXGI_ADAPTER_DESC2 description{};FLUTTER_WINRT_CHECK(adapter2->GetDesc2(&description));
        char adapterName[256]{};
        WideCharToMultiByte(CP_UTF8,0,description.Description,-1,adapterName,sizeof(adapterName),nullptr,nullptr);
        Diagnostic(adapterName,S_OK);
        Diagnostic("ANGLE adapter VendorId",static_cast<HRESULT>(description.VendorId));
        Diagnostic("ANGLE adapter flags",static_cast<HRESULT>(description.Flags));
        Diagnostic("ANGLE feature level",static_cast<HRESULT>(d3d->GetFeatureLevel()));
        if(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) {
            Diagnostic("Rejected software DXGI adapter",E_FAIL);return E_FAIL;
        }
        Diagnostic("ANGLE actual hardware adapter verified",S_OK);
        const EGLint configAttributes[]={EGL_SURFACE_TYPE,nativeWindow?(EGL_WINDOW_BIT|EGL_PBUFFER_BIT):EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
        EGLint count=0;
        if(!choose(display,configAttributes,&config,1,&count) || count!=1)return EglFailure("ANGLE config");
        const EGLint pbufferAttributes[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
        surface=nativeWindow
            ? createSurface(display,config,reinterpret_cast<EGLNativeWindowType>(nativeWindow),windowAttributes)
            : createPbuffer(display,config,pbufferAttributes);
        if(surface==EGL_NO_SURFACE)return EglFailure("ANGLE render surface");
        const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
        context=createContext(display,config,EGL_NO_CONTEXT,contextAttributes);
        if(context==EGL_NO_CONTEXT || !makeCurrent(display,surface,surface,context))return EglFailure("ANGLE GLES2 context");
        const auto renderer=getString(GL_RENDERER);
        if(renderer)Diagnostic(reinterpret_cast<const char*>(renderer),S_OK);
        Diagnostic(nativeWindow?"ANGLE GLES2 CoreWindow ready":"ANGLE GLES2 offscreen ready",S_OK);
        return S_OK;
    }

    HRESULT InitializeResourceContext() {
        auto createContext=Proc<PFNEGLCREATECONTEXTPROC>(egl,"eglCreateContext");
        auto createPbuffer=Proc<PFNEGLCREATEPBUFFERSURFACEPROC>(egl,"eglCreatePbufferSurface");
        const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
        const EGLint surfaceAttributes[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
        if(!createContext || !createPbuffer)return E_NOINTERFACE;
        resourceContext=createContext(display,config,context,contextAttributes);
        resourceSurface=createPbuffer(display,config,surfaceAttributes);
        if(resourceContext==EGL_NO_CONTEXT || resourceSurface==EGL_NO_SURFACE)
            return EglFailure("Flutter shared resource context");
        Diagnostic("Flutter shared hardware resource context",S_OK);
        return S_OK;
    }
    void ShutdownAngle() {
        if(displayAcquired) {
            if(makeCurrent)makeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
            if(resourceSurface!=EGL_NO_SURFACE && destroySurface)destroySurface(display,resourceSurface);
            if(resourceContext!=EGL_NO_CONTEXT && destroyContext)destroyContext(display,resourceContext);
            if(surface!=EGL_NO_SURFACE && destroySurface)destroySurface(display,surface);
            if(context!=EGL_NO_CONTEXT && destroyContext)destroyContext(display,context);
            std::lock_guard<std::mutex> lock(displaysMutex);
            auto found=displays.find(display);
            if(found!=displays.end() && --found->second==0) {
                if(terminate)terminate(display);
                displays.erase(found);
            }
        }
        displayAcquired=false;display=EGL_NO_DISPLAY;
        surface=resourceSurface=EGL_NO_SURFACE;
        context=resourceContext=EGL_NO_CONTEXT;
        if(gles)FreeLibrary(gles);if(egl)FreeLibrary(egl);
        gles=egl=nullptr;
    }
};

class AngleView : public Ref<IFrameworkView>, protected AngleSurface {
protected:
    virtual const EGLint* WindowSurfaceAttributes() {return nullptr;}
    ComPtr<ICoreWindow> window;
    ComPtr<ICoreDispatcher> dispatcher;
    bool closed=false,presented=false;
    boolean visible=false;
    HRESULT InitializeAngle() {return AngleSurface::InitializeAngle(window.Get(),WindowSurfaceAttributes());}
public:
    HRESULT STDMETHODCALLTYPE Initialize(ICoreApplicationView* view) override {
        auto handler=callback<ITypedEventHandler<CoreApplicationView*,IActivatedEventArgs*>,ICoreApplicationView,IActivatedEventArgs>(
            [this](ICoreApplicationView*,IActivatedEventArgs*) -> HRESULT {return window->Activate();});
        EventRegistrationToken token;return view->add_Activated(handler.Get(),&token);
    }
    HRESULT STDMETHODCALLTYPE SetWindow(ICoreWindow* value) override {
        window=value;FLUTTER_WINRT_CHECK(window->get_Dispatcher(&dispatcher));
        auto close=callback<ITypedEventHandler<CoreWindow*,CoreWindowEventArgs*>,ICoreWindow,ICoreWindowEventArgs>(
            [this](ICoreWindow*,ICoreWindowEventArgs*) -> HRESULT {closed=true;return S_OK;});
        auto visibility=callback<ITypedEventHandler<CoreWindow*,VisibilityChangedEventArgs*>,ICoreWindow,IVisibilityChangedEventArgs>(
            [this](ICoreWindow*,IVisibilityChangedEventArgs* args) -> HRESULT {return args->get_Visible(&visible);});
        EventRegistrationToken token;FLUTTER_WINRT_CHECK(window->add_Closed(close.Get(),&token));
        FLUTTER_WINRT_CHECK(window->add_VisibilityChanged(visibility.Get(),&token));
        // Initialization waits for activation/visibility in Run.
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load(HSTRING) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE Uninitialize() override {
        ShutdownAngle();
        dispatcher.Reset();window.Reset();return S_OK;
    }
};

#undef FLUTTER_WINRT_CHECK
}  // namespace flutter::winrt
