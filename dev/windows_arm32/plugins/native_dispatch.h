// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "flutter/shell/platform/windows/uwp/diagnostics.h"
#include "flutter/shell/platform/windows/uwp/runtime_support.h"
#include "worker_messenger.h"

namespace flutter::winrt::plugins {
class PlatformReplyTask final
    : public Ref<ABI::Windows::UI::Core::IDispatchedHandler> {
 public:
  explicit PlatformReplyTask(WorkerMessenger::Task task)
      : task_(std::move(task)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
    if (id == __uuidof(IAgileObject)) {
      if (!out) return E_POINTER;
      *out = static_cast<ABI::Windows::UI::Core::IDispatchedHandler*>(this);
      AddRef();
      return S_OK;
    }
    return Ref<ABI::Windows::UI::Core::IDispatchedHandler>::QueryInterface(id,
                                                                           out);
  }
  HRESULT STDMETHODCALLTYPE Invoke() override {
    try {
      task_();
      return S_OK;
    } catch (...) {
      Diagnostic("Native plugin platform callback failed", E_FAIL);
      return E_FAIL;
    }
  }

 private:
  WorkerMessenger::Task task_;
};
inline WorkerMessenger::PlatformPost PostToDispatcher(
    ABI::Windows::UI::Core::ICoreDispatcher* dispatcher) {
  ComPtr<ABI::Windows::UI::Core::ICoreDispatcher> owned = dispatcher;
  return [owned](WorkerMessenger::Task task) {
    if (!owned) return false;
    ComPtr<ABI::Windows::UI::Core::IDispatchedHandler> handler;
    handler.Attach(new PlatformReplyTask(std::move(task)));
    ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
    const HRESULT hr =
        owned->RunAsync(ABI::Windows::UI::Core::CoreDispatcherPriority_Normal,
                        handler.Get(), &action);
    if (FAILED(hr)) Diagnostic("Native plugin reply dispatch failed", hr);
    return SUCCEEDED(hr);
  };
}
}  // namespace flutter::winrt::plugins
