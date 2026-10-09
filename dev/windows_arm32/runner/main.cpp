#include "flutter/shell/platform/windows/uwp/flutter_view.h"
#include "flutter/shell/platform/windows/uwp/diagnostics.h"

// Defined by SDK-generated build glue from the application's dependencies.
void RegisterUwpPlugins(flutter::winrt::PlatformMessageHandlers& handlers);
namespace {
class FlutterSource : public flutter::winrt::Ref<ABI::Windows::ApplicationModel::Core::IFrameworkViewSource> {
 public:
  HRESULT STDMETHODCALLTYPE CreateView(ABI::Windows::ApplicationModel::Core::IFrameworkView** result) override {
    flutter::winrt::PlatformMessageHandlers handlers;
    RegisterUwpPlugins(handlers);
    return flutter::winrt::CreateFlutterView(std::move(handlers), result);
  }
};
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  using namespace flutter::winrt;
  using namespace ABI::Windows::ApplicationModel::Core;
  HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
  if (FAILED(hr)) return 1;
  HSTRING name = nullptr;
  hr = WindowsCreateString(L"Windows.ApplicationModel.Core.CoreApplication", 45, &name);
  ComPtr<ICoreApplication> app;
  if (SUCCEEDED(hr)) hr = RoGetActivationFactory(name, IID_PPV_ARGS(&app));
  WindowsDeleteString(name);
  if (SUCCEEDED(hr)) {
    ComPtr<IFrameworkViewSource> source;
    source.Attach(new FlutterSource());
    hr = app->Run(source.Get());
  }
  Diagnostic("CoreApplication.Run", hr);
  app.Reset();
  RoUninitialize();
  return FAILED(hr) ? 2 : 0;
}
