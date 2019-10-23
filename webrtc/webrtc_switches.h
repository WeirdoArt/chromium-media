// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines all command-line switches for media/webrtc.

#ifndef MEDIA_WEBRTC_WEBRTC_SWITCHES_H_
#define MEDIA_WEBRTC_WEBRTC_SWITCHES_H_

#include "base/component_export.h"
#include "base/feature_list.h"

namespace switches {

COMPONENT_EXPORT(MEDIA_WEBRTC) extern const char kAgcStartupMinVolume[];

}  // namespace switches

namespace features {

COMPONENT_EXPORT(MEDIA_WEBRTC)
extern const base::Feature kWebRtcApmInAudioService;

COMPONENT_EXPORT(MEDIA_WEBRTC)
extern const base::Feature kWebRtcEnableMultiChannelApm;

COMPONENT_EXPORT(MEDIA_WEBRTC)
extern const base::Feature kWebRtcHybridAgc;

}  // namespace features

namespace switches {
COMPONENT_EXPORT(MEDIA_WEBRTC)
extern const char kForceDisableWebRtcApmInAudioService[];
}  // namespace switches

namespace media {

COMPONENT_EXPORT(MEDIA_WEBRTC) bool IsWebRtcApmInAudioServiceEnabled();

}  // namespace media

#endif  // MEDIA_WEBRTC_WEBRTC_SWITCHES_H_
