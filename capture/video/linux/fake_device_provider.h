// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAPTURE_VIDEO_LINUX_FAKE_DEVICE_PROVIDER_H_
#define MEDIA_CAPTURE_VIDEO_LINUX_FAKE_DEVICE_PROVIDER_H_

#include <string>
#include <vector>

#include "media/capture/video/linux/video_capture_device_factory_linux.h"
#include "media/capture/video/video_capture_device_descriptor.h"
#include "media/capture/video_capture_types.h"

namespace media {

class FakeDeviceProvider
    : public VideoCaptureDeviceFactoryLinux::DeviceProvider {
 public:
  FakeDeviceProvider();
  ~FakeDeviceProvider() override;

  void AddDevice(const VideoCaptureDeviceDescriptor& descriptor);

  void GetDeviceIds(std::vector<std::string>* target_container) override;
  std::string GetDeviceModelId(const std::string& device_id) override;
  std::string GetDeviceDisplayName(const std::string& device_id) override;
  VideoFacingMode GetCameraFacing(const std::string& device_id,
                                  const std::string& model_id) override;
  int GetOrientation(const std::string& device_id,
                     const std::string& model_id) override;

 private:
  std::vector<VideoCaptureDeviceDescriptor> descriptors_;
};
}  // namespace media
#endif  // MEDIA_CAPTURE_VIDEO_LINUX_FAKE_DEVICE_PROVIDER_H_
