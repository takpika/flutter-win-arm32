#pragma once
#include "dev/windows_arm32/plugins/native_window_session.h"
#include "window_constraints.h"
#include "window_messages.h"
#include "window_visibility.h"
#include <atomic>
#include <cmath>

namespace flutter::winrt::plugins::bitsdojo {

// Package-owned policies and FFI actions, dispatched by the SDK host's one
// NativeWindowSession. This class never installs a separate OS subclass.
class WindowBinding {
 public:
  explicit WindowBinding(std::shared_ptr<NativeWindowSession> session)
      : session_(std::move(session)), native_(session_->native()) {}
  HWND window() const { return session_->window(); }
  bool dpi_aware() const { return native_.get_dpi != nullptr; }
  void SetCutOnMaximize(int value) { cut_on_maximize_.store(value); }
  void Configure(unsigned flags) {
    custom_frame_.store((flags & 1) != 0);
    visibility.Configure(flags);
  }
  bool Drag() {
    HWND handle = window();
    return handle && native_.send(handle, WM_BDW_ACTION, kDragAction, 0) == 1;
  }
  WindowConstraints constraints;
  WindowVisibility visibility;

  std::optional<LRESULT> Handle(HWND window, UINT message, WPARAM wparam,
                               LPARAM lparam, const PlatformMessageContext& context) {
    if (!context.engine) {
      if (ReleaseWindowActionPayload(message, wparam, lparam)) return 0;
      return std::nullopt;
    }
    if (message == WM_BDW_ACTION && wparam == kDragAction) {
      if (!context.window || !context.engine) return 0;
      HRESULT result = context.window->ReleasePointerCapture();
      if (FAILED(result)) {
        Diagnostic("bitsdojo release pointer capture", result);
        return 0;
      }
      auto previous_moves = move_requests_;
      native_.send(window, 0x0112, 0xf012, 0);
      // A nonmovable OS window must not report a successful drag merely
      // because SendMessage returned zero. Observe actual move-loop entry.
      return move_requests_ != previous_moves ? 1 : 0;
    }
    if (message == 0x0231) ++move_requests_;  // WM_ENTERSIZEMOVE
    if (message == 0x0083 && custom_frame_.load()) {
      return CalculateClient(window, wparam, lparam);
    }
    if (message == 0x0024) {
      constraints.Apply(
          reinterpret_cast<WindowConstraints::Information*>(lparam),
          native_.get_dpi ? native_.get_dpi(window) : 96);
      return 0;
    }
    if (message == 0x0046) {
      visibility.Apply(
          reinterpret_cast<WindowVisibility::Position*>(lparam));
    }
    // The retirement branch above releases queued allocations without engine
    // calls. Live actions retain the original package's message contract.
    if (HandleWindowAction(native_, window, message, wparam, lparam,
                           context.engine, context.api)) return 0;
    return std::nullopt;
  }

 private:
  LRESULT CalculateClient(HWND window, WPARAM wparam, LPARAM lparam) {
    if (!wparam) return 0;
    struct ClientParameters {
      RECT rectangles[3];
      WindowVisibility::Position* position;
    };
    auto* parameters = reinterpret_cast<ClientParameters*>(lparam);
    NativeWindowApi::MonitorInformation monitor{};
    monitor.size = sizeof(monitor);
    HANDLE handle = native_.monitor_from_window(window, 2);
    if (!parameters || !handle || !native_.get_monitor_info(handle, &monitor)) {
      Diagnostic("bitsdojo custom frame monitor", E_FAIL);
      return session_->Forward(window, 0x0083, wparam, lparam);
    }
    // Preserve the original package's work-area clipping and default-procedure
    // ordering; do not substitute Flutter's render bounds for OS window bounds.
    const RECT& work = monitor.work;
    auto* position = parameters->position;
    if (position && position->x < work.left && position->y < work.top &&
        position->cx > work.right - work.left &&
        position->cy > work.bottom - work.top) {
      position->x = work.left;
      position->y = work.top;
      position->cx = work.right - work.left;
      position->cy = work.bottom - work.top;
    }
    for (auto& rectangle : parameters->rectangles) {
      if (rectangle.left < work.left && rectangle.top < work.top &&
          rectangle.right > work.right && rectangle.bottom > work.bottom)
        rectangle = work;
    }
    RECT initial = parameters->rectangles[0];
    LRESULT result = session_->Forward(window, 0x0083, wparam, lparam);
    if (result) return result;
    parameters->rectangles[0] = initial;
    if (native_.get(window, -16) & 0x01000000) {
      double scale = (native_.get_dpi ? native_.get_dpi(window) : 96) / 96.0;
      int sides = static_cast<int>(std::ceil(scale * cut_on_maximize_.load()));
      parameters->rectangles[0].top -= sides + static_cast<int>(std::ceil(scale)) + 1;
      parameters->rectangles[0].left -= sides;
      parameters->rectangles[0].right += sides;
      parameters->rectangles[0].bottom += sides;
    } else {
      --parameters->rectangles[0].top;
    }
    return 0;
  }

  static constexpr WPARAM kDragAction = 4;
  std::atomic<int> cut_on_maximize_{0};
  std::atomic<bool> custom_frame_{false};
  std::shared_ptr<NativeWindowSession> session_;
  const NativeWindowApi& native_;
  unsigned move_requests_ = 0;
};

}  // namespace flutter::winrt::plugins::bitsdojo
