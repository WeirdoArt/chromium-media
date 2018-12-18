// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/v4l2/v4l2_slice_video_decode_accelerator.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <memory>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/numerics/safe_conversions.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/media_switches.h"
#include "media/base/unaligned_shared_memory.h"
#include "media/base/video_types.h"
#include "media/gpu/macros.h"
#include "media/gpu/v4l2/v4l2_decode_surface.h"
#include "media/gpu/v4l2/v4l2_h264_accelerator.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_image.h"
#include "ui/gl/scoped_binders.h"

#define NOTIFY_ERROR(x)                       \
  do {                                        \
    VLOGF(1) << "Setting error state: " << x; \
    SetErrorState(x);                         \
  } while (0)

#define IOCTL_OR_ERROR_RETURN_VALUE(type, arg, value, type_str) \
  do {                                                          \
    if (device_->Ioctl(type, arg) != 0) {                       \
      VPLOGF(1) << "ioctl() failed: " << type_str;              \
      return value;                                             \
    }                                                           \
  } while (0)

#define IOCTL_OR_ERROR_RETURN(type, arg) \
  IOCTL_OR_ERROR_RETURN_VALUE(type, arg, ((void)0), #type)

#define IOCTL_OR_ERROR_RETURN_FALSE(type, arg) \
  IOCTL_OR_ERROR_RETURN_VALUE(type, arg, false, #type)

#define IOCTL_OR_LOG_ERROR(type, arg)           \
  do {                                          \
    if (device_->Ioctl(type, arg) != 0)         \
      VPLOGF(1) << "ioctl() failed: " << #type; \
  } while (0)

namespace media {

// static
const uint32_t V4L2SliceVideoDecodeAccelerator::supported_input_fourccs_[] = {
    V4L2_PIX_FMT_H264_SLICE, V4L2_PIX_FMT_VP8_FRAME, V4L2_PIX_FMT_VP9_FRAME,
};

V4L2SliceVideoDecodeAccelerator::InputRecord::InputRecord()
    : input_id(-1),
      address(nullptr),
      length(0),
      bytes_used(0),
      at_device(false) {}

V4L2SliceVideoDecodeAccelerator::OutputRecord::OutputRecord()
    : at_device(false),
      at_client(false),
      num_times_sent_to_client(0),
      picture_id(-1),
      texture_id(0),
      cleared(false) {}

V4L2SliceVideoDecodeAccelerator::OutputRecord::OutputRecord(OutputRecord&&) =
    default;

V4L2SliceVideoDecodeAccelerator::OutputRecord::~OutputRecord() = default;

struct V4L2SliceVideoDecodeAccelerator::BitstreamBufferRef {
  BitstreamBufferRef(
      base::WeakPtr<VideoDecodeAccelerator::Client>& client,
      const scoped_refptr<base::SingleThreadTaskRunner>& client_task_runner,
      scoped_refptr<DecoderBuffer> buffer,
      int32_t input_id);
  ~BitstreamBufferRef();
  const base::WeakPtr<VideoDecodeAccelerator::Client> client;
  const scoped_refptr<base::SingleThreadTaskRunner> client_task_runner;
  scoped_refptr<DecoderBuffer> buffer;
  off_t bytes_used;
  const int32_t input_id;
};

V4L2SliceVideoDecodeAccelerator::BitstreamBufferRef::BitstreamBufferRef(
    base::WeakPtr<VideoDecodeAccelerator::Client>& client,
    const scoped_refptr<base::SingleThreadTaskRunner>& client_task_runner,
    scoped_refptr<DecoderBuffer> buffer,
    int32_t input_id)
    : client(client),
      client_task_runner(client_task_runner),
      buffer(std::move(buffer)),
      bytes_used(0),
      input_id(input_id) {}

V4L2SliceVideoDecodeAccelerator::BitstreamBufferRef::~BitstreamBufferRef() {
  if (input_id >= 0) {
    DVLOGF(5) << "returning input_id: " << input_id;
    client_task_runner->PostTask(
        FROM_HERE,
        base::Bind(&VideoDecodeAccelerator::Client::NotifyEndOfBitstreamBuffer,
                   client, input_id));
  }
}

V4L2SliceVideoDecodeAccelerator::PictureRecord::PictureRecord(
    bool cleared,
    const Picture& picture)
    : cleared(cleared), picture(picture) {}

V4L2SliceVideoDecodeAccelerator::PictureRecord::~PictureRecord() {}

class V4L2SliceVideoDecodeAccelerator::V4L2VP8Accelerator
    : public VP8Decoder::VP8Accelerator {
 public:
  explicit V4L2VP8Accelerator(V4L2DecodeSurfaceHandler* surface_handler,
                              V4L2Device* device);
  ~V4L2VP8Accelerator() override;

  // VP8Decoder::VP8Accelerator implementation.
  scoped_refptr<VP8Picture> CreateVP8Picture() override;

  bool SubmitDecode(scoped_refptr<VP8Picture> pic,
                    const Vp8ReferenceFrameVector& reference_frames) override;

  bool OutputPicture(const scoped_refptr<VP8Picture>& pic) override;

 private:
  scoped_refptr<V4L2DecodeSurface> VP8PictureToV4L2DecodeSurface(
      const scoped_refptr<VP8Picture>& pic);

  V4L2DecodeSurfaceHandler* const surface_handler_;
  V4L2Device* const device_;

  DISALLOW_COPY_AND_ASSIGN(V4L2VP8Accelerator);
};

class V4L2SliceVideoDecodeAccelerator::V4L2VP9Accelerator
    : public VP9Decoder::VP9Accelerator {
 public:
  explicit V4L2VP9Accelerator(V4L2DecodeSurfaceHandler* surface_handler,
                              V4L2Device* device);
  ~V4L2VP9Accelerator() override;

  // VP9Decoder::VP9Accelerator implementation.
  scoped_refptr<VP9Picture> CreateVP9Picture() override;

  bool SubmitDecode(const scoped_refptr<VP9Picture>& pic,
                    const Vp9SegmentationParams& segm_params,
                    const Vp9LoopFilterParams& lf_params,
                    const std::vector<scoped_refptr<VP9Picture>>& ref_pictures,
                    const base::Closure& done_cb) override;

  bool OutputPicture(const scoped_refptr<VP9Picture>& pic) override;

  bool GetFrameContext(const scoped_refptr<VP9Picture>& pic,
                       Vp9FrameContext* frame_ctx) override;

  bool IsFrameContextRequired() const override {
    return device_needs_frame_context_;
  }

 private:
  scoped_refptr<V4L2DecodeSurface> VP9PictureToV4L2DecodeSurface(
      const scoped_refptr<VP9Picture>& pic);

  bool device_needs_frame_context_;

  V4L2DecodeSurfaceHandler* const surface_handler_;
  V4L2Device* const device_;

  DISALLOW_COPY_AND_ASSIGN(V4L2VP9Accelerator);
};

// Codec-specific subclasses of software decoder picture classes.
// This allows us to keep decoders oblivious of our implementation details.
class V4L2VP8Picture : public VP8Picture {
 public:
  explicit V4L2VP8Picture(const scoped_refptr<V4L2DecodeSurface>& dec_surface);

  V4L2VP8Picture* AsV4L2VP8Picture() override { return this; }
  scoped_refptr<V4L2DecodeSurface> dec_surface() { return dec_surface_; }

 private:
  ~V4L2VP8Picture() override;

  scoped_refptr<V4L2DecodeSurface> dec_surface_;

  DISALLOW_COPY_AND_ASSIGN(V4L2VP8Picture);
};

V4L2VP8Picture::V4L2VP8Picture(
    const scoped_refptr<V4L2DecodeSurface>& dec_surface)
    : dec_surface_(dec_surface) {}

V4L2VP8Picture::~V4L2VP8Picture() {}

class V4L2VP9Picture : public VP9Picture {
 public:
  explicit V4L2VP9Picture(const scoped_refptr<V4L2DecodeSurface>& dec_surface);

  V4L2VP9Picture* AsV4L2VP9Picture() override { return this; }
  scoped_refptr<V4L2DecodeSurface> dec_surface() { return dec_surface_; }

 private:
  ~V4L2VP9Picture() override;

  scoped_refptr<VP9Picture> CreateDuplicate() override;

  scoped_refptr<V4L2DecodeSurface> dec_surface_;

  DISALLOW_COPY_AND_ASSIGN(V4L2VP9Picture);
};

V4L2VP9Picture::V4L2VP9Picture(
    const scoped_refptr<V4L2DecodeSurface>& dec_surface)
    : dec_surface_(dec_surface) {}

V4L2VP9Picture::~V4L2VP9Picture() {}

scoped_refptr<VP9Picture> V4L2VP9Picture::CreateDuplicate() {
  return new V4L2VP9Picture(dec_surface_);
}

V4L2SliceVideoDecodeAccelerator::V4L2SliceVideoDecodeAccelerator(
    const scoped_refptr<V4L2Device>& device,
    EGLDisplay egl_display,
    const BindGLImageCallback& bind_image_cb,
    const MakeGLContextCurrentCallback& make_context_current_cb)
    : input_planes_count_(0),
      output_planes_count_(0),
      child_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      device_(device),
      decoder_thread_("V4L2SliceVideoDecodeAcceleratorThread"),
      device_poll_thread_("V4L2SliceVideoDecodeAcceleratorDevicePollThread"),
      input_streamon_(false),
      input_buffer_queued_count_(0),
      output_streamon_(false),
      output_buffer_queued_count_(0),
      video_profile_(VIDEO_CODEC_PROFILE_UNKNOWN),
      input_format_fourcc_(0),
      output_format_fourcc_(0),
      state_(kUninitialized),
      output_mode_(Config::OutputMode::ALLOCATE),
      decoder_flushing_(false),
      decoder_resetting_(false),
      surface_set_change_pending_(false),
      picture_clearing_count_(0),
      egl_display_(egl_display),
      bind_image_cb_(bind_image_cb),
      make_context_current_cb_(make_context_current_cb),
      weak_this_factory_(this) {
  weak_this_ = weak_this_factory_.GetWeakPtr();
}

V4L2SliceVideoDecodeAccelerator::~V4L2SliceVideoDecodeAccelerator() {
  DVLOGF(2);

  DCHECK(child_task_runner_->BelongsToCurrentThread());
  DCHECK(!decoder_thread_.IsRunning());
  DCHECK(!device_poll_thread_.IsRunning());

  DCHECK(input_buffer_map_.empty());
  DCHECK(output_buffer_map_.empty());
}

void V4L2SliceVideoDecodeAccelerator::NotifyError(Error error) {
  // Notifying the client should only happen from the client's thread.
  if (!child_task_runner_->BelongsToCurrentThread()) {
    child_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&V4L2SliceVideoDecodeAccelerator::NotifyError,
                                  weak_this_, error));
    return;
  }

  // Notify the decoder's client an error has occurred.
  if (client_) {
    client_->NotifyError(error);
    client_ptr_factory_.reset();
  }
}

bool V4L2SliceVideoDecodeAccelerator::Initialize(const Config& config,
                                                 Client* client) {
  VLOGF(2) << "profile: " << config.profile;
  DCHECK(child_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(state_, kUninitialized);

  if (config.is_encrypted()) {
    NOTREACHED() << "Encrypted streams are not supported for this VDA";
    return false;
  }

  if (config.output_mode != Config::OutputMode::ALLOCATE &&
      config.output_mode != Config::OutputMode::IMPORT) {
    NOTREACHED() << "Only ALLOCATE and IMPORT OutputModes are supported";
    return false;
  }

  client_ptr_factory_.reset(
      new base::WeakPtrFactory<VideoDecodeAccelerator::Client>(client));
  client_ = client_ptr_factory_->GetWeakPtr();
  // If we haven't been set up to decode on separate thread via
  // TryToSetupDecodeOnSeparateThread(), use the main thread/client for
  // decode tasks.
  if (!decode_task_runner_) {
    decode_task_runner_ = child_task_runner_;
    DCHECK(!decode_client_);
    decode_client_ = client_;
  }

  // We need the context to be initialized to query extensions.
  if (make_context_current_cb_) {
    if (egl_display_ == EGL_NO_DISPLAY) {
      VLOGF(1) << "could not get EGLDisplay";
      return false;
    }

    if (!make_context_current_cb_.Run()) {
      VLOGF(1) << "could not make context current";
      return false;
    }

    if (!gl::g_driver_egl.ext.b_EGL_KHR_fence_sync) {
      VLOGF(1) << "context does not have EGL_KHR_fence_sync";
      return false;
    }
  } else {
    DVLOGF(2) << "No GL callbacks provided, initializing without GL support";
  }

  video_profile_ = config.profile;

  // TODO(posciak): This needs to be queried once supported.
  input_planes_count_ = 1;
  output_planes_count_ = 1;

  input_format_fourcc_ =
      V4L2Device::VideoCodecProfileToV4L2PixFmt(video_profile_, true);

  if (!device_->Open(V4L2Device::Type::kDecoder, input_format_fourcc_)) {
    VLOGF(1) << "Failed to open device for profile: " << config.profile
             << " fourcc: " << FourccToString(input_format_fourcc_);
    return false;
  }

  if (video_profile_ >= H264PROFILE_MIN && video_profile_ <= H264PROFILE_MAX) {
    decoder_.reset(new H264Decoder(
        std::make_unique<V4L2H264Accelerator>(this, device_.get())));
  } else if (video_profile_ >= VP8PROFILE_MIN &&
             video_profile_ <= VP8PROFILE_MAX) {
    decoder_.reset(new VP8Decoder(
        std::make_unique<V4L2VP8Accelerator>(this, device_.get())));
  } else if (video_profile_ >= VP9PROFILE_MIN &&
             video_profile_ <= VP9PROFILE_MAX) {
    decoder_.reset(new VP9Decoder(
        std::make_unique<V4L2VP9Accelerator>(this, device_.get())));
  } else {
    NOTREACHED() << "Unsupported profile " << GetProfileName(video_profile_);
    return false;
  }

  // Capabilities check.
  struct v4l2_capability caps;
  const __u32 kCapsRequired = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
  IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_QUERYCAP, &caps);
  if ((caps.capabilities & kCapsRequired) != kCapsRequired) {
    VLOGF(1) << "ioctl() failed: VIDIOC_QUERYCAP"
             << ", caps check failed: 0x" << std::hex << caps.capabilities;
    return false;
  }

  if (!SetupFormats())
    return false;

  if (!decoder_thread_.Start()) {
    VLOGF(1) << "device thread failed to start";
    return false;
  }
  decoder_thread_task_runner_ = decoder_thread_.task_runner();

  state_ = kInitialized;
  output_mode_ = config.output_mode;

  // InitializeTask will NOTIFY_ERROR on failure.
  decoder_thread_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&V4L2SliceVideoDecodeAccelerator::InitializeTask,
                     base::Unretained(this)));

  VLOGF(2) << "V4L2SliceVideoDecodeAccelerator initialized";
  return true;
}

