// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cdm/media_foundation_cdm_data.h"

namespace media {

MediaFoundationCdmData::MediaFoundationCdmData() = default;

MediaFoundationCdmData::MediaFoundationCdmData(
    base::UnguessableToken origin_id,
    absl::optional<std::vector<uint8_t>> client_token)
    : origin_id(origin_id), client_token(client_token) {}

MediaFoundationCdmData::~MediaFoundationCdmData() = default;

}  // namespace media
