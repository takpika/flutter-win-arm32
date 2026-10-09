#include <flutter/encodable_value.h>
#include <flutter/method_result.h>
#include <flutter/event_sink.h>

// These are the unchanged plugin call forms accepted by the real MSVC probe.
void verify_plugin_calls(flutter::MethodResult<flutter::EncodableValue>& result,
                         flutter::EventSink<flutter::EncodableValue>& sink,
                         flutter::EncodableList values) {
  result.Success(flutter::EncodableList(values));
  sink.Success(flutter::EncodableList(values));
  result.Success(true);
  result.Success(nullptr);
}