void V4L2SliceVideoDecodeAccelerator::InitializeTask() {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(state_, kInitialized);

  if (IsDestroyPending())
    return;

  if (!CreateInputBuffers())
    NOTIFY_ERROR(PLATFORM_FAILURE);

  // Output buffers will be created once decoder gives us information
  // about their size and required count.
  state_ = kDecoding;
}

void V4L2SliceVideoDecodeAccelerator::Destroy() {
  VLOGF(2);
  DCHECK(child_task_runner_->BelongsToCurrentThread());

  // Signal any waiting/sleeping tasks to early exit as soon as possible to
  // avoid waiting too long for the decoder_thread_ to Stop().
  destroy_pending_.Signal();

  if (decoder_thread_.IsRunning()) {
    decoder_thread_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&V4L2SliceVideoDecodeAccelerator::DestroyTask,
                                  base::Unretained(this)));

    // Wait for tasks to finish/early-exit.
    decoder_thread_.Stop();
  }

  delete this;
  VLOGF(2) << "Destroyed";
}

void V4L2SliceVideoDecodeAccelerator::DestroyTask() {
  DVLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  state_ = kDestroying;

  decoder_->Reset();

  decoder_current_bitstream_buffer_.reset();
  while (!decoder_input_queue_.empty())
    decoder_input_queue_.pop();

  // Stop streaming and the device_poll_thread_.
  StopDevicePoll(false);

  DestroyInputBuffers();
  DestroyOutputs(false);

  DCHECK(surfaces_at_device_.empty());
  DCHECK(surfaces_at_display_.empty());
  DCHECK(decoder_display_queue_.empty());
}

bool V4L2SliceVideoDecodeAccelerator::SetupFormats() {
  DCHECK_EQ(state_, kUninitialized);

  size_t input_size;
  gfx::Size max_resolution, min_resolution;
  device_->GetSupportedResolution(input_format_fourcc_, &min_resolution,
                                  &max_resolution);
  if (max_resolution.width() > 1920 && max_resolution.height() > 1088)
    input_size = kInputBufferMaxSizeFor4k;
  else
    input_size = kInputBufferMaxSizeFor1080p;

  struct v4l2_fmtdesc fmtdesc;
  memset(&fmtdesc, 0, sizeof(fmtdesc));
  fmtdesc.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  bool is_format_supported = false;
  while (device_->Ioctl(VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
    if (fmtdesc.pixelformat == input_format_fourcc_) {
      is_format_supported = true;
      break;
    }
    ++fmtdesc.index;
  }

  if (!is_format_supported) {
    DVLOGF(1) << "Input fourcc " << input_format_fourcc_
              << " not supported by device.";
    return false;
  }

  struct v4l2_format format;
  memset(&format, 0, sizeof(format));
  format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  format.fmt.pix_mp.pixelformat = input_format_fourcc_;
  format.fmt.pix_mp.plane_fmt[0].sizeimage = input_size;
  format.fmt.pix_mp.num_planes = input_planes_count_;
  IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_S_FMT, &format);
  DCHECK_EQ(format.fmt.pix_mp.pixelformat, input_format_fourcc_);

  // We have to set up the format for output, because the driver may not allow
  // changing it once we start streaming; whether it can support our chosen
  // output format or not may depend on the input format.
  memset(&fmtdesc, 0, sizeof(fmtdesc));
  fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  output_format_fourcc_ = 0;
  while (device_->Ioctl(VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
    if (device_->CanCreateEGLImageFrom(fmtdesc.pixelformat)) {
      output_format_fourcc_ = fmtdesc.pixelformat;
      break;
    }
    ++fmtdesc.index;
  }

  if (output_format_fourcc_ == 0) {
    VLOGF(1) << "Could not find a usable output format";
    return false;
  }

  // Only set fourcc for output; resolution, etc., will come from the
  // driver once it extracts it from the stream.
  memset(&format, 0, sizeof(format));
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  format.fmt.pix_mp.pixelformat = output_format_fourcc_;
  format.fmt.pix_mp.num_planes = output_planes_count_;
  IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_S_FMT, &format);
  DCHECK_EQ(format.fmt.pix_mp.pixelformat, output_format_fourcc_);

  return true;
}

bool V4L2SliceVideoDecodeAccelerator::CreateInputBuffers() {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  DCHECK(!input_streamon_);
  DCHECK(input_buffer_map_.empty());

  struct v4l2_requestbuffers reqbufs;
  memset(&reqbufs, 0, sizeof(reqbufs));
  reqbufs.count = kNumInputBuffers;
  reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  reqbufs.memory = V4L2_MEMORY_MMAP;
  IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_REQBUFS, &reqbufs);
  if (reqbufs.count < kNumInputBuffers) {
    VLOGF(1) << "Could not allocate enough output buffers";
    return false;
  }
  input_buffer_map_.resize(reqbufs.count);
  for (size_t i = 0; i < input_buffer_map_.size(); ++i) {
    free_input_buffers_.push_back(i);

    // Query for the MEMORY_MMAP pointer.
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    struct v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    memset(planes, 0, sizeof(planes));
    buffer.index = i;
    buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.m.planes = planes;
    buffer.length = input_planes_count_;
    IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_QUERYBUF, &buffer);
    void* address = device_->Mmap(nullptr,
                                  buffer.m.planes[0].length,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED,
                                  buffer.m.planes[0].m.mem_offset);
    if (address == MAP_FAILED) {
      VPLOGF(1) << "mmap() failed";
      return false;
    }
    input_buffer_map_[i].address = address;
    input_buffer_map_[i].length = buffer.m.planes[0].length;
  }

  return true;
}

bool V4L2SliceVideoDecodeAccelerator::CreateOutputBuffers() {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  DCHECK(!output_streamon_);
  DCHECK(output_buffer_map_.empty());
  DCHECK(surfaces_at_display_.empty());
  DCHECK(surfaces_at_device_.empty());

  gfx::Size pic_size = decoder_->GetPicSize();
  size_t num_pictures = decoder_->GetRequiredNumOfPictures();

  DCHECK_GT(num_pictures, 0u);
  DCHECK(!pic_size.IsEmpty());

  struct v4l2_format format;
  memset(&format, 0, sizeof(format));
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  format.fmt.pix_mp.pixelformat = output_format_fourcc_;
  format.fmt.pix_mp.width = pic_size.width();
  format.fmt.pix_mp.height = pic_size.height();
  format.fmt.pix_mp.num_planes = output_planes_count_;

  if (device_->Ioctl(VIDIOC_S_FMT, &format) != 0) {
    VPLOGF(1) << "Failed setting format to: " << output_format_fourcc_;
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return false;
  }

  coded_size_.SetSize(base::checked_cast<int>(format.fmt.pix_mp.width),
                      base::checked_cast<int>(format.fmt.pix_mp.height));
  DCHECK_EQ(coded_size_.width() % 16, 0);
  DCHECK_EQ(coded_size_.height() % 16, 0);

  if (!gfx::Rect(coded_size_).Contains(gfx::Rect(pic_size))) {
    VLOGF(1) << "Got invalid adjusted coded size: " << coded_size_.ToString();
    return false;
  }

  DVLOGF(3) << "buffer_count=" << num_pictures
            << ", pic size=" << pic_size.ToString()
            << ", coded size=" << coded_size_.ToString();

  VideoPixelFormat pixel_format =
      V4L2Device::V4L2PixFmtToVideoPixelFormat(output_format_fourcc_);

  child_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&VideoDecodeAccelerator::Client::ProvidePictureBuffers,
                 client_, num_pictures, pixel_format, 1, coded_size_,
                 device_->GetTextureTarget()));

  // Go into kAwaitingPictureBuffers to prevent us from doing any more decoding
  // or event handling while we are waiting for AssignPictureBuffers(). Not
  // having Pictures available would not have prevented us from making decoding
  // progress entirely e.g. in the case of H.264 where we could further decode
  // non-slice NALUs and could even get another resolution change before we were
  // done with this one. After we get the buffers, we'll go back into kIdle and
  // kick off further event processing, and eventually go back into kDecoding
  // once no more events are pending (if any).
  state_ = kAwaitingPictureBuffers;
  return true;
}

void V4L2SliceVideoDecodeAccelerator::DestroyInputBuffers() {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread() ||
         !decoder_thread_.IsRunning());
  DCHECK(!input_streamon_);

  if (input_buffer_map_.empty())
    return;

  for (auto& input_record : input_buffer_map_) {
    if (input_record.address != nullptr)
      device_->Munmap(input_record.address, input_record.length);
  }

  struct v4l2_requestbuffers reqbufs;
  memset(&reqbufs, 0, sizeof(reqbufs));
  reqbufs.count = 0;
  reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  reqbufs.memory = V4L2_MEMORY_MMAP;
  IOCTL_OR_LOG_ERROR(VIDIOC_REQBUFS, &reqbufs);

  input_buffer_map_.clear();
  free_input_buffers_.clear();
}

void V4L2SliceVideoDecodeAccelerator::DismissPictures(
    const std::vector<int32_t>& picture_buffer_ids,
    base::WaitableEvent* done) {
  DVLOGF(3);
  DCHECK(child_task_runner_->BelongsToCurrentThread());

  for (auto picture_buffer_id : picture_buffer_ids) {
    DVLOGF(4) << "dismissing PictureBuffer id=" << picture_buffer_id;
    client_->DismissPictureBuffer(picture_buffer_id);
  }

  done->Signal();
}

void V4L2SliceVideoDecodeAccelerator::DevicePollTask(bool poll_device) {
  DVLOGF(3);
  DCHECK(device_poll_thread_.task_runner()->BelongsToCurrentThread());

  bool event_pending;
  if (!device_->Poll(poll_device, &event_pending)) {
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return;
  }

  // All processing should happen on ServiceDeviceTask(), since we shouldn't
  // touch encoder state from this thread.
  decoder_thread_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&V4L2SliceVideoDecodeAccelerator::ServiceDeviceTask,
                     base::Unretained(this)));
}

void V4L2SliceVideoDecodeAccelerator::ServiceDeviceTask() {
  DVLOGF(4);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (IsDestroyPending())
    return;

  // ServiceDeviceTask() should only ever be scheduled from DevicePollTask().

  Dequeue();
  SchedulePollIfNeeded();
}

void V4L2SliceVideoDecodeAccelerator::SchedulePollIfNeeded() {
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (!device_poll_thread_.IsRunning()) {
    DVLOGF(4) << "Device poll thread stopped, will not schedule poll";
    return;
  }

  DCHECK(input_streamon_ || output_streamon_);

  if (input_buffer_queued_count_ + output_buffer_queued_count_ == 0) {
    DVLOGF(4) << "No buffers queued, will not schedule poll";
    return;
  }

  DVLOGF(4) << "Scheduling device poll task";

  device_poll_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&V4L2SliceVideoDecodeAccelerator::DevicePollTask,
                     base::Unretained(this), true));

  DVLOGF(3) << "buffer counts: "
            << "INPUT[" << decoder_input_queue_.size() << "]"
            << " => DEVICE["
            << free_input_buffers_.size() << "+"
            << input_buffer_queued_count_ << "/"
            << input_buffer_map_.size() << "]->["
            << free_output_buffers_.size() << "+"
            << output_buffer_queued_count_ << "/"
            << output_buffer_map_.size() << "]"
            << " => DISPLAYQ[" << decoder_display_queue_.size() << "]"
            << " => CLIENT[" << surfaces_at_display_.size() << "]";
}

