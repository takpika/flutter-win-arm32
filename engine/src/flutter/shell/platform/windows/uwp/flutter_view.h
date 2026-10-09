#pragma once
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"
namespace flutter::winrt {
HRESULT CreateFlutterView(PlatformMessageHandlers handlers,
                         ABI::Windows::ApplicationModel::Core::IFrameworkView** view);
}
