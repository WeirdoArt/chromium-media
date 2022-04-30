// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_MEDIA_UTIL_H_
#define MEDIA_BASE_MEDIA_UTIL_H_

#include <stdint.h>
#include <vector>

#include "media/base/audio_codecs.h"
#include "media/base/audio_parameters.h"
#include "media/base/media_export.h"
#include "media/base/media_log.h"

namespace media {

// Simply returns an empty vector. {Audio|Video}DecoderConfig are often
// constructed with empty extra data.
MEDIA_EXPORT std::vector<uint8_t> EmptyExtraData();

// Helpers for PPAPI UMAs. There wasn't an obvious place to put them in
// //content/renderer/pepper.
MEDIA_EXPORT void ReportPepperVideoDecoderOutputPictureCountHW(int height);
MEDIA_EXPORT void ReportPepperVideoDecoderOutputPictureCountSW(int height);

class MEDIA_EXPORT NullMediaLog : public media::MediaLog {
 public:
  NullMediaLog() = default;

  NullMediaLog(const NullMediaLog&) = delete;
  NullMediaLog& operator=(const NullMediaLog&) = delete;

  ~NullMediaLog() override = default;

  void AddLogRecordLocked(
      std::unique_ptr<media::MediaLogRecord> event) override {}
};

// Converts Audio Codec Type to Bitstream Format.
MEDIA_EXPORT AudioParameters::Format ConvertAudioCodecToBitstreamFormat(
    media::AudioCodec codec);

}  // namespace media

#endif  // MEDIA_BASE_MEDIA_UTIL_H_
