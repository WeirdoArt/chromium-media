// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/fake_video_capture_device.h"

#include <stddef.h>
#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_checker.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "media/audio/fake_audio_input_stream.h"
#include "media/base/video_frame.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"

namespace media {

namespace {
// Sweep at 600 deg/sec.
static const float kPacmanAngularVelocity = 600;
// Beep every 500 ms.
static const int kBeepInterval = 500;
// Gradient travels from bottom to top in 5 seconds.
static const float kGradientFrequency = 1.f / 5;

static const double kMinZoom = 100.0;
static const double kMaxZoom = 400.0;
static const double kZoomStep = 1.0;
static const double kInitialZoom = 100.0;

static const gfx::Size kSupportedSizesOrderedByIncreasingWidth[] = {
    gfx::Size(96, 96), gfx::Size(320, 240), gfx::Size(640, 480),
    gfx::Size(1280, 720), gfx::Size(1920, 1080)};
static const int kSupportedSizesCount =
    arraysize(kSupportedSizesOrderedByIncreasingWidth);

static gfx::Size SnapToSupportedSize(const gfx::Size& requested_size) {
  for (const gfx::Size& supported_size :
       kSupportedSizesOrderedByIncreasingWidth) {
    if (requested_size.width() <= supported_size.width())
      return supported_size;
  }
  return kSupportedSizesOrderedByIncreasingWidth[kSupportedSizesCount - 1];
}

// Represents the current state of a FakeVideoCaptureDevice.
// This is a separate struct because read-access to it is shared with several
// collaborating classes.
struct FakeDeviceState {
  FakeDeviceState(float zoom, float frame_rate, VideoPixelFormat pixel_format)
      : zoom(zoom),
        format(gfx::Size(), frame_rate, pixel_format, PIXEL_STORAGE_CPU) {}

  uint32_t zoom;
  VideoCaptureFormat format;
};

// Paints a "pacman-like" animated circle including textual information such
// as a frame count and timer.
class PacmanFramePainter {
 public:
  enum class Format { I420, SK_N32, Y16 };
  PacmanFramePainter(Format pixel_format,
                     const FakeDeviceState* fake_device_state);

  void PaintFrame(base::TimeDelta elapsed_time, uint8_t* target_buffer);

 private:
  void DrawGradientSquares(base::TimeDelta elapsed_time,
                           uint8_t* target_buffer);

  void DrawPacman(base::TimeDelta elapsed_time, uint8_t* target_buffer);

  const Format pixel_format_;
  const FakeDeviceState* fake_device_state_ = nullptr;
};

// Paints and delivers frames to a client, which is set via Initialize().
class FrameDeliverer {
 public:
  FrameDeliverer(std::unique_ptr<PacmanFramePainter> frame_painter)
      : frame_painter_(std::move(frame_painter)) {}
  virtual ~FrameDeliverer() {}
  virtual void Initialize(VideoPixelFormat pixel_format,
                          std::unique_ptr<VideoCaptureDevice::Client> client,
                          const FakeDeviceState* device_state) {
    client_ = std::move(client);
    device_state_ = device_state;
    client_->OnStarted();
  }
  virtual void Uninitialize() {
    client_.reset();
    device_state_ = nullptr;
  }
  virtual void PaintAndDeliverNextFrame(base::TimeDelta timestamp_to_paint) = 0;

 protected:
  base::TimeDelta CalculateTimeSinceFirstInvocation(base::TimeTicks now) {
    if (first_ref_time_.is_null())
      first_ref_time_ = now;
    return now - first_ref_time_;
  }

  PacmanFramePainter* frame_painter() { return frame_painter_.get(); }
  const FakeDeviceState* device_state() { return device_state_; }
  VideoCaptureDevice::Client* client() { return client_.get(); }

 private:
  const std::unique_ptr<PacmanFramePainter> frame_painter_;
  const FakeDeviceState* device_state_ = nullptr;
  std::unique_ptr<VideoCaptureDevice::Client> client_;
  base::TimeTicks first_ref_time_;
};

// Delivers frames using its own buffers via OnIncomingCapturedData().
class OwnBufferFrameDeliverer : public FrameDeliverer {
 public:
  OwnBufferFrameDeliverer(std::unique_ptr<PacmanFramePainter> frame_painter);
  ~OwnBufferFrameDeliverer() override;

