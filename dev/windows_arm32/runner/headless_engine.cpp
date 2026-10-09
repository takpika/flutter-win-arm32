// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "headless_engine.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>

#include "package_paths.h"
#include "flutter/shell/platform/windows/uwp/angle_surface.h"
#include "flutter/shell/platform/windows/uwp/binary_messenger.h"
#include "flutter/shell/platform/windows/uwp/services/application_environment.h"
#include "flutter/shell/platform/windows/uwp/services/phone-locales.h"
#include "flutter/shell/platform/windows/uwp/services/phone-settings.h"

void RegisterUwpPlugins(flutter::winrt::PlatformMessageHandlers& handlers);

namespace flutter::winrt::host {
class HeadlessEngine::State : private AngleSurface {
 public:
  struct Queue {
    std::mutex mutex;
    std::condition_variable wake;
    bool active = true;
    std::deque<std::function<void()>> native;
    std::multimap<uint64_t, FlutterTask> dart;
  };
  std::shared_ptr<Queue> queue = std::make_shared<Queue>();
  EmbedderMessenger messenger;
  FlutterEngineProcTable api{};
  FlutterEngine engine = nullptr;
  FlutterEngineAOTData aot = nullptr;
  HMODULE library = nullptr;
  DWORD thread = 0;
  Entrypoint entry;
  std::string image, assets, icu, callback_cache;
  PlatformMessageHandlers plugins;
  FlutterTaskRunnerDescription runner{};
  FlutterCustomTaskRunners runners{};
  ComPtr<ABI::Windows::UI::ViewManagement::IUISettings3> settings;
  EventRegistrationToken settings_token{};
  bool settings_registered = false;
  std::shared_ptr<LONG> settings_pending = std::make_shared<LONG>(0);

