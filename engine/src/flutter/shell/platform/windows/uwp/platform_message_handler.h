#pragma once
#include "flutter/shell/platform/windows/uwp/runtime_support.h"
#include <functional>
#include <vector>
#include <flutter/binary_messenger.h>
#include <windows.applicationmodel.activation.h>

namespace flutter::winrt {
// Host-owned state supplied when dispatching a package's platform message.
// Channel names and package codecs belong to the registered native adapters.
struct PlatformMessageContext {
  ABI::Windows::UI::Core::ICoreWindow* window;
  ABI::Windows::UI::Core::ICoreDispatcher* dispatcher;
  ABI::Windows::ApplicationModel::Core::IFrameworkView* owner;
  double pixel_ratio;
  bool focused;
  FlutterEngine engine;
  const FlutterEngineProcTable& api;
  flutter::BinaryMessenger* messenger = nullptr;
  // Platform-thread dispatch for hosts without a CoreDispatcher/CoreWindow.
  std::function<bool(std::function<void()>)> post_task = {};
};

class PlatformMessageHandlers {
 public:
  using Handler = std::function<bool(const FlutterPlatformMessage*,
                                    const PlatformMessageContext&)>;
  void Register(Handler handler) { handlers_.push_back(std::move(handler)); }
  using ActivationHandler = std::function<void(
      ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*)>;
  void RegisterActivationHandler(ActivationHandler handler) {
    activation_handlers_.push_back(std::move(handler));
  }
  // The OS can activate a view before its engine/plugins are initialized.
  // Adapters own interpretation and any deferred delivery of these arguments.
  void Activated(ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs* args) const {
    for (const auto& handler : activation_handlers_) handler(args);
  }
  using Initializer = std::function<HRESULT(const PlatformMessageContext&)>;
  void RegisterInitializer(Initializer initializer) { initializers_.push_back(std::move(initializer)); }
  HRESULT Initialize(const PlatformMessageContext& context) {
    if (initialized_) return E_UNEXPECTED;
    initialized_ = true;
    for (const auto& initializer : initializers_) {
      HRESULT result = initializer(context);
      if (FAILED(result)) return result;
    }
    return S_OK;
  }
  // Release native subscriptions while the engine and platform thread still exist.
  void Clear() { activation_handlers_.clear(); initializers_.clear(); handlers_.clear(); initialized_ = false; }
  bool Handle(const FlutterPlatformMessage* message,
              const PlatformMessageContext& context) const {
    for (const auto& handler : handlers_) {
      if (handler(message, context)) return true;
    }
    return false;
  }

 private:
  std::vector<Handler> handlers_;
  std::vector<ActivationHandler> activation_handlers_;
  std::vector<Initializer> initializers_;
  bool initialized_ = false;
};
}  // namespace flutter::winrt
