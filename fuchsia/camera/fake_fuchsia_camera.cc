// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/fuchsia/camera/fake_fuchsia_camera.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "base/fuchsia/default_context.h"
#include "base/memory/platform_shared_memory_region.h"
#include "base/memory/writable_shared_memory_region.h"
#include "base/message_loop/message_loop_current.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

namespace {

constexpr uint8_t kYPlaneSalt = 1;
constexpr uint8_t kUPlaneSalt = 2;
constexpr uint8_t kVPlaneSalt = 3;

uint8_t GetTestFrameValue(gfx::Size size, int x, int y, uint8_t salt) {
  return static_cast<uint8_t>(y + x * size.height() + salt);
}

void FillPlane(uint8_t* data,
               gfx::Size size,
               size_t x_step,
               size_t y_step,
               uint8_t salt) {
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      data[x * x_step + y * y_step] = GetTestFrameValue(size, x, y, salt);
    }
  }
}

void ValidatePlane(const uint8_t* data,
                   gfx::Size size,
                   size_t x_step,
                   size_t y_step,
                   uint8_t salt) {
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      SCOPED_TRACE(testing::Message() << "x=" << x << " y=" << y);
      EXPECT_EQ(data[x * x_step + y * y_step],
                GetTestFrameValue(size, x, y, salt));
    }
  }
}

}  // namespace

// static
void FakeCameraStream::ValidateFrameData(const uint8_t* data,
                                         gfx::Size size,
                                         uint8_t salt) {
  const uint8_t* y_plane = data;
  {
    SCOPED_TRACE("Y plane");
    ValidatePlane(y_plane, size, 1, size.width(), salt + kYPlaneSalt);
  }

  gfx::Size uv_size(size.width() / 2, size.height() / 2);
  const uint8_t* u_plane = y_plane + size.width() * size.height();
  {
    SCOPED_TRACE("U plane");
    ValidatePlane(u_plane, uv_size, 1, uv_size.width(), salt + kUPlaneSalt);
  }

  const uint8_t* v_plane = u_plane + uv_size.width() * uv_size.height();
  {
    SCOPED_TRACE("V plane");
    ValidatePlane(v_plane, uv_size, 1, uv_size.width(), salt + kVPlaneSalt);
  }
}

struct FakeCameraStream::Buffer {
  explicit Buffer(base::WritableSharedMemoryMapping mapping)
      : mapping(std::move(mapping)),
        release_fence_watch_controller(FROM_HERE) {}

  base::WritableSharedMemoryMapping mapping;

  // Frame is used by the client when the |release_fence| is not null.
  zx::eventpair release_fence;

  base::MessagePumpForIO::ZxHandleWatchController
      release_fence_watch_controller;
};

FakeCameraStream::FakeCameraStream() : binding_(this) {}
FakeCameraStream::~FakeCameraStream() = default;

void FakeCameraStream::Bind(
    fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  binding_.Bind(std::move(request));
}

void FakeCameraStream::SetFakeResolution(gfx::Size resolution) {
  resolution_ = resolution;
  resolution_update_ =
      fuchsia::math::Size{resolution_.width(), resolution_.height()};
  SendResolution();
}

bool FakeCameraStream::WaitBuffersAllocated() {
  EXPECT_FALSE(wait_buffers_allocated_run_loop_);

  if (!buffers_.empty())
    return true;

  wait_buffers_allocated_run_loop_.emplace();
  wait_buffers_allocated_run_loop_->Run();
  wait_buffers_allocated_run_loop_.reset();

  return !buffers_.empty();
}

bool FakeCameraStream::WaitFreeBuffer() {
  EXPECT_FALSE(wait_free_buffer_run_loop_);

  if (num_used_buffers_ < buffers_.size())
    return true;

  wait_free_buffer_run_loop_.emplace();
  wait_free_buffer_run_loop_->Run();
  wait_free_buffer_run_loop_.reset();

  return num_used_buffers_ < buffers_.size();
}

