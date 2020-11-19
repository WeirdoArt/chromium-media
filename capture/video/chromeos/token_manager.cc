// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/chromeos/token_manager.h"

#include <grp.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

namespace {

gid_t GetArcCameraGid() {
  auto* group = getgrnam("arc-camera");
  return group != nullptr ? group->gr_gid : 0;
}

bool EnsureTokenDirectoryExists(const base::FilePath& token_path) {
  static const gid_t gid = GetArcCameraGid();
  if (gid == 0) {
    LOG(ERROR) << "Failed to query the GID of arc-camera";
    return false;
  }

  base::FilePath dir_name = token_path.DirName();
  if (!base::CreateDirectory(dir_name) ||
      !base::SetPosixFilePermissions(dir_name, 0770)) {
    LOG(ERROR) << "Failed to create token directory at "
               << token_path.AsUTF8Unsafe();
    return false;
  }

  if (chown(dir_name.AsUTF8Unsafe().c_str(), -1, gid) != 0) {
    LOG(ERROR) << "Failed to chown token directory to arc-camera";
    return false;
  }
  return true;
}

bool WriteTokenToFile(const base::FilePath& token_path,
                      const base::UnguessableToken& token) {
  if (!EnsureTokenDirectoryExists(token_path)) {
    LOG(ERROR) << "Failed to ensure token directory exists";
    return false;
  }
  base::File token_file(
      token_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!token_file.IsValid()) {
    LOG(ERROR) << "Failed to create token file at "
               << token_path.AsUTF8Unsafe();
    return false;
  }
  std::string token_string = token.ToString();
  token_file.WriteAtCurrentPos(token_string.c_str(), token_string.length());
  return true;
}

}  // namespace

namespace media {

TokenManager::TokenManager() = default;
TokenManager::~TokenManager() = default;

bool TokenManager::GenerateServerToken() {
  static constexpr char kServerTokenPath[] = "/run/camera_tokens/server/token";

  server_token_ = base::UnguessableToken::Create();
  return WriteTokenToFile(base::FilePath(kServerTokenPath), server_token_);
}

bool TokenManager::GenerateTestClientToken() {
  static constexpr char kTestClientTokenPath[] =
      "/run/camera_tokens/testing/token";

  return WriteTokenToFile(
      base::FilePath(kTestClientTokenPath),
      GetTokenForTrustedClient(cros::mojom::CameraClientType::TESTING));
}

base::UnguessableToken TokenManager::GetTokenForTrustedClient(
    cros::mojom::CameraClientType type) {
  // pluginvm's token should be generated by vm_permission_service.
  CHECK_NE(type, cros::mojom::CameraClientType::PLUGINVM);
  auto& token = client_token_map_[type];
  if (token.is_empty()) {
    token = base::UnguessableToken::Create();
  }
  return token;
}

bool TokenManager::AuthenticateClient(cros::mojom::CameraClientType type,
                                      const base::UnguessableToken& token) {
  // TODO(b/170075468): Check other clients after they have been migrated to
  // CameraHalDispatcher::RegisterClientWithToken.
  if (type != cros::mojom::CameraClientType::CHROME) {
    return true;
  }
  auto it = client_token_map_.find(type);
  return it != client_token_map_.end() && it->second == token;
}

}  // namespace media