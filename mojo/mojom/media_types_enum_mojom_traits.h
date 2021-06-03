// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MOJO_MOJOM_MEDIA_TYPES_ENUM_MOJOM_TRAITS_H_
#define MEDIA_MOJO_MOJOM_MEDIA_TYPES_ENUM_MOJOM_TRAITS_H_

#include "base/notreached.h"
#include "media/base/renderer_factory_selector.h"
#include "media/base/video_frame_metadata.h"
#include "media/base/video_transformation.h"
#include "media/mojo/mojom/media_types.mojom-shared.h"

// Most enums have automatically generated traits, in media_types.mojom.h, due
// to their [native] attribute. This file defines traits for enums that are used
// in files that cannot directly include media_types.mojom.h.

namespace mojo {

template <>
struct EnumTraits<media::mojom::CdmSessionClosedReason,
                  ::media::CdmSessionClosedReason> {
  static media::mojom::CdmSessionClosedReason ToMojom(
      ::media::CdmSessionClosedReason input) {
    switch (input) {
      case ::media::CdmSessionClosedReason::kUnknown:
        return media::mojom::CdmSessionClosedReason::kUnknown;
      case ::media::CdmSessionClosedReason::kClose:
        return media::mojom::CdmSessionClosedReason::kClose;
      case ::media::CdmSessionClosedReason::kCdmUnavailable:
        return media::mojom::CdmSessionClosedReason::kCdmUnavailable;
      case ::media::CdmSessionClosedReason::kHardwareContextReset:
        return media::mojom::CdmSessionClosedReason::kHardwareContextReset;
      case ::media::CdmSessionClosedReason::kResourceEvicted:
        return media::mojom::CdmSessionClosedReason::kResourceEvicted;
    }

    NOTREACHED();
    return static_cast<media::mojom::CdmSessionClosedReason>(input);
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::mojom::CdmSessionClosedReason input,
                        ::media::CdmSessionClosedReason* output) {
    switch (input) {
      case media::mojom::CdmSessionClosedReason::kUnknown:
        *output = ::media::CdmSessionClosedReason::kUnknown;
        return true;
      case media::mojom::CdmSessionClosedReason::kClose:
        *output = ::media::CdmSessionClosedReason::kClose;
        return true;
      case media::mojom::CdmSessionClosedReason::kCdmUnavailable:
        *output = ::media::CdmSessionClosedReason::kCdmUnavailable;
        return true;
      case media::mojom::CdmSessionClosedReason::kHardwareContextReset:
        *output = ::media::CdmSessionClosedReason::kHardwareContextReset;
        return true;
      case media::mojom::CdmSessionClosedReason::kResourceEvicted:
        *output = ::media::CdmSessionClosedReason::kResourceEvicted;
        return true;
    }

    NOTREACHED();
    *output = static_cast<::media::CdmSessionClosedReason>(input);
    return false;
  }
};

template <>
struct EnumTraits<media::mojom::VideoRotation, ::media::VideoRotation> {
  static media::mojom::VideoRotation ToMojom(::media::VideoRotation input) {
    switch (input) {
      case ::media::VideoRotation::VIDEO_ROTATION_0:
        return media::mojom::VideoRotation::kVideoRotation0;
      case ::media::VideoRotation::VIDEO_ROTATION_90:
        return media::mojom::VideoRotation::kVideoRotation90;
      case ::media::VideoRotation::VIDEO_ROTATION_180:
        return media::mojom::VideoRotation::kVideoRotation180;
      case ::media::VideoRotation::VIDEO_ROTATION_270:
        return media::mojom::VideoRotation::kVideoRotation270;
    }

    NOTREACHED();
    return static_cast<media::mojom::VideoRotation>(input);
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::mojom::VideoRotation input,
                        media::VideoRotation* output) {
    switch (input) {
      case media::mojom::VideoRotation::kVideoRotation0:
        *output = ::media::VideoRotation::VIDEO_ROTATION_0;
        return true;
      case media::mojom::VideoRotation::kVideoRotation90:
        *output = ::media::VideoRotation::VIDEO_ROTATION_90;
        return true;
      case media::mojom::VideoRotation::kVideoRotation180:
        *output = ::media::VideoRotation::VIDEO_ROTATION_180;
        return true;
      case media::mojom::VideoRotation::kVideoRotation270:
        *output = ::media::VideoRotation::VIDEO_ROTATION_270;
        return true;
    }

    NOTREACHED();
    *output = static_cast<::media::VideoRotation>(input);
    return true;
  }
};

template <>
struct EnumTraits<media::mojom::CopyMode,
                  ::media::VideoFrameMetadata::CopyMode> {
  static media::mojom::CopyMode ToMojom(
      ::media::VideoFrameMetadata::CopyMode input) {
    switch (input) {
      case ::media::VideoFrameMetadata::CopyMode::kCopyToNewTexture:
        return media::mojom::CopyMode::kCopyToNewTexture;
      case ::media::VideoFrameMetadata::CopyMode::kCopyMailboxesOnly:
        return media::mojom::CopyMode::kCopyMailboxesOnly;
    }

    NOTREACHED();
    return static_cast<media::mojom::CopyMode>(input);
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::mojom::CopyMode input,
                        media::VideoFrameMetadata::CopyMode* output) {
    switch (input) {
      case media::mojom::CopyMode::kCopyToNewTexture:
        *output = ::media::VideoFrameMetadata::CopyMode::kCopyToNewTexture;
        return true;
      case media::mojom::CopyMode::kCopyMailboxesOnly:
        *output = ::media::VideoFrameMetadata::CopyMode::kCopyMailboxesOnly;
        return true;
    }

    NOTREACHED();
    *output = static_cast<::media::VideoFrameMetadata::CopyMode>(input);
    return false;
  }
};

template <>
struct EnumTraits<media::mojom::RendererType, ::media::RendererType> {
  static media::mojom::RendererType ToMojom(::media::RendererType input) {
    switch (input) {
      case ::media::RendererType::kDefault:
        return media::mojom::RendererType::kDefault;
      case ::media::RendererType::kMojo:
        return media::mojom::RendererType::kMojo;
      case ::media::RendererType::kMediaPlayer:
        return media::mojom::RendererType::kMediaPlayer;
      case ::media::RendererType::kCourier:
        return media::mojom::RendererType::kCourier;
      case ::media::RendererType::kFlinging:
        return media::mojom::RendererType::kFlinging;
      case ::media::RendererType::kCast:
        return media::mojom::RendererType::kCast;
      case ::media::RendererType::kMediaFoundation:
        return media::mojom::RendererType::kMediaFoundation;
      case ::media::RendererType::kFuchsia:
        return media::mojom::RendererType::kFuchsia;
      case ::media::RendererType::kRemoting:
        return media::mojom::RendererType::kRemoting;
      case ::media::RendererType::kCastStreaming:
        return media::mojom::RendererType::kCastStreaming;
    }

    NOTREACHED();
    return static_cast<media::mojom::RendererType>(input);
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::mojom::RendererType input,
                        ::media::RendererType* output) {
    switch (input) {
      case media::mojom::RendererType::kDefault:
        *output = ::media::RendererType::kDefault;
        return true;
      case media::mojom::RendererType::kMojo:
        *output = ::media::RendererType::kMojo;
        return true;
      case media::mojom::RendererType::kMediaPlayer:
        *output = ::media::RendererType::kMediaPlayer;
        return true;
      case media::mojom::RendererType::kCourier:
        *output = ::media::RendererType::kCourier;
        return true;
      case media::mojom::RendererType::kFlinging:
        *output = ::media::RendererType::kFlinging;
        return true;
      case media::mojom::RendererType::kCast:
        *output = ::media::RendererType::kCast;
        return true;
      case media::mojom::RendererType::kMediaFoundation:
        *output = ::media::RendererType::kMediaFoundation;
        return true;
      case media::mojom::RendererType::kFuchsia:
        *output = ::media::RendererType::kFuchsia;
        return true;
      case media::mojom::RendererType::kRemoting:
        *output = ::media::RendererType::kRemoting;
        return true;
      case media::mojom::RendererType::kCastStreaming:
        *output = ::media::RendererType::kCastStreaming;
        return true;
    }

    NOTREACHED();
    *output = static_cast<::media::RendererType>(input);
    return false;
  }
};

}  // namespace mojo

#endif  // MEDIA_MOJO_MOJOM_MEDIA_TYPES_ENUM_MOJOM_TRAITS_H_
