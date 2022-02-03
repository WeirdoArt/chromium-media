// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_WINDOWS_D3D11_STATUS_H_
#define MEDIA_GPU_WINDOWS_D3D11_STATUS_H_

#include <wrl/client.h>

#include "media/base/status.h"

namespace media {

enum class D3D11StatusCode : StatusCodeType {
  kOk = 0,

  kFailedToGetAngleDevice = 1,
  kUnsupportedFeatureLevel = 2,
  kFailedToGetVideoDevice = 3,
  kFailedToGetDeviceContext = 4,
  kFailedToInitializeGPUProcess = 5,
  kDecoderFailedDecode = 6,
  kDecoderUnsupportedProfile = 7,
  kDecoderUnsupportedCodec = 8,
  kDecoderUnsupportedConfig = 9,
  kDecoderCreationFailed = 10,
  kMakeContextCurrentFailed = 11,
  kCreateTextureSelectorFailed = 12,
  kQueryID3D11MultithreadFailed = 13,
  kGetDecoderConfigCountFailed = 14,
  kGetDecoderConfigFailed = 15,
  kProcessTextureFailed = 16,
  kUnsupportedTextureFormatForBind = 17,
  kCreateDecoderOutputViewFailed = 18,
  kAllocateTextureForCopyingWrapperFailed = 19,
  kCreateDecoderOutputTextureFailed = 20,
  kCreateVideoProcessorInputViewFailed = 21,
  kVideoProcessorBltFailed = 22,
  kCreateVideoProcessorOutputViewFailed = 23,
  kCreateVideoProcessorFailed = 24,
  kQueryVideoContextFailed = 25,
  kAcceleratorFlushFailed = 26,
  kTryAgainNotSupported = 27,
  kCryptoConfigFailed = 28,
  kDecoderBeginFrameFailed = 29,
  kGetPicParamBufferFailed = 30,
  kReleasePicParamBufferFailed = 31,
  kGetBitstreamBufferFailed = 32,
  kReleaseBitstreamBufferFailed = 33,
  kGetSliceControlBufferFailed = 34,
  kReleaseSliceControlBufferFailed = 35,
  kDecoderEndFrameFailed = 36,
  kSubmitDecoderBuffersFailed = 37,
  kGetQuantBufferFailed = 38,
  kReleaseQuantBufferFailed = 39,
  kBitstreamBufferSliceTooBig = 40,
  kCreateSharedImageFailed = 41,
  kGetKeyedMutexFailed = 42,
  kAcquireKeyedMutexFailed = 43,
  kReleaseKeyedMutexFailed = 44,
  kCreateSharedHandleFailed = 45,
};

struct D3D11StatusTraits {
  using Codes = D3D11StatusCode;
  static constexpr StatusGroupType Group() { return "D3D11Status"; }
  static constexpr D3D11StatusCode DefaultEnumValue() {
    return D3D11StatusCode::kOk;
  }
};

using D3D11Status = TypedStatus<D3D11StatusTraits>;

D3D11Status HresultToStatus(
    HRESULT hresult,
    D3D11Status::Codes code,
    const base::Location& location = base::Location::Current());

}  // namespace media

#endif  // MEDIA_GPU_WINDOWS_D3D11_STATUS_H_