  // Implementation of FrameDeliverer
  void Initialize(VideoPixelFormat pixel_format,
                  std::unique_ptr<VideoCaptureDevice::Client> client,
                  const FakeDeviceState* device_state) override;
  void Uninitialize() override;
  void PaintAndDeliverNextFrame(base::TimeDelta timestamp_to_paint) override;

 private:
  std::unique_ptr<uint8_t[]> buffer_;
};

// Delivers frames using buffers provided by the client via
// OnIncomingCapturedBuffer().
class ClientBufferFrameDeliverer : public FrameDeliverer {
 public:
  ClientBufferFrameDeliverer(std::unique_ptr<PacmanFramePainter> frame_painter);
  ~ClientBufferFrameDeliverer() override;

  // Implementation of FrameDeliverer
  void PaintAndDeliverNextFrame(base::TimeDelta timestamp_to_paint) override;
};

class JpegEncodingFrameDeliverer : public FrameDeliverer {
 public:
  JpegEncodingFrameDeliverer(std::unique_ptr<PacmanFramePainter> frame_painter);
  ~JpegEncodingFrameDeliverer() override;

  // Implementation of FrameDeliveryStrategy
  void Uninitialize() override;
  void PaintAndDeliverNextFrame(base::TimeDelta timestamp_to_paint) override;

 private:
  std::vector<uint8_t> sk_n32_buffer_;
  std::vector<unsigned char> jpeg_buffer_;
};

// Implements the photo functionality of a VideoCaptureDevice
class FakePhotoDevice {
 public:
  FakePhotoDevice(std::unique_ptr<PacmanFramePainter> painter,
                  const FakeDeviceState* fake_device_state);
  ~FakePhotoDevice();

  void GetPhotoCapabilities(
      VideoCaptureDevice::GetPhotoCapabilitiesCallback callback);
  void TakePhoto(VideoCaptureDevice::TakePhotoCallback callback,
                 base::TimeDelta elapsed_time);

 private:
  const std::unique_ptr<PacmanFramePainter> painter_;
  const FakeDeviceState* const fake_device_state_;
};

// Implementation of VideoCaptureDevice that generates test frames. This is
// useful for testing the video capture components without having to use real
// devices. The implementation schedules delayed tasks to itself to generate and
// deliver frames at the requested rate.
class FakeVideoCaptureDevice : public VideoCaptureDevice {
 public:
  FakeVideoCaptureDevice(
      std::unique_ptr<FrameDeliverer> frame_delivery_strategy,
      std::unique_ptr<FakePhotoDevice> photo_device,
      std::unique_ptr<FakeDeviceState> device_state);
  ~FakeVideoCaptureDevice() override;

  // VideoCaptureDevice implementation.
  void AllocateAndStart(const VideoCaptureParams& params,
                        std::unique_ptr<Client> client) override;
  void StopAndDeAllocate() override;
  void GetPhotoCapabilities(GetPhotoCapabilitiesCallback callback) override;
  void SetPhotoOptions(mojom::PhotoSettingsPtr settings,
                       SetPhotoOptionsCallback callback) override;
  void TakePhoto(TakePhotoCallback callback) override;

 private:
  void BeepAndScheduleNextCapture(base::TimeTicks expected_execution_time);
  void OnNextFrameDue(base::TimeTicks expected_execution_time, int session_id);

  const std::unique_ptr<FrameDeliverer> frame_deliverer_;
  const std::unique_ptr<FakePhotoDevice> photo_device_;
  const std::unique_ptr<FakeDeviceState> device_state_;
  int current_session_id_ = 0;

  // Time when the next beep occurs.
  base::TimeDelta beep_time_;
  // Time since the fake video started rendering frames.
  base::TimeDelta elapsed_time_;

  base::ThreadChecker thread_checker_;

