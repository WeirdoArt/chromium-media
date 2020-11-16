// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/av1_decoder.h"

#include <bitset>

#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/stl_util.h"
#include "media/base/limits.h"
#include "media/gpu/av1_picture.h"
#include "third_party/libgav1/src/src/decoder_state.h"
#include "third_party/libgav1/src/src/gav1/status_code.h"
#include "third_party/libgav1/src/src/utils/constants.h"

namespace media {
namespace {
// (Section 6.4.1):
//
// - "An operating point specifies which spatial and temporal layers should be
//   decoded."
//
// - "The order of operating points indicates the preferred order for producing
//   an output: a decoder should select the earliest operating point in the list
//   that meets its decoding capabilities as expressed by the level associated
//   with each operating point."
//
// For simplicity, we always select operating point 0 and will validate that it
// doesn't have scalability information.
constexpr unsigned int kDefaultOperatingPoint = 0;

// Conversion function from libgav1 profiles to media::VideoCodecProfile.
VideoCodecProfile AV1ProfileToVideoCodecProfile(
    libgav1::BitstreamProfile profile) {
  switch (profile) {
    case libgav1::kProfile0:
      return AV1PROFILE_PROFILE_MAIN;
    case libgav1::kProfile1:
      return AV1PROFILE_PROFILE_HIGH;
    case libgav1::kProfile2:
      return AV1PROFILE_PROFILE_PRO;
    default:
      // ObuParser::ParseSequenceHeader() validates the profile.
      NOTREACHED() << "Invalid profile: " << base::strict_cast<int>(profile);
      return AV1PROFILE_PROFILE_MAIN;
  }
}

// Returns true iff the sequence has spatial or temporal scalability information
// for the selected operating point.
bool SequenceUsesScalability(int operating_point_idc) {
  return operating_point_idc != 0;
}

bool IsYUV420Sequence(const libgav1::ColorConfig& color_config) {
  return color_config.subsampling_x == 1 && color_config.subsampling_y == 1 &&
         !color_config.is_monochrome;
}
}  // namespace

AV1Decoder::AV1Decoder(std::unique_ptr<AV1Accelerator> accelerator,
                       VideoCodecProfile profile)
    : buffer_pool_(std::make_unique<libgav1::BufferPool>(
          /*on_frame_buffer_size_changed=*/nullptr,
          /*get_frame_buffer=*/nullptr,
          /*release_frame_buffer=*/nullptr,
          /*callback_private_data=*/nullptr)),
      state_(std::make_unique<libgav1::DecoderState>()),
      accelerator_(std::move(accelerator)),
      profile_(profile) {
  ref_frames_.fill(nullptr);
}

AV1Decoder::~AV1Decoder() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // |buffer_pool_| checks that all the allocated frames are released in its
  // dtor. Explicitly destruct |state_| before |buffer_pool_| to release frames
  // in |reference_frame| in |state_|.
  state_.reset();
}

bool AV1Decoder::HasNewSequenceHeader() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(parser_);
  const auto& obu_headers = parser_->obu_headers();
  const bool has_sequence_header =
      std::find_if(obu_headers.begin(), obu_headers.end(),
                   [](const auto& obu_header) {
                     return obu_header.type == libgav1::kObuSequenceHeader;
                   }) != obu_headers.end();
  if (!has_sequence_header)
    return false;
  if (!current_sequence_header_)
    return true;
  return parser_->sequence_header().ParametersChanged(
      *current_sequence_header_);
}

bool AV1Decoder::Flush() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(2) << "Decoder flush";
  Reset();
  return true;
}

void AV1Decoder::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ClearCurrentFrame();
  // Resetting current sequence header might not be necessary. But this violates
  // AcceleratedVideoDecoder interface spec, "Reset any current state that may
  // be cached in the accelerator."
  // TODO(hiroh): We may want to change this interface spec so that a caller
  // doesn't have to allocate video frames buffers every seek operation.
  current_sequence_header_.reset();
  visible_rect_ = gfx::Rect();
  frame_size_ = gfx::Size();
  profile_ = VideoCodecProfile::VIDEO_CODEC_PROFILE_UNKNOWN;
  stream_id_ = 0;
  stream_ = nullptr;
  stream_size_ = 0;
  on_error_ = false;

  state_ = std::make_unique<libgav1::DecoderState>();
  ClearReferenceFrames();
  parser_.reset();

  buffer_pool_ = std::make_unique<libgav1::BufferPool>(
      /*on_frame_buffer_size_changed=*/nullptr,
      /*get_frame_buffer=*/nullptr,
      /*release_frame_buffer=*/nullptr,
      /*callback_private_data=*/nullptr);
}

