#pragma once
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"
#include <flutter/standard_method_codec.h>
#include <windows.networking.connectivity.h>
#include <ipifcons.h>
#include <atomic>
#include <set>

namespace flutter::winrt::plugins {
namespace connectivity {
using namespace ABI::Windows::Networking::Connectivity;
using Value = flutter::EncodableValue;
using List = flutter::EncodableList;
inline constexpr char kMethods[] = "dev.fluttercommunity.plus/connectivity";
inline constexpr char kEvents[] = "dev.fluttercommunity.plus/connectivity_status";

inline HRESULT Factory(INetworkInformationStatics** result) {
  const wchar_t* name = L"Windows.Networking.Connectivity.NetworkInformation";
  HSTRING text = nullptr;
  HRESULT hr = WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), &text);
  if (SUCCEEDED(hr)) hr = RoGetActivationFactory(text, __uuidof(INetworkInformationStatics),
                                               reinterpret_cast<void**>(result));
  WindowsDeleteString(text);
  return hr;
}

inline HRESULT Read(INetworkInformationStatics* network, Value* result) {
  using Profiles = ABI::Windows::Foundation::Collections::IVectorView<ConnectionProfile*>;
  ComPtr<Profiles> profiles;
  HRESULT hr = network->GetConnectionProfiles(&profiles);
  if (FAILED(hr)) return hr;
  UINT32 count = 0;
  hr = profiles->get_Size(&count);
  std::set<std::string> types;
  for (UINT32 index = 0; SUCCEEDED(hr) && index < count; ++index) {
    ComPtr<IConnectionProfile> profile;
    hr = profiles->GetAt(index, &profile);
    NetworkConnectivityLevel level = NetworkConnectivityLevel_None;
    if (SUCCEEDED(hr)) hr = profile->GetNetworkConnectivityLevel(&level);
    if (FAILED(hr)) break;
    if (level == NetworkConnectivityLevel_None) continue;
    ComPtr<INetworkAdapter> adapter;
    hr = profile->get_NetworkAdapter(&adapter);
    UINT32 type = 0;
    if (SUCCEEDED(hr) && adapter) hr = adapter->get_IanaInterfaceType(&type);
    if (FAILED(hr)) break;
    switch (type) {
      case IF_TYPE_ETHERNET_CSMACD:
      case IF_TYPE_IEEE1394: types.insert("ethernet"); break;
      case IF_TYPE_IEEE80211: types.insert("wifi"); break;
      case IF_TYPE_TUNNEL:
      case IF_TYPE_PPP: types.insert("vpn"); break;
      default: {
        ComPtr<IConnectionProfile2> extended;
        boolean mobile = false;
        hr = profile.As(&extended);
        if (SUCCEEDED(hr)) hr = extended->get_IsWwanConnectionProfile(&mobile);
        if (FAILED(hr)) break;
        types.insert(mobile ? "mobile" : "other");
      }
    }
  }
  if (FAILED(hr)) return hr;
  if (types.empty()) types.insert("none");
  List list;
  for (const auto& type : types) list.emplace_back(type);
  *result = Value(std::move(list));
  return S_OK;
}

inline std::unique_ptr<std::vector<uint8_t>> Envelope(HRESULT hr, const Value* value = nullptr) {
  const auto& codec = flutter::StandardMethodCodec::GetInstance();
  return SUCCEEDED(hr) ? codec.EncodeSuccessEnvelope(value) :
      codec.EncodeErrorEnvelope("WINRT_NETWORK", "NetworkInformation failed: " +
                                std::to_string(static_cast<unsigned long>(hr)));
}

struct Subscription : std::enable_shared_from_this<Subscription> {
  ComPtr<INetworkInformationStatics> network;
  ComPtr<ABI::Windows::UI::Core::ICoreDispatcher> dispatcher;
  FlutterEngine engine;
  FlutterEngineProcTable api;
  std::atomic<bool> active{true};
  EventRegistrationToken token{};
  bool registered = false;

  Subscription(INetworkInformationStatics* source, const PlatformMessageContext& context)
      : network(source), dispatcher(context.dispatcher), engine(context.engine), api(context.api) {}

  void Emit() {
    if (!active.load()) return;
    Value value;
    HRESULT hr = Read(network.Get(), &value);
    auto bytes = Envelope(hr, &value);
    FlutterPlatformMessage message{};
    message.struct_size = sizeof(message);
    message.channel = kEvents;
    message.message = bytes->data();
    message.message_size = bytes->size();
    api.SendPlatformMessage(engine, &message);
  }

  struct Dispatch : Ref<ABI::Windows::UI::Core::IDispatchedHandler> {
    std::weak_ptr<Subscription> subscription;
    explicit Dispatch(std::weak_ptr<Subscription> value) : subscription(std::move(value)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
      if (id == __uuidof(IAgileObject)) {
        if (!out) return E_POINTER;
        *out = static_cast<ABI::Windows::UI::Core::IDispatchedHandler*>(this);
        AddRef(); return S_OK;
      }
      return Ref::QueryInterface(id, out);
    }
    HRESULT STDMETHODCALLTYPE Invoke() override {
      if (auto state = subscription.lock()) {
        try { state->Emit(); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      }
      return S_OK;
    }
  };

  HRESULT Schedule() {
    if (!active.load()) return S_OK;
    ComPtr<ABI::Windows::UI::Core::IDispatchedHandler> work;
    work.Attach(new (std::nothrow) Dispatch(weak_from_this()));
    if (!work) return E_OUTOFMEMORY;
    ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
    return dispatcher->RunAsync(ABI::Windows::UI::Core::CoreDispatcherPriority_Normal,
                                work.Get(), &action);
  }
};

struct Changed : Ref<INetworkStatusChangedEventHandler> {
  std::weak_ptr<Subscription> subscription;
  explicit Changed(std::weak_ptr<Subscription> value) : subscription(std::move(value)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
    if (id == __uuidof(IAgileObject)) {
      if (!out) return E_POINTER;
      *out = static_cast<INetworkStatusChangedEventHandler*>(this);
      AddRef(); return S_OK;
    }
    return Ref::QueryInterface(id, out);
  }
  HRESULT STDMETHODCALLTYPE Invoke(IInspectable*) override {
    if (auto state = subscription.lock()) return state->Schedule();
    return S_OK;
  }
};

class Plugin {
  ComPtr<INetworkInformationStatics> network_;
  std::shared_ptr<Subscription> subscription_;

  HRESULT Stop() {
    if (!subscription_) return S_OK;
    subscription_->active.store(false);
    if (subscription_->registered) {
      HRESULT hr = network_->remove_NetworkStatusChanged(subscription_->token);
      if (FAILED(hr)) return hr;
      subscription_->registered = false;
    }
    subscription_.reset();
    return S_OK;
  }
  HRESULT Listen(const PlatformMessageContext& context) {
    HRESULT hr = Stop();
    if (FAILED(hr)) return hr;
    ComPtr<INetworkStatusChangedEventHandler> handler;
    try {
      subscription_ = std::make_shared<Subscription>(network_.Get(), context);
      handler.Attach(new Changed(subscription_));
    } catch (const std::bad_alloc&) {
      Stop();
      return E_OUTOFMEMORY;
    }
    hr = network_->add_NetworkStatusChanged(handler.Get(), &subscription_->token);
    if (SUCCEEDED(hr)) {
      subscription_->registered = true;
      hr = subscription_->Schedule();
    }
    if (FAILED(hr)) Stop();
    return hr;
  }

 public:
  ~Plugin() { Stop(); }
  bool Handle(const FlutterPlatformMessage* message, const PlatformMessageContext& context) {
    bool events = strcmp(message->channel, kEvents) == 0;
    if (!events && strcmp(message->channel, kMethods) != 0) return false;
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    auto call = codec.DecodeMethodCall(message->message, message->message_size);
    std::unique_ptr<std::vector<uint8_t>> response;
    if (!call) {
      response = codec.EncodeErrorEnvelope("INVALID_ARGUMENT", "Invalid connectivity method call");
    } else if ((!events && call->method_name() == "check") ||
               (events && (call->method_name() == "listen" || call->method_name() == "cancel"))) {
      // Cancel is idempotent, including after a failed listen or before listen.
      // It does not need to activate the OS network service.
      bool cancel = events && call->method_name() == "cancel";
      HRESULT hr = cancel ? Stop() : (network_ ? S_OK : Factory(&network_));
      Value value;
      if (SUCCEEDED(hr) && !cancel) {
        if (!events) hr = Read(network_.Get(), &value);
        else hr = Listen(context);
      }
      response = Envelope(hr, events ? nullptr : &value);
    }
    if (message->response_handle) {
      context.api.SendPlatformMessageResponse(context.engine, message->response_handle,
          response ? response->data() : nullptr, response ? response->size() : 0);
    }
    return true;
  }
};
}  // namespace connectivity

inline void RegisterConnectivity(PlatformMessageHandlers& handlers) {
  auto plugin = std::make_shared<connectivity::Plugin>();
  handlers.Register([plugin](const FlutterPlatformMessage* message,
                             const PlatformMessageContext& context) {
    return plugin->Handle(message, context);
  });
}
}  // namespace flutter::winrt::plugins
