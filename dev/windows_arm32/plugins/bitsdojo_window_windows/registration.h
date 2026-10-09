#pragma once
#include "window_binding.h"

namespace flutter::winrt::plugins {
void RegisterBitsdojoWindow(PlatformMessageHandlers& handlers,
                           const std::shared_ptr<NativeWindowSession>& session);
}
