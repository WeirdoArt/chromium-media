// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_V4L2_TEST_AV1_DECODER_H_
#define MEDIA_GPU_V4L2_TEST_AV1_DECODER_H_

#include "media/gpu/v4l2/test/video_decoder.h"

// TODO(b/234019411): Move this include to v4l2_stateless_decoder.cc
// once the bug is fixed.
#include <linux/media/av1-ctrls.h>

#include <set>

#include "base/files/memory_mapped_file.h"
#include "media/filters/ivf_parser.h"
#include "media/gpu/v4l2/test/v4l2_ioctl_shim.h"
// For libgav1::ObuSequenceHeader. absl::optional demands ObuSequenceHeader to
// fulfill std::is_trivially_constructible if it is forward-declared. But
// ObuSequenceHeader doesn't.
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/libgav1/src/src/obu_parser.h"

// TODO(stevecho): RESTORATION_TILESIZE_MAX in the spec is not available in the
// AV1 uAPI. It was recommended to be added in the userspace code. If the uAPI
// stays as it is for upstreaming, then #ifndef can be removed. If the uAPI ends
// up adding this constant, then we can remove this define at that time.
// https://patchwork.linuxtv.org/project/linux-media/patch/20210810220552.298140-2-daniel.almeida@collabora.com/
#ifndef V4L2_AV1_RESTORATION_TILESIZE_MAX
#define V4L2_AV1_RESTORATION_TILESIZE_MAX 256
#endif

namespace media {
namespace v4l2_test {

constexpr int8_t kAv1NumRefFrames = libgav1::kNumReferenceFrameTypes;

// A Av1Decoder decodes AV1-encoded IVF streams using v4l2 ioctl calls.
class Av1Decoder : public VideoDecoder {
 public:
  Av1Decoder(const Av1Decoder&) = delete;
  Av1Decoder& operator=(const Av1Decoder&) = delete;
  ~Av1Decoder() override;

  // Creates a Av1Decoder after verifying that the underlying implementation
  // supports AV1 stateless decoding.
  static std::unique_ptr<Av1Decoder> Create(
      const base::MemoryMappedFile& stream);

  // TODO(stevecho): implement DecodeNextFrame() function
  // Parses next frame from IVF stream and decodes the frame. This method will
  // place the Y, U, and V values into the respective vectors and update the
  // size with the display area size of the decoded frame.
  VideoDecoder::Result DecodeNextFrame(std::vector<char>& y_plane,
                                       std::vector<char>& u_plane,
                                       std::vector<char>& v_plane,
                                       gfx::Size& size,
                                       const int frame_number) override;

 private:
  enum class ParsingResult {
    kFailed,
    kOk,
    kEOStream,
  };

  Av1Decoder(std::unique_ptr<IvfParser> ivf_parser,
             std::unique_ptr<V4L2IoctlShim> v4l2_ioctl,
             std::unique_ptr<V4L2Queue> OUTPUT_queue,
             std::unique_ptr<V4L2Queue> CAPTURE_queue);

  // Reads an OBU frame, if there is one available. If an |obu_parser_|
  // didn't exist and there is data to be read, |obu_parser_| will be
  // created. If there is an existing |current_sequence_header_|, this
  // will be passed to the ObuParser that is created. If successful
  // (indicated by returning VideoDecoder::kOk), then the fields
  // |ivf_frame_header_|, |ivf_frame_data_|, and |current_frame_| will be
  // set upon completion.
  ParsingResult ReadNextFrame(libgav1::RefCountedBufferPtr& current_frame);

  // Copies the frame data into the V4L2 buffer of OUTPUT |queue|.
  void CopyFrameData(const libgav1::ObuFrameHeader& frame_hdr,
                     std::unique_ptr<V4L2Queue>& queue);

  // Sets up per frame parameters |v4l2_frame_params| needed for AV1 decoding
  // with VIDIOC_S_EXT_CTRLS ioctl call.
  void SetupFrameParams(
      struct v4l2_ctrl_av1_frame_header* v4l2_frame_params,
      const absl::optional<libgav1::ObuSequenceHeader>& seq_header,
      const libgav1::ObuFrameHeader& frm_header);

  // Refreshes |ref_frames_| slots with the current |buffer| and refreshes
  // |state_| with |current_frame|. Returns |reusable_buffer_slots| to indicate
  // which CAPTURE buffers can be reused for VIDIOC_QBUF ioctl call.
  std::set<int> RefreshReferenceSlots(
      uint8_t refresh_frame_flags,
      libgav1::RefCountedBufferPtr current_frame,
      scoped_refptr<MmapedBuffer> buffer,
      uint32_t last_queued_buffer_index);

  // Reference frames currently in use.
  std::array<scoped_refptr<MmapedBuffer>, kAv1NumRefFrames> ref_frames_;

  // Parser for the IVF stream to decode.
  const std::unique_ptr<IvfParser> ivf_parser_;

  IvfFrameHeader ivf_frame_header_{};
  const uint8_t* ivf_frame_data_ = nullptr;

  // AV1-specific data.
  std::unique_ptr<libgav1::ObuParser> obu_parser_;
  std::unique_ptr<libgav1::BufferPool> buffer_pool_;
  std::unique_ptr<libgav1::DecoderState> state_;
  absl::optional<libgav1::ObuSequenceHeader> current_sequence_header_;
};

}  // namespace v4l2_test
}  // namespace media

#endif  // MEDIA_GPU_V4L2_TEST_AV1_DECODER_H_
