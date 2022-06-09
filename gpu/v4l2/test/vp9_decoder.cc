// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/v4l2/test/vp9_decoder.h"

#include <linux/media/vp9-ctrls-upstream.h>
#include <sys/ioctl.h>

#include "base/bits.h"
#include "base/files/memory_mapped_file.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "media/filters/ivf_parser.h"
#include "media/filters/vp9_parser.h"
#include "media/gpu/macros.h"

namespace media {

namespace v4l2_test {

constexpr uint32_t kNumberOfBuffersInCaptureQueue = 10;

static_assert(kNumberOfBuffersInCaptureQueue <= 16,
              "Too many CAPTURE buffers are used. The number of CAPTURE "
              "buffers is currently assumed to be no larger than 16.");

#define SET_IF(bit_field, cond, mask) (bit_field) |= ((cond) ? (mask) : 0)

inline void conditionally_set_flag(struct v4l2_ctrl_vp9_frame& params,
                                   const bool condition,
                                   const __u32 flag) {
  params.flags |= condition ? flag : 0;
}

void FillV4L2VP9QuantizationParams(
    const Vp9QuantizationParams& vp9_quant_params,
    struct v4l2_vp9_quantization* v4l2_quant) {
  v4l2_quant->base_q_idx =
      base::checked_cast<__u8>(vp9_quant_params.base_q_idx);
  v4l2_quant->delta_q_y_dc =
      base::checked_cast<__s8>(vp9_quant_params.delta_q_y_dc);
  v4l2_quant->delta_q_uv_dc =
      base::checked_cast<__s8>(vp9_quant_params.delta_q_uv_dc);
  v4l2_quant->delta_q_uv_ac =
      base::checked_cast<__s8>(vp9_quant_params.delta_q_uv_ac);
}

void FillV4L2VP9LoopFilterParams(const Vp9LoopFilterParams& vp9_lf_params,
                                 struct v4l2_vp9_loop_filter* v4l2_lf) {
  SET_IF(v4l2_lf->flags, vp9_lf_params.delta_enabled,
         V4L2_VP9_LOOP_FILTER_FLAG_DELTA_ENABLED);

  SET_IF(v4l2_lf->flags, vp9_lf_params.delta_update,
         V4L2_VP9_LOOP_FILTER_FLAG_DELTA_UPDATE);

  v4l2_lf->level = vp9_lf_params.level;
  v4l2_lf->sharpness = vp9_lf_params.sharpness;
  SafeArrayMemcpy(v4l2_lf->ref_deltas, vp9_lf_params.ref_deltas);
  SafeArrayMemcpy(v4l2_lf->mode_deltas, vp9_lf_params.mode_deltas);
}

void FillV4L2VP9SegmentationParams(const Vp9SegmentationParams& vp9_seg_params,
                                   struct v4l2_vp9_segmentation* v4l2_seg) {
  SET_IF(v4l2_seg->flags, vp9_seg_params.enabled,
         V4L2_VP9_SEGMENTATION_FLAG_ENABLED);
  SET_IF(v4l2_seg->flags, vp9_seg_params.update_map,
         V4L2_VP9_SEGMENTATION_FLAG_UPDATE_MAP);
  SET_IF(v4l2_seg->flags, vp9_seg_params.temporal_update,
         V4L2_VP9_SEGMENTATION_FLAG_TEMPORAL_UPDATE);
  SET_IF(v4l2_seg->flags, vp9_seg_params.update_data,
         V4L2_VP9_SEGMENTATION_FLAG_UPDATE_DATA);
  SET_IF(v4l2_seg->flags, vp9_seg_params.abs_or_delta_update,
         V4L2_VP9_SEGMENTATION_FLAG_ABS_OR_DELTA_UPDATE);

  SafeArrayMemcpy(v4l2_seg->tree_probs, vp9_seg_params.tree_probs);
  SafeArrayMemcpy(v4l2_seg->pred_probs, vp9_seg_params.pred_probs);

  static_assert(static_cast<size_t>(Vp9SegmentationParams::SEG_LVL_MAX) ==
                    static_cast<size_t>(V4L2_VP9_SEG_LVL_MAX),
                "mismatch in number of segmentation features");

  for (size_t j = 0;
       j < std::extent<decltype(vp9_seg_params.feature_enabled), 0>::value;
       j++) {
    for (size_t i = 0;
         i < std::extent<decltype(vp9_seg_params.feature_enabled), 1>::value;
         i++) {
      if (vp9_seg_params.feature_enabled[j][i])
        v4l2_seg->feature_enabled[j] |= V4L2_VP9_SEGMENT_FEATURE_ENABLED(i);
    }
  }

  SafeArrayMemcpy(v4l2_seg->feature_data, vp9_seg_params.feature_data);
}

// TODO(b/228876644): assert that |parsing_compressed_header| is indeed false.
Vp9Decoder::Vp9Decoder(std::unique_ptr<IvfParser> ivf_parser,
                       std::unique_ptr<V4L2IoctlShim> v4l2_ioctl,
                       std::unique_ptr<V4L2Queue> OUTPUT_queue,
                       std::unique_ptr<V4L2Queue> CAPTURE_queue)
    : VideoDecoder::VideoDecoder(std::move(v4l2_ioctl),
                                 std::move(OUTPUT_queue),
                                 std::move(CAPTURE_queue)),
      ivf_parser_(std::move(ivf_parser)),
      vp9_parser_(
          std::make_unique<Vp9Parser>(/*parsing_compressed_header=*/false)) {
  DCHECK(v4l2_ioctl_);

  // TODO(b/230021497): add change in SetExtCtrls function.
  CHECK(!v4l2_ioctl_->QueryCtrl(V4L2_CID_STATELESS_VP9_COMPRESSED_HDR))
      << "VP9 compressed header not supported with current platform decoding "
         "code.";

  // MTK8192, MTK8195 don't support V4L2_CID_STATELESS_VP9_COMPRESSED_HDR.
  LOG(INFO) << "VIDIOC_QUERYCTRL ioctl failure with "
               "V4L2_CID_STATELESS_VP9_COMPRESSED_HDR is expected because VP9 "
               "compressed header is not supported on current platform. VP9 "
               "compressed header support is optional.";

  // This control was landed in v5.17 and is pretty much a marker that the
  // driver supports the stable API.
  DCHECK(v4l2_ioctl_->QueryCtrl(V4L2_CID_STATELESS_VP9_FRAME));
}

Vp9Decoder::~Vp9Decoder() = default;

// static
std::unique_ptr<Vp9Decoder> Vp9Decoder::Create(
    const base::MemoryMappedFile& stream) {
  constexpr uint32_t kDriverCodecFourcc = V4L2_PIX_FMT_VP9_FRAME;

  VLOG(2) << "Attempting to create decoder with codec "
          << media::FourccToString(kDriverCodecFourcc);

  // Set up video parser.
  auto ivf_parser = std::make_unique<media::IvfParser>();
  media::IvfFileHeader file_header{};

  if (!ivf_parser->Initialize(stream.data(), stream.length(), &file_header)) {
    LOG(ERROR) << "Couldn't initialize IVF parser";
    return nullptr;
  }

  const auto driver_codec_fourcc =
      media::v4l2_test::FileFourccToDriverFourcc(file_header.fourcc);

  if (driver_codec_fourcc != kDriverCodecFourcc) {
    VLOG(2) << "File fourcc (" << media::FourccToString(driver_codec_fourcc)
            << ") does not match expected fourcc("
            << media::FourccToString(kDriverCodecFourcc) << ").";
    return nullptr;
  }

  // MM21 is an uncompressed opaque format that is produced by MediaTek
  // video decoders.
  const uint32_t kUncompressedFourcc = v4l2_fourcc('M', 'M', '2', '1');

  auto v4l2_ioctl = std::make_unique<V4L2IoctlShim>();

  if (!v4l2_ioctl->VerifyCapabilities(kDriverCodecFourcc,
                                      kUncompressedFourcc)) {
    LOG(ERROR) << "Device doesn't support the provided FourCCs.";
    return nullptr;
  }

  LOG(INFO) << "Ivf file header: " << file_header.width << " x "
            << file_header.height;

  // TODO(stevecho): might need to consider using more than 1 file descriptor
  // (fd) & buffer with the output queue for 4K60 requirement.
  // https://buganizer.corp.google.com/issues/202214561#comment31
  auto OUTPUT_queue = std::make_unique<V4L2Queue>(
      V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, kDriverCodecFourcc,
      gfx::Size(file_header.width, file_header.height), /*num_planes=*/1,
      V4L2_MEMORY_MMAP, /*num_buffers=*/1);

  // TODO(stevecho): enable V4L2_MEMORY_DMABUF memory for CAPTURE queue.
  // |num_planes| represents separate memory buffers, not planes for Y, U, V.
  // https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/pixfmt-v4l2-mplane.html#c.V4L.v4l2_plane_pix_format
  auto CAPTURE_queue = std::make_unique<V4L2Queue>(
      V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, kUncompressedFourcc,
      gfx::Size(file_header.width, file_header.height), /*num_planes=*/2,
      V4L2_MEMORY_MMAP, /*num_buffers=*/kNumberOfBuffersInCaptureQueue);

  return base::WrapUnique(
      new Vp9Decoder(std::move(ivf_parser), std::move(v4l2_ioctl),
                     std::move(OUTPUT_queue), std::move(CAPTURE_queue)));
}

std::set<int> Vp9Decoder::RefreshReferenceSlots(
    uint8_t refresh_frame_flags,
    scoped_refptr<MmapedBuffer> buffer,
    uint32_t last_queued_buffer_index) {
  const std::bitset<kVp9NumRefFrames> refresh_frame_slots(refresh_frame_flags);

  std::set<int> reusable_buffer_slots;

  static_assert(kVp9NumRefFrames == sizeof(refresh_frame_flags) * CHAR_BIT,
                "|refresh_frame_flags| size should not be larger than "
                "|kVp9NumRefFrames|");

  constexpr uint8_t kRefreshFrameFlagsNone = 0;
  if (refresh_frame_flags == kRefreshFrameFlagsNone) {
    // Indicates to reuse currently decoded CAPTURE buffer.
    reusable_buffer_slots.insert(buffer->buffer_id());

    return reusable_buffer_slots;
  }

  constexpr uint8_t kRefreshFrameFlagsAll = 0xFF;
  if (refresh_frame_flags == kRefreshFrameFlagsAll) {
    // After decoding a key frame, all CAPTURE buffers can be reused except the
    // CAPTURE buffer corresponding to the key frame.
    for (size_t i = 0; i < kNumberOfBuffersInCaptureQueue; i++)
      reusable_buffer_slots.insert(i);

    reusable_buffer_slots.erase(buffer->buffer_id());

    // Note that the CAPTURE buffer for previous frame can be used as well,
    // but it is already queued again at this point.
    reusable_buffer_slots.erase(last_queued_buffer_index);

    // Updates to assign current key frame as a reference frame for all
    // reference frame slots in the reference frames list.
    ref_frames_.fill(buffer);

    return reusable_buffer_slots;
  }

  // More than one slots in |refresh_frame_flags| can be set.
  uint16_t reusable_candidate_buffer_id;
  for (size_t i = 0; i < kVp9NumRefFrames; i++) {
    if (!refresh_frame_slots[i])
      continue;

    // It is not required to check whether existing reference frame slot is
    // already pointing to a reference frame. This is because reference
    // frame slots are empty only after the first key frame decoding.
    reusable_candidate_buffer_id = ref_frames_[i]->buffer_id();
    reusable_buffer_slots.insert(reusable_candidate_buffer_id);

    // Checks to make sure |reusable_candidate_buffer_id| is not used in
    // different reference frame slots in the reference frames list.
    for (size_t j = 0; j < kVp9NumRefFrames; j++) {
      const bool is_refresh_slot_not_used = (refresh_frame_slots[j] == false);
      const bool is_candidate_not_used =
          (ref_frames_[j]->buffer_id() == reusable_candidate_buffer_id);

      if (is_refresh_slot_not_used && is_candidate_not_used) {
        reusable_buffer_slots.erase(reusable_candidate_buffer_id);
        break;
      }
    }
    ref_frames_[i] = buffer;
  }

  return reusable_buffer_slots;
}

Vp9Parser::Result Vp9Decoder::ReadNextFrame(Vp9FrameHeader& vp9_frame_header,
                                            gfx::Size& size) {
  // TODO(jchinlee): reexamine this loop for cleanup.
  while (true) {
    std::unique_ptr<DecryptConfig> null_config;
    Vp9Parser::Result res =
        vp9_parser_->ParseNextFrame(&vp9_frame_header, &size, &null_config);
    if (res == Vp9Parser::kEOStream) {
      IvfFrameHeader ivf_frame_header{};
      const uint8_t* ivf_frame_data;

      if (!ivf_parser_->ParseNextFrame(&ivf_frame_header, &ivf_frame_data))
        return Vp9Parser::kEOStream;

      vp9_parser_->SetStream(ivf_frame_data, ivf_frame_header.frame_size,
                             /*stream_config=*/nullptr);
      continue;
    }

    return res;
  }
}

void Vp9Decoder::SetupFrameParams(
    const Vp9FrameHeader& frame_hdr,
    struct v4l2_ctrl_vp9_frame* v4l2_frame_params) {
  conditionally_set_flag(*v4l2_frame_params,
                         frame_hdr.frame_type == Vp9FrameHeader::KEYFRAME,
                         V4L2_VP9_FRAME_FLAG_KEY_FRAME);
  conditionally_set_flag(*v4l2_frame_params, frame_hdr.show_frame,
                         V4L2_VP9_FRAME_FLAG_SHOW_FRAME);
  conditionally_set_flag(*v4l2_frame_params, frame_hdr.error_resilient_mode,
                         V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT);
  conditionally_set_flag(*v4l2_frame_params, frame_hdr.intra_only,
                         V4L2_VP9_FRAME_FLAG_INTRA_ONLY);
  conditionally_set_flag(*v4l2_frame_params, frame_hdr.allow_high_precision_mv,
                         V4L2_VP9_FRAME_FLAG_ALLOW_HIGH_PREC_MV);
  conditionally_set_flag(*v4l2_frame_params, frame_hdr.refresh_frame_context,
                         V4L2_VP9_FRAME_FLAG_REFRESH_FRAME_CTX);
  conditionally_set_flag(*v4l2_frame_params,
                         frame_hdr.frame_parallel_decoding_mode,
                         V4L2_VP9_FRAME_FLAG_PARALLEL_DEC_MODE);
  conditionally_set_flag(*v4l2_frame_params, frame_hdr.subsampling_x,
                         V4L2_VP9_FRAME_FLAG_X_SUBSAMPLING);
  conditionally_set_flag(*v4l2_frame_params, frame_hdr.subsampling_y,
                         V4L2_VP9_FRAME_FLAG_Y_SUBSAMPLING);
  conditionally_set_flag(*v4l2_frame_params, frame_hdr.color_range,
                         V4L2_VP9_FRAME_FLAG_COLOR_RANGE_FULL_SWING);

  v4l2_frame_params->compressed_header_size = frame_hdr.header_size_in_bytes;
  v4l2_frame_params->uncompressed_header_size =
      frame_hdr.uncompressed_header_size;
  v4l2_frame_params->profile = frame_hdr.profile;
  // As per the VP9 specification:
  switch (frame_hdr.reset_frame_context) {
    // "0 or 1 implies don’t reset."
    case 0:
    case 1:
      v4l2_frame_params->reset_frame_context = V4L2_VP9_RESET_FRAME_CTX_NONE;
      break;
    // "2 resets just the context specified in the frame header."
    case 2:
      v4l2_frame_params->reset_frame_context = V4L2_VP9_RESET_FRAME_CTX_SPEC;
      break;
    // "3 reset all contexts."
    case 3:
      v4l2_frame_params->reset_frame_context = V4L2_VP9_RESET_FRAME_CTX_ALL;
      break;
    default:
      LOG(FATAL) << "Invalid reset frame context value!";
      v4l2_frame_params->reset_frame_context = V4L2_VP9_RESET_FRAME_CTX_NONE;
      break;
  }
  v4l2_frame_params->frame_context_idx =
      frame_hdr.frame_context_idx_to_save_probs;
  v4l2_frame_params->bit_depth = frame_hdr.bit_depth;
  v4l2_frame_params->interpolation_filter = frame_hdr.interpolation_filter;
  v4l2_frame_params->tile_cols_log2 = frame_hdr.tile_cols_log2;
  v4l2_frame_params->tile_rows_log2 = frame_hdr.tile_rows_log2;
  v4l2_frame_params->reference_mode =
      frame_hdr.compressed_header.reference_mode;
  static_assert(Vp9RefType::VP9_FRAME_MAX - VP9_FRAME_LAST <
                    std::extent<decltype(frame_hdr.ref_frame_sign_bias)>::value,
                "array sizes are incompatible");
  for (size_t i = 0; i < Vp9RefType::VP9_FRAME_MAX - VP9_FRAME_LAST; i++) {
    v4l2_frame_params->ref_frame_sign_bias |=
        (frame_hdr.ref_frame_sign_bias[i + VP9_FRAME_LAST] ? (1 << i) : 0);
  }
  v4l2_frame_params->frame_width_minus_1 = frame_hdr.frame_width - 1;
  v4l2_frame_params->frame_height_minus_1 = frame_hdr.frame_height - 1;
  v4l2_frame_params->render_width_minus_1 = frame_hdr.render_width - 1;
  v4l2_frame_params->render_height_minus_1 = frame_hdr.render_height - 1;

  constexpr uint64_t kInvalidSurface = std::numeric_limits<uint32_t>::max();

  for (size_t i = 0; i < std::size(frame_hdr.ref_frame_idx); ++i) {
    const auto idx = frame_hdr.ref_frame_idx[i];

    LOG_ASSERT(idx < kVp9NumRefFrames) << "Invalid reference frame index.\n";

    // We need to convert a reference frame's frame_number() (in  microseconds)
    // to reference ID (in nanoseconds). Technically, v4l2_timeval_to_ns() is
    // suggested to be used to convert timestamp to nanoseconds, but multiplying
    // the microseconds part of timestamp |tv_usec| by |kTimestampToNanoSecs| to
    // make it nanoseconds is also known to work. This is how it is implemented
    // in v4l2 video decode accelerator tests as well as in gstreamer.
    // https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/dev-stateless-decoder.html#buffer-management-while-decoding
    constexpr size_t kTimestampToNanoSecs = 1000;

    const auto reference_id =
        ref_frames_[idx]
            ? ref_frames_[idx]->frame_number() * kTimestampToNanoSecs
            : kInvalidSurface;

    // Only partially/indirectly documented in the VP9 spec, but this array
    // contains LAST, GOLDEN, and ALT, in that order.
    switch (i) {
      case 0:
        v4l2_frame_params->last_frame_ts = reference_id;
        break;
      case 1:
        v4l2_frame_params->golden_frame_ts = reference_id;
        break;
      case 2:
        v4l2_frame_params->alt_frame_ts = reference_id;
        break;
      default:
        NOTREACHED() << "Invalid reference frame index";
    }
  }

  FillV4L2VP9QuantizationParams(frame_hdr.quant_params,
                                &v4l2_frame_params->quant);

  const Vp9Parser::Context& context = vp9_parser_->context();
  const Vp9LoopFilterParams& lf_params = context.loop_filter();
  const Vp9SegmentationParams& segm_params = context.segmentation();

  FillV4L2VP9LoopFilterParams(lf_params, &v4l2_frame_params->lf);
  FillV4L2VP9SegmentationParams(segm_params, &v4l2_frame_params->seg);
}

void Vp9Decoder::CopyFrameData(const Vp9FrameHeader& frame_hdr,
                               std::unique_ptr<V4L2Queue>& queue) {
  LOG_ASSERT(queue->num_buffers() == 1)
      << "Only 1 buffer is expected to be used for OUTPUT queue for now.";

  LOG_ASSERT(queue->num_planes() == 1)
      << "Number of planes is expected to be 1 for OUTPUT queue.";

  scoped_refptr<MmapedBuffer> buffer = queue->GetBuffer(0);

  memcpy(static_cast<uint8_t*>(buffer->mmaped_planes()[0].start_addr),
         frame_hdr.data, frame_hdr.frame_size);
}

VideoDecoder::Result Vp9Decoder::DecodeNextFrame(std::vector<char>& y_plane,
                                                 std::vector<char>& u_plane,
                                                 std::vector<char>& v_plane,
                                                 gfx::Size& size,
                                                 const int frame_number) {
  Vp9FrameHeader frame_hdr{};

  Vp9Parser::Result parser_res = ReadNextFrame(frame_hdr, size);
  switch (parser_res) {
    case Vp9Parser::kInvalidStream:
      LOG_ASSERT(false) << "Failed to parse frame.";
      return Vp9Decoder::kError;
    case Vp9Parser::kAwaitingRefresh:
      LOG_ASSERT(false) << "Unsupported parser return value.";
      return Vp9Decoder::kError;
    case Vp9Parser::kEOStream:
      return Vp9Decoder::kEOStream;
    case Vp9Parser::kOk:
      break;
  }

  VLOG_IF(2, !frame_hdr.show_frame) << "not displaying frame";
  last_decoded_frame_visible_ = frame_hdr.show_frame;

  CopyFrameData(frame_hdr, OUTPUT_queue_);

  LOG_ASSERT(OUTPUT_queue_->num_buffers() == 1)
      << "Too many buffers in OUTPUT queue. It is currently designed to "
         "support only 1 request at a time.";

  OUTPUT_queue_->GetBuffer(0)->set_frame_number(frame_number);

  if (!v4l2_ioctl_->QBuf(OUTPUT_queue_, 0))
    LOG(FATAL) << "VIDIOC_QBUF failed for OUTPUT queue.";

  struct v4l2_ctrl_vp9_frame v4l2_frame_params;
  memset(&v4l2_frame_params, 0, sizeof(v4l2_frame_params));

  SetupFrameParams(frame_hdr, &v4l2_frame_params);

  struct v4l2_ext_control ext_ctrl = {.id = V4L2_CID_STATELESS_VP9_FRAME,
                                      .size = sizeof(v4l2_frame_params),
                                      .ptr = &v4l2_frame_params};

  if (!v4l2_ioctl_->SetExtCtrls(OUTPUT_queue_, ext_ctrl))
    LOG(FATAL) << "VIDIOC_S_EXT_CTRLS failed.";

  if (!v4l2_ioctl_->MediaRequestIocQueue(OUTPUT_queue_))
    LOG(FATAL) << "MEDIA_REQUEST_IOC_QUEUE failed.";

  uint32_t index;

  if (!v4l2_ioctl_->DQBuf(CAPTURE_queue_, &index))
    LOG(FATAL) << "VIDIOC_DQBUF failed for CAPTURE queue.";

  scoped_refptr<MmapedBuffer> buffer = CAPTURE_queue_->GetBuffer(index);
  CHECK_EQ(buffer->mmaped_planes().size(), 2u)
      << "MM21 should have exactly 2 planes but CAPTURE queue does not.";

  CHECK_EQ(CAPTURE_queue_->fourcc(), v4l2_fourcc('M', 'M', '2', '1'));
  size = CAPTURE_queue_->display_size();
  ConvertMM21ToYUV(y_plane, u_plane, v_plane,
                   static_cast<char*>(buffer->mmaped_planes()[0].start_addr),
                   static_cast<char*>(buffer->mmaped_planes()[1].start_addr),
                   size);

  const std::set<int> reusable_buffer_slots = RefreshReferenceSlots(
      frame_hdr.refresh_frame_flags, CAPTURE_queue_->GetBuffer(index),
      CAPTURE_queue_->last_queued_buffer_index());

  for (const auto reusable_buffer_slot : reusable_buffer_slots) {
    if (!v4l2_ioctl_->QBuf(CAPTURE_queue_, reusable_buffer_slot))
      LOG(ERROR) << "VIDIOC_QBUF failed for CAPTURE queue.";

    // For inter frames, |refresh_frame_flags| indicates which reference frame
    // slot (usually 1 slot, but can be more than 1 slots) can be reused. Then,
    // CAPTURE buffer corresponding to this reference frame slot is queued
    // again. If we encounter a key frame now, |refresh_frame_flags = 0xFF|
    // indicates all reference frame slots can be reused. But we already queued
    // one CAPTURE buffer again after decoding the previous frame. So we want to
    // avoid queuing this specific CAPTURE buffer again.
    // This issue only happens at key frames, which comes after inter frames.
    // Inter frames coming right after key frames doesn't have this issue, so we
    // don't need to track which buffer was queued for key frames.
    if (frame_hdr.frame_type == Vp9FrameHeader::INTERFRAME)
      CAPTURE_queue_->set_last_queued_buffer_index(reusable_buffer_slot);
  }

  if (!v4l2_ioctl_->DQBuf(OUTPUT_queue_, &index))
    LOG(FATAL) << "VIDIOC_DQBUF failed for OUTPUT queue.";

  // TODO(stevecho): With current VP9 API, VIDIOC_G_EXT_CTRLS ioctl call is
  // needed when forward probabilities update is used. With new VP9 API landing
  // in kernel 5.17, VIDIOC_G_EXT_CTRLS ioctl call is no longer needed, see:
  // https://lwn.net/Articles/855419/

  if (!v4l2_ioctl_->MediaRequestIocReinit(OUTPUT_queue_))
    LOG(FATAL) << "MEDIA_REQUEST_IOC_REINIT failed.";

  return Vp9Decoder::kOk;
}

}  // namespace v4l2_test
}  // namespace media