  ~State() {
    if (settings_registered) settings->remove_ColorValuesChanged(settings_token);
    {
      std::lock_guard<std::mutex> lock(queue->mutex);
      queue->active = false;
    }
    messenger.Stop();
    plugins.Clear();
    if (engine) api.Shutdown(engine);
    engine = nullptr;
    messenger.EngineDidShutdown();
    if (aot) api.CollectAOTData(aot);
    aot = nullptr;
    ShutdownAngle();
    if (library) FreeLibrary(library);
    queue->native.clear();
    queue->dart.clear();
  }
  Post Poster() {
    std::weak_ptr<Queue> weak = queue;
    return [weak](std::function<void()> task) {
      auto current = weak.lock();
      if (!current) return false;
      {
        std::lock_guard<std::mutex> lock(current->mutex);
        if (!current->active) return false;
        current->native.push_back(std::move(task));
      }
      current->wake.notify_one();
      return true;
    };
  }
  PlatformMessageContext Context() {
    return {nullptr, nullptr, nullptr, 1.0, false, engine, api,
            &messenger, Poster()};
  }
  static State* Self(void* data) { return static_cast<State*>(data); }
  static bool Current(void* data) {
    auto* self = Self(data);
    return self->makeCurrent(self->display, self->surface, self->surface,
                             self->context) == EGL_TRUE;
  }
  static bool Clear(void* data) {
    auto* self = Self(data);
    return self->makeCurrent(self->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                             EGL_NO_CONTEXT) == EGL_TRUE;
  }
  static bool Resource(void* data) {
    auto* self = Self(data);
    return self->makeCurrent(self->display, self->resourceSurface,
                             self->resourceSurface, self->resourceContext) == EGL_TRUE;
  }
  static bool Present(void* data) {
    auto* self = Self(data);
    return self->swapBuffers(self->display, self->surface) == EGL_TRUE;
  }
  static uint32_t Fbo(void*) { return 0; }
  static void* Resolve(void* data, const char* name) {
    auto* self = Self(data);
    if (auto address = GetProcAddress(self->gles, name))
      return reinterpret_cast<void*>(address);
    auto resolver = reinterpret_cast<PFNEGLGETPROCADDRESSPROC>(
        GetProcAddress(self->egl, "eglGetProcAddress"));
    return resolver ? reinterpret_cast<void*>(resolver(name)) : nullptr;
  }
  static bool OnThread(void* data) {
    return GetCurrentThreadId() == Self(data)->thread;
  }
  static void Log(const char* tag, const char* message, void*) {
    char line[4000]{};
    snprintf(line, sizeof(line), "Headless Flutter %s: %s",
             tag ? tag : "", message ? message : "");
    Diagnostic(line, S_OK);
  }
  static void PostDart(FlutterTask task, uint64_t target, void* data) {
    const auto current = Self(data)->queue;
    {
      std::lock_guard<std::mutex> lock(current->mutex);
      if (!current->active) return;
      current->dart.emplace(target, task);
    }
    current->wake.notify_one();
  }
  static void PlatformMessage(const FlutterPlatformMessage* message, void* data) {
    auto* self = Self(data);
    if (self->messenger.Handle(message)) return;
    if (self->plugins.Handle(message, self->Context())) return;
    if (message->response_handle)
      self->api.SendPlatformMessageResponse(self->engine, message->response_handle,
                                            nullptr, 0);
  }
  HRESULT Start(Entrypoint value) {
    if (library || engine || value.library_uri.empty() || value.function.empty())
      return E_INVALIDARG;
    thread = GetCurrentThreadId();
    entry = std::move(value);
    std::wstring installed, local;
    HRESULT hr = InstalledDirectory(installed);
    if (SUCCEEDED(hr)) hr = LocalDirectory(local);
    if (SUCCEEDED(hr)) hr = FlutterWinRTSetApplicationEnvironment();
    if (FAILED(hr)) return hr;
    const auto directory = Utf8Path(installed);
    callback_cache = Utf8Path(local);
    if (directory.empty() || callback_cache.empty()) return E_INVALIDARG;
    image = directory + "\\data\\app.so";
    assets = directory + "\\data\\flutter_assets";
    icu = directory + "\\data\\icudtl.dat";
    library = LoadPackagedLibrary(L"flutter_engine.dll", 0);
    if (!library) return HRESULT_FROM_WIN32(GetLastError());
    auto get_api = reinterpret_cast<FlutterEngineResult (*)(FlutterEngineProcTable*)>(
        GetProcAddress(library, "FlutterEngineGetProcAddresses"));
    api.struct_size = sizeof(api);
    if (!get_api || get_api(&api) != kSuccess || !api.RunsAOTCompiledDartCode())
      return E_NOINTERFACE;
    hr = InitializeAngle(nullptr, nullptr);
    if (SUCCEEDED(hr)) hr = InitializeResourceContext();
    if (FAILED(hr)) return hr;
    FlutterEngineAOTDataSource source{};
    source.type = kFlutterEngineAOTDataSourceTypeElfPath;
    source.elf_path = image.c_str();
    if (api.CreateAOTData(&source, &aot) != kSuccess) return E_FAIL;
    runner.struct_size = sizeof(runner);
    runner.user_data = this;
    runner.runs_task_on_current_thread_callback = OnThread;
    runner.post_task_callback = PostDart;
    runner.identifier = reinterpret_cast<size_t>(this);
    runners.struct_size = sizeof(runners);
    runners.platform_task_runner = &runner;
    FlutterRendererConfig renderer{};
    renderer.type = kOpenGL;
    renderer.open_gl.struct_size = sizeof(renderer.open_gl);
    renderer.open_gl.make_current = Current;
    renderer.open_gl.clear_current = Clear;
    renderer.open_gl.present = Present;
    renderer.open_gl.fbo_callback = Fbo;
    renderer.open_gl.make_resource_current = Resource;
    renderer.open_gl.gl_proc_resolver = Resolve;
    renderer.open_gl.fbo_reset_after_present = true;
    FlutterProjectArgs project{};
    project.struct_size = sizeof(project);
    project.assets_path = assets.c_str();
    project.icu_data_path = icu.c_str();
    project.aot_data = aot;
    project.custom_task_runners = &runners;
    project.platform_message_callback = PlatformMessage;
    project.log_message_callback = Log;
    project.custom_dart_entrypoint = entry.function.c_str();
    project.custom_dart_entrypoint_library = entry.library_uri.c_str();
    project.dart_callback_cache_path = callback_cache.c_str();
    project.shutdown_dart_vm_when_done = true;
    Clear(this);
    if (api.Initialize(FLUTTER_ENGINE_VERSION, &renderer, &project, this, &engine) != kSuccess)
      return E_FAIL;
    messenger.Attach(engine, api);
    RegisterUwpPlugins(plugins);
    hr = plugins.Initialize(Context());
    if (FAILED(hr)) return hr;
    if (api.RunInitialized(engine) != kSuccess) return E_FAIL;
    Diagnostic("Headless Flutter retained entrypoint running", S_OK);
    hr = PhoneUpdateLocales(engine, api);
    if (SUCCEEDED(hr)) hr = PhoneCreateSettings(&settings);
    if (FAILED(hr)) return hr;
    using namespace ABI::Windows::UI::ViewManagement;
    std::weak_ptr<Queue> weak = queue;
    auto changed = callback<ITypedEventHandler<UISettings*, IInspectable*>, IUISettings, IInspectable>(
        [pending = settings_pending, weak](IUISettings*, IInspectable*) -> HRESULT {
          InterlockedExchange(pending.get(), 1);
          if (auto current = weak.lock()) current->wake.notify_one();
          return S_OK;
        });
    hr = settings->add_ColorValuesChanged(changed.Get(), &settings_token);
    if (FAILED(hr)) return hr;
    settings_registered = true;
    hr = PhoneSendSettings(settings.Get(), engine, api);
    if (FAILED(hr)) return hr;
    FlutterWindowMetricsEvent metrics{};
    metrics.struct_size = sizeof(metrics);
    metrics.width = metrics.height = 1;
    metrics.pixel_ratio = 1.0;
    return api.SendWindowMetricsEvent(engine, &metrics) == kSuccess ? S_OK : E_FAIL;
  }
  bool PumpUntil(const std::function<bool()>& complete,
                 std::chrono::steady_clock::time_point deadline,
                 const std::function<bool()>& canceled) {
    if (!engine || !OnThread(this)) return false;
    while (!complete()) {
      if (canceled() || std::chrono::steady_clock::now() >= deadline) return false;
      if (InterlockedExchange(settings_pending.get(), 0) &&
          FAILED(PhoneSendSettings(settings.Get(), engine, api))) return false;
      std::function<void()> native;
      FlutterTask task{};
      bool have_task = false;
      {
        std::unique_lock<std::mutex> lock(queue->mutex);
        if (!queue->active) return false;
        const auto now = api.GetCurrentTime();
        if (!queue->native.empty()) {
          native = std::move(queue->native.front());
          queue->native.pop_front();
        } else if (!queue->dart.empty() && queue->dart.begin()->first <= now) {
          task = queue->dart.begin()->second;
          queue->dart.erase(queue->dart.begin());
          have_task = true;
        } else {
          const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
              deadline - std::chrono::steady_clock::now());
          auto wait = std::min(remaining, std::chrono::nanoseconds(100000000));
          if (!queue->dart.empty()) {
            const auto delta = std::min<uint64_t>(queue->dart.begin()->first - now, 100000000);
            wait = std::min(wait, std::chrono::nanoseconds(delta));
          }
          if (wait.count() > 0) queue->wake.wait_for(lock, wait);
          continue;
        }
      }
      try {
        if (native) native();
        if (have_task && api.RunTask(engine, &task) != kSuccess) return false;
      } catch (...) {
        Diagnostic("Headless platform task failed", E_FAIL);
        return false;
      }
    }
    return !canceled();
  }
};
HeadlessEngine::HeadlessEngine() : state_(std::make_unique<State>()) {}
HeadlessEngine::~HeadlessEngine() = default;
HRESULT HeadlessEngine::Start(Entrypoint entrypoint) { return state_->Start(std::move(entrypoint)); }
BinaryMessenger& HeadlessEngine::messenger() { return state_->messenger; }
bool HeadlessEngine::PumpUntil(const std::function<bool()>& complete,
                               std::chrono::steady_clock::time_point deadline,
                               const std::function<bool()>& canceled) {
  return state_->PumpUntil(complete, deadline, canceled);
}
}  // namespace flutter::winrt::host