void V4L2SliceVideoDecodeAccelerator::Enqueue(
    const scoped_refptr<V4L2DecodeSurface>& dec_surface) {
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  const int old_inputs_queued = input_buffer_queued_count_;
  const int old_outputs_queued = output_buffer_queued_count_;

  if (!EnqueueInputRecord(dec_surface->input_record(),
                          dec_surface->config_store())) {
    VLOGF(1) << "Failed queueing an input buffer";
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return;
  }

  if (!EnqueueOutputRecord(dec_surface->output_record())) {
    VLOGF(1) << "Failed queueing an output buffer";
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return;
  }

  bool inserted =
      surfaces_at_device_
          .insert(std::make_pair(dec_surface->output_record(), dec_surface))
          .second;
  DCHECK(inserted);

  if (old_inputs_queued == 0 && old_outputs_queued == 0)
    SchedulePollIfNeeded();
}

void V4L2SliceVideoDecodeAccelerator::Dequeue() {
  DVLOGF(4);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  struct v4l2_buffer dqbuf;
  struct v4l2_plane planes[VIDEO_MAX_PLANES];
  while (input_buffer_queued_count_ > 0) {
    DCHECK(input_streamon_);
    memset(&dqbuf, 0, sizeof(dqbuf));
    memset(&planes, 0, sizeof(planes));
    dqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    dqbuf.memory = V4L2_MEMORY_MMAP;
    dqbuf.m.planes = planes;
    dqbuf.length = input_planes_count_;
    if (device_->Ioctl(VIDIOC_DQBUF, &dqbuf) != 0) {
      if (errno == EAGAIN) {
        // EAGAIN if we're just out of buffers to dequeue.
        break;
      }
      VPLOGF(1) << "ioctl() failed: VIDIOC_DQBUF";
      NOTIFY_ERROR(PLATFORM_FAILURE);
      return;
    }
    InputRecord& input_record = input_buffer_map_[dqbuf.index];
    DCHECK(input_record.at_device);
    input_record.at_device = false;
    ReuseInputBuffer(dqbuf.index);
    input_buffer_queued_count_--;
    DVLOGF(4) << "Dequeued input=" << dqbuf.index
              << " count: " << input_buffer_queued_count_;
  }

  while (output_buffer_queued_count_ > 0) {
    DCHECK(output_streamon_);
    memset(&dqbuf, 0, sizeof(dqbuf));
    memset(&planes, 0, sizeof(planes));
    dqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    dqbuf.memory =
        (output_mode_ == Config::OutputMode::ALLOCATE ? V4L2_MEMORY_MMAP
                                                      : V4L2_MEMORY_DMABUF);
    dqbuf.m.planes = planes;
    dqbuf.length = output_planes_count_;
    if (device_->Ioctl(VIDIOC_DQBUF, &dqbuf) != 0) {
      if (errno == EAGAIN) {
        // EAGAIN if we're just out of buffers to dequeue.
        break;
      }
      VPLOGF(1) << "ioctl() failed: VIDIOC_DQBUF";
      NOTIFY_ERROR(PLATFORM_FAILURE);
      return;
    }
    OutputRecord& output_record = output_buffer_map_[dqbuf.index];
    DCHECK(output_record.at_device);
    output_record.at_device = false;
    output_buffer_queued_count_--;
    DVLOGF(4) << "Dequeued output=" << dqbuf.index << " count "
              << output_buffer_queued_count_;

    V4L2DecodeSurfaceByOutputId::iterator it =
        surfaces_at_device_.find(dqbuf.index);
    if (it == surfaces_at_device_.end()) {
      VLOGF(1) << "Got invalid surface from device.";
      NOTIFY_ERROR(PLATFORM_FAILURE);
      return;
    }

    it->second->SetDecoded();
    surfaces_at_device_.erase(it);
  }

  // A frame was decoded, see if we can output it.
  TryOutputSurfaces();

  ProcessPendingEventsIfNeeded();
  ScheduleDecodeBufferTaskIfNeeded();
}

void V4L2SliceVideoDecodeAccelerator::NewEventPending() {
  // Switch to event processing mode if we are decoding. Otherwise we are either
  // already in it, or we will potentially switch to it later, after finishing
  // other tasks.
  if (state_ == kDecoding)
    state_ = kIdle;

  ProcessPendingEventsIfNeeded();
}

bool V4L2SliceVideoDecodeAccelerator::FinishEventProcessing() {
  DCHECK_EQ(state_, kIdle);

  state_ = kDecoding;
  ScheduleDecodeBufferTaskIfNeeded();

  return true;
}

void V4L2SliceVideoDecodeAccelerator::ProcessPendingEventsIfNeeded() {
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  // Process pending events, if any, in the correct order.
  // We always first process the surface set change, as it is an internal
  // event from the decoder and interleaving it with external requests would
  // put the decoder in an undefined state.
  using ProcessFunc = bool (V4L2SliceVideoDecodeAccelerator::*)();
  const ProcessFunc process_functions[] = {
      &V4L2SliceVideoDecodeAccelerator::FinishSurfaceSetChange,
      &V4L2SliceVideoDecodeAccelerator::FinishFlush,
      &V4L2SliceVideoDecodeAccelerator::FinishReset,
      &V4L2SliceVideoDecodeAccelerator::FinishEventProcessing,
  };

  for (const auto& fn : process_functions) {
    if (state_ != kIdle)
      return;

    if (!(this->*fn)())
      return;
  }
}

void V4L2SliceVideoDecodeAccelerator::ReuseInputBuffer(int index) {
  DVLOGF(4) << "Reusing input buffer, index=" << index;
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  DCHECK_LT(index, static_cast<int>(input_buffer_map_.size()));
  InputRecord& input_record = input_buffer_map_[index];

  DCHECK(!input_record.at_device);
  input_record.input_id = -1;
  input_record.bytes_used = 0;

  DCHECK_EQ(
      std::count(free_input_buffers_.begin(), free_input_buffers_.end(), index),
      0);
  free_input_buffers_.push_back(index);
}

void V4L2SliceVideoDecodeAccelerator::ReuseOutputBuffer(int index) {
  DVLOGF(4) << "Reusing output buffer, index=" << index;
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  DCHECK_LT(index, static_cast<int>(output_buffer_map_.size()));
  OutputRecord& output_record = output_buffer_map_[index];
  DCHECK(!output_record.at_device);
  DCHECK(!output_record.at_client);

  DCHECK_EQ(std::count(free_output_buffers_.begin(), free_output_buffers_.end(),
                       index),
            0);
  free_output_buffers_.push_back(index);

  ScheduleDecodeBufferTaskIfNeeded();
}

bool V4L2SliceVideoDecodeAccelerator::EnqueueInputRecord(
    int index,
    uint32_t config_store) {
  DVLOGF(4);
  DCHECK_LT(index, static_cast<int>(input_buffer_map_.size()));
  DCHECK_GT(config_store, 0u);

  // Enqueue an input (VIDEO_OUTPUT) buffer for an input video frame.
  InputRecord& input_record = input_buffer_map_[index];
  DCHECK(!input_record.at_device);
  struct v4l2_buffer qbuf;
  struct v4l2_plane qbuf_planes[VIDEO_MAX_PLANES];
  memset(&qbuf, 0, sizeof(qbuf));
  memset(qbuf_planes, 0, sizeof(qbuf_planes));
  qbuf.index = index;
  qbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  qbuf.memory = V4L2_MEMORY_MMAP;
  qbuf.m.planes = qbuf_planes;
  qbuf.m.planes[0].bytesused = input_record.bytes_used;
  qbuf.length = input_planes_count_;
  qbuf.config_store = config_store;
  IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_QBUF, &qbuf);
  input_record.at_device = true;
  input_buffer_queued_count_++;
  DVLOGF(4) << "Enqueued input=" << qbuf.index
            << " count: " << input_buffer_queued_count_;

  return true;
}

bool V4L2SliceVideoDecodeAccelerator::EnqueueOutputRecord(int index) {
  DVLOGF(4);
  DCHECK_LT(index, static_cast<int>(output_buffer_map_.size()));

  // Enqueue an output (VIDEO_CAPTURE) buffer.
  OutputRecord& output_record = output_buffer_map_[index];
  DCHECK(!output_record.at_device);
  DCHECK(!output_record.at_client);
  DCHECK_NE(output_record.picture_id, -1);

  if (output_record.egl_fence) {
    // If we have to wait for completion, wait. Note that free_output_buffers_
    // is a FIFO queue, so we always wait on the buffer that has been in the
    // queue the longest. Every 100ms we check whether the decoder is shutting
    // down, or we might get stuck waiting on a fence that will never come:
    // https://crbug.com/845645
    while (!IsDestroyPending()) {
      const EGLTimeKHR wait_ns =
          base::TimeDelta::FromMilliseconds(100).InNanoseconds();
      EGLint result =
          output_record.egl_fence->ClientWaitWithTimeoutNanos(wait_ns);
      if (result == EGL_CONDITION_SATISFIED_KHR) {
        break;
      } else if (result == EGL_FALSE) {
        // This will cause tearing, but is safe otherwise.
        DVLOGF(1) << "GLFenceEGL::ClientWaitWithTimeoutNanos failed!";
        break;
      }
      DCHECK_EQ(result, EGL_TIMEOUT_EXPIRED_KHR);
    }

    if (IsDestroyPending())
      return false;

    output_record.egl_fence.reset();
  }

  struct v4l2_buffer qbuf;
  struct v4l2_plane qbuf_planes[VIDEO_MAX_PLANES];
  memset(&qbuf, 0, sizeof(qbuf));
  memset(qbuf_planes, 0, sizeof(qbuf_planes));
  qbuf.index = index;
  qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  if (output_mode_ == Config::OutputMode::ALLOCATE) {
    qbuf.memory = V4L2_MEMORY_MMAP;
  } else {
    qbuf.memory = V4L2_MEMORY_DMABUF;
    DCHECK_EQ(output_planes_count_, output_record.dmabuf_fds.size());
    for (size_t i = 0; i < output_record.dmabuf_fds.size(); ++i) {
      DCHECK(output_record.dmabuf_fds[i].is_valid());
      qbuf_planes[i].m.fd = output_record.dmabuf_fds[i].get();
    }
  }
  qbuf.m.planes = qbuf_planes;
  qbuf.length = output_planes_count_;
  IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_QBUF, &qbuf);
  output_record.at_device = true;
  output_buffer_queued_count_++;
  DVLOGF(4) << "Enqueued output=" << qbuf.index
            << " count: " << output_buffer_queued_count_;

  return true;
}

bool V4L2SliceVideoDecodeAccelerator::StartDevicePoll() {
  DVLOGF(3) << "Starting device poll";
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  DCHECK(!device_poll_thread_.IsRunning());

  // Start up the device poll thread and schedule its first DevicePollTask().
  if (!device_poll_thread_.Start()) {
    VLOGF(1) << "Device thread failed to start";
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return false;
  }
  if (!input_streamon_) {
    __u32 type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_STREAMON, &type);
    input_streamon_ = true;
  }

  if (!output_streamon_) {
    __u32 type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_STREAMON, &type);
    output_streamon_ = true;
  }

  device_poll_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&V4L2SliceVideoDecodeAccelerator::DevicePollTask,
                     base::Unretained(this), true));

  return true;
}

bool V4L2SliceVideoDecodeAccelerator::StopDevicePoll(bool keep_input_state) {
  DVLOGF(3) << "Stopping device poll";
  if (decoder_thread_.IsRunning())
    DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  // Signal the DevicePollTask() to stop, and stop the device poll thread.
  if (!device_->SetDevicePollInterrupt()) {
    VPLOGF(1) << "SetDevicePollInterrupt(): failed";
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return false;
  }
  device_poll_thread_.Stop();
  DVLOGF(3) << "Device poll thread stopped";

  // Clear the interrupt now, to be sure.
  if (!device_->ClearDevicePollInterrupt()) {
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return false;
  }

  if (!keep_input_state) {
    if (input_streamon_) {
      __u32 type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
      IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_STREAMOFF, &type);
    }
    input_streamon_ = false;
  }

  if (output_streamon_) {
    __u32 type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_STREAMOFF, &type);
  }
  output_streamon_ = false;

  if (!keep_input_state) {
    for (size_t i = 0; i < input_buffer_map_.size(); ++i) {
      InputRecord& input_record = input_buffer_map_[i];
      if (input_record.at_device) {
        input_record.at_device = false;
        ReuseInputBuffer(i);
        input_buffer_queued_count_--;
      }
    }
    DCHECK_EQ(input_buffer_queued_count_, 0);
  }

  // STREAMOFF makes the driver drop all buffers without decoding and DQBUFing,
  // so we mark them all as at_device = false and clear surfaces_at_device_.
  for (size_t i = 0; i < output_buffer_map_.size(); ++i) {
    OutputRecord& output_record = output_buffer_map_[i];
    if (output_record.at_device) {
      output_record.at_device = false;
      output_buffer_queued_count_--;
    }
  }
  // Mark as decoded to allow reuse.
  for (auto kv : surfaces_at_device_) {
    kv.second->SetDecoded();
  }
  surfaces_at_device_.clear();
  DCHECK_EQ(output_buffer_queued_count_, 0);

  // Drop all surfaces that were awaiting decode before being displayed,
  // since we've just cancelled all outstanding decodes.
  while (!decoder_display_queue_.empty())
    decoder_display_queue_.pop();

  DVLOGF(3) << "Device poll stopped";
  return true;
}

