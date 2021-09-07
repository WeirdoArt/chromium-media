// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/vaapi/vaapi_image_processor_backend.h"

#include <stdint.h>

#include <va/va.h>

#include "base/bind.h"
#include "base/callback.h"
#include "base/containers/contains.h"
#include "base/memory/ptr_util.h"
#include "base/memory/scoped_refptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/stl_util.h"
#include "base/task/post_task.h"
#include "media/gpu/chromeos/fourcc.h"
#include "media/gpu/chromeos/platform_video_frame_utils.h"
#include "media/gpu/macros.h"
#include "media/gpu/vaapi/va_surface.h"
#include "media/gpu/vaapi/vaapi_utils.h"
#include "media/gpu/vaapi/vaapi_wrapper.h"
#include "ui/gfx/native_pixmap.h"

namespace media {

#if defined(OS_CHROMEOS)
namespace {
bool IsSupported(const ImageProcessorBackend::PortConfig& config) {
  if (!config.fourcc.ToVAFourCC())
    return false;
  const uint32_t va_fourcc = *config.fourcc.ToVAFourCC();
  if (!VaapiWrapper::IsVppFormatSupported(va_fourcc)) {
    VLOGF(2) << "Unsupported format: VA_FOURCC_" << FourccToString(va_fourcc);
    return false;
  }
  if (!VaapiWrapper::IsVppResolutionAllowed(config.size)) {
    VLOGF(2) << "Unsupported size: " << config.size.ToString();
    return false;
  }
  const gfx::Size& visible_size = config.visible_rect.size();
  if (!VaapiWrapper::IsVppResolutionAllowed(visible_size)) {
    VLOGF(2) << "Unsupported visible size: " << visible_size.ToString();
    return false;
  }
  // TODO(b/195351653): this check only makes sense if
  // ImageProcessor::Process() validates the visible rectangle for each frame.
  if (!gfx::Rect(config.size).Contains(config.visible_rect)) {
    VLOGF(2) << "The frame size (" << config.size.ToString()
             << ") does not contain the visible rect ("
             << config.visible_rect.ToString() << ")";
    return false;
  }
  return true;
}

}  // namespace
#endif

// static
std::unique_ptr<ImageProcessorBackend> VaapiImageProcessorBackend::Create(
    const PortConfig& input_config,
    const PortConfig& output_config,
    const std::vector<OutputMode>& preferred_output_modes,
    VideoRotation relative_rotation,
    ErrorCB error_cb,
    scoped_refptr<base::SequencedTaskRunner> backend_task_runner) {
// VaapiImageProcessorBackend supports ChromeOS only.
#if !defined(OS_CHROMEOS)
  return nullptr;
#else
  if (!IsSupported(input_config) || !IsSupported(output_config))
    return nullptr;

  if (!base::Contains(input_config.preferred_storage_types,
                      VideoFrame::STORAGE_DMABUFS) &&
      !base::Contains(input_config.preferred_storage_types,
                      VideoFrame::STORAGE_GPU_MEMORY_BUFFER)) {
    VLOGF(2) << "VaapiImageProcessorBackend supports Dmabuf-backed or "
                "GpuMemoryBuffer based VideoFrame only for input";
    return nullptr;
  }
  if (!base::Contains(output_config.preferred_storage_types,
                      VideoFrame::STORAGE_DMABUFS) &&
      !base::Contains(output_config.preferred_storage_types,
                      VideoFrame::STORAGE_GPU_MEMORY_BUFFER)) {
    VLOGF(2) << "VaapiImageProcessorBackend supports Dmabuf-backed or "
                "GpuMemoryBuffer based VideoFrame only for output";
    return nullptr;
  }

  if (!base::Contains(preferred_output_modes, OutputMode::IMPORT)) {
    VLOGF(2) << "VaapiImageProcessorBackend only supports IMPORT mode.";
    return nullptr;
  }

  // Note that EncryptionScheme::kUnencrypted is fine even for the use case
  // where the VPP is needed for processing protected content after decoding.
  // That's because when calling VaapiWrapper::BlitSurface(), we re-use the
  // protected session ID created at decoding time.
  auto vaapi_wrapper = VaapiWrapper::Create(
      VaapiWrapper::kVideoProcess, VAProfileNone,
      EncryptionScheme::kUnencrypted,
      base::BindRepeating(&ReportVaapiErrorToUMA,
                          "Media.VaapiImageProcessorBackend.VAAPIError"));
  if (!vaapi_wrapper) {
    VLOGF(1) << "Failed to create VaapiWrapper";
    return nullptr;
  }

  // Size is irrelevant for a VPP context.
  if (!vaapi_wrapper->CreateContext(gfx::Size())) {
    VLOGF(1) << "Failed to create context for VPP";
    return nullptr;
  }

  // Checks if VA-API driver supports rotation.
  if (relative_rotation != VIDEO_ROTATION_0 &&
      !vaapi_wrapper->IsRotationSupported()) {
    VLOGF(1) << "VaapiIP doesn't support rotation";
    return nullptr;
  }

  // We should restrict the acceptable PortConfig for input and output both to
  // the one returned by GetPlatformVideoFrameLayout(). However,
  // ImageProcessorFactory interface doesn't provide information about what
  // ImageProcessor will be used for. (e.g. format conversion after decoding and
  // scaling before encoding). Thus we cannot execute
  // GetPlatformVideoFrameLayout() with a proper gfx::BufferUsage.
  // TODO(crbug.com/898423): Adjust layout once ImageProcessor provide the use
  // scenario.
  return base::WrapUnique<ImageProcessorBackend>(new VaapiImageProcessorBackend(
      std::move(vaapi_wrapper), input_config, output_config, OutputMode::IMPORT,
      relative_rotation, std::move(error_cb), std::move(backend_task_runner)));
#endif
}

VaapiImageProcessorBackend::VaapiImageProcessorBackend(
    scoped_refptr<VaapiWrapper> vaapi_wrapper,
    const PortConfig& input_config,
    const PortConfig& output_config,
    OutputMode output_mode,
    VideoRotation relative_rotation,
    ErrorCB error_cb,
    scoped_refptr<base::SequencedTaskRunner> backend_task_runner)
    : ImageProcessorBackend(input_config,
                            output_config,
                            output_mode,
                            relative_rotation,
                            std::move(error_cb),
                            std::move(backend_task_runner)),
      vaapi_wrapper_(std::move(vaapi_wrapper)) {}

VaapiImageProcessorBackend::~VaapiImageProcessorBackend() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(backend_sequence_checker_);
}

void VaapiImageProcessorBackend::Process(scoped_refptr<VideoFrame> input_frame,
                                         scoped_refptr<VideoFrame> output_frame,
                                         FrameReadyCB cb) {
  DVLOGF(4);
  DCHECK_CALLED_ON_VALID_SEQUENCE(backend_sequence_checker_);

  bool use_protected = false;
#if BUILDFLAG(IS_CHROMEOS_ASH)
  VAProtectedSessionID va_protected_session_id = VA_INVALID_ID;
  if (input_frame->metadata().hw_va_protected_session_id.has_value()) {
    static_assert(
        std::is_same<decltype(input_frame->metadata()
                                  .hw_va_protected_session_id)::value_type,
                     VAProtectedSessionID>::value,
        "The optional type of VideoFrameMetadata::hw_va_protected_session_id "
        "is "
        "not VAProtectedSessionID");
    va_protected_session_id =
        input_frame->metadata().hw_va_protected_session_id.value();
    use_protected = va_protected_session_id != VA_INVALID_ID;
  }
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  DCHECK(input_frame);
  DCHECK(output_frame);
  scoped_refptr<gfx::NativePixmap> input_pixmap =
      CreateNativePixmapDmaBuf(input_frame.get());
  if (!input_pixmap) {
    VLOGF(1) << "Failed to create NativePixmap from VideoFrame";
    error_cb_.Run();
    return;
  }

  auto src_va_surface = vaapi_wrapper_->CreateVASurfaceForPixmap(
      std::move(input_pixmap), use_protected);
  if (!src_va_surface) {
    error_cb_.Run();
    return;
  }

  scoped_refptr<gfx::NativePixmap> output_pixmap =
      CreateNativePixmapDmaBuf(output_frame.get());
  if (!output_pixmap) {
    VLOGF(1) << "Failed to create NativePixmap from VideoFrame";
    error_cb_.Run();
    return;
  }

  auto dst_va_surface = vaapi_wrapper_->CreateVASurfaceForPixmap(
      std::move(output_pixmap), use_protected);
  if (!dst_va_surface) {
    error_cb_.Run();
    return;
  }

  // VA-API performs pixel format conversion and scaling without any filters.
  if (!vaapi_wrapper_->BlitSurface(
          *src_va_surface, *dst_va_surface, input_frame->visible_rect(),
          output_frame->visible_rect(), relative_rotation_
#if BUILDFLAG(IS_CHROMEOS_ASH)
          ,
          va_protected_session_id
#endif
          )) {
#if BUILDFLAG(IS_CHROMEOS_ASH)
    if (use_protected &&
        vaapi_wrapper_->IsProtectedSessionDead(va_protected_session_id)) {
      DCHECK_NE(va_protected_session_id, VA_INVALID_ID);

      // If the VPP failed because the protected session is dead, we should
      // still output the frame. That's because we don't want to put the
      // VideoDecoderPipeline into an unusable error state: the
      // VaapiVideoDecoder can recover from a dead protected session later and
      // the compositor should not try to render the frame we output here
      // anyway.
      output_frame->set_timestamp(input_frame->timestamp());
      std::move(cb).Run(std::move(output_frame));
      return;
    }
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
    error_cb_.Run();
    return;
  }

  output_frame->set_timestamp(input_frame->timestamp());
  std::move(cb).Run(std::move(output_frame));
}

}  // namespace media
