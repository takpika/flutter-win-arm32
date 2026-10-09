#pragma once
#include "dev/windows_arm32/plugins/native_window_session.h"

namespace flutter::winrt::plugins {
void RegisterWindowsTaskbar(PlatformMessageHandlers& handlers,
                           const std::shared_ptr<NativeWindowSession>& session);
}