  // FakeVideoCaptureDevice post tasks to itself for frame construction and
  // needs to deal with asynchronous StopAndDeallocate().
  base::WeakPtrFactory<FakeVideoCaptureDevice> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(FakeVideoCaptureDevice);
};

}  // anonymous namespace

// static
void FakeVideoCaptureDeviceMaker::GetSupportedSizes(
    std::vector<gfx::Size>* supported_sizes) {
  for (int i = 0; i < kSupportedSizesCount; i++)
    supported_sizes->push_back(kSupportedSizesOrderedByIncreasingWidth[i]);
}

// static
std::unique_ptr<VideoCaptureDevice> FakeVideoCaptureDeviceMaker::MakeInstance(
    PixelFormat pixel_format,
    DeliveryMode delivery_mode,
    float frame_rate) {
  auto device_state = base::MakeUnique<FakeDeviceState>(
      kInitialZoom, frame_rate,
      static_cast<media::VideoPixelFormat>(pixel_format));
  PacmanFramePainter::Format painter_format;
  switch (pixel_format) {
    case PixelFormat::I420:
      painter_format = PacmanFramePainter::Format::I420;
      break;
    case PixelFormat::Y16:
      painter_format = PacmanFramePainter::Format::Y16;
      break;
    case PixelFormat::MJPEG:
      painter_format = PacmanFramePainter::Format::SK_N32;
      break;
  }
  auto video_frame_painter =
      base::MakeUnique<PacmanFramePainter>(painter_format, device_state.get());

  std::unique_ptr<FrameDeliverer> frame_delivery_strategy;
  switch (delivery_mode) {
    case DeliveryMode::USE_DEVICE_INTERNAL_BUFFERS:
      if (pixel_format == PixelFormat::MJPEG) {
        frame_delivery_strategy = base::MakeUnique<JpegEncodingFrameDeliverer>(
            std::move(video_frame_painter));
      } else {
        frame_delivery_strategy = base::MakeUnique<OwnBufferFrameDeliverer>(
            std::move(video_frame_painter));
      }
      break;
    case DeliveryMode::USE_CLIENT_PROVIDED_BUFFERS:
      if (pixel_format == PixelFormat::MJPEG) {
        DLOG(ERROR) << "PixelFormat::MJPEG cannot be used in combination with "
                    << "USE_CLIENT_PROVIDED_BUFFERS.";
        return nullptr;
      }
      frame_delivery_strategy = base::MakeUnique<ClientBufferFrameDeliverer>(
          std::move(video_frame_painter));
      break;
  }

  auto photo_frame_painter = base::MakeUnique<PacmanFramePainter>(
      PacmanFramePainter::Format::SK_N32, device_state.get());
  auto photo_device = base::MakeUnique<FakePhotoDevice>(
      std::move(photo_frame_painter), device_state.get());

  return base::MakeUnique<FakeVideoCaptureDevice>(
      std::move(frame_delivery_strategy), std::move(photo_device),
      std::move(device_state));
}

PacmanFramePainter::PacmanFramePainter(Format pixel_format,
                                       const FakeDeviceState* fake_device_state)
    : pixel_format_(pixel_format), fake_device_state_(fake_device_state) {}

void PacmanFramePainter::PaintFrame(base::TimeDelta elapsed_time,
                                    uint8_t* target_buffer) {
  DrawPacman(elapsed_time, target_buffer);
  DrawGradientSquares(elapsed_time, target_buffer);
}

// Starting from top left, -45 deg gradient.  Value at point (row, column) is
// calculated as (top_left_value + (row + column) * step) % MAX_VALUE, where
// step is MAX_VALUE / (width + height).  MAX_VALUE is 255 (for 8 bit per
// component) or 65535 for Y16.
// This is handy for pixel tests where we use the squares to verify rendering.
void PacmanFramePainter::DrawGradientSquares(base::TimeDelta elapsed_time,
                                             uint8_t* target_buffer) {
  const int width = fake_device_state_->format.frame_size.width();
  const int height = fake_device_state_->format.frame_size.height();

  const int side = width / 16;  // square side length.
  DCHECK(side);
  const gfx::Point squares[] = {{0, 0},
                                {width - side, 0},
                                {0, height - side},
                                {width - side, height - side}};
  const float start =
      fmod(65536 * elapsed_time.InSecondsF() * kGradientFrequency, 65536);
  const float color_step = 65535 / static_cast<float>(width + height);
  for (const auto& corner : squares) {
    for (int y = corner.y(); y < corner.y() + side; ++y) {
      for (int x = corner.x(); x < corner.x() + side; ++x) {
        const unsigned int value =
            static_cast<unsigned int>(start + (x + y) * color_step) & 0xFFFF;
        size_t offset = (y * width) + x;
        switch (pixel_format_) {
          case Format::Y16:
            target_buffer[offset * sizeof(uint16_t)] = value & 0xFF;
            target_buffer[offset * sizeof(uint16_t) + 1] = value >> 8;
            break;
          case Format::SK_N32:
            target_buffer[offset * sizeof(uint32_t) + 1] = value >> 8;
            target_buffer[offset * sizeof(uint32_t) + 2] = value >> 8;
            target_buffer[offset * sizeof(uint32_t) + 3] = value >> 8;
            break;
          case Format::I420:
            target_buffer[offset] = value >> 8;
            break;
        }
      }
    }
  }
}

void PacmanFramePainter::DrawPacman(base::TimeDelta elapsed_time,
                                    uint8_t* target_buffer) {
  const int width = fake_device_state_->format.frame_size.width();
  const int height = fake_device_state_->format.frame_size.height();

  SkColorType colorspace = kAlpha_8_SkColorType;
  switch (pixel_format_) {
    case Format::I420:
      // Skia doesn't support painting in I420. Instead, paint an 8bpp
      // monochrome image to the beginning of |target_buffer|. This section of
      // |target_buffer| corresponds to the Y-plane of the YUV image. Do not
      // touch the U or V planes of |target_buffer|. Assuming they have been
      // initialized to 0, which corresponds to a green color tone, the result
      // will be an green-ish monochrome frame.
      colorspace = kAlpha_8_SkColorType;
      break;
    case Format::SK_N32:
      // SkColorType is RGBA on some platforms and BGRA on others.
      colorspace = kN32_SkColorType;
      break;
    case Format::Y16:
      // Skia doesn't support painting in Y16. Instead, paint an 8bpp monochrome
      // image to the beginning of |target_buffer|. Later, move the 8bit pixel
      // values to a position corresponding to the high byte values of 16bit
      // pixel values (assuming the byte order is little-endian).
      colorspace = kAlpha_8_SkColorType;
      break;
  }

  const SkImageInfo info =
      SkImageInfo::Make(width, height, colorspace, kOpaque_SkAlphaType);
  SkBitmap bitmap;
  bitmap.setInfo(info);
  bitmap.setPixels(target_buffer);
  SkPaint paint;
  paint.setStyle(SkPaint::kFill_Style);
  SkCanvas canvas(bitmap);

  const SkScalar unscaled_zoom = fake_device_state_->zoom / 100.f;
  SkMatrix matrix;
  matrix.setScale(unscaled_zoom, unscaled_zoom, width / 2, height / 2);
  canvas.setMatrix(matrix);

  // For the SK_N32 case, match the green color tone produced by the
  // I420 case.
  if (pixel_format_ == Format::SK_N32) {
    const SkRect full_frame = SkRect::MakeWH(width, height);
    paint.setARGB(255, 0, 127, 0);
    canvas.drawRect(full_frame, paint);
    paint.setColor(SK_ColorGREEN);
  }

  // Draw a sweeping circle to show an animation.
  const float end_angle =
      fmod(kPacmanAngularVelocity * elapsed_time.InSecondsF(), 361);
  const int radius = std::min(width, height) / 4;
  const SkRect rect = SkRect::MakeXYWH(width / 2 - radius, height / 2 - radius,
                                       2 * radius, 2 * radius);
  canvas.drawArc(rect, 0, end_angle, true, paint);

  // Draw current time.
  const int milliseconds = elapsed_time.InMilliseconds() % 1000;
  const int seconds = elapsed_time.InSeconds() % 60;
  const int minutes = elapsed_time.InMinutes() % 60;
  const int hours = elapsed_time.InHours();
  const int frame_count = elapsed_time.InMilliseconds() *
                          fake_device_state_->format.frame_rate / 1000;

  const std::string time_string =
      base::StringPrintf("%d:%02d:%02d:%03d %d", hours, minutes, seconds,
                         milliseconds, frame_count);
  canvas.scale(3, 3);
  canvas.drawText(time_string.data(), time_string.length(), 30, 20, paint);

  if (pixel_format_ == Format::Y16) {
    // Use 8 bit bitmap rendered to first half of the buffer as high byte values
    // for the whole buffer. Low byte values are not important.
    for (int i = (width * height) - 1; i >= 0; --i)
      target_buffer[i * 2 + 1] = target_buffer[i];
  }
}

FakePhotoDevice::FakePhotoDevice(std::unique_ptr<PacmanFramePainter> painter,
                                 const FakeDeviceState* fake_device_state)
    : painter_(std::move(painter)), fake_device_state_(fake_device_state) {}

FakePhotoDevice::~FakePhotoDevice() = default;

void FakePhotoDevice::TakePhoto(VideoCaptureDevice::TakePhotoCallback callback,
                                base::TimeDelta elapsed_time) {
  // Create a PNG-encoded frame and send it back to |callback|.
  auto required_sk_n32_buffer_size = VideoFrame::AllocationSize(
      PIXEL_FORMAT_ARGB, fake_device_state_->format.frame_size);
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[required_sk_n32_buffer_size]);
  memset(buffer.get(), 0, required_sk_n32_buffer_size);
  painter_->PaintFrame(elapsed_time, buffer.get());
  mojom::BlobPtr blob = mojom::Blob::New();
  const gfx::PNGCodec::ColorFormat encoding_source_format =
      (kN32_SkColorType == kRGBA_8888_SkColorType) ? gfx::PNGCodec::FORMAT_RGBA
                                                   : gfx::PNGCodec::FORMAT_BGRA;
  const bool result = gfx::PNGCodec::Encode(
      buffer.get(), encoding_source_format,
      fake_device_state_->format.frame_size,
      VideoFrame::RowBytes(0 /* plane */, PIXEL_FORMAT_ARGB,
                           fake_device_state_->format.frame_size.width()),
      true /* discard_transparency */, std::vector<gfx::PNGCodec::Comment>(),
      &blob->data);
  DCHECK(result);

