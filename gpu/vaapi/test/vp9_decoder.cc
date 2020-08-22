// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <va/va.h>

#include "media/gpu/macros.h"
#include "media/gpu/vaapi/test/macros.h"
#include "media/gpu/vaapi/test/vp9_decoder.h"

namespace media {
namespace vaapi_test {

Vp9Decoder::Vp9Decoder(std::unique_ptr<IvfParser> ivf_parser,
                       const VaapiDevice& va)
    : ivf_parser_(std::move(ivf_parser)),
      va_(va),
      context_id_(VA_INVALID_ID),
      vp9_parser_(
          std::make_unique<Vp9Parser>(/*parsing_compressed_header=*/false)),
      ref_frames_(kVp9NumRefFrames) {}

Vp9Decoder::~Vp9Decoder() {
  if (context_id_ != VA_INVALID_ID) {
    VAStatus res = vaDestroyContext(va_.display(), context_id_);
    VA_LOG_ASSERT(res, "vaDestroyContext");
  }
}

Vp9Parser::Result Vp9Decoder::ReadNextFrame(Vp9FrameHeader& vp9_frame_header,
                                            gfx::Size& size) {
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

void Vp9Decoder::RefreshReferenceSlots(uint8_t refresh_frame_flags,
                                       scoped_refptr<SharedVASurface> surface) {
  const std::bitset<kVp9NumRefFrames> slots(refresh_frame_flags);
  for (size_t i = 0; i < kVp9NumRefFrames; i++) {
    if (slots[i])
      ref_frames_[i] = surface;
  }
}

VideoDecoder::Result Vp9Decoder::DecodeNextFrame() {
  // Parse next frame from stream.
  gfx::Size size;
  Vp9FrameHeader frame_hdr{};
  Vp9Parser::Result parser_res = ReadNextFrame(frame_hdr, size);
  if (parser_res == Vp9Parser::kEOStream)
    return VideoDecoder::kEOStream;
  if (parser_res != Vp9Parser::kOk) {
    LOG(ERROR) << "Failed to parse next frame, got " << parser_res;
    return VideoDecoder::kFailed;
  }

  VLOG_IF(2, !frame_hdr.show_frame) << "not displaying frame";

  if (frame_hdr.show_existing_frame) {
    last_decoded_surface_ = ref_frames_[frame_hdr.frame_to_show_map_idx];
    LOG_ASSERT(last_decoded_surface_)
        << "got invalid surface as existing frame to decode";
    VLOG(2) << "ref_frame: " << last_decoded_surface_->id();
    return VideoDecoder::kOk;
  }

  // Create context for decode.
  VAStatus res;
  if (context_id_ == VA_INVALID_ID) {
    res = vaCreateContext(va_.display(), va_.config_id(), size.width(),
                          size.height(), VA_PROGRESSIVE,
                          /*render_targets=*/nullptr,
                          /*num_render_targets=*/0, &context_id_);
    VA_LOG_ASSERT(res, "vaCreateContext");
  }

  // Create surfaces for decode.
  VASurfaceAttrib attribute{};
  attribute.type = VASurfaceAttribUsageHint;
  attribute.flags = VA_SURFACE_ATTRIB_SETTABLE;
  attribute.value.type = VAGenericValueTypeInteger;
  attribute.value.value.i = VA_SURFACE_ATTRIB_USAGE_HINT_DECODER;
  scoped_refptr<SharedVASurface> surface =
      SharedVASurface::Create(va_, size, attribute);

  const Vp9Parser::Context& context = vp9_parser_->context();
  const Vp9SegmentationParams& seg = context.segmentation();
  const Vp9LoopFilterParams& lf = context.loop_filter();

  // Set up buffer for picture level parameters.
  VADecPictureParameterBufferVP9 pic_param{};

  pic_param.frame_width = base::checked_cast<uint16_t>(frame_hdr.frame_width);
  pic_param.frame_height = base::checked_cast<uint16_t>(frame_hdr.frame_height);
  CHECK_EQ(kVp9NumRefFrames, base::size(pic_param.reference_frames));
  CHECK_EQ(kVp9NumRefFrames, ref_frames_.size());
  for (size_t i = 0; i < base::size(pic_param.reference_frames); ++i) {
    pic_param.reference_frames[i] =
        ref_frames_[i] ? ref_frames_[i]->id() : VA_INVALID_SURFACE;
  }

#define FHDR_TO_PP_PF1(a) pic_param.pic_fields.bits.a = frame_hdr.a
#define FHDR_TO_PP_PF2(a, b) pic_param.pic_fields.bits.a = b
  FHDR_TO_PP_PF2(subsampling_x, frame_hdr.subsampling_x == 1);
  FHDR_TO_PP_PF2(subsampling_y, frame_hdr.subsampling_y == 1);
  FHDR_TO_PP_PF2(frame_type, frame_hdr.IsKeyframe() ? 0 : 1);
  FHDR_TO_PP_PF1(show_frame);
  FHDR_TO_PP_PF1(error_resilient_mode);
  FHDR_TO_PP_PF1(intra_only);
  FHDR_TO_PP_PF1(allow_high_precision_mv);
  FHDR_TO_PP_PF2(mcomp_filter_type, frame_hdr.interpolation_filter);
  FHDR_TO_PP_PF1(frame_parallel_decoding_mode);
  FHDR_TO_PP_PF1(reset_frame_context);
  FHDR_TO_PP_PF1(refresh_frame_context);
  FHDR_TO_PP_PF2(frame_context_idx, frame_hdr.frame_context_idx_to_save_probs);
  FHDR_TO_PP_PF2(segmentation_enabled, seg.enabled);
  FHDR_TO_PP_PF2(segmentation_temporal_update, seg.temporal_update);
  FHDR_TO_PP_PF2(segmentation_update_map, seg.update_map);
  FHDR_TO_PP_PF2(last_ref_frame, frame_hdr.ref_frame_idx[0]);
  FHDR_TO_PP_PF2(last_ref_frame_sign_bias,
                 frame_hdr.ref_frame_sign_bias[Vp9RefType::VP9_FRAME_LAST]);
  FHDR_TO_PP_PF2(golden_ref_frame, frame_hdr.ref_frame_idx[1]);
  FHDR_TO_PP_PF2(golden_ref_frame_sign_bias,
                 frame_hdr.ref_frame_sign_bias[Vp9RefType::VP9_FRAME_GOLDEN]);
  FHDR_TO_PP_PF2(alt_ref_frame, frame_hdr.ref_frame_idx[2]);
  FHDR_TO_PP_PF2(alt_ref_frame_sign_bias,
                 frame_hdr.ref_frame_sign_bias[Vp9RefType::VP9_FRAME_ALTREF]);
  FHDR_TO_PP_PF2(lossless_flag, frame_hdr.quant_params.IsLossless());
#undef FHDR_TO_PP_PF2
#undef FHDR_TO_PP_PF1

  pic_param.filter_level = lf.level;
  pic_param.sharpness_level = lf.sharpness;
  pic_param.log2_tile_rows = frame_hdr.tile_rows_log2;
  pic_param.log2_tile_columns = frame_hdr.tile_cols_log2;
  pic_param.frame_header_length_in_bytes = frame_hdr.uncompressed_header_size;
  pic_param.first_partition_size = frame_hdr.header_size_in_bytes;

  SafeArrayMemcpy(pic_param.mb_segment_tree_probs, seg.tree_probs);
  SafeArrayMemcpy(pic_param.segment_pred_probs, seg.pred_probs);

  pic_param.profile = frame_hdr.profile;
  pic_param.bit_depth = frame_hdr.bit_depth;
  DCHECK((pic_param.profile == 0 && pic_param.bit_depth == 8) ||
         (pic_param.profile == 2 && pic_param.bit_depth == 10));

  std::vector<VABufferID> buffers;
  VABufferID buffer_id;
  res = vaCreateBuffer(va_.display(), context_id_, VAPictureParameterBufferType,
                       sizeof(VADecPictureParameterBufferVP9), 1u, &pic_param,
                       &buffer_id);
  VA_LOG_ASSERT(res, "vaCreateBuffer");
  buffers.push_back(buffer_id);

  // Set up buffer for slice decoding.
  VASliceParameterBufferVP9 slice_param{};
  slice_param.slice_data_size = frame_hdr.frame_size;
  slice_param.slice_data_offset = 0;
  slice_param.slice_data_flag = VA_SLICE_DATA_FLAG_ALL;

  for (size_t i = 0; i < base::size(slice_param.seg_param); ++i) {
    VASegmentParameterVP9& seg_param = slice_param.seg_param[i];
#define SEG_TO_SP_SF(a, b) seg_param.segment_flags.fields.a = b
    SEG_TO_SP_SF(
        segment_reference_enabled,
        seg.FeatureEnabled(i, Vp9SegmentationParams::SEG_LVL_REF_FRAME));
    SEG_TO_SP_SF(segment_reference,
                 seg.FeatureData(i, Vp9SegmentationParams::SEG_LVL_REF_FRAME));
    SEG_TO_SP_SF(segment_reference_skipped,
                 seg.FeatureEnabled(i, Vp9SegmentationParams::SEG_LVL_SKIP));
#undef SEG_TO_SP_SF

    SafeArrayMemcpy(seg_param.filter_level, lf.lvl[i]);

    seg_param.luma_dc_quant_scale = seg.y_dequant[i][0];
    seg_param.luma_ac_quant_scale = seg.y_dequant[i][1];
    seg_param.chroma_dc_quant_scale = seg.uv_dequant[i][0];
    seg_param.chroma_ac_quant_scale = seg.uv_dequant[i][1];
  }

  res = vaCreateBuffer(va_.display(), context_id_, VASliceParameterBufferType,
                       sizeof(VASliceParameterBufferVP9), 1u, &slice_param,
                       &buffer_id);
  VA_LOG_ASSERT(res, "vaCreateBuffer");
  buffers.push_back(buffer_id);

  // Set up buffer for frame header.
  res = vaCreateBuffer(va_.display(), context_id_, VASliceDataBufferType,
                       frame_hdr.frame_size, 1u,
                       const_cast<uint8_t*>(frame_hdr.data), &buffer_id);
  VA_LOG_ASSERT(res, "vaCreateBuffer");
  buffers.push_back(buffer_id);

  res = vaBeginPicture(va_.display(), context_id_, surface->id());
  VA_LOG_ASSERT(res, "vaBeginPicture");

  res = vaRenderPicture(va_.display(), context_id_, buffers.data(),
                        buffers.size());
  VA_LOG_ASSERT(res, "vaRenderPicture");

  res = vaEndPicture(va_.display(), context_id_);
  VA_LOG_ASSERT(res, "vaEndPicture");

  last_decoded_surface_ = surface;

  RefreshReferenceSlots(frame_hdr.refresh_frame_flags, surface);

  for (auto id : buffers)
    vaDestroyBuffer(va_.display(), id);
  buffers.clear();

  return VideoDecoder::kOk;
}

void Vp9Decoder::LastDecodedFrameToPNG(const std::string& path) {
  last_decoded_surface_->SaveAsPNG(path);
}

}  // namespace vaapi_test
}  // namespace media