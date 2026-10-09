// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <array>
#include <memory>
#include <vector>

#include "flutter/shell/platform/windows/keyboard_key_channel_handler.h"
#include "flutter/shell/platform/windows/keyboard_key_embedder_handler.h"

// Adapt CoreWindow events and state, retaining the upstream Windows keyboard
// state machine, generated key maps, modern API and legacy system channel.
class PhoneKeyboard {
 public:
  ~PhoneKeyboard() { Close(); }

  HRESULT Initialize(ICoreWindow* window,
                     ICoreDispatcher* dispatcher,
                     flutter::BinaryMessenger* messenger,
                     FlutterEngine engine,
                     const FlutterEngineProcTable& api) {
    if (!window || !dispatcher || !messenger || !engine || !api.SendKeyEvent)
      return E_INVALIDARG;
    window_ = window;
    HRESULT projection =
        dispatcher->QueryInterface(IID_PPV_ARGS(&accelerator_));
    if (FAILED(projection))
      return projection;
    states_ = ReadStates();
    auto state = [this](int key) -> SHORT {
      for (size_t i = 0; i < state_keys_.size(); ++i)
        if (state_keys_[i] == key)
          return states_[i];
      return 0;
    };
    handler_ = std::make_unique<flutter::KeyboardKeyHandler>(messenger);
    handler_->AddDelegate(std::make_unique<flutter::KeyboardKeyEmbedderHandler>(
        [engine, api](const FlutterKeyEvent& event,
                      FlutterKeyEventCallback callback, void* data) {
          auto result = api.SendKeyEvent(engine, &event, callback, data);
          if (result != kSuccess && callback)
            callback(false, data);
        },
        state, CriticalScanCode));
    handler_->AddDelegate(
        std::make_unique<flutter::KeyboardKeyChannelHandler>(messenger, state));
    handler_->InitKeyboardChannel();
    auto key =
        callback<ITypedEventHandler<CoreDispatcher*, AcceleratorKeyEventArgs*>,
                 ICoreDispatcher, IAcceleratorKeyEventArgs>(
            [this](ICoreDispatcher*,
                   IAcceleratorKeyEventArgs* args) -> HRESULT {
              CoreAcceleratorKeyEventType type{};
              ABI::Windows::System::VirtualKey key{};
              CorePhysicalKeyStatus status{};
              HRESULT hr = args->get_EventType(&type);
              if (SUCCEEDED(hr))
                hr = args->get_VirtualKey(&key);
              if (SUCCEEDED(hr))
                hr = args->get_KeyStatus(&status);
              if (FAILED(hr))
                return hr;
              int action = 0;
              switch (type) {
                case CoreAcceleratorKeyEventType_KeyDown:
                  action = WM_KEYDOWN;
                  break;
                case CoreAcceleratorKeyEventType_KeyUp:
                  action = WM_KEYUP;
                  break;
                case CoreAcceleratorKeyEventType_SystemKeyDown:
                  action = WM_SYSKEYDOWN;
                  break;
                case CoreAcceleratorKeyEventType_SystemKeyUp:
                  action = WM_SYSKEYUP;
                  break;
                default:
                  return S_OK;
              }
              int virtual_key = static_cast<int>(key);
              if (virtual_key == VK_SHIFT)
                virtual_key = status.ScanCode == 0x36 ? VK_RSHIFT : VK_LSHIFT;
              else if (virtual_key == VK_CONTROL)
                virtual_key = status.IsExtendedKey ? VK_RCONTROL : VK_LCONTROL;
              else if (virtual_key == VK_MENU)
                virtual_key = status.IsExtendedKey ? VK_RMENU : VK_LMENU;
              // CharacterReceived for the previous key has now been dispatched.
              // Preserve native order before starting the next key event.
              Flush();
              pending_.push_back(
                  {virtual_key, status, action, 0, ReadStates()});
              // CoreText owns IME editing. Do not suppress its native event
              // stream.
              return S_OK;
            });
    HRESULT hr =
        accelerator_->add_AcceleratorKeyActivated(key.Get(), &key_token_);
    if (FAILED(hr))
      return hr;
    key_registered_ = true;
    auto character =
        callback<ITypedEventHandler<CoreWindow*, CharacterReceivedEventArgs*>,
                 ICoreWindow, ICharacterReceivedEventArgs>(
            [this](ICoreWindow*, ICharacterReceivedEventArgs* args) -> HRESULT {
              UINT32 value = 0;
              CorePhysicalKeyStatus status{};
              HRESULT hr = args->get_KeyCode(&value);
              if (SUCCEEDED(hr))
                hr = args->get_KeyStatus(&status);
              if (FAILED(hr))
                return hr;
              for (auto i = pending_.rbegin(); i != pending_.rend(); ++i) {
                if ((i->action == WM_KEYDOWN || i->action == WM_SYSKEYDOWN) &&
                    i->status.ScanCode == status.ScanCode) {
                  if (value >= 0xdc00 && value <= 0xdfff &&
                      i->character >= 0xd800 && i->character <= 0xdbff)
                    i->character = 0x10000 + ((i->character - 0xd800) << 10) +
                                   value - 0xdc00;
                  else
                    i->character = value;
                  break;
                }
              }
              return S_OK;
            });
    hr = window_->add_CharacterReceived(character.Get(), &character_token_);
    if (FAILED(hr)) {
      Close();
      return hr;
    }
    character_registered_ = true;
    Diagnostic("Phone hardware keyboard registration", S_OK);
    return S_OK;
  }

