// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_FUCHSIA_CAMERA_FAKE_FUCHSIA_CAMERA_H_
#define MEDIA_FUCHSIA_CAMERA_FAKE_FUCHSIA_CAMERA_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>

#include <vector>

#include "base/message_loop/message_pump_for_io.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "ui/gfx/geometry/size.h"

namespace media {

class FakeCameraStream : public fuchsia::camera3::testing::Stream_TestBase,
                         public base::MessagePumpForIO::ZxHandleWatcher {
 public:
  static const gfx::Size kMaxFrameSize;
  static const gfx::Size kDefaultFrameSize;

  // Verifies that the I420 image stored at |data| matches the frame produced
  // by ProduceFrame().
  static void ValidateFrameData(const uint8_t* data,
                                gfx::Size size,
                                uint8_t salt);

  FakeCameraStream();
  ~FakeCameraStream() final;

  FakeCameraStream(const FakeCameraStream&) = delete;
  FakeCameraStream& operator=(const FakeCameraStream&) = delete;

  void Bind(fidl::InterfaceRequest<fuchsia::camera3::Stream> request);

  void SetFakeResolution(gfx::Size resolution);

  // Waits for the buffer collection to be allocated. Returns true if the buffer
  // collection was allocated successfully.
  bool WaitBuffersAllocated();

  // Waits until there is at least one free buffer that can be used for the next
  // frame.
  bool WaitFreeBuffer();

  void ProduceFrame(base::TimeTicks timestamp, uint8_t salt);

 private:
  struct Buffer;

  // fuchsia::camera3::Stream implementation.
  void WatchResolution(WatchResolutionCallback callback) final;
  void SetBufferCollection(
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>
          token_handle) final;
  void WatchBufferCollection(WatchBufferCollectionCallback callback) final;
  void GetNextFrame(GetNextFrameCallback callback) final;

  // fuchsia::camera3::testing::Stream_TestBase override.
  void NotImplemented_(const std::string& name) override;

  void OnBufferCollectionError(zx_status_t status);

  void OnBufferCollectionAllocated(
      zx_status_t status,
      fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info);

  // Calls callback for the pending WatchResolution() if the call is pending and
  // resolution has been updated.
  void SendResolution();

  // Calls callback for the pending WatchBufferCollection() if we have a new
  // token and the call is pending.
  void SendBufferCollection();

  // Calls callback for the pending GetNextFrame() if we have a new frame and
  // the call is pending.
  void SendNextFrame();

  // ZxHandleWatcher interface. Used to wait for frame release_fences to get
  // notified when the client releases a buffer.
  void OnZxHandleSignalled(zx_handle_t handle, zx_signals_t signals) final;

  fidl::Binding<fuchsia::camera3::Stream> binding_;

  gfx::Size resolution_ = kDefaultFrameSize;

  base::Optional<fuchsia::math::Size> resolution_update_ = fuchsia::math::Size{
      kDefaultFrameSize.width(), kDefaultFrameSize.height()};
  WatchResolutionCallback watch_resolution_callback_;

  base::Optional<fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>>
      new_buffer_collection_token_;
  WatchBufferCollectionCallback watch_buffer_collection_callback_;

  base::Optional<fuchsia::camera3::FrameInfo> next_frame_;
  GetNextFrameCallback get_next_frame_callback_;

  fuchsia::sysmem::BufferCollectionPtr buffer_collection_;

  base::Optional<base::RunLoop> wait_buffers_allocated_run_loop_;
  base::Optional<base::RunLoop> wait_free_buffer_run_loop_;

  std::vector<std::unique_ptr<Buffer>> buffers_;
  size_t num_used_buffers_ = 0;

  size_t frame_counter_ = 0;
};

class FakeCameraDevice : public fuchsia::camera3::testing::Device_TestBase {
 public:
  explicit FakeCameraDevice(FakeCameraStream* stream);
  ~FakeCameraDevice() final;

  FakeCameraDevice(const FakeCameraDevice&) = delete;
  FakeCameraDevice& operator=(const FakeCameraDevice&) = delete;

  void Bind(fidl::InterfaceRequest<fuchsia::camera3::Device> request);

 private:
  // fuchsia::camera3::Device implementation.
  void GetIdentifier(GetIdentifierCallback callback) final;
  void GetConfigurations(GetConfigurationsCallback callback) final;
  void ConnectToStream(
      uint32_t index,
      fidl::InterfaceRequest<fuchsia::camera3::Stream> request) final;

  // fuchsia::camera3::testing::Device_TestBase override.
  void NotImplemented_(const std::string& name) override;

  fidl::Binding<fuchsia::camera3::Device> binding_;
  FakeCameraStream* const stream_;
};

}  // namespace media

#endif  // MEDIA_FUCHSIA_CAMERA_FAKE_FUCHSIA_CAMERA_H_
