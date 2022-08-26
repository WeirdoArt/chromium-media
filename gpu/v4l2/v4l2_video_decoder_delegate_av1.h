// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_V4L2_V4L2_VIDEO_DECODER_DELEGATE_AV1_H_
#define MEDIA_GPU_V4L2_V4L2_VIDEO_DECODER_DELEGATE_AV1_H_

#include "media/gpu/av1_decoder.h"

namespace media {

class V4L2DecodeSurfaceHandler;
class V4L2Device;

class V4L2VideoDecoderDelegateAV1 : public AV1Decoder::AV1Accelerator {
 public:
  V4L2VideoDecoderDelegateAV1(V4L2DecodeSurfaceHandler* surface_handler,
                              V4L2Device* device);

  V4L2VideoDecoderDelegateAV1(const V4L2VideoDecoderDelegateAV1&) = delete;
  V4L2VideoDecoderDelegateAV1& operator=(const V4L2VideoDecoderDelegateAV1&) =
      delete;

  ~V4L2VideoDecoderDelegateAV1() override;

 private:
  V4L2DecodeSurfaceHandler* const surface_handler_;
  V4L2Device* const device_;
};

}  // namespace media

#endif  // MEDIA_GPU_V4L2_V4L2_VIDEO_DECODER_DELEGATE_AV1_H_