  blob->mime_type = "image/png";
  callback.Run(std::move(blob));
}

FakeVideoCaptureDevice::FakeVideoCaptureDevice(
    std::unique_ptr<FrameDeliverer> frame_delivery_strategy,
    std::unique_ptr<FakePhotoDevice> photo_device,
    std::unique_ptr<FakeDeviceState> device_state)
    : frame_deliverer_(std::move(frame_delivery_strategy)),
      photo_device_(std::move(photo_device)),
      device_state_(std::move(device_state)),
      weak_factory_(this) {}

FakeVideoCaptureDevice::~FakeVideoCaptureDevice() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void FakeVideoCaptureDevice::AllocateAndStart(
    const VideoCaptureParams& params,
    std::unique_ptr<VideoCaptureDevice::Client> client) {
  DCHECK(thread_checker_.CalledOnValidThread());

  beep_time_ = base::TimeDelta();
  elapsed_time_ = base::TimeDelta();
  device_state_->format.frame_size =
      SnapToSupportedSize(params.requested_format.frame_size);
  frame_deliverer_->Initialize(device_state_->format.pixel_format,
                               std::move(client), device_state_.get());
  current_session_id_++;
  BeepAndScheduleNextCapture(base::TimeTicks::Now());
}

void FakeVideoCaptureDevice::StopAndDeAllocate() {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Invalidate WeakPtr to stop the perpetual scheduling of tasks.
  weak_factory_.InvalidateWeakPtrs();
  frame_deliverer_->Uninitialize();
}

