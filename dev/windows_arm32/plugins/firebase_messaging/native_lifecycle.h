// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "../native_dispatch.h"

#include <atomic>

namespace flutter::winrt::plugins::firebase_messaging {
// OS wake notification only. Execution eligibility remains in the shared SDK.
class NativeLifecycle {
 public:
  ~NativeLifecycle() {
    state_->active = false;
    if (visible_registered_)
      window_->remove_VisibilityChanged(visible_token_);
    if (resume_registered_)
      application_->remove_Resuming(resume_token_);
  }
  HRESULT Attach(ABI::Windows::UI::Core::ICoreWindow* window,
                 WorkerMessenger::PlatformPost post, std::function<void()> wake) {
    using namespace ABI::Windows::Foundation;
    using namespace ABI::Windows::UI::Core;
    if (!window || !post || !wake) return E_INVALIDARG;
    window_ = window;
    state_->post = std::move(post);
    state_->wake = std::move(wake);
    std::weak_ptr<State> weak = state_;
    auto visible = callback<
        ITypedEventHandler<CoreWindow*, VisibilityChangedEventArgs*>,
        ICoreWindow, IVisibilityChangedEventArgs>(
        [weak](ICoreWindow*, IVisibilityChangedEventArgs* args) -> HRESULT {
          boolean shown = false;
          const HRESULT hr = args->get_Visible(&shown);
          if (SUCCEEDED(hr) && shown) Queue(weak);
          return hr;
        });
    HRESULT hr = window_->add_VisibilityChanged(visible.Get(), &visible_token_);
    if (FAILED(hr)) return hr;
    visible_registered_ = true;
    HSTRING name = nullptr;
    constexpr wchar_t cls[] =
        L"Windows.ApplicationModel.Core.CoreApplication";
    hr = WindowsCreateString(cls, ARRAYSIZE(cls) - 1, &name);
    if (SUCCEEDED(hr))
      hr = RoGetActivationFactory(name, IID_PPV_ARGS(&application_));
    WindowsDeleteString(name);
    if (FAILED(hr)) return hr;
    auto resumed = callback<IEventHandler<IInspectable*>, IInspectable,
                            IInspectable>(
        [weak](IInspectable*, IInspectable*) -> HRESULT {
          Queue(weak);
          return S_OK;
        });
    hr = application_->add_Resuming(resumed.Get(), &resume_token_);
    resume_registered_ = SUCCEEDED(hr);
    return hr;
  }

 private:
  struct State {
    std::atomic<bool> active{true}, queued{false};
    WorkerMessenger::PlatformPost post;
    std::function<void()> wake;
  };
  static void Queue(std::weak_ptr<State> weak) {
    auto state = weak.lock();
    if (!state || !state->active || state->queued.exchange(true)) return;
    // Let every OS event subscriber (including the SDK's visibility observer)
    // finish before asking the plugin worker to drain. Never fabricate state.
    if (!state->post([weak] {
          auto state = weak.lock();
          if (!state || !state->active) return;
          state->queued = false;
          state->wake();
        }))
      state->queued = false;
  }
  std::shared_ptr<State> state_ = std::make_shared<State>();
  ComPtr<ABI::Windows::UI::Core::ICoreWindow> window_;
  ComPtr<ABI::Windows::ApplicationModel::Core::ICoreApplication> application_;
  EventRegistrationToken visible_token_{}, resume_token_{};
  bool visible_registered_ = false, resume_registered_ = false;
};
}  // namespace flutter::winrt::plugins::firebase_messaging