void V4L2SliceVideoDecodeAccelerator::Decode(
    const BitstreamBuffer& bitstream_buffer) {
  Decode(bitstream_buffer.ToDecoderBuffer(), bitstream_buffer.id());
}

void V4L2SliceVideoDecodeAccelerator::Decode(
    scoped_refptr<DecoderBuffer> buffer,
    int32_t bitstream_id) {
  DVLOGF(4) << "input_id=" << bitstream_id
            << ", size=" << (buffer ? buffer->data_size() : 0);
  DCHECK(decode_task_runner_->BelongsToCurrentThread());

  if (bitstream_id < 0) {
    VLOGF(1) << "Invalid bitstream buffer, id: " << bitstream_id;
    NOTIFY_ERROR(INVALID_ARGUMENT);
    return;
  }

  decoder_thread_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&V4L2SliceVideoDecodeAccelerator::DecodeTask,
                     base::Unretained(this), std::move(buffer), bitstream_id));
}

void V4L2SliceVideoDecodeAccelerator::DecodeTask(
    scoped_refptr<DecoderBuffer> buffer,
    int32_t bitstream_id) {
  DVLOGF(4) << "input_id=" << bitstream_id
            << " size=" << (buffer ? buffer->data_size() : 0);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (IsDestroyPending())
    return;

  std::unique_ptr<BitstreamBufferRef> bitstream_record(new BitstreamBufferRef(
      decode_client_, decode_task_runner_, std::move(buffer), bitstream_id));

  // Skip empty buffer.
  if (!bitstream_record->buffer)
    return;

  decoder_input_queue_.push(std::move(bitstream_record));

  ScheduleDecodeBufferTaskIfNeeded();
}

bool V4L2SliceVideoDecodeAccelerator::TrySetNewBistreamBuffer() {
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  DCHECK(!decoder_current_bitstream_buffer_);

  if (decoder_input_queue_.empty())
    return false;

  decoder_current_bitstream_buffer_ = std::move(decoder_input_queue_.front());
  decoder_input_queue_.pop();

  if (decoder_current_bitstream_buffer_->input_id == kFlushBufferId) {
    // This is a buffer we queued for ourselves to trigger flush at this time.
    InitiateFlush();
    return false;
  }

  const uint8_t* const data = decoder_current_bitstream_buffer_->buffer->data();
  const size_t data_size =
      decoder_current_bitstream_buffer_->buffer->data_size();
  decoder_->SetStream(decoder_current_bitstream_buffer_->input_id, data,
                      data_size);

  return true;
}

void V4L2SliceVideoDecodeAccelerator::ScheduleDecodeBufferTaskIfNeeded() {
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  if (state_ == kDecoding) {
    decoder_thread_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&V4L2SliceVideoDecodeAccelerator::DecodeBufferTask,
                   base::Unretained(this)));
  }
}

void V4L2SliceVideoDecodeAccelerator::DecodeBufferTask() {
  DVLOGF(4);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (IsDestroyPending())
    return;

  if (state_ != kDecoding) {
    DVLOGF(3) << "Early exit, not in kDecoding";
    return;
  }

  while (true) {
    AcceleratedVideoDecoder::DecodeResult res;
    res = decoder_->Decode();
    switch (res) {
      case AcceleratedVideoDecoder::kAllocateNewSurfaces:
        VLOGF(2) << "Decoder requesting a new set of surfaces";
        InitiateSurfaceSetChange();
        return;

      case AcceleratedVideoDecoder::kRanOutOfStreamData:
        decoder_current_bitstream_buffer_.reset();
        if (!TrySetNewBistreamBuffer())
          return;

        break;

      case AcceleratedVideoDecoder::kRanOutOfSurfaces:
        // No more surfaces for the decoder, we'll come back once we have more.
        DVLOGF(4) << "Ran out of surfaces";
        return;

      case AcceleratedVideoDecoder::kNeedContextUpdate:
        DVLOGF(4) << "Awaiting context update";
        return;

      case AcceleratedVideoDecoder::kDecodeError:
        VLOGF(1) << "Error decoding stream";
        NOTIFY_ERROR(PLATFORM_FAILURE);
        return;

      case AcceleratedVideoDecoder::kTryAgain:
        NOTREACHED() << "Should not reach here unless this class accepts "
                        "encrypted streams.";
        DVLOGF(4) << "No key for decoding stream.";
        NOTIFY_ERROR(PLATFORM_FAILURE);
        return;
    }
  }
}

void V4L2SliceVideoDecodeAccelerator::InitiateSurfaceSetChange() {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(state_, kDecoding);

  DCHECK(!surface_set_change_pending_);
  surface_set_change_pending_ = true;
  NewEventPending();
}

bool V4L2SliceVideoDecodeAccelerator::FinishSurfaceSetChange() {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (!surface_set_change_pending_)
    return true;

  if (!surfaces_at_device_.empty())
    return false;

  DCHECK_EQ(state_, kIdle);
  DCHECK(decoder_display_queue_.empty());
  // All output buffers should've been returned from decoder and device by now.
  // The only remaining owner of surfaces may be display (client), and we will
  // dismiss them when destroying output buffers below.
  DCHECK_EQ(free_output_buffers_.size() + surfaces_at_display_.size(),
            output_buffer_map_.size());

  // Keep input queue running while we switch outputs.
  if (!StopDevicePoll(true)) {
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return false;
  }

  // Dequeued decoded surfaces may be pended in pending_picture_ready_ if they
  // are waiting for some pictures to be cleared. We should post them right away
  // because they are about to be dismissed and destroyed for surface set
  // change.
  SendPictureReady();

  // This will return only once all buffers are dismissed and destroyed.
  // This does not wait until they are displayed however, as display retains
  // references to the buffers bound to textures and will release them
  // after displaying.
  if (!DestroyOutputs(true)) {
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return false;
  }

  if (!CreateOutputBuffers()) {
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return false;
  }

  surface_set_change_pending_ = false;
  VLOGF(2) << "Surface set change finished";
  return true;
}

bool V4L2SliceVideoDecodeAccelerator::DestroyOutputs(bool dismiss) {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  std::vector<int32_t> picture_buffers_to_dismiss;

  if (output_buffer_map_.empty())
    return true;

  for (auto& output_record : output_buffer_map_) {
    DCHECK(!output_record.at_device);

    output_record.egl_fence.reset();

    picture_buffers_to_dismiss.push_back(output_record.picture_id);
  }

  if (dismiss) {
    VLOGF(2) << "Scheduling picture dismissal";
    base::WaitableEvent done(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                             base::WaitableEvent::InitialState::NOT_SIGNALED);
    child_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&V4L2SliceVideoDecodeAccelerator::DismissPictures,
                       weak_this_, picture_buffers_to_dismiss, &done));
    done.Wait();
  }

  // At this point client can't call ReusePictureBuffer on any of the pictures
  // anymore, so it's safe to destroy.
  return DestroyOutputBuffers();
}

bool V4L2SliceVideoDecodeAccelerator::DestroyOutputBuffers() {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread() ||
         !decoder_thread_.IsRunning());
  DCHECK(!output_streamon_);
  DCHECK(surfaces_at_device_.empty());
  DCHECK(decoder_display_queue_.empty());
  DCHECK_EQ(surfaces_at_display_.size() + free_output_buffers_.size(),
            output_buffer_map_.size());

  if (output_buffer_map_.empty())
    return true;

  // It's ok to do this, client will retain references to textures, but we are
  // not interested in reusing the surfaces anymore.
  // This will prevent us from reusing old surfaces in case we have some
  // ReusePictureBuffer() pending on ChildThread already. It's ok to ignore
  // them, because we have already dismissed them (in DestroyOutputs()).
  for (const auto& surface_at_display : surfaces_at_display_) {
    size_t index = surface_at_display.second->output_record();
    DCHECK_LT(index, output_buffer_map_.size());
    OutputRecord& output_record = output_buffer_map_[index];
    DCHECK(output_record.at_client);
    output_record.at_client = false;
    output_record.num_times_sent_to_client = 0;
  }
  surfaces_at_display_.clear();
  DCHECK_EQ(free_output_buffers_.size(), output_buffer_map_.size());

  free_output_buffers_.clear();
  output_buffer_map_.clear();

  struct v4l2_requestbuffers reqbufs;
  memset(&reqbufs, 0, sizeof(reqbufs));
  reqbufs.count = 0;
  reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  reqbufs.memory = V4L2_MEMORY_MMAP;
  IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_REQBUFS, &reqbufs);

  return true;
}

void V4L2SliceVideoDecodeAccelerator::AssignPictureBuffers(
    const std::vector<PictureBuffer>& buffers) {
  VLOGF(2);
  DCHECK(child_task_runner_->BelongsToCurrentThread());

  decoder_thread_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&V4L2SliceVideoDecodeAccelerator::AssignPictureBuffersTask,
                 base::Unretained(this), buffers));
}

void V4L2SliceVideoDecodeAccelerator::AssignPictureBuffersTask(
    const std::vector<PictureBuffer>& buffers) {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(state_, kAwaitingPictureBuffers);

  if (IsDestroyPending())
    return;

  const uint32_t req_buffer_count = decoder_->GetRequiredNumOfPictures();

  if (buffers.size() < req_buffer_count) {
    VLOGF(1) << "Failed to provide requested picture buffers. "
             << "(Got " << buffers.size() << ", requested " << req_buffer_count
             << ")";
    NOTIFY_ERROR(INVALID_ARGUMENT);
    return;
  }

  // Allocate the output buffers.
  struct v4l2_requestbuffers reqbufs;
  memset(&reqbufs, 0, sizeof(reqbufs));
  reqbufs.count = buffers.size();
  reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  reqbufs.memory =
      (output_mode_ == Config::OutputMode::ALLOCATE ? V4L2_MEMORY_MMAP
                                                    : V4L2_MEMORY_DMABUF);
  IOCTL_OR_ERROR_RETURN(VIDIOC_REQBUFS, &reqbufs);

  if (reqbufs.count != buffers.size()) {
    VLOGF(1) << "Could not allocate enough output buffers";
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return;
  }

  DCHECK(free_output_buffers_.empty());
  DCHECK(output_buffer_map_.empty());
  output_buffer_map_.resize(buffers.size());
  for (size_t i = 0; i < output_buffer_map_.size(); ++i) {
    DCHECK(buffers[i].size() == coded_size_);

    OutputRecord& output_record = output_buffer_map_[i];
    DCHECK(!output_record.at_device);
    DCHECK(!output_record.at_client);
    DCHECK(!output_record.egl_fence);
    DCHECK_EQ(output_record.picture_id, -1);
    DCHECK(output_record.dmabuf_fds.empty());
    DCHECK_EQ(output_record.cleared, false);

    output_record.picture_id = buffers[i].id();
    output_record.texture_id = buffers[i].service_texture_ids().empty()
                                   ? 0
                                   : buffers[i].service_texture_ids()[0];

    output_record.client_texture_id = buffers[i].client_texture_ids().empty()
                                          ? 0
                                          : buffers[i].client_texture_ids()[0];

    // This will remain true until ImportBufferForPicture is called, either by
    // the client, or by ourselves, if we are allocating.
    output_record.at_client = true;
    if (output_mode_ == Config::OutputMode::ALLOCATE) {
      std::vector<base::ScopedFD> passed_dmabuf_fds =
          device_->GetDmabufsForV4L2Buffer(i, output_planes_count_,
                                           V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
      if (passed_dmabuf_fds.empty()) {
        NOTIFY_ERROR(PLATFORM_FAILURE);
        return;
      }

      ImportBufferForPictureTask(output_record.picture_id,
                                 std::move(passed_dmabuf_fds));
    }  // else we'll get triggered via ImportBufferForPicture() from client.
    DVLOGF(3) << "buffer[" << i << "]: picture_id=" << output_record.picture_id;
  }

  if (!StartDevicePoll()) {
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return;
  }

  // Put us in kIdle to allow further event processing.
  // ProcessPendingEventsIfNeeded() will put us back into kDecoding after all
  // other pending events are processed successfully.
  state_ = kIdle;
  ProcessPendingEventsIfNeeded();
}

void V4L2SliceVideoDecodeAccelerator::CreateGLImageFor(
    size_t buffer_index,
    int32_t picture_buffer_id,
    std::vector<base::ScopedFD> passed_dmabuf_fds,
    GLuint client_texture_id,
    GLuint texture_id,
    const gfx::Size& size,
    uint32_t fourcc) {
  DVLOGF(3) << "index=" << buffer_index;
  DCHECK(child_task_runner_->BelongsToCurrentThread());
  DCHECK_NE(texture_id, 0u);

  if (!make_context_current_cb_) {
    VLOGF(1) << "GL callbacks required for binding to GLImages";
    NOTIFY_ERROR(INVALID_ARGUMENT);
    return;
  }
  if (!make_context_current_cb_.Run()) {
    VLOGF(1) << "No GL context";
    NOTIFY_ERROR(INVALID_ARGUMENT);
    return;
  }

  scoped_refptr<gl::GLImage> gl_image =
      device_->CreateGLImage(size, fourcc, passed_dmabuf_fds);
  if (!gl_image) {
    VLOGF(1) << "Could not create GLImage,"
             << " index=" << buffer_index << " texture_id=" << texture_id;
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return;
  }
  gl::ScopedTextureBinder bind_restore(device_->GetTextureTarget(), texture_id);
  bool ret = gl_image->BindTexImage(device_->GetTextureTarget());
  DCHECK(ret);
  bind_image_cb_.Run(client_texture_id, device_->GetTextureTarget(), gl_image,
                     true);
  decoder_thread_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&V4L2SliceVideoDecodeAccelerator::AssignDmaBufs,
                     base::Unretained(this), buffer_index, picture_buffer_id,
                     std::move(passed_dmabuf_fds)));
}

void V4L2SliceVideoDecodeAccelerator::AssignDmaBufs(
    size_t buffer_index,
    int32_t picture_buffer_id,
    std::vector<base::ScopedFD> passed_dmabuf_fds) {
  DVLOGF(3) << "index=" << buffer_index;
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (IsDestroyPending())
    return;

  // It's possible that while waiting for the EGLImages to be allocated and
  // assigned, we have already decoded more of the stream and saw another
  // resolution change. This is a normal situation, in such a case either there
  // is no output record with this index awaiting an EGLImage to be assigned to
  // it, or the record is already updated to use a newer PictureBuffer and is
  // awaiting an EGLImage associated with a different picture_buffer_id. If so,
  // just discard this image, we will get the one we are waiting for later.
  if (buffer_index >= output_buffer_map_.size() ||
      output_buffer_map_[buffer_index].picture_id != picture_buffer_id) {
    DVLOGF(4) << "Picture set already changed, dropping EGLImage";
    return;
  }

  OutputRecord& output_record = output_buffer_map_[buffer_index];
  DCHECK(!output_record.egl_fence);
  DCHECK(!output_record.at_client);
  DCHECK(!output_record.at_device);

  if (output_mode_ == Config::OutputMode::IMPORT) {
    DCHECK(output_record.dmabuf_fds.empty());
    output_record.dmabuf_fds = std::move(passed_dmabuf_fds);
  }

  DCHECK_EQ(std::count(free_output_buffers_.begin(), free_output_buffers_.end(),
                       buffer_index),
            0);
  free_output_buffers_.push_back(buffer_index);
  ScheduleDecodeBufferTaskIfNeeded();
}

void V4L2SliceVideoDecodeAccelerator::ImportBufferForPicture(
    int32_t picture_buffer_id,
    VideoPixelFormat pixel_format,
    const gfx::GpuMemoryBufferHandle& gpu_memory_buffer_handle) {
  DVLOGF(3) << "picture_buffer_id=" << picture_buffer_id;
  DCHECK(child_task_runner_->BelongsToCurrentThread());

  std::vector<base::ScopedFD> passed_dmabuf_fds;
#if defined(USE_OZONE)
  for (const auto& fd : gpu_memory_buffer_handle.native_pixmap_handle.fds) {
    DCHECK_NE(fd.fd, -1);
    passed_dmabuf_fds.push_back(base::ScopedFD(fd.fd));
  }
#endif

  if (output_mode_ != Config::OutputMode::IMPORT) {
    VLOGF(1) << "Cannot import in non-import mode";
    NOTIFY_ERROR(INVALID_ARGUMENT);
    return;
  }

  if (pixel_format !=
      V4L2Device::V4L2PixFmtToVideoPixelFormat(output_format_fourcc_)) {
    VLOGF(1) << "Unsupported import format: "
             << VideoPixelFormatToString(pixel_format);
    NOTIFY_ERROR(INVALID_ARGUMENT);
    return;
  }

  decoder_thread_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &V4L2SliceVideoDecodeAccelerator::ImportBufferForPictureTask,
          base::Unretained(this), picture_buffer_id,
          std::move(passed_dmabuf_fds)));
}

void V4L2SliceVideoDecodeAccelerator::ImportBufferForPictureTask(
    int32_t picture_buffer_id,
    std::vector<base::ScopedFD> passed_dmabuf_fds) {
  DVLOGF(3) << "picture_buffer_id=" << picture_buffer_id;
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (IsDestroyPending())
    return;

  const auto iter =
      std::find_if(output_buffer_map_.begin(), output_buffer_map_.end(),
                   [picture_buffer_id](const OutputRecord& output_record) {
                     return output_record.picture_id == picture_buffer_id;
                   });
  if (iter == output_buffer_map_.end()) {
    // It's possible that we've already posted a DismissPictureBuffer for this
    // picture, but it has not yet executed when this ImportBufferForPicture was
    // posted to us by the client. In that case just ignore this (we've already
    // dismissed it and accounted for that).
    DVLOGF(3) << "got picture id=" << picture_buffer_id
              << " not in use (anymore?).";
    return;
  }

  if (!iter->at_client) {
    VLOGF(1) << "Cannot import buffer that not owned by client";
    NOTIFY_ERROR(INVALID_ARGUMENT);
    return;
  }

  size_t index = iter - output_buffer_map_.begin();
  DCHECK_EQ(std::count(free_output_buffers_.begin(), free_output_buffers_.end(),
                       index),
            0);

  DCHECK(!iter->at_device);
  iter->at_client = false;
  if (iter->texture_id != 0) {
    child_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&V4L2SliceVideoDecodeAccelerator::CreateGLImageFor,
                       weak_this_, index, picture_buffer_id,
                       std::move(passed_dmabuf_fds), iter->client_texture_id,
                       iter->texture_id, coded_size_, output_format_fourcc_));
  } else {
    // No need for a GLImage, start using this buffer now.
    DCHECK_EQ(output_planes_count_, passed_dmabuf_fds.size());
    iter->dmabuf_fds = std::move(passed_dmabuf_fds);
    free_output_buffers_.push_back(index);
    ScheduleDecodeBufferTaskIfNeeded();
  }
}

void V4L2SliceVideoDecodeAccelerator::ReusePictureBuffer(
    int32_t picture_buffer_id) {
  DCHECK(child_task_runner_->BelongsToCurrentThread());
  DVLOGF(4) << "picture_buffer_id=" << picture_buffer_id;

  std::unique_ptr<gl::GLFenceEGL> egl_fence;

  if (make_context_current_cb_) {
    if (!make_context_current_cb_.Run()) {
      VLOGF(1) << "could not make context current";
      NOTIFY_ERROR(PLATFORM_FAILURE);
      return;
    }

    egl_fence = gl::GLFenceEGL::Create();
    if (!egl_fence) {
      VLOGF(1) << "gl::GLFenceEGL::Create() failed";
      NOTIFY_ERROR(PLATFORM_FAILURE);
      return;
    }
  }

  decoder_thread_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&V4L2SliceVideoDecodeAccelerator::ReusePictureBufferTask,
                 base::Unretained(this), picture_buffer_id,
                 base::Passed(&egl_fence)));
}

void V4L2SliceVideoDecodeAccelerator::ReusePictureBufferTask(
    int32_t picture_buffer_id,
    std::unique_ptr<gl::GLFenceEGL> egl_fence) {
  DVLOGF(4) << "picture_buffer_id=" << picture_buffer_id;
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (IsDestroyPending())
    return;

  V4L2DecodeSurfaceByPictureBufferId::iterator it =
      surfaces_at_display_.find(picture_buffer_id);
  if (it == surfaces_at_display_.end()) {
    // It's possible that we've already posted a DismissPictureBuffer for this
    // picture, but it has not yet executed when this ReusePictureBuffer was
    // posted to us by the client. In that case just ignore this (we've already
    // dismissed it and accounted for that) and let the fence object get
    // destroyed.
    DVLOGF(3) << "got picture id=" << picture_buffer_id
              << " not in use (anymore?).";
    return;
  }

  OutputRecord& output_record = output_buffer_map_[it->second->output_record()];
  if (output_record.at_device || !output_record.at_client) {
    VLOGF(1) << "picture_buffer_id not reusable";
    NOTIFY_ERROR(INVALID_ARGUMENT);
    return;
  }

  DCHECK(!output_record.at_device);
  --output_record.num_times_sent_to_client;
  // A output buffer might be sent multiple times. We only use the last fence.
  // When the last fence is signaled, all the previous fences must be executed.
  if (output_record.num_times_sent_to_client == 0) {
    output_record.at_client = false;
    // Take ownership of the EGL fence.
    output_record.egl_fence = std::move(egl_fence);

    surfaces_at_display_.erase(it);
  }
}

void V4L2SliceVideoDecodeAccelerator::Flush() {
  VLOGF(2);
  DCHECK(child_task_runner_->BelongsToCurrentThread());

  decoder_thread_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&V4L2SliceVideoDecodeAccelerator::FlushTask,
                                base::Unretained(this)));
}

void V4L2SliceVideoDecodeAccelerator::FlushTask() {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (IsDestroyPending())
    return;

  // Queue an empty buffer which - when reached - will trigger flush sequence.
  decoder_input_queue_.push(std::make_unique<BitstreamBufferRef>(
      decode_client_, decode_task_runner_, nullptr, kFlushBufferId));

  ScheduleDecodeBufferTaskIfNeeded();
}

void V4L2SliceVideoDecodeAccelerator::InitiateFlush() {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  // This will trigger output for all remaining surfaces in the decoder.
  // However, not all of them may be decoded yet (they would be queued
  // in hardware then).
  if (!decoder_->Flush()) {
    DVLOGF(1) << "Failed flushing the decoder.";
    NOTIFY_ERROR(PLATFORM_FAILURE);
    return;
  }

  // Put the decoder in an idle state, ready to resume.
  decoder_->Reset();

  DCHECK(!decoder_flushing_);
  decoder_flushing_ = true;
  NewEventPending();
}

bool V4L2SliceVideoDecodeAccelerator::FinishFlush() {
  VLOGF(4);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (!decoder_flushing_)
    return true;

  if (!surfaces_at_device_.empty())
    return false;

  DCHECK_EQ(state_, kIdle);

  // At this point, all remaining surfaces are decoded and dequeued, and since
  // we have already scheduled output for them in InitiateFlush(), their
  // respective PictureReady calls have been posted (or they have been queued on
  // pending_picture_ready_). So at this time, once we SendPictureReady(),
  // we will have all remaining PictureReady() posted to the client and we
  // can post NotifyFlushDone().
  DCHECK(decoder_display_queue_.empty());

  // Decoder should have already returned all surfaces and all surfaces are
  // out of hardware. There can be no other owners of input buffers.
  DCHECK_EQ(free_input_buffers_.size(), input_buffer_map_.size());

  SendPictureReady();

  decoder_flushing_ = false;
  VLOGF(2) << "Flush finished";

  child_task_runner_->PostTask(FROM_HERE,
                               base::Bind(&Client::NotifyFlushDone, client_));

  return true;
}

void V4L2SliceVideoDecodeAccelerator::Reset() {
  VLOGF(2);
  DCHECK(child_task_runner_->BelongsToCurrentThread());

  decoder_thread_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&V4L2SliceVideoDecodeAccelerator::ResetTask,
                                base::Unretained(this)));
}

void V4L2SliceVideoDecodeAccelerator::ResetTask() {
  VLOGF(2);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (IsDestroyPending())
    return;

  if (decoder_resetting_) {
    // This is a bug in the client, multiple Reset()s before NotifyResetDone()
    // are not allowed.
    NOTREACHED() << "Client should not be requesting multiple Reset()s";
    return;
  }

  // Put the decoder in an idle state, ready to resume.
  decoder_->Reset();

  // Drop all remaining inputs.
  decoder_current_bitstream_buffer_.reset();
  while (!decoder_input_queue_.empty())
    decoder_input_queue_.pop();

  decoder_resetting_ = true;
  NewEventPending();
}

bool V4L2SliceVideoDecodeAccelerator::FinishReset() {
  VLOGF(4);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  if (!decoder_resetting_)
    return true;

  if (!surfaces_at_device_.empty())
    return false;

  DCHECK_EQ(state_, kIdle);
  DCHECK(!decoder_flushing_);
  SendPictureReady();

  // Drop any pending outputs.
  while (!decoder_display_queue_.empty())
    decoder_display_queue_.pop();

  // At this point we can have no input buffers in the decoder, because we
  // Reset()ed it in ResetTask(), and have not scheduled any new Decode()s
  // having been in kIdle since. We don't have any surfaces in the HW either -
  // we just checked that surfaces_at_device_.empty(), and inputs are tied
  // to surfaces. Since there can be no other owners of input buffers, we can
  // simply mark them all as available.
  DCHECK_EQ(input_buffer_queued_count_, 0);
  free_input_buffers_.clear();
  for (size_t i = 0; i < input_buffer_map_.size(); ++i) {
    DCHECK(!input_buffer_map_[i].at_device);
    ReuseInputBuffer(i);
  }

  decoder_resetting_ = false;
  VLOGF(2) << "Reset finished";

  child_task_runner_->PostTask(FROM_HERE,
                               base::Bind(&Client::NotifyResetDone, client_));

  return true;
}

bool V4L2SliceVideoDecodeAccelerator::IsDestroyPending() {
  return destroy_pending_.IsSignaled();
}

void V4L2SliceVideoDecodeAccelerator::SetErrorState(Error error) {
  // We can touch decoder_state_ only if this is the decoder thread or the
  // decoder thread isn't running.
  if (decoder_thread_.IsRunning() &&
      !decoder_thread_task_runner_->BelongsToCurrentThread()) {
    decoder_thread_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&V4L2SliceVideoDecodeAccelerator::SetErrorState,
                       base::Unretained(this), error));
    return;
  }

  // Notifying the client of an error will only happen if we are already
  // initialized, as the API does not allow doing so before that. Subsequent
  // errors and errors while destroying will be suppressed.
  if (state_ != kError && state_ != kUninitialized && state_ != kDestroying)
    NotifyError(error);

  state_ = kError;
}

bool V4L2SliceVideoDecodeAccelerator::SubmitSlice(
    const scoped_refptr<V4L2DecodeSurface>& dec_surface,
    const uint8_t* data,
    size_t size) {
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  InputRecord& input_record = input_buffer_map_[dec_surface->input_record()];

  if (input_record.bytes_used + size > input_record.length) {
    VLOGF(1) << "Input buffer too small";
    return false;
  }

  memcpy(static_cast<uint8_t*>(input_record.address) + input_record.bytes_used,
         data, size);
  input_record.bytes_used += size;

  return true;
}

void V4L2SliceVideoDecodeAccelerator::DecodeSurface(
    const scoped_refptr<V4L2DecodeSurface>& dec_surface) {
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  DVLOGF(3) << "Submitting decode for surface: " << dec_surface->ToString();
  Enqueue(dec_surface);
}

V4L2SliceVideoDecodeAccelerator::V4L2VP8Accelerator::V4L2VP8Accelerator(
    V4L2DecodeSurfaceHandler* surface_handler,
    V4L2Device* device)
    : surface_handler_(surface_handler), device_(device) {
  DCHECK(surface_handler_);
}

V4L2SliceVideoDecodeAccelerator::V4L2VP8Accelerator::~V4L2VP8Accelerator() {}

scoped_refptr<VP8Picture>
V4L2SliceVideoDecodeAccelerator::V4L2VP8Accelerator::CreateVP8Picture() {
  scoped_refptr<V4L2DecodeSurface> dec_surface =
      surface_handler_->CreateSurface();
  if (!dec_surface)
    return nullptr;

  return new V4L2VP8Picture(dec_surface);
}

static void FillV4L2SegmentationHeader(
    const Vp8SegmentationHeader& vp8_sgmnt_hdr,
    struct v4l2_vp8_sgmnt_hdr* v4l2_sgmnt_hdr) {
#define SET_V4L2_SGMNT_HDR_FLAG_IF(cond, flag) \
  v4l2_sgmnt_hdr->flags |= ((vp8_sgmnt_hdr.cond) ? (flag) : 0)
  SET_V4L2_SGMNT_HDR_FLAG_IF(segmentation_enabled,
                             V4L2_VP8_SEGMNT_HDR_FLAG_ENABLED);
  SET_V4L2_SGMNT_HDR_FLAG_IF(update_mb_segmentation_map,
                             V4L2_VP8_SEGMNT_HDR_FLAG_UPDATE_MAP);
  SET_V4L2_SGMNT_HDR_FLAG_IF(update_segment_feature_data,
                             V4L2_VP8_SEGMNT_HDR_FLAG_UPDATE_FEATURE_DATA);
#undef SET_V4L2_SPARM_FLAG_IF
  v4l2_sgmnt_hdr->segment_feature_mode = vp8_sgmnt_hdr.segment_feature_mode;

  SafeArrayMemcpy(v4l2_sgmnt_hdr->quant_update,
                  vp8_sgmnt_hdr.quantizer_update_value);
  SafeArrayMemcpy(v4l2_sgmnt_hdr->lf_update, vp8_sgmnt_hdr.lf_update_value);
  SafeArrayMemcpy(v4l2_sgmnt_hdr->segment_probs, vp8_sgmnt_hdr.segment_prob);
}

static void FillV4L2LoopfilterHeader(
    const Vp8LoopFilterHeader& vp8_loopfilter_hdr,
    struct v4l2_vp8_loopfilter_hdr* v4l2_lf_hdr) {
#define SET_V4L2_LF_HDR_FLAG_IF(cond, flag) \
  v4l2_lf_hdr->flags |= ((vp8_loopfilter_hdr.cond) ? (flag) : 0)
  SET_V4L2_LF_HDR_FLAG_IF(loop_filter_adj_enable, V4L2_VP8_LF_HDR_ADJ_ENABLE);
  SET_V4L2_LF_HDR_FLAG_IF(mode_ref_lf_delta_update,
                          V4L2_VP8_LF_HDR_DELTA_UPDATE);
#undef SET_V4L2_SGMNT_HDR_FLAG_IF

#define LF_HDR_TO_V4L2_LF_HDR(a) v4l2_lf_hdr->a = vp8_loopfilter_hdr.a;
  LF_HDR_TO_V4L2_LF_HDR(type);
  LF_HDR_TO_V4L2_LF_HDR(level);
  LF_HDR_TO_V4L2_LF_HDR(sharpness_level);
#undef LF_HDR_TO_V4L2_LF_HDR

  SafeArrayMemcpy(v4l2_lf_hdr->ref_frm_delta_magnitude,
                  vp8_loopfilter_hdr.ref_frame_delta);
  SafeArrayMemcpy(v4l2_lf_hdr->mb_mode_delta_magnitude,
                  vp8_loopfilter_hdr.mb_mode_delta);
}

static void FillV4L2QuantizationHeader(
    const Vp8QuantizationHeader& vp8_quant_hdr,
    struct v4l2_vp8_quantization_hdr* v4l2_quant_hdr) {
  v4l2_quant_hdr->y_ac_qi = vp8_quant_hdr.y_ac_qi;
  v4l2_quant_hdr->y_dc_delta = vp8_quant_hdr.y_dc_delta;
  v4l2_quant_hdr->y2_dc_delta = vp8_quant_hdr.y2_dc_delta;
  v4l2_quant_hdr->y2_ac_delta = vp8_quant_hdr.y2_ac_delta;
  v4l2_quant_hdr->uv_dc_delta = vp8_quant_hdr.uv_dc_delta;
  v4l2_quant_hdr->uv_ac_delta = vp8_quant_hdr.uv_ac_delta;
}

static void FillV4L2Vp8EntropyHeader(
    const Vp8EntropyHeader& vp8_entropy_hdr,
    struct v4l2_vp8_entropy_hdr* v4l2_entropy_hdr) {
  SafeArrayMemcpy(v4l2_entropy_hdr->coeff_probs, vp8_entropy_hdr.coeff_probs);
  SafeArrayMemcpy(v4l2_entropy_hdr->y_mode_probs, vp8_entropy_hdr.y_mode_probs);
  SafeArrayMemcpy(v4l2_entropy_hdr->uv_mode_probs,
                  vp8_entropy_hdr.uv_mode_probs);
  SafeArrayMemcpy(v4l2_entropy_hdr->mv_probs, vp8_entropy_hdr.mv_probs);
}

bool V4L2SliceVideoDecodeAccelerator::V4L2VP8Accelerator::SubmitDecode(
    scoped_refptr<VP8Picture> pic,
    const Vp8ReferenceFrameVector& reference_frames) {
  struct v4l2_ctrl_vp8_frame_hdr v4l2_frame_hdr;
  memset(&v4l2_frame_hdr, 0, sizeof(v4l2_frame_hdr));

  const auto& frame_hdr = pic->frame_hdr;
  v4l2_frame_hdr.key_frame = frame_hdr->frame_type;
#define FHDR_TO_V4L2_FHDR(a) v4l2_frame_hdr.a = frame_hdr->a
  FHDR_TO_V4L2_FHDR(version);
  FHDR_TO_V4L2_FHDR(width);
  FHDR_TO_V4L2_FHDR(horizontal_scale);
  FHDR_TO_V4L2_FHDR(height);
  FHDR_TO_V4L2_FHDR(vertical_scale);
  FHDR_TO_V4L2_FHDR(sign_bias_golden);
  FHDR_TO_V4L2_FHDR(sign_bias_alternate);
  FHDR_TO_V4L2_FHDR(prob_skip_false);
  FHDR_TO_V4L2_FHDR(prob_intra);
  FHDR_TO_V4L2_FHDR(prob_last);
  FHDR_TO_V4L2_FHDR(prob_gf);
  FHDR_TO_V4L2_FHDR(bool_dec_range);
  FHDR_TO_V4L2_FHDR(bool_dec_value);
  FHDR_TO_V4L2_FHDR(bool_dec_count);
#undef FHDR_TO_V4L2_FHDR

#define SET_V4L2_FRM_HDR_FLAG_IF(cond, flag) \
  v4l2_frame_hdr.flags |= ((frame_hdr->cond) ? (flag) : 0)
  SET_V4L2_FRM_HDR_FLAG_IF(is_experimental,
                           V4L2_VP8_FRAME_HDR_FLAG_EXPERIMENTAL);
  SET_V4L2_FRM_HDR_FLAG_IF(show_frame, V4L2_VP8_FRAME_HDR_FLAG_SHOW_FRAME);
  SET_V4L2_FRM_HDR_FLAG_IF(mb_no_skip_coeff,
                           V4L2_VP8_FRAME_HDR_FLAG_MB_NO_SKIP_COEFF);
#undef SET_V4L2_FRM_HDR_FLAG_IF

  FillV4L2SegmentationHeader(frame_hdr->segmentation_hdr,
                             &v4l2_frame_hdr.sgmnt_hdr);

  FillV4L2LoopfilterHeader(frame_hdr->loopfilter_hdr, &v4l2_frame_hdr.lf_hdr);

  FillV4L2QuantizationHeader(frame_hdr->quantization_hdr,
                             &v4l2_frame_hdr.quant_hdr);

  FillV4L2Vp8EntropyHeader(frame_hdr->entropy_hdr, &v4l2_frame_hdr.entropy_hdr);

  v4l2_frame_hdr.first_part_size =
      base::checked_cast<__u32>(frame_hdr->first_part_size);
  v4l2_frame_hdr.first_part_offset =
      base::checked_cast<__u32>(frame_hdr->first_part_offset);
  v4l2_frame_hdr.macroblock_bit_offset =
      base::checked_cast<__u32>(frame_hdr->macroblock_bit_offset);
  v4l2_frame_hdr.num_dct_parts = frame_hdr->num_of_dct_partitions;

  static_assert(arraysize(v4l2_frame_hdr.dct_part_sizes) ==
                    arraysize(frame_hdr->dct_partition_sizes),
                "DCT partition size arrays must have equal number of elements");
  for (size_t i = 0; i < frame_hdr->num_of_dct_partitions &&
                     i < arraysize(v4l2_frame_hdr.dct_part_sizes);
       ++i)
    v4l2_frame_hdr.dct_part_sizes[i] = frame_hdr->dct_partition_sizes[i];

  scoped_refptr<V4L2DecodeSurface> dec_surface =
      VP8PictureToV4L2DecodeSurface(pic);
  std::vector<scoped_refptr<V4L2DecodeSurface>> ref_surfaces;

  const auto last_frame = reference_frames.GetFrame(Vp8RefType::VP8_FRAME_LAST);
  if (last_frame) {
    scoped_refptr<V4L2DecodeSurface> last_frame_surface =
        VP8PictureToV4L2DecodeSurface(last_frame);
    v4l2_frame_hdr.last_frame = last_frame_surface->output_record();
    ref_surfaces.push_back(last_frame_surface);
  } else {
    v4l2_frame_hdr.last_frame = VIDEO_MAX_FRAME;
  }

  const auto golden_frame =
      reference_frames.GetFrame(Vp8RefType::VP8_FRAME_GOLDEN);
  if (golden_frame) {
    scoped_refptr<V4L2DecodeSurface> golden_frame_surface =
        VP8PictureToV4L2DecodeSurface(golden_frame);
    v4l2_frame_hdr.golden_frame = golden_frame_surface->output_record();
    ref_surfaces.push_back(golden_frame_surface);
  } else {
    v4l2_frame_hdr.golden_frame = VIDEO_MAX_FRAME;
  }

  const auto alt_frame =
      reference_frames.GetFrame(Vp8RefType::VP8_FRAME_ALTREF);
  if (alt_frame) {
    scoped_refptr<V4L2DecodeSurface> alt_frame_surface =
        VP8PictureToV4L2DecodeSurface(alt_frame);
    v4l2_frame_hdr.alt_frame = alt_frame_surface->output_record();
    ref_surfaces.push_back(alt_frame_surface);
  } else {
    v4l2_frame_hdr.alt_frame = VIDEO_MAX_FRAME;
  }

  struct v4l2_ext_control ctrl;
  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.id = V4L2_CID_MPEG_VIDEO_VP8_FRAME_HDR;
  ctrl.size = sizeof(v4l2_frame_hdr);
  ctrl.p_vp8_frame_hdr = &v4l2_frame_hdr;

  struct v4l2_ext_controls ext_ctrls;
  memset(&ext_ctrls, 0, sizeof(ext_ctrls));
  ext_ctrls.count = 1;
  ext_ctrls.controls = &ctrl;
  ext_ctrls.config_store = dec_surface->config_store();
  IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_S_EXT_CTRLS, &ext_ctrls);

  dec_surface->SetReferenceSurfaces(ref_surfaces);

  if (!surface_handler_->SubmitSlice(dec_surface, frame_hdr->data,
                                     frame_hdr->frame_size))
    return false;

  DVLOGF(4) << "Submitting decode for surface: " << dec_surface->ToString();
  surface_handler_->DecodeSurface(dec_surface);
  return true;
}

bool V4L2SliceVideoDecodeAccelerator::V4L2VP8Accelerator::OutputPicture(
    const scoped_refptr<VP8Picture>& pic) {
  // TODO(crbug.com/647725): Insert correct color space.
  surface_handler_->SurfaceReady(VP8PictureToV4L2DecodeSurface(pic),
                                 pic->bitstream_id(), pic->visible_rect(),
                                 VideoColorSpace());
  return true;
}

scoped_refptr<V4L2DecodeSurface> V4L2SliceVideoDecodeAccelerator::
    V4L2VP8Accelerator::VP8PictureToV4L2DecodeSurface(
        const scoped_refptr<VP8Picture>& pic) {
  V4L2VP8Picture* v4l2_pic = pic->AsV4L2VP8Picture();
  CHECK(v4l2_pic);
  return v4l2_pic->dec_surface();
}

V4L2SliceVideoDecodeAccelerator::V4L2VP9Accelerator::V4L2VP9Accelerator(
    V4L2DecodeSurfaceHandler* surface_handler,
    V4L2Device* device)
    : surface_handler_(surface_handler), device_(device) {
  DCHECK(surface_handler_);

  struct v4l2_queryctrl query_ctrl;
  memset(&query_ctrl, 0, sizeof(query_ctrl));
  query_ctrl.id = V4L2_CID_MPEG_VIDEO_VP9_ENTROPY;
  device_needs_frame_context_ =
      (device_->Ioctl(VIDIOC_QUERYCTRL, &query_ctrl) == 0);

  DVLOG_IF(1, device_needs_frame_context_)
      << "Device requires frame context parsing";
}

V4L2SliceVideoDecodeAccelerator::V4L2VP9Accelerator::~V4L2VP9Accelerator() {}

scoped_refptr<VP9Picture>
V4L2SliceVideoDecodeAccelerator::V4L2VP9Accelerator::CreateVP9Picture() {
  scoped_refptr<V4L2DecodeSurface> dec_surface =
      surface_handler_->CreateSurface();
  if (!dec_surface)
    return nullptr;

  return new V4L2VP9Picture(dec_surface);
}

static void FillV4L2VP9LoopFilterParams(
    const Vp9LoopFilterParams& vp9_lf_params,
    struct v4l2_vp9_loop_filter_params* v4l2_lf_params) {
#define SET_LF_PARAMS_FLAG_IF(cond, flag) \
  v4l2_lf_params->flags |= ((vp9_lf_params.cond) ? (flag) : 0)
  SET_LF_PARAMS_FLAG_IF(delta_enabled, V4L2_VP9_LOOP_FLTR_FLAG_DELTA_ENABLED);
  SET_LF_PARAMS_FLAG_IF(delta_update, V4L2_VP9_LOOP_FLTR_FLAG_DELTA_UPDATE);
#undef SET_LF_PARAMS_FLAG_IF

  v4l2_lf_params->level = vp9_lf_params.level;
  v4l2_lf_params->sharpness = vp9_lf_params.sharpness;

  SafeArrayMemcpy(v4l2_lf_params->deltas, vp9_lf_params.ref_deltas);
  SafeArrayMemcpy(v4l2_lf_params->mode_deltas, vp9_lf_params.mode_deltas);
  SafeArrayMemcpy(v4l2_lf_params->lvl_lookup, vp9_lf_params.lvl);
}

static void FillV4L2VP9QuantizationParams(
    const Vp9QuantizationParams& vp9_quant_params,
    struct v4l2_vp9_quantization_params* v4l2_q_params) {
#define SET_Q_PARAMS_FLAG_IF(cond, flag) \
  v4l2_q_params->flags |= ((vp9_quant_params.cond) ? (flag) : 0)
  SET_Q_PARAMS_FLAG_IF(IsLossless(), V4L2_VP9_QUANT_PARAMS_FLAG_LOSSLESS);
#undef SET_Q_PARAMS_FLAG_IF

#define Q_PARAMS_TO_V4L2_Q_PARAMS(a) v4l2_q_params->a = vp9_quant_params.a
  Q_PARAMS_TO_V4L2_Q_PARAMS(base_q_idx);
  Q_PARAMS_TO_V4L2_Q_PARAMS(delta_q_y_dc);
  Q_PARAMS_TO_V4L2_Q_PARAMS(delta_q_uv_dc);
  Q_PARAMS_TO_V4L2_Q_PARAMS(delta_q_uv_ac);
#undef Q_PARAMS_TO_V4L2_Q_PARAMS
}

static void FillV4L2VP9SegmentationParams(
    const Vp9SegmentationParams& vp9_segm_params,
    struct v4l2_vp9_segmentation_params* v4l2_segm_params) {
#define SET_SEG_PARAMS_FLAG_IF(cond, flag) \
  v4l2_segm_params->flags |= ((vp9_segm_params.cond) ? (flag) : 0)
  SET_SEG_PARAMS_FLAG_IF(enabled, V4L2_VP9_SGMNT_PARAM_FLAG_ENABLED);
  SET_SEG_PARAMS_FLAG_IF(update_map, V4L2_VP9_SGMNT_PARAM_FLAG_UPDATE_MAP);
  SET_SEG_PARAMS_FLAG_IF(temporal_update,
                         V4L2_VP9_SGMNT_PARAM_FLAG_TEMPORAL_UPDATE);
  SET_SEG_PARAMS_FLAG_IF(update_data, V4L2_VP9_SGMNT_PARAM_FLAG_UPDATE_DATA);
  SET_SEG_PARAMS_FLAG_IF(abs_or_delta_update,
                         V4L2_VP9_SGMNT_PARAM_FLAG_ABS_OR_DELTA_UPDATE);
#undef SET_SEG_PARAMS_FLAG_IF

  SafeArrayMemcpy(v4l2_segm_params->tree_probs, vp9_segm_params.tree_probs);
  SafeArrayMemcpy(v4l2_segm_params->pred_probs, vp9_segm_params.pred_probs);
  SafeArrayMemcpy(v4l2_segm_params->feature_data, vp9_segm_params.feature_data);

  static_assert(arraysize(v4l2_segm_params->feature_enabled) ==
                        arraysize(vp9_segm_params.feature_enabled) &&
                    arraysize(v4l2_segm_params->feature_enabled[0]) ==
                        arraysize(vp9_segm_params.feature_enabled[0]),
                "feature_enabled arrays must be of same size");
  for (size_t i = 0; i < arraysize(v4l2_segm_params->feature_enabled); ++i) {
    for (size_t j = 0; j < arraysize(v4l2_segm_params->feature_enabled[i]);
         ++j) {
      v4l2_segm_params->feature_enabled[i][j] =
          vp9_segm_params.feature_enabled[i][j];
    }
  }
}

static void FillV4L2Vp9EntropyContext(
    const Vp9FrameContext& vp9_frame_ctx,
    struct v4l2_vp9_entropy_ctx* v4l2_entropy_ctx) {
#define ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(a) \
  SafeArrayMemcpy(v4l2_entropy_ctx->a, vp9_frame_ctx.a)
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(tx_probs_8x8);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(tx_probs_16x16);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(tx_probs_32x32);

  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(coef_probs);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(skip_prob);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(inter_mode_probs);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(interp_filter_probs);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(is_inter_prob);

  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(comp_mode_prob);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(single_ref_prob);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(comp_ref_prob);

  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(y_mode_probs);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(uv_mode_probs);

  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(partition_probs);

  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(mv_joint_probs);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(mv_sign_prob);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(mv_class_probs);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(mv_class0_bit_prob);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(mv_bits_prob);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(mv_class0_fr_probs);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(mv_fr_probs);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(mv_class0_hp_prob);
  ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR(mv_hp_prob);
#undef ARRAY_MEMCPY_CHECKED_FRM_CTX_TO_V4L2_ENTR
}

bool V4L2SliceVideoDecodeAccelerator::V4L2VP9Accelerator::SubmitDecode(
    const scoped_refptr<VP9Picture>& pic,
    const Vp9SegmentationParams& segm_params,
    const Vp9LoopFilterParams& lf_params,
    const std::vector<scoped_refptr<VP9Picture>>& ref_pictures,
    const base::Closure& done_cb) {
  const Vp9FrameHeader* frame_hdr = pic->frame_hdr.get();
  DCHECK(frame_hdr);

  struct v4l2_ctrl_vp9_frame_hdr v4l2_frame_hdr;
  memset(&v4l2_frame_hdr, 0, sizeof(v4l2_frame_hdr));

#define FHDR_TO_V4L2_FHDR(a) v4l2_frame_hdr.a = frame_hdr->a
  FHDR_TO_V4L2_FHDR(profile);
  FHDR_TO_V4L2_FHDR(frame_type);

  FHDR_TO_V4L2_FHDR(bit_depth);
  FHDR_TO_V4L2_FHDR(color_range);
  FHDR_TO_V4L2_FHDR(subsampling_x);
  FHDR_TO_V4L2_FHDR(subsampling_y);

  FHDR_TO_V4L2_FHDR(frame_width);
  FHDR_TO_V4L2_FHDR(frame_height);
  FHDR_TO_V4L2_FHDR(render_width);
  FHDR_TO_V4L2_FHDR(render_height);

  FHDR_TO_V4L2_FHDR(reset_frame_context);

  FHDR_TO_V4L2_FHDR(interpolation_filter);
  FHDR_TO_V4L2_FHDR(frame_context_idx);

  FHDR_TO_V4L2_FHDR(tile_cols_log2);
  FHDR_TO_V4L2_FHDR(tile_rows_log2);

  FHDR_TO_V4L2_FHDR(header_size_in_bytes);
#undef FHDR_TO_V4L2_FHDR
  v4l2_frame_hdr.color_space = static_cast<uint8_t>(frame_hdr->color_space);

  FillV4L2VP9QuantizationParams(frame_hdr->quant_params,
                                &v4l2_frame_hdr.quant_params);

#define SET_V4L2_FRM_HDR_FLAG_IF(cond, flag) \
  v4l2_frame_hdr.flags |= ((frame_hdr->cond) ? (flag) : 0)
  SET_V4L2_FRM_HDR_FLAG_IF(show_frame, V4L2_VP9_FRAME_HDR_FLAG_SHOW_FRAME);
  SET_V4L2_FRM_HDR_FLAG_IF(error_resilient_mode,
                           V4L2_VP9_FRAME_HDR_FLAG_ERR_RES);
  SET_V4L2_FRM_HDR_FLAG_IF(intra_only, V4L2_VP9_FRAME_HDR_FLAG_FRAME_INTRA);
  SET_V4L2_FRM_HDR_FLAG_IF(allow_high_precision_mv,
                           V4L2_VP9_FRAME_HDR_ALLOW_HIGH_PREC_MV);
  SET_V4L2_FRM_HDR_FLAG_IF(refresh_frame_context,
                           V4L2_VP9_FRAME_HDR_REFRESH_FRAME_CTX);
  SET_V4L2_FRM_HDR_FLAG_IF(frame_parallel_decoding_mode,
                           V4L2_VP9_FRAME_HDR_PARALLEL_DEC_MODE);
#undef SET_V4L2_FRM_HDR_FLAG_IF

  FillV4L2VP9LoopFilterParams(lf_params, &v4l2_frame_hdr.lf_params);
  FillV4L2VP9SegmentationParams(segm_params, &v4l2_frame_hdr.sgmnt_params);

  std::vector<struct v4l2_ext_control> ctrls;

  struct v4l2_ext_control ctrl;
  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.id = V4L2_CID_MPEG_VIDEO_VP9_FRAME_HDR;
  ctrl.size = sizeof(v4l2_frame_hdr);
  ctrl.p_vp9_frame_hdr = &v4l2_frame_hdr;
  ctrls.push_back(ctrl);

  struct v4l2_ctrl_vp9_decode_param v4l2_decode_param;
  memset(&v4l2_decode_param, 0, sizeof(v4l2_decode_param));
  DCHECK_EQ(ref_pictures.size(), arraysize(v4l2_decode_param.ref_frames));

  std::vector<scoped_refptr<V4L2DecodeSurface>> ref_surfaces;
  for (size_t i = 0; i < ref_pictures.size(); ++i) {
    if (ref_pictures[i]) {
      scoped_refptr<V4L2DecodeSurface> ref_surface =
          VP9PictureToV4L2DecodeSurface(ref_pictures[i]);

      v4l2_decode_param.ref_frames[i] = ref_surface->output_record();
      ref_surfaces.push_back(ref_surface);
    } else {
      v4l2_decode_param.ref_frames[i] = VIDEO_MAX_FRAME;
    }
  }

  static_assert(arraysize(v4l2_decode_param.active_ref_frames) ==
                    arraysize(frame_hdr->ref_frame_idx),
                "active reference frame array sizes mismatch");

  for (size_t i = 0; i < arraysize(frame_hdr->ref_frame_idx); ++i) {
    uint8_t idx = frame_hdr->ref_frame_idx[i];
    if (idx >= ref_pictures.size())
      return false;

    struct v4l2_vp9_reference_frame* v4l2_ref_frame =
        &v4l2_decode_param.active_ref_frames[i];

    scoped_refptr<VP9Picture> ref_pic = ref_pictures[idx];
    if (ref_pic) {
      scoped_refptr<V4L2DecodeSurface> ref_surface =
          VP9PictureToV4L2DecodeSurface(ref_pic);
      v4l2_ref_frame->buf_index = ref_surface->output_record();
#define REF_TO_V4L2_REF(a) v4l2_ref_frame->a = ref_pic->frame_hdr->a
      REF_TO_V4L2_REF(frame_width);
      REF_TO_V4L2_REF(frame_height);
      REF_TO_V4L2_REF(bit_depth);
      REF_TO_V4L2_REF(subsampling_x);
      REF_TO_V4L2_REF(subsampling_y);
#undef REF_TO_V4L2_REF
    } else {
      v4l2_ref_frame->buf_index = VIDEO_MAX_FRAME;
    }
  }

  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.id = V4L2_CID_MPEG_VIDEO_VP9_DECODE_PARAM;
  ctrl.size = sizeof(v4l2_decode_param);
  ctrl.p_vp9_decode_param = &v4l2_decode_param;
  ctrls.push_back(ctrl);

  // Defined outside of the if() clause below as it must remain valid until
  // the call to SubmitExtControls().
  struct v4l2_ctrl_vp9_entropy v4l2_entropy;
  if (device_needs_frame_context_) {
    memset(&v4l2_entropy, 0, sizeof(v4l2_entropy));
    FillV4L2Vp9EntropyContext(frame_hdr->initial_frame_context,
                              &v4l2_entropy.initial_entropy_ctx);
    FillV4L2Vp9EntropyContext(frame_hdr->frame_context,
                              &v4l2_entropy.current_entropy_ctx);
    v4l2_entropy.tx_mode = frame_hdr->compressed_header.tx_mode;
    v4l2_entropy.reference_mode = frame_hdr->compressed_header.reference_mode;

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_MPEG_VIDEO_VP9_ENTROPY;
    ctrl.size = sizeof(v4l2_entropy);
    ctrl.p_vp9_entropy = &v4l2_entropy;
    ctrls.push_back(ctrl);
  }

  scoped_refptr<V4L2DecodeSurface> dec_surface =
      VP9PictureToV4L2DecodeSurface(pic);

  struct v4l2_ext_controls ext_ctrls;
  memset(&ext_ctrls, 0, sizeof(ext_ctrls));
  ext_ctrls.count = ctrls.size();
  ext_ctrls.controls = &ctrls[0];
  ext_ctrls.config_store = dec_surface->config_store();
  IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_S_EXT_CTRLS, &ext_ctrls);

  dec_surface->SetReferenceSurfaces(ref_surfaces);
  dec_surface->SetDecodeDoneCallback(done_cb);

  if (!surface_handler_->SubmitSlice(dec_surface, frame_hdr->data,
                                     frame_hdr->frame_size))
    return false;

  DVLOGF(4) << "Submitting decode for surface: " << dec_surface->ToString();
  surface_handler_->DecodeSurface(dec_surface);
  return true;
}

bool V4L2SliceVideoDecodeAccelerator::V4L2VP9Accelerator::OutputPicture(
    const scoped_refptr<VP9Picture>& pic) {
  // TODO(crbug.com/647725): Insert correct color space.
  surface_handler_->SurfaceReady(VP9PictureToV4L2DecodeSurface(pic),
                                 pic->bitstream_id(), pic->visible_rect(),
                                 VideoColorSpace());
  return true;
}

static void FillVp9FrameContext(struct v4l2_vp9_entropy_ctx& v4l2_entropy_ctx,
                                Vp9FrameContext* vp9_frame_ctx) {
#define ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(a) \
  SafeArrayMemcpy(vp9_frame_ctx->a, v4l2_entropy_ctx.a)
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(tx_probs_8x8);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(tx_probs_16x16);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(tx_probs_32x32);

  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(coef_probs);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(skip_prob);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(inter_mode_probs);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(interp_filter_probs);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(is_inter_prob);

  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(comp_mode_prob);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(single_ref_prob);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(comp_ref_prob);

  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(y_mode_probs);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(uv_mode_probs);

  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(partition_probs);

  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(mv_joint_probs);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(mv_sign_prob);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(mv_class_probs);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(mv_class0_bit_prob);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(mv_bits_prob);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(mv_class0_fr_probs);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(mv_fr_probs);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(mv_class0_hp_prob);
  ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX(mv_hp_prob);
#undef ARRAY_MEMCPY_CHECKED_V4L2_ENTR_TO_FRM_CTX
}

bool V4L2SliceVideoDecodeAccelerator::V4L2VP9Accelerator::GetFrameContext(
    const scoped_refptr<VP9Picture>& pic,
    Vp9FrameContext* frame_ctx) {
  struct v4l2_ctrl_vp9_entropy v4l2_entropy;
  memset(&v4l2_entropy, 0, sizeof(v4l2_entropy));

  struct v4l2_ext_control ctrl;
  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.id = V4L2_CID_MPEG_VIDEO_VP9_ENTROPY;
  ctrl.size = sizeof(v4l2_entropy);
  ctrl.p_vp9_entropy = &v4l2_entropy;

  scoped_refptr<V4L2DecodeSurface> dec_surface =
      VP9PictureToV4L2DecodeSurface(pic);

  struct v4l2_ext_controls ext_ctrls;
  memset(&ext_ctrls, 0, sizeof(ext_ctrls));
  ext_ctrls.count = 1;
  ext_ctrls.controls = &ctrl;
  ext_ctrls.config_store = dec_surface->config_store();
  IOCTL_OR_ERROR_RETURN_FALSE(VIDIOC_G_EXT_CTRLS, &ext_ctrls);

  FillVp9FrameContext(v4l2_entropy.current_entropy_ctx, frame_ctx);
  return true;
}

scoped_refptr<V4L2DecodeSurface> V4L2SliceVideoDecodeAccelerator::
    V4L2VP9Accelerator::VP9PictureToV4L2DecodeSurface(
        const scoped_refptr<VP9Picture>& pic) {
  V4L2VP9Picture* v4l2_pic = pic->AsV4L2VP9Picture();
  CHECK(v4l2_pic);
  return v4l2_pic->dec_surface();
}

void V4L2SliceVideoDecodeAccelerator::SurfaceReady(
    const scoped_refptr<V4L2DecodeSurface>& dec_surface,
    int32_t bitstream_id,
    const gfx::Rect& visible_rect,
    const VideoColorSpace& /* color_space */) {
  DVLOGF(4);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  dec_surface->SetVisibleRect(visible_rect);
  decoder_display_queue_.push(std::make_pair(bitstream_id, dec_surface));
  TryOutputSurfaces();
}

void V4L2SliceVideoDecodeAccelerator::TryOutputSurfaces() {
  while (!decoder_display_queue_.empty()) {
    scoped_refptr<V4L2DecodeSurface> dec_surface =
        decoder_display_queue_.front().second;

    if (!dec_surface->decoded())
      break;

    int32_t bitstream_id = decoder_display_queue_.front().first;
    decoder_display_queue_.pop();
    OutputSurface(bitstream_id, dec_surface);
  }
}

void V4L2SliceVideoDecodeAccelerator::OutputSurface(
    int32_t bitstream_id,
    const scoped_refptr<V4L2DecodeSurface>& dec_surface) {
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());

  OutputRecord& output_record =
      output_buffer_map_[dec_surface->output_record()];

  if (output_record.num_times_sent_to_client == 0) {
    DCHECK(!output_record.at_client);
    output_record.at_client = true;
    bool inserted =
        surfaces_at_display_
            .insert(std::make_pair(output_record.picture_id, dec_surface))
            .second;
    DCHECK(inserted);
  } else {
    // The surface is already sent to client, and not returned back yet.
    DCHECK(output_record.at_client);
    DCHECK(surfaces_at_display_.find(output_record.picture_id) !=
           surfaces_at_display_.end());
    CHECK(surfaces_at_display_[output_record.picture_id].get() ==
          dec_surface.get());
  }

  DCHECK(!output_record.at_device);
  DCHECK_NE(output_record.picture_id, -1);
  ++output_record.num_times_sent_to_client;

  // TODO(hubbe): Insert correct color space. http://crbug.com/647725
  Picture picture(output_record.picture_id, bitstream_id,
                  dec_surface->visible_rect(), gfx::ColorSpace(),
                  true /* allow_overlay */);
  DVLOGF(4) << dec_surface->ToString()
            << ", bitstream_id: " << picture.bitstream_buffer_id()
            << ", picture_id: " << picture.picture_buffer_id()
            << ", visible_rect: " << picture.visible_rect().ToString();
  pending_picture_ready_.push(PictureRecord(output_record.cleared, picture));
  SendPictureReady();
  output_record.cleared = true;
}

scoped_refptr<V4L2DecodeSurface>
V4L2SliceVideoDecodeAccelerator::CreateSurface() {
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(state_, kDecoding);

  if (free_input_buffers_.empty() || free_output_buffers_.empty())
    return nullptr;

  int input = free_input_buffers_.front();
  free_input_buffers_.pop_front();
  int output = free_output_buffers_.front();
  free_output_buffers_.pop_front();

  InputRecord& input_record = input_buffer_map_[input];
  DCHECK_EQ(input_record.bytes_used, 0u);
  DCHECK_EQ(input_record.input_id, -1);
  DCHECK(decoder_current_bitstream_buffer_ != nullptr);
  input_record.input_id = decoder_current_bitstream_buffer_->input_id;

  scoped_refptr<V4L2DecodeSurface> dec_surface = new V4L2DecodeSurface(
      input, output,
      base::Bind(&V4L2SliceVideoDecodeAccelerator::ReuseOutputBuffer,
                 base::Unretained(this)));

  DVLOGF(4) << "Created surface " << input << " -> " << output;
  return dec_surface;
}

void V4L2SliceVideoDecodeAccelerator::SendPictureReady() {
  DVLOGF(4);
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  bool send_now =
      (decoder_resetting_ || decoder_flushing_ || surface_set_change_pending_);
  while (!pending_picture_ready_.empty()) {
    bool cleared = pending_picture_ready_.front().cleared;
    const Picture& picture = pending_picture_ready_.front().picture;
    if (cleared && picture_clearing_count_ == 0) {
      DVLOGF(4) << "Posting picture ready to decode task runner for: "
                << picture.picture_buffer_id();
      // This picture is cleared. It can be posted to a thread different than
      // the main GPU thread to reduce latency. This should be the case after
      // all pictures are cleared at the beginning.
      decode_task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&Client::PictureReady, decode_client_, picture));
      pending_picture_ready_.pop();
    } else if (!cleared || send_now) {
      DVLOGF(4) << "cleared=" << pending_picture_ready_.front().cleared
                << ", decoder_resetting_=" << decoder_resetting_
                << ", decoder_flushing_=" << decoder_flushing_
                << ", surface_set_change_pending_="
                << surface_set_change_pending_
                << ", picture_clearing_count_=" << picture_clearing_count_;
      DVLOGF(4) << "Posting picture ready to GPU for: "
                << picture.picture_buffer_id();
      // If the picture is not cleared, post it to the child thread because it
      // has to be cleared in the child thread. A picture only needs to be
      // cleared once. If the decoder is resetting or flushing or changing
      // resolution, send all pictures to ensure PictureReady arrive before
      // reset done, flush done, or picture dismissed.
      child_task_runner_->PostTaskAndReply(
          FROM_HERE, base::BindOnce(&Client::PictureReady, client_, picture),
          // Unretained is safe. If Client::PictureReady gets to run, |this| is
          // alive. Destroy() will wait the decode thread to finish.
          base::Bind(&V4L2SliceVideoDecodeAccelerator::PictureCleared,
                     base::Unretained(this)));
      picture_clearing_count_++;
      pending_picture_ready_.pop();
    } else {
      // This picture is cleared. But some pictures are about to be cleared on
      // the child thread. To preserve the order, do not send this until those
      // pictures are cleared.
      break;
    }
  }
}

void V4L2SliceVideoDecodeAccelerator::PictureCleared() {
  DVLOGF(4) << "clearing count=" << picture_clearing_count_;
  DCHECK(decoder_thread_task_runner_->BelongsToCurrentThread());
  DCHECK_GT(picture_clearing_count_, 0);
  picture_clearing_count_--;
  SendPictureReady();
}

bool V4L2SliceVideoDecodeAccelerator::TryToSetupDecodeOnSeparateThread(
    const base::WeakPtr<Client>& decode_client,
    const scoped_refptr<base::SingleThreadTaskRunner>& decode_task_runner) {
  decode_client_ = decode_client;
  decode_task_runner_ = decode_task_runner;
  return true;
}

// static
VideoDecodeAccelerator::SupportedProfiles
V4L2SliceVideoDecodeAccelerator::GetSupportedProfiles() {
  scoped_refptr<V4L2Device> device = V4L2Device::Create();
  if (!device)
    return SupportedProfiles();

  return device->GetSupportedDecodeProfiles(arraysize(supported_input_fourccs_),
                                            supported_input_fourccs_);
}

}  // namespace media
