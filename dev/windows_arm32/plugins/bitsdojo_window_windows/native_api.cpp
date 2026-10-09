#include "registration.h"
#include "bitsdojo_window.h"
#include "bitsdojo_window_common.h"
#include <flutter/standard_method_codec.h>
#include <mutex>

namespace {
using Binding = flutter::winrt::plugins::bitsdojo::WindowBinding;
std::mutex binding_mutex;
std::weak_ptr<Binding> binding;
unsigned configuration = 0;

std::shared_ptr<Binding> CurrentBinding() {
  std::lock_guard<std::mutex> guard(binding_mutex);
  return binding.lock();
}
}

// These definitions back the original package's unmodified API table source.
// Its Dart implementation and public/private struct layouts remain upstream.
namespace bitsdojo_window {
bool isBitsdojoWindowLoaded() {
  auto current = CurrentBinding();
  return current && current->window();
}
HWND getAppWindow() {
  auto current = CurrentBinding();
  return current ? current->window() : nullptr;
}
void setWindowCanBeShown(bool allowed) {
  if (auto current = CurrentBinding()) current->visibility.AllowDisplay(allowed);
}
void setMinSize(int width, int height) {
  if (auto current = CurrentBinding()) current->constraints.SetMinimum(width, height);
}
void setMaxSize(int width, int height) {
  if (auto current = CurrentBinding()) current->constraints.SetMaximum(width, height);
}
void setWindowCutOnMaximize(int value) {
  if (auto current = CurrentBinding()) current->SetCutOnMaximize(value);
}
bool isDPIAware() {
  auto current = CurrentBinding();
  return current && current->dpi_aware();
}
bool dragAppWindow() {
  auto current = CurrentBinding();
  return current && current->Drag();
}
}

BDW_EXPORT int bitsdojo_window_configure(unsigned flags) {
  std::lock_guard<std::mutex> guard(binding_mutex);
  configuration = flags;
  if (auto current = binding.lock()) current->Configure(flags);
  return 1;
}

namespace flutter::winrt::plugins {
void RegisterBitsdojoWindow(PlatformMessageHandlers& handlers,
                           const std::shared_ptr<NativeWindowSession>& session) {
  auto current = std::make_shared<Binding>(session);
  session->Register([current](HWND window, UINT message, WPARAM wparam,
                             LPARAM lparam, const PlatformMessageContext& context) {
    return current->Handle(window, message, wparam, lparam, context);
  });
  {
    std::lock_guard<std::mutex> guard(binding_mutex);
    current->Configure(configuration);
    binding = current;
  }
  handlers.Register([current](const FlutterPlatformMessage* message,
                              const PlatformMessageContext& context) {
    if (strcmp(message->channel, "bitsdojo/window") != 0) return false;
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    auto call = codec.DecodeMethodCall(message->message, message->message_size);
    std::unique_ptr<std::vector<uint8_t>> response;
    if (!call) {
      response = codec.EncodeErrorEnvelope("INVALID_ARGUMENTS", "Invalid method call");
    } else if (call->method_name() != "dragAppWindow") {
      context.api.SendPlatformMessageResponse(context.engine, message->response_handle,
                                               nullptr, 0);
      return true;
    } else if (current->Drag()) {
      response = codec.EncodeSuccessEnvelope();
    } else {
      response = codec.EncodeErrorEnvelope("ERROR_DRAG_APP_WINDOW_FAILED");
    }
    context.api.SendPlatformMessageResponse(context.engine, message->response_handle,
                                             response->data(), response->size());
    return true;
  });
}
}