  void Flush() {
    auto events = std::move(pending_);
    pending_.clear();
    for (const auto& event : events) {
      states_ = event.states;
      char32_t character = event.character;
      if (character > 0x10ffff || (character >= 0xd800 && character <= 0xdfff))
        character = 0;
      handler_->KeyboardHook(event.key, event.status.ScanCode, event.action,
                             character, event.status.IsExtendedKey,
                             event.status.WasKeyDown, [](bool) {});
      if (event.key == VK_ESCAPE || event.key == VK_TAB) {
        char line[100];
        snprintf(line, sizeof(line),
                 "Phone keyboard navigation key=%d scan=%u action=%d",
                 event.key, event.status.ScanCode, event.action);
        Diagnostic(line, S_OK);
      }
    }
  }

  void Close() {
    if (key_registered_)
      accelerator_->remove_AcceleratorKeyActivated(key_token_);
    if (character_registered_)
      window_->remove_CharacterReceived(character_token_);
    key_registered_ = character_registered_ = false;
    pending_.clear();
  }
  // Keep response batons alive until engine shutdown and messenger completion.
  void EngineDidShutdown() { handler_.reset(); }

 private:
  // Only the upstream handler's initial modifier/lock states need a scan code
  // before any event is seen. Other keys use CorePhysicalKeyStatus.ScanCode.
  static SHORT CriticalScanCode(UINT key, bool) {
    switch (key) {
      case VK_LSHIFT:
        return 0x2a;
      case VK_RSHIFT:
        return 0x36;
      case VK_LCONTROL:
      case VK_RCONTROL:
        return 0x1d;
      case VK_LMENU:
      case VK_RMENU:
        return 0x38;
      case VK_LWIN:
        return 0x5b;
      case VK_RWIN:
        return 0x5c;
      case VK_CAPITAL:
        return 0x3a;
      case VK_SCROLL:
        return 0x46;
      case VK_NUMLOCK:
        return 0x45;
      default:
        return 0;
    }
  }
  inline static constexpr std::array<int, 14> state_keys_ = {
      VK_SHIFT,    VK_LSHIFT,  VK_RSHIFT, VK_CONTROL, VK_LCONTROL,
      VK_RCONTROL, VK_MENU,    VK_LMENU,  VK_RMENU,   VK_LWIN,
      VK_RWIN,     VK_CAPITAL, VK_SCROLL, VK_NUMLOCK};
  using States = std::array<SHORT, state_keys_.size()>;
  States ReadStates() const {
    States result{};
    for (size_t i = 0; i < state_keys_.size(); ++i) {
      CoreVirtualKeyStates value{};
      if (SUCCEEDED(window_->GetKeyState(
              static_cast<ABI::Windows::System::VirtualKey>(state_keys_[i]),
              &value)))
        result[i] = static_cast<SHORT>(
            ((value & CoreVirtualKeyStates_Down) ? -128 : 0) |
            ((value & CoreVirtualKeyStates_Locked) ? 1 : 0));
    }
    return result;
  }
  struct Key {
    int key;
    CorePhysicalKeyStatus status;
    int action;
    char32_t character;
    States states;
  };
  ComPtr<ICoreWindow> window_;
  ComPtr<ICoreAcceleratorKeys> accelerator_;
  std::unique_ptr<flutter::KeyboardKeyHandler> handler_;
  std::vector<Key> pending_;
  States states_{};
  EventRegistrationToken key_token_{}, character_token_{};
  bool key_registered_ = false, character_registered_ = false;
};
