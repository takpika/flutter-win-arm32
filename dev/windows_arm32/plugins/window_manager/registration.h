#pragma once
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"
#include "window_manager.h"
#include "dev/windows_arm32/plugins/native_window_session.h"

namespace flutter::winrt::plugins {
inline void RegisterWindowPlugins(PlatformMessageHandlers& handlers,
                                  const std::shared_ptr<NativeWindowSession>& session) {
  auto plugin = std::make_shared<FlutterWinRTWindowPlugins>();
  session->Register([plugin](HWND, UINT message, WPARAM, LPARAM,
                            const PlatformMessageContext& context) {
    return plugin->HandleNative(message, context);
  });
  handlers.Register([plugin](const FlutterPlatformMessage* message,
                             const PlatformMessageContext& context) {
    return plugin->Handle(message, context.window, context.pixel_ratio,
                          context.focused, context.engine, context.api);
  });
}
}  // namespace flutter::winrt::plugins
