// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#import "media/base/mac/avfoundation_glue.h"
#include "media/capture/video/mac/video_capture_device_factory_mac.h"
#include "media/capture/video/mac/video_capture_device_mac.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

TEST(VideoCaptureDeviceFactoryMacTest, ListDevicesAVFoundation) {
  AVFoundationGlue::InitializeAVFoundation();
  VideoCaptureDeviceFactoryMac video_capture_device_factory;

  VideoCaptureDevice::Names names;
  video_capture_device_factory.GetDeviceNames(&names);
  if (names.empty()) {
    DVLOG(1) << "No camera available. Exiting test.";
    return;
  }
  for (const auto& name : names)
    EXPECT_EQ(VideoCaptureDevice::Name::AVFOUNDATION, name.capture_api_type());
}

};  // namespace media