void FakeCameraStream::ProduceFrame(base::TimeTicks timestamp, uint8_t salt) {
  ASSERT_LT(num_used_buffers_, buffers_.size());
  ASSERT_FALSE(next_frame_);

  size_t index = buffers_.size();
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if (!buffers_[i]->release_fence) {
      index = i;
      break;
    }
  }
  EXPECT_LT(index, buffers_.size());

  auto* buffer = buffers_[index].get();

  gfx::Size coded_size((resolution_.width() + 1) & ~1,
                       (resolution_.height() + 1) & ~1);

  // Fill Y plane.
  uint8_t* y_plane = reinterpret_cast<uint8_t*>(buffer->mapping.memory());
  size_t stride = kMaxFrameSize.width();
  FillPlane(y_plane, coded_size, /*x_step=*/1, /*y_step=*/stride,
            salt + kYPlaneSalt);

  // Fill UV plane.
  gfx::Size uv_size(coded_size.width() / 2, coded_size.height() / 2);
  uint8_t* uv_plane = y_plane + kMaxFrameSize.width() * kMaxFrameSize.height();
  FillPlane(uv_plane, uv_size, /*x_step=*/2, /*y_step=*/stride,
            salt + kUPlaneSalt);
  FillPlane(uv_plane + 1, uv_size, /*x_step=*/2, /*y_step=*/stride,
            salt + kVPlaneSalt);

  // Create FrameInfo.
  fuchsia::camera3::FrameInfo frame;
  frame.frame_counter = frame_counter_++;
  frame.buffer_index = 0;
  frame.timestamp = timestamp.ToZxTime();
  EXPECT_EQ(
      zx::eventpair::create(0u, &frame.release_fence, &buffer->release_fence),
      ZX_OK);

  // Watch release fence to get notified when the frame is released.
  base::MessageLoopCurrentForIO::Get()->WatchZxHandle(
      buffer->release_fence.get(), /*persistent=*/false,
      ZX_EVENTPAIR_PEER_CLOSED, &buffer->release_fence_watch_controller, this);

  num_used_buffers_++;
  next_frame_ = std::move(frame);
  SendNextFrame();
}

void FakeCameraStream::WatchResolution(WatchResolutionCallback callback) {
  EXPECT_FALSE(watch_resolution_callback_);
  watch_resolution_callback_ = std::move(callback);
  SendResolution();
}

void FakeCameraStream::SetBufferCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>
        token_handle) {
  EXPECT_TRUE(token_handle);

  // Drop old buffers.
  buffers_.clear();
  if (buffer_collection_) {
    buffer_collection_->Close();
    buffer_collection_.Unbind();
  }

  // Use a SyncPtr to be able to wait for Sync() synchronously.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr token;
  token.Bind(std::move(token_handle));

  // Duplicate the token to access from the stream.
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> local_token;
  zx_status_t status =
      token->Duplicate(/*rights_attenuation_mask=*/0, local_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);

  status = token->Sync();
  EXPECT_EQ(status, ZX_OK);

  // Return the token back to the client.
  new_buffer_collection_token_ = token.Unbind();
  SendBufferCollection();

  // Initialize the new collection using |local_token|.
  auto allocator = base::fuchsia::ComponentContextForCurrentProcess()
                       ->svc()
                       ->Connect<fuchsia::sysmem::Allocator>();

  allocator->BindSharedCollection(std::move(local_token),
                                  buffer_collection_.NewRequest());
  EXPECT_EQ(status, ZX_OK);

  buffer_collection_.set_error_handler(
      fit::bind_member(this, &FakeCameraStream::OnBufferCollectionError));

  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageWrite;

  // The client is expected to request buffers it may need. We don't need to
  // reserve any for the server side.
  constraints.min_buffer_count_for_camping = 0;

  // Initialize image format.
  constraints.image_format_constraints_count = 1;
  constraints.image_format_constraints[0].pixel_format.type =
      fuchsia::sysmem::PixelFormatType::NV12;
  constraints.image_format_constraints[0].color_spaces_count = 1;
  constraints.image_format_constraints[0].color_space[0].type =
      fuchsia::sysmem::ColorSpaceType::REC601_NTSC;
  constraints.image_format_constraints[0].required_max_coded_width =
      kMaxFrameSize.width();
  constraints.image_format_constraints[0].required_max_coded_height =
      kMaxFrameSize.height();

  buffer_collection_->SetConstraints(/*has_constraints=*/true,
                                     std::move(constraints));
  buffer_collection_->WaitForBuffersAllocated(
      fit::bind_member(this, &FakeCameraStream::OnBufferCollectionAllocated));
}

void FakeCameraStream::WatchBufferCollection(
    WatchBufferCollectionCallback callback) {
  EXPECT_FALSE(watch_buffer_collection_callback_);
  watch_buffer_collection_callback_ = std::move(callback);
  SendBufferCollection();
}

void FakeCameraStream::GetNextFrame(GetNextFrameCallback callback) {
  EXPECT_FALSE(get_next_frame_callback_);
  get_next_frame_callback_ = std::move(callback);
  SendNextFrame();
}

void FakeCameraStream::NotImplemented_(const std::string& name) {
  ADD_FAILURE() << "NotImplemented_: " << name;
}

