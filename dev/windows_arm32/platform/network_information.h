#pragma once
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.networking.connectivity.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace flutter::winrt::network {
using namespace ABI::Windows::Networking::Connectivity;
using Microsoft::WRL::ComPtr;

struct LanAddress {
  UINT32 type;
  std::vector<BYTE> value;
};

// Preserve OS values without translating them into synthetic WLAN attributes.
// A legacy WLAN ABI adapter must separately validate each required field.
struct WlanProfile {
  GUID adapter;
  std::wstring ssid;
  NetworkConnectivityLevel connectivity;
  NetworkAuthenticationType authentication;
  NetworkEncryptionType encryption;
  std::vector<LanAddress> infrastructure;
};

inline HRESULT ReadWlanProfiles(std::vector<WlanProfile>* result) {
  if (!result) return E_POINTER;
  const wchar_t* name = L"Windows.Networking.Connectivity.NetworkInformation";
  HSTRING text = nullptr;
  HRESULT hr = WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), &text);
  ComPtr<INetworkInformationStatics> network;
  if (SUCCEEDED(hr)) hr = RoGetActivationFactory(text, IID_PPV_ARGS(&network));
  WindowsDeleteString(text);
  if (FAILED(hr)) return hr;
  using Profiles = ABI::Windows::Foundation::Collections::IVectorView<ConnectionProfile*>;
  using Identifiers = ABI::Windows::Foundation::Collections::IVectorView<LanIdentifier*>;
  ComPtr<Profiles> profiles;
  ComPtr<Identifiers> identifiers;
  hr = network->GetConnectionProfiles(&profiles);
  if (SUCCEEDED(hr)) hr = network->GetLanIdentifiers(&identifiers);
  if (FAILED(hr)) return hr;
  if (!profiles || !identifiers) return E_UNEXPECTED;
  UINT32 count = 0, identifier_count = 0;
  hr = profiles->get_Size(&count);
  if (SUCCEEDED(hr)) hr = identifiers->get_Size(&identifier_count);
  if (FAILED(hr)) return hr;
  std::vector<WlanProfile> values;
  for (UINT32 index = 0; index < count; ++index) {
    ComPtr<IConnectionProfile> profile;
    ComPtr<IConnectionProfile2> extended;
    hr = profiles->GetAt(index, &profile);
    if (SUCCEEDED(hr)) hr = profile.As(&extended);
    boolean wlan = false;
    if (SUCCEEDED(hr)) hr = extended->get_IsWlanConnectionProfile(&wlan);
    if (FAILED(hr)) return hr;
    if (!wlan) continue;
    WlanProfile value{};
    ComPtr<INetworkAdapter> adapter;
    ComPtr<IWlanConnectionProfileDetails> details;
    ComPtr<INetworkSecuritySettings> security;
    hr = profile->get_NetworkAdapter(&adapter);
    if (SUCCEEDED(hr) && !adapter) hr = E_UNEXPECTED;
    if (SUCCEEDED(hr)) hr = adapter->get_NetworkAdapterId(&value.adapter);
    if (SUCCEEDED(hr)) hr = profile->GetNetworkConnectivityLevel(&value.connectivity);
    if (SUCCEEDED(hr)) hr = extended->get_WlanConnectionProfileDetails(&details);
    if (SUCCEEDED(hr) && !details) hr = E_UNEXPECTED;
    HSTRING ssid = nullptr;
    if (SUCCEEDED(hr)) hr = details->GetConnectedSsid(&ssid);
    if (SUCCEEDED(hr)) {
      UINT32 length = 0;
      const wchar_t* characters = WindowsGetStringRawBuffer(ssid, &length);
      value.ssid.assign(characters, length);
    }
    WindowsDeleteString(ssid);
    if (SUCCEEDED(hr)) hr = profile->get_NetworkSecuritySettings(&security);
    if (SUCCEEDED(hr) && !security) hr = E_UNEXPECTED;
    if (SUCCEEDED(hr)) hr = security->get_NetworkAuthenticationType(&value.authentication);
    if (SUCCEEDED(hr)) hr = security->get_NetworkEncryptionType(&value.encryption);
    if (FAILED(hr)) return hr;
    for (UINT32 item = 0; item < identifier_count; ++item) {
      ComPtr<ILanIdentifier> identifier;
      GUID id{};
      hr = identifiers->GetAt(item, &identifier);
      if (SUCCEEDED(hr)) hr = identifier->get_NetworkAdapterId(&id);
      if (FAILED(hr)) return hr;
      if (!IsEqualGUID(id, value.adapter)) continue;
      ComPtr<ILanIdentifierData> data;
      hr = identifier->get_InfrastructureId(&data);
      if (FAILED(hr)) return hr;
      if (!data) continue;
      LanAddress address{};
      ComPtr<ABI::Windows::Foundation::Collections::IVectorView<BYTE>> bytes;
      hr = data->get_Type(&address.type);
      if (SUCCEEDED(hr)) hr = data->get_Value(&bytes);
      if (SUCCEEDED(hr) && !bytes) hr = E_UNEXPECTED;
      UINT32 size = 0;
      if (SUCCEEDED(hr)) hr = bytes->get_Size(&size);
      if (FAILED(hr)) return hr;
      address.value.resize(size);
      UINT32 actual = 0;
      if (size) hr = bytes->GetMany(0, size, address.value.data(), &actual);
      if (FAILED(hr)) return hr;
      if (actual != size) return E_UNEXPECTED;
      value.infrastructure.push_back(std::move(address));
    }
    values.push_back(std::move(value));
  }
  *result = std::move(values);
  return S_OK;
}
}  // namespace flutter::winrt::network
