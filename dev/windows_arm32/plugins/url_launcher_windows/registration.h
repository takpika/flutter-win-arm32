#pragma once
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"
#include "url_launcher.h"

namespace flutter::winrt::plugins {
inline void RegisterUrlLauncher(PlatformMessageHandlers& handlers) {
  auto plugin = std::make_shared<FlutterWinRTUrlLauncher>();
  handlers.Register([plugin](const FlutterPlatformMessage* message,
                             const PlatformMessageContext& context) {
    return plugin->Handle(message, context.dispatcher, context.owner,
                          context.engine, context.api);
  });
}
}  // namespace flutter::winrt::plugins
