// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../sdk_client.h"

#include <cassert>
#include <set>

using namespace flutter::winrt::plugins;
namespace {
constexpr HRESULT kFailure = static_cast<HRESULT>(UINT32_C(0x80004005));
uint32_t abi = 21;
int imports = 0, setters = 0, getters = 0, network_opens = 0;
int sdk_value = 0;
bool saved = false, fail_read = false, fail_network = false;
bool expected_observer = true;
std::set<fcm_sdk_handle> handles;
fcm_sdk_handle sequence = 0;
firebase_core::ConfiguredApp Configured() {
  return {"[DEFAULT]", "Synthetic",
          firebase_core::AppRegistry::Options("key", "windows-core", "sender", "project"),
          firebase_core::MessagingConfiguration{
              "key", "android-fis", "project", "sender", "synthetic.package",
              "0123456789012345678901234567890123456789"}};
}
template <class Call>
void Fails(Call call) {
  bool failed = false;
  try {
    call();
  } catch (const firebase_core::StateError&) {
    failed = true;
  }
  assert(failed);
}
}  // namespace

extern "C" {
uint32_t fcm_sdk_abi_version() { return abi; }
const char* fcm_sdk_error_name(HRESULT) { return "synthetic-error"; }
HRESULT fcm_sdk_open_with_core_identity(
    const fcm_sdk_configuration* configuration, const char* core,
    const fcm_sdk_messaging_options* options, fcm_sdk_handle* client) {
  assert(std::string(configuration->app_id) == "android-fis");
  assert(std::string(core) == "windows-core");
  assert(configuration->background_observer == (expected_observer ? 1 : 0));
  assert(std::string(configuration->project_id) == "project");
  if (options) {
    ++network_opens;
    if (fail_network) return kFailure;
    assert(options->size == sizeof(*options));
    assert(std::string(options->android_package) == "synthetic.package");
  }
  *client = ++sequence;
  handles.insert(*client);
  return 0;
}
HRESULT fcm_sdk_close(fcm_sdk_handle client) {
  assert(handles.erase(client) == 1);
  return 0;
}
HRESULT fcm_sdk_get_data_collection_default_enabled(fcm_sdk_handle client,
                                                    int* enabled) {
  assert(handles.count(client));
  ++getters;
  if (fail_read) return kFailure;
  *enabled = sdk_value;
  return 0;
}
HRESULT fcm_sdk_import_data_collection_default_enabled(fcm_sdk_handle client,
                                                       int legacy, int* enabled) {
  assert(handles.count(client));
  ++imports;
  const bool existed = saved;
  if (!saved) sdk_value = legacy;
  saved = true;
  *enabled = sdk_value;
  return existed ? 1 : 0;  // Both S_OK and S_FALSE return the resolved value.
}
HRESULT fcm_sdk_set_data_collection_default_enabled(fcm_sdk_handle client,
                                                    int enabled) {
  assert(handles.count(client));
  ++setters;
  saved = true;
  sdk_value = enabled;
  return 0;
}
}

int main() {
  auto configured = Configured();
  firebase_core::App app{configured.core_options};
  // A signed false must survive the bridge's initial true and receiver open.
  {
    firebase_messaging::SdkClient client(configured, app, true);
    assert(network_opens == 0);
    expected_observer = false;
    client.SetBackgroundObserver(false);
    assert(network_opens == 0);  // Registration cannot publish network early.
    Fails([&] { client.PublishNetworkOptions(); });
    fail_read = true;
    Fails([&] { client.RestoreDataCollection(app); });
    Fails([&] { client.PublishNetworkOptions(); });
    fail_read = false;
    client.RestoreDataCollection(app);
    assert(!app.automatic_data_collection && imports == 0 && setters == 0);
    assert(getters == 2);
    const auto receiver = client.handle();
    fail_network = true;
    Fails([&] { client.PublishNetworkOptions(); });
    assert(client.handle() == receiver && handles.count(receiver));
    fail_network = false;
    client.PublishNetworkOptions();
    assert(client.handle() != receiver && !handles.count(receiver));
    assert(handles.size() == 1);
    const auto published = client.handle();
    expected_observer = true;
    fail_network = true;
    Fails([&] { client.SetBackgroundObserver(true); });
    assert(client.handle() == published && handles.count(published));
    fail_network = false;
    client.SetBackgroundObserver(true);
    assert(client.handle() != published && !handles.count(published));
  }
  assert(handles.empty());
  // A legacy explicit false is imported once. Retrying initialization after
  // the SDK has a newer true value must keep that value, even for old explicit
  // provenance. Only a fresh public setter may overwrite it.
  for (const auto source : {firebase_core::DataCollectionSource::kLegacySaved,
                            firebase_core::DataCollectionSource::kExplicitSetting}) {
    saved = false;
    app.automatic_data_collection = false;
    app.data_collection_source = source;
    {
      firebase_messaging::SdkClient client(configured, app, true);
      client.RestoreDataCollection(app);
      assert(!app.automatic_data_collection);
      client.SetDataCollection(true);
    }
    app.automatic_data_collection = false;  // Stale engine preference.
    {
      firebase_messaging::SdkClient client(configured, app, true);
      client.RestoreDataCollection(app);
      assert(app.automatic_data_collection);
      client.SetDataCollection(false);
      assert(sdk_value == 0);
    }
    assert(handles.empty());
  }
  abi = 20;
  Fails([&] { firebase_messaging::SdkClient client(configured, app, true); });
  abi = 22;  // Additive future ABIs remain valid.
  { firebase_messaging::SdkClient client(configured, app, true); }
  assert(handles.empty());
}
