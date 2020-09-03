// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_FUCHSIA_CDM_SERVICE_FUCHSIA_CDM_MANAGER_H_
#define MEDIA_FUCHSIA_CDM_SERVICE_FUCHSIA_CDM_MANAGER_H_

#include <fuchsia/media/drm/cpp/fidl.h>
#include <string>

#include "base/callback_forward.h"
#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/threading/thread_checker.h"
#include "media/base/provision_fetcher.h"

namespace url {
class Origin;
}  // namespace url

namespace media {

// Create and connect to Fuchsia CDM services. Additionally manages the storage
// for CDM user data.
class FuchsiaCdmManager {
 public:
  using CreateKeySystemCallback = base::RepeatingCallback<
      fidl::InterfaceHandle<fuchsia::media::drm::KeySystem>()>;

  // A map from key system name to its CreateKeySystemCallback.
  using CreateKeySystemCallbackMap =
      base::flat_map<std::string, CreateKeySystemCallback>;

  FuchsiaCdmManager(
      CreateKeySystemCallbackMap create_key_system_callbacks_by_name,
      base::FilePath cdm_data_path);

  ~FuchsiaCdmManager();

  FuchsiaCdmManager(FuchsiaCdmManager&&) = delete;
  FuchsiaCdmManager& operator=(FuchsiaCdmManager&&) = delete;

  void CreateAndProvision(
      const std::string& key_system,
      const url::Origin& origin,
      CreateFetcherCB create_fetcher_cb,
      fidl::InterfaceRequest<fuchsia::media::drm::ContentDecryptionModule>
          request);

  // Used by tests to monitor for key system disconnection events. The key
  // system name is passed as a parameter to the callback.
  void set_on_key_system_disconnect_for_test_callback(
      base::RepeatingCallback<void(const std::string&)> disconnect_callback);

 private:
  class KeySystemClient;
  using KeySystemClientMap =
      base::flat_map<std::string, std::unique_ptr<KeySystemClient>>;

  KeySystemClient* GetOrCreateKeySystemClient(
      const std::string& key_system_name);
  KeySystemClient* CreateKeySystemClient(const std::string& key_system_name);
  base::FilePath GetStoragePath(const std::string& key_system_name,
                                const url::Origin& origin);
  void OnKeySystemClientError(const std::string& key_system_name);

  // A map of callbacks to create KeySystem channels indexed by their EME name.
  const CreateKeySystemCallbackMap create_key_system_callbacks_by_name_;
  const base::FilePath cdm_data_path_;

  // A map of the active KeySystem clients indexed by their EME name.  Entries
  // in this map will be added on the first CreateAndProvision call for that
  // particular KeySystem. They will only be removed if the KeySystem channel
  // receives an error.
  KeySystemClientMap active_key_system_clients_by_name_;

  base::RepeatingCallback<void(const std::string&)>
      on_key_system_disconnect_for_test_callback_;

  THREAD_CHECKER(thread_checker_);
};

}  // namespace media

#endif  // MEDIA_FUCHSIA_CDM_SERVICE_FUCHSIA_CDM_MANAGER_H_