void AV1Decoder::SetStream(int32_t id, const DecoderBuffer& decoder_buffer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  stream_id_ = id;
  stream_ = decoder_buffer.data();
  stream_size_ = decoder_buffer.data_size();
  ClearCurrentFrame();

  parser_ = base::WrapUnique(new (std::nothrow) libgav1::ObuParser(
      decoder_buffer.data(), decoder_buffer.data_size(), kDefaultOperatingPoint,
      buffer_pool_.get(), state_.get()));
  if (!parser_) {
    on_error_ = true;
    return;
  }

  if (current_sequence_header_)
    parser_->set_sequence_header(*current_sequence_header_);
}

void AV1Decoder::ClearCurrentFrame() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  current_frame_.reset();
  current_frame_header_.reset();
}

AcceleratedVideoDecoder::DecodeResult AV1Decoder::Decode() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (on_error_)
    return kDecodeError;
  auto result = DecodeInternal();
  on_error_ = result == kDecodeError;
  return result;
}

AcceleratedVideoDecoder::DecodeResult AV1Decoder::DecodeInternal() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!parser_) {
    DLOG(ERROR) << "Decode() is called before SetStream()";
    return kDecodeError;
  }
  while (parser_->HasData() || current_frame_header_) {
    base::ScopedClosureRunner clear_current_frame(
        base::BindOnce(&AV1Decoder::ClearCurrentFrame, base::Unretained(this)));
    if (!current_frame_header_) {
      libgav1::StatusCode status_code = parser_->ParseOneFrame(&current_frame_);
      if (status_code != libgav1::kStatusOk) {
        DLOG(WARNING) << "Failed to parse OBU: "
                      << libgav1::GetErrorString(status_code);
        return kDecodeError;
      }
      if (!current_frame_) {
        DLOG(WARNING) << "No frame found. Skipping the current stream";
        continue;
      }

      current_frame_header_ = parser_->frame_header();
      // Detects if a new coded video sequence is starting.
      // TODO(b/171853869): Replace HasNewSequenceHeader() with whatever
      // libgav1::ObuParser provides for more than one sequence headers case.
      if (HasNewSequenceHeader()) {
        // TODO(b/171853869): Remove this check once libgav1::ObuParser does
        // this check.
        if (current_frame_header_->frame_type != libgav1::kFrameKey ||
            !current_frame_header_->show_frame ||
            current_frame_header_->show_existing_frame ||
            current_frame_->temporal_id() != 0) {
          // Section 7.5.
          DVLOG(1)
              << "The first frame successive to sequence header OBU must be a "
              << "keyframe with show_frame=1, show_existing_frame=0 and "
              << "temporal_id=0";
          return kDecodeError;
        }
        if (SequenceUsesScalability(
                parser_->sequence_header()
                    .operating_point_idc[kDefaultOperatingPoint])) {
          DVLOG(3) << "Either temporal or spatial layer decoding is not "
                   << "supported";
          return kDecodeError;
        }

        current_sequence_header_ = parser_->sequence_header();
        // TODO(hiroh): Expose the bit depth to let the AV1Decoder client
        // perform these checks.
        if (current_sequence_header_->color_config.bitdepth != 8) {
          // TODO(hiroh): Handle 10/12 bit depth.
          DVLOG(1) << "10/12 bit depth are not supported";
          return kDecodeError;
        }
        if (!IsYUV420Sequence(current_sequence_header_->color_config)) {
          DVLOG(1) << "Only YUV 4:2:0 is supported";
          return kDecodeError;
        }

        const gfx::Size new_frame_size(
            base::strict_cast<int>(current_sequence_header_->max_frame_width),
            base::strict_cast<int>(current_sequence_header_->max_frame_height));
        gfx::Rect new_visible_rect(
            base::strict_cast<int>(current_frame_header_->render_width),
            base::strict_cast<int>(current_frame_header_->render_height));
        const VideoCodecProfile new_profile =
            AV1ProfileToVideoCodecProfile(current_sequence_header_->profile);
        DCHECK(!new_frame_size.IsEmpty());
        if (!gfx::Rect(new_frame_size).Contains(new_visible_rect)) {
          DVLOG(1) << "Render size exceeds picture size. render size: "
                   << new_visible_rect.ToString()
                   << ", picture size: " << new_frame_size.ToString();
          new_visible_rect = gfx::Rect(new_frame_size);
        }

        ClearReferenceFrames();
        // Issues kConfigChange only if either the dimensions or profile is
        // changed.
        if (frame_size_ != new_frame_size ||
            visible_rect_ != new_visible_rect || profile_ != new_profile) {
          frame_size_ = new_frame_size;
          visible_rect_ = new_visible_rect;
          profile_ = new_profile;
          clear_current_frame.ReplaceClosure(base::DoNothing());
          return kConfigChange;
        }
      }
    }

    if (!current_sequence_header_) {
      // Decoding is not doable because we haven't received a sequence header.
      // This occurs when seeking a video.
      DVLOG(3) << "Discarded the current frame because no sequence header has "
               << "been found yet";
      continue;
    }

    DCHECK(current_frame_header_);
    const auto& frame_header = *current_frame_header_;
    if (frame_header.show_existing_frame) {
      const size_t frame_to_show =
          base::checked_cast<size_t>(frame_header.frame_to_show);
      DCHECK_LE(0u, frame_to_show);
      DCHECK_LT(frame_to_show, ref_frames_.size());
      if (!CheckAndCleanUpReferenceFrames()) {
        DLOG(ERROR) << "The states of reference frames are different between"
                    << "|ref_frames_| and |state_->reference_frame|";
        return kDecodeError;
      }

      auto pic = ref_frames_[frame_to_show];
      CHECK(pic);
      pic = pic->Duplicate();
      if (!pic) {
        DVLOG(1) << "Failed duplication";
        return kDecodeError;
      }

      pic->set_bitstream_id(stream_id_);
      if (!accelerator_->OutputPicture(*pic)) {
        return kDecodeError;
      }

      // libgav1::ObuParser sets |current_frame_| to the frame to show while
      // |current_frame_header_| is the frame header of the currently parsed
      // frame. If |current_frame_| is a keyframe, then refresh_frame_flags must
      // be 0xff. Otherwise, refresh_frame_flags must be 0x00 (Section 5.9.2).
      DCHECK(current_frame_->frame_type() == libgav1::kFrameKey ||
             current_frame_header_->refresh_frame_flags == 0x00);
      DCHECK(current_frame_->frame_type() != libgav1::kFrameKey ||
             current_frame_header_->refresh_frame_flags == 0xff);
      UpdateReferenceFrames(std::move(pic));
      continue;
    }

    if (parser_->tile_buffers().empty()) {
      // The last call to ParseOneFrame() didn't actually have any tile groups.
      // This could happen in rare cases (for example, if there is a Metadata
      // OBU after the TileGroup OBU). Ignore this case.
      continue;
    }

    const gfx::Size current_frame_size(
        base::strict_cast<int>(frame_header.width),
        base::strict_cast<int>(frame_header.height));
    if (current_frame_size != frame_size_) {
      // TODO(hiroh): This must be handled in decoding spatial layer.
      DVLOG(1) << "Resolution change in the middle of video sequence (i.e."
               << " between sequence headers) is not supported";
      return kDecodeError;
    }
    if (current_frame_size.width() !=
        base::strict_cast<int>(frame_header.upscaled_width)) {
      DVLOG(1) << "Super resolution is not supported";
      return kDecodeError;
    }
    const gfx::Rect current_visible_rect(
        base::strict_cast<int>(frame_header.render_width),
        base::strict_cast<int>(frame_header.render_height));
    if (current_visible_rect != visible_rect_) {
      // TODO(andrescj): Handle the visible rectangle change in the middle of
      // video sequence.
      DVLOG(1) << "Visible rectangle change in the middle of video sequence"
               << "(i.e. between sequence headers) is not supported";
      return kDecodeError;
    }

    auto pic = accelerator_->CreateAV1Picture(
        frame_header.film_grain_params.apply_grain);
    if (!pic) {
      clear_current_frame.ReplaceClosure(base::DoNothing());
      return kRanOutOfSurfaces;
    }

    pic->set_visible_rect(current_visible_rect);
    pic->set_bitstream_id(stream_id_);
    pic->frame_header = frame_header;
    // TODO(hiroh): Set color space and decrypt config.
    if (!DecodeAndOutputPicture(std::move(pic), parser_->tile_buffers()))
      return kDecodeError;
  }
  return kRanOutOfStreamData;
}

void AV1Decoder::UpdateReferenceFrames(scoped_refptr<AV1Picture> pic) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(state_);
  DCHECK(current_frame_header_);
  const uint8_t refresh_frame_flags =
      current_frame_header_->refresh_frame_flags;
  DCHECK(current_frame_->frame_type() != libgav1::kFrameKey ||
         current_frame_header_->refresh_frame_flags == 0xff);
  const std::bitset<libgav1::kNumReferenceFrameTypes> update_reference_frame(
      refresh_frame_flags);
  for (size_t i = 0; i < libgav1::kNumReferenceFrameTypes; ++i) {
    if (update_reference_frame[i])
      ref_frames_[i] = pic;
  }
  state_->UpdateReferenceFrames(current_frame_,
                                base::strict_cast<int>(refresh_frame_flags));
}

void AV1Decoder::ClearReferenceFrames() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(state_);
  ref_frames_.fill(nullptr);
  // If AV1Decoder has decided to clear the reference frames, then ObuParser
  // must have also decided to do so.
  DCHECK_EQ(base::STLCount(state_->reference_frame, nullptr),
            static_cast<int>(state_->reference_frame.size()));
}

bool AV1Decoder::CheckAndCleanUpReferenceFrames() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(state_);
  const auto& reference_valid = state_->reference_valid;
  for (size_t i = 0; i < libgav1::kNumReferenceFrameTypes; ++i) {
    if (reference_valid[i] && !ref_frames_[i])
      return false;
    if (!reference_valid[i] && ref_frames_[i])
      ref_frames_[i].reset();
  }
  return true;
}