void FakeVideoCaptureDevice::GetPhotoCapabilities(
    GetPhotoCapabilitiesCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  photo_device_->GetPhotoCapabilities(std::move(callback));
}

void FakePhotoDevice::GetPhotoCapabilities(
    VideoCaptureDevice::GetPhotoCapabilitiesCallback callback) {
  mojom::PhotoCapabilitiesPtr photo_capabilities =
      mojom::PhotoCapabilities::New();
  photo_capabilities->iso = mojom::Range::New();
  photo_capabilities->iso->current = 100.0;
  photo_capabilities->iso->max = 100.0;
  photo_capabilities->iso->min = 100.0;
  photo_capabilities->iso->step = 0.0;
  photo_capabilities->height = mojom::Range::New();
  photo_capabilities->height->current =
      fake_device_state_->format.frame_size.height();
  photo_capabilities->height->max = 1080.0;
  photo_capabilities->height->min = 96.0;
  photo_capabilities->height->step = 1.0;
  photo_capabilities->width = mojom::Range::New();
  photo_capabilities->width->current =
      fake_device_state_->format.frame_size.width();
  photo_capabilities->width->max = 1920.0;
  photo_capabilities->width->min = 96.0;
  photo_capabilities->width->step = 1.0;
  photo_capabilities->zoom = mojom::Range::New();
  photo_capabilities->zoom->current = fake_device_state_->zoom;
  photo_capabilities->zoom->max = kMaxZoom;
  photo_capabilities->zoom->min = kMinZoom;
  photo_capabilities->zoom->step = kZoomStep;
  photo_capabilities->focus_mode = mojom::MeteringMode::NONE;
  photo_capabilities->exposure_mode = mojom::MeteringMode::NONE;
  photo_capabilities->exposure_compensation = mojom::Range::New();
  photo_capabilities->white_balance_mode = mojom::MeteringMode::NONE;
  photo_capabilities->fill_light_mode = mojom::FillLightMode::NONE;
  photo_capabilities->red_eye_reduction = false;
  photo_capabilities->color_temperature = mojom::Range::New();
  photo_capabilities->brightness = media::mojom::Range::New();
  photo_capabilities->contrast = media::mojom::Range::New();
  photo_capabilities->saturation = media::mojom::Range::New();
  photo_capabilities->sharpness = media::mojom::Range::New();
  callback.Run(std::move(photo_capabilities));
}

void FakeVideoCaptureDevice::SetPhotoOptions(mojom::PhotoSettingsPtr settings,
                                             SetPhotoOptionsCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (settings->has_zoom) {
    device_state_->zoom =
        std::max(kMinZoom, std::min(settings->zoom, kMaxZoom));
  }

  callback.Run(true);
}

void FakeVideoCaptureDevice::TakePhoto(TakePhotoCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&FakePhotoDevice::TakePhoto,
                            base::Unretained(photo_device_.get()),
                            base::Passed(&callback), elapsed_time_));
}

OwnBufferFrameDeliverer::OwnBufferFrameDeliverer(
    std::unique_ptr<PacmanFramePainter> frame_painter)
    : FrameDeliverer(std::move(frame_painter)) {}

OwnBufferFrameDeliverer::~OwnBufferFrameDeliverer() = default;

void OwnBufferFrameDeliverer::Initialize(
    VideoPixelFormat pixel_format,
    std::unique_ptr<VideoCaptureDevice::Client> client,
    const FakeDeviceState* device_state) {
  FrameDeliverer::Initialize(pixel_format, std::move(client), device_state);
  buffer_.reset(new uint8_t[VideoFrame::AllocationSize(
      pixel_format, device_state->format.frame_size)]);
}

void OwnBufferFrameDeliverer::Uninitialize() {
  FrameDeliverer::Uninitialize();
  buffer_.reset();
}

void OwnBufferFrameDeliverer::PaintAndDeliverNextFrame(
    base::TimeDelta timestamp_to_paint) {
  if (!client())
    return;
  const size_t frame_size = device_state()->format.ImageAllocationSize();
  memset(buffer_.get(), 0, frame_size);
  frame_painter()->PaintFrame(timestamp_to_paint, buffer_.get());
  base::TimeTicks now = base::TimeTicks::Now();
  client()->OnIncomingCapturedData(buffer_.get(), frame_size,
                                   device_state()->format, 0 /* rotation */,
                                   now, CalculateTimeSinceFirstInvocation(now));
}

ClientBufferFrameDeliverer::ClientBufferFrameDeliverer(
    std::unique_ptr<PacmanFramePainter> frame_painter)
    : FrameDeliverer(std::move(frame_painter)) {}

ClientBufferFrameDeliverer::~ClientBufferFrameDeliverer() = default;

void ClientBufferFrameDeliverer::PaintAndDeliverNextFrame(
    base::TimeDelta timestamp_to_paint) {
  if (!client())
    return;

  const int arbitrary_frame_feedback_id = 0;
  auto capture_buffer = client()->ReserveOutputBuffer(
      device_state()->format.frame_size, device_state()->format.pixel_format,
      device_state()->format.pixel_storage, arbitrary_frame_feedback_id);
  DLOG_IF(ERROR, !capture_buffer.is_valid())
      << "Couldn't allocate Capture Buffer";
  auto buffer_access =
      capture_buffer.handle_provider->GetHandleForInProcessAccess();
  DCHECK(buffer_access->data()) << "Buffer has NO backing memory";

  DCHECK_EQ(PIXEL_STORAGE_CPU, device_state()->format.pixel_storage);

  uint8_t* data_ptr = buffer_access->data();
  memset(data_ptr, 0, buffer_access->mapped_size());
  frame_painter()->PaintFrame(timestamp_to_paint, data_ptr);

  base::TimeTicks now = base::TimeTicks::Now();
  client()->OnIncomingCapturedBuffer(std::move(capture_buffer),
                                     device_state()->format, now,
                                     CalculateTimeSinceFirstInvocation(now));
}

JpegEncodingFrameDeliverer::JpegEncodingFrameDeliverer(
    std::unique_ptr<PacmanFramePainter> frame_painter)
    : FrameDeliverer(std::move(frame_painter)) {}

JpegEncodingFrameDeliverer::~JpegEncodingFrameDeliverer() = default;

void JpegEncodingFrameDeliverer::Uninitialize() {
  FrameDeliverer::Uninitialize();
  sk_n32_buffer_.clear();
  jpeg_buffer_.clear();
}

void JpegEncodingFrameDeliverer::PaintAndDeliverNextFrame(
    base::TimeDelta timestamp_to_paint) {
  if (!client())
    return;

  auto required_sk_n32_buffer_size = VideoFrame::AllocationSize(
      PIXEL_FORMAT_ARGB, device_state()->format.frame_size);
  sk_n32_buffer_.resize(required_sk_n32_buffer_size);
  memset(&sk_n32_buffer_[0], 0, required_sk_n32_buffer_size);

  frame_painter()->PaintFrame(timestamp_to_paint, &sk_n32_buffer_[0]);

  static const int kQuality = 75;
  const gfx::JPEGCodec::ColorFormat encoding_source_format =
      (kN32_SkColorType == kRGBA_8888_SkColorType)
          ? gfx::JPEGCodec::FORMAT_RGBA
          : gfx::JPEGCodec::FORMAT_BGRA;
  bool success = gfx::JPEGCodec::Encode(
      &sk_n32_buffer_[0], encoding_source_format,
      device_state()->format.frame_size.width(),
      device_state()->format.frame_size.height(),
      VideoFrame::RowBytes(0 /* plane */, PIXEL_FORMAT_ARGB,
                           device_state()->format.frame_size.width()),
      kQuality, &jpeg_buffer_);
  if (!success) {
    DLOG(ERROR) << "Jpeg encoding failed";
    return;
  }

  const size_t frame_size = jpeg_buffer_.size();
  base::TimeTicks now = base::TimeTicks::Now();
  client()->OnIncomingCapturedData(&jpeg_buffer_[0], frame_size,
                                   device_state()->format, 0 /* rotation */,
                                   now, CalculateTimeSinceFirstInvocation(now));
}

void FakeVideoCaptureDevice::BeepAndScheduleNextCapture(
    base::TimeTicks expected_execution_time) {
  DCHECK(thread_checker_.CalledOnValidThread());
  const base::TimeDelta beep_interval =
      base::TimeDelta::FromMilliseconds(kBeepInterval);
  const base::TimeDelta frame_interval =
      base::TimeDelta::FromMicroseconds(1e6 / device_state_->format.frame_rate);
  beep_time_ += frame_interval;
  elapsed_time_ += frame_interval;

  // Generate a synchronized beep twice per second.
  if (beep_time_ >= beep_interval) {
    FakeAudioInputStream::BeepOnce();
    beep_time_ -= beep_interval;
  }

  // Reschedule next CaptureTask.
  const base::TimeTicks current_time = base::TimeTicks::Now();
  // Don't accumulate any debt if we are lagging behind - just post the next
  // frame immediately and continue as normal.
  const base::TimeTicks next_execution_time =
      std::max(current_time, expected_execution_time + frame_interval);
  const base::TimeDelta delay = next_execution_time - current_time;
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&FakeVideoCaptureDevice::OnNextFrameDue,
                 weak_factory_.GetWeakPtr(), next_execution_time,
                 current_session_id_),
      delay);
}

void FakeVideoCaptureDevice::OnNextFrameDue(
    base::TimeTicks expected_execution_time,
    int session_id) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (session_id != current_session_id_)
    return;

  frame_deliverer_->PaintAndDeliverNextFrame(elapsed_time_);
  BeepAndScheduleNextCapture(expected_execution_time);
}

}  // namespace media