void FakeCameraStream::OnBufferCollectionError(zx_status_t status) {
  ADD_FAILURE() << "BufferCollection failed.";
  if (wait_buffers_allocated_run_loop_)
    wait_buffers_allocated_run_loop_->Quit();
  if (wait_free_buffer_run_loop_)
    wait_free_buffer_run_loop_->Quit();
}

void FakeCameraStream::OnBufferCollectionAllocated(
    zx_status_t status,
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info) {
  if (status != ZX_OK) {
    OnBufferCollectionError(status);
    return;
  }

  EXPECT_TRUE(buffers_.empty());
  EXPECT_TRUE(buffer_collection_info.settings.has_image_format_constraints);
  EXPECT_EQ(buffer_collection_info.settings.image_format_constraints
                .pixel_format.type,
            fuchsia::sysmem::PixelFormatType::NV12);

  size_t buffer_size =
      buffer_collection_info.settings.buffer_settings.size_bytes;
  for (size_t i = 0; i < buffer_collection_info.buffer_count; ++i) {
    auto& buffer = buffer_collection_info.buffers[i];
    EXPECT_EQ(buffer.vmo_usable_start, 0U);
    auto region = base::WritableSharedMemoryRegion::Deserialize(
        base::subtle::PlatformSharedMemoryRegion::Take(
            std::move(buffer.vmo),
            base::subtle::PlatformSharedMemoryRegion::Mode::kWritable,
            buffer_size, base::UnguessableToken::Create()));
    auto mapping = region.Map();
    EXPECT_TRUE(mapping.IsValid());
    buffers_.push_back(std::make_unique<Buffer>(std::move(mapping)));
  }

  if (wait_buffers_allocated_run_loop_)
    wait_buffers_allocated_run_loop_->Quit();
}

void FakeCameraStream::SendResolution() {
  if (!watch_resolution_callback_ || !resolution_update_)
    return;
  watch_resolution_callback_(resolution_update_.value());
  watch_resolution_callback_ = {};
  resolution_update_.reset();
}

void FakeCameraStream::SendBufferCollection() {
  if (!watch_buffer_collection_callback_ || !new_buffer_collection_token_)
    return;
  watch_buffer_collection_callback_(
      std::move(new_buffer_collection_token_.value()));
  watch_buffer_collection_callback_ = {};
  new_buffer_collection_token_.reset();
}

void FakeCameraStream::SendNextFrame() {
  if (!get_next_frame_callback_ || !next_frame_)
    return;
  get_next_frame_callback_(std::move(next_frame_.value()));
  get_next_frame_callback_ = {};
  next_frame_.reset();
}

void FakeCameraStream::OnZxHandleSignalled(zx_handle_t handle,
                                           zx_signals_t signals) {
  EXPECT_EQ(signals, ZX_EVENTPAIR_PEER_CLOSED);

  // Find the buffer that corresponds to the |handle|.
  size_t index = buffers_.size();
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if (buffers_[i]->release_fence.get() == handle) {
      index = i;
      break;
    }
  }
  ASSERT_LT(index, buffers_.size());
  buffers_[index]->release_fence = {};
  buffers_[index]->release_fence_watch_controller.StopWatchingZxHandle();
  num_used_buffers_--;

  if (wait_free_buffer_run_loop_)
    wait_free_buffer_run_loop_->Quit();
}
FakeCameraDevice::FakeCameraDevice(FakeCameraStream* stream)
    : binding_(this), stream_(stream) {}

FakeCameraDevice::~FakeCameraDevice() = default;

void FakeCameraDevice::Bind(
    fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  binding_.Bind(std::move(request));
}

void FakeCameraDevice::GetIdentifier(GetIdentifierCallback callback) {
  callback("Fake Camera");
}

void FakeCameraDevice::GetConfigurations(GetConfigurationsCallback callback) {
  std::vector<fuchsia::camera3::Configuration> configurations(1);
  configurations[0].streams.resize(1);
  configurations[0].streams[0].frame_rate.numerator = 30;
  configurations[0].streams[0].frame_rate.denominator = 1;
  configurations[0].streams[0].image_format.pixel_format.type =
      fuchsia::sysmem::PixelFormatType::NV12;
  configurations[0].streams[0].image_format.coded_width = 640;
  configurations[0].streams[0].image_format.coded_height = 480;
  configurations[0].streams[0].image_format.bytes_per_row = 640;
  callback(std::move(configurations));
}

void FakeCameraDevice::ConnectToStream(
    uint32_t index,
    fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  EXPECT_EQ(index, 0U);
  stream_->Bind(std::move(request));
}

void FakeCameraDevice::NotImplemented_(const std::string& name) {
  ADD_FAILURE() << "NotImplemented_: " << name;
}

}  // namespace media