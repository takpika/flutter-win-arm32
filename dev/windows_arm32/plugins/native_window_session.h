#pragma once
#include "native_window_api.h"
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"
#include "flutter/shell/platform/windows/uwp/diagnostics.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace flutter::winrt::plugins {

// SDK-host service: one OS subclass for one CoreWindow, with package-owned
// handlers. No package channel names, window policies or payload layouts live
// here. Registration and teardown run on the platform thread.
class NativeWindowSession : public std::enable_shared_from_this<NativeWindowSession> {
 public:
  using Handler = std::function<std::optional<LRESULT>(
      HWND, UINT, WPARAM, LPARAM, const PlatformMessageContext&)>;
  void Register(Handler handler) {
    if (handlers_snapshot_) throw std::logic_error("Register native handlers before Attach");
    if (!handler) throw std::invalid_argument("A native handler is required");
    handlers_.push_back(std::move(handler));
  }

  HRESULT Attach(const PlatformMessageContext& context) {
    if (active_) return E_UNEXPECTED;
    HRESULT result = native_.Open();
    if (FAILED(result)) return result;
    HWND handle = nullptr;
    result = GetCoreWindowHandle(context.window, &handle);
    if (FAILED(result)) return result;
    window_ = handle;
    api_ = context.api;
    context_ = std::make_shared<PlatformMessageContext>(PlatformMessageContext{
        context.window, context.dispatcher, context.owner, context.pixel_ratio,
        context.focused, context.engine, api_});
    handlers_snapshot_ = std::make_shared<const std::vector<Handler>>(std::move(handlers_));
    active_ = shared_from_this();
    SetLastError(ERROR_SUCCESS);
    original_ = reinterpret_cast<NativeWindowApi::Procedure>(native_.set(
        handle, -4, reinterpret_cast<LONG_PTR>(&Procedure)));
    DWORD error = GetLastError();
    if (!original_) {
      active_.reset();
      window_ = nullptr;
      context_.reset();
      handlers_snapshot_.reset();
      return HRESULT_FROM_WIN32(error ? error : ERROR_INVALID_WINDOW_HANDLE);
    }
    attached_.store(true);
    return S_OK;
  }

  HRESULT Detach() {
    attached_.store(false);
    if (context_) {
      // Retained cleanup handlers may free posted payloads after a failed
      // detach, but cannot call a retired engine or dereference host objects.
      context_->window = nullptr;
      context_->dispatcher = nullptr;
      context_->owner = nullptr;
      context_->engine = nullptr;
    }
    HWND handle = window_.load();
    if (!handle) { handlers_snapshot_.reset(); handlers_.clear(); return S_OK; }
    if (native_.get(handle, -4) != reinterpret_cast<LONG_PTR>(&Procedure))
      return HRESULT_FROM_WIN32(ERROR_BUSY);
    SetLastError(ERROR_SUCCESS);
    auto previous = native_.set(handle, -4, reinterpret_cast<LONG_PTR>(original_));
    DWORD error = GetLastError();
    if (!previous && error) return HRESULT_FROM_WIN32(error);
    window_ = nullptr;
    original_ = nullptr;
    handlers_snapshot_.reset();
    context_.reset();
    active_.reset();
    return S_OK;
  }

  HWND window() const { return attached_.load() ? window_.load() : nullptr; }
  const NativeWindowApi& native() const { return native_; }
  LRESULT Forward(HWND window, UINT message, WPARAM wparam, LPARAM lparam) const {
    if (!original_) return 0;  // A reentrant close already destroyed the window.
    return native_.call(original_, window, message, wparam, lparam);
  }

 private:
  static LRESULT CALLBACK Procedure(HWND window, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    auto session = active_;
    if (!session || !session->original_) return 0;
    // Dispatch snapshots keep handlers alive if a reentrant operation closes
    // the host. Destruction notifications always reach the original procedure.
    auto handlers = session->handlers_snapshot_;
    auto context = session->context_;
    auto original = session->original_;
    std::optional<LRESULT> handled;
    if (handlers && context) {
      for (const auto& handler : *handlers) {
        handled = handler(window, message, wparam, lparam, *context);
        if (handled) break;
      }
    }
    if (message != 0x0082 && handled) return *handled;
    auto result = session->native_.call(original, window, message, wparam, lparam);
    if (message == 0x0082) {  // WM_NCDESTROY
      session->attached_.store(false);
      session->window_ = nullptr;
      session->original_ = nullptr;
      if (context) {
        context->engine = nullptr;
        context->window = nullptr;
        context->dispatcher = nullptr;
        context->owner = nullptr;
      }
      session->handlers_snapshot_.reset();
      session->context_.reset();
      active_.reset();
    }
    return result;
  }

  inline static std::shared_ptr<NativeWindowSession> active_;
  NativeWindowApi native_;
  std::atomic<HWND> window_{nullptr};
  std::atomic<bool> attached_{false};
  NativeWindowApi::Procedure original_ = nullptr;
  FlutterEngineProcTable api_{};
  std::shared_ptr<PlatformMessageContext> context_;
  std::vector<Handler> handlers_;
  std::shared_ptr<const std::vector<Handler>> handlers_snapshot_;
};

class NativeWindowLifetime {
 public:
  NativeWindowLifetime() : session(std::make_shared<NativeWindowSession>()) {}
  ~NativeWindowLifetime() {
    HRESULT result = session->Detach();
    if (FAILED(result)) flutter::winrt::Diagnostic("native window detach", result);
  }
  std::shared_ptr<NativeWindowSession> session;
};

inline std::shared_ptr<NativeWindowSession> RegisterNativeWindowSession(
    PlatformMessageHandlers& handlers) {
  auto owner = std::make_shared<NativeWindowLifetime>();
  handlers.RegisterInitializer([owner](const PlatformMessageContext& context) {
    return owner->session->Attach(context);
  });
  return owner->session;
}

}  // namespace flutter::winrt::plugins