bool AV1Decoder::DecodeAndOutputPicture(
    scoped_refptr<AV1Picture> pic,
    const libgav1::Vector<libgav1::TileBuffer>& tile_buffers) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(pic);
  DCHECK(current_sequence_header_);
  DCHECK(stream_);
  DCHECK_GT(stream_size_, 0u);
  if (!CheckAndCleanUpReferenceFrames()) {
    DLOG(ERROR) << "The states of reference frames are different between"
                << "|ref_frames_| and |state_->reference_frame|";
    return false;
  }
  if (!accelerator_->SubmitDecode(*pic, *current_sequence_header_, ref_frames_,
                                  tile_buffers,
                                  base::make_span(stream_, stream_size_))) {
    return false;
  }

  if (pic->frame_header.show_frame && !accelerator_->OutputPicture(*pic))
    return false;
  UpdateReferenceFrames(std::move(pic));
  return true;
}

gfx::Size AV1Decoder::GetPicSize() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TODO(hiroh): It should be safer to align this by 64 or 128 (depending on
  // use_128x128_superblock) so that a driver doesn't touch out of the buffer.
  return frame_size_;
}

gfx::Rect AV1Decoder::GetVisibleRect() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return visible_rect_;
}

VideoCodecProfile AV1Decoder::GetProfile() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return profile_;
}

size_t AV1Decoder::GetRequiredNumOfPictures() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TODO(hiroh): Double this value in the case of film grain sequence.
  constexpr size_t kPicsInPipeline = limits::kMaxVideoFrames + 1;
  return kPicsInPipeline + GetNumReferenceFrames();
}

size_t AV1Decoder::GetNumReferenceFrames() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return libgav1::kNumReferenceFrameTypes;
}
}  // namespace media