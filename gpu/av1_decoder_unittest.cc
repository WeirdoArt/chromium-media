// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "media/gpu/av1_decoder.h"

#include <string.h>

#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "media/base/decoder_buffer.h"
#include "media/base/test_data_util.h"
#include "media/ffmpeg/ffmpeg_common.h"
#include "media/filters/ffmpeg_demuxer.h"
#include "media/filters/in_memory_url_protocol.h"
#include "media/filters/ivf_parser.h"
#include "media/gpu/av1_picture.h"
#include "media/media_buildflags.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/libgav1/src/src/obu_parser.h"
#include "third_party/libgav1/src/src/utils/constants.h"
#include "third_party/libgav1/src/src/utils/types.h"

#if !BUILDFLAG(USE_CHROMEOS_MEDIA_ACCELERATION)
#error "This test requires Chrome OS media acceleration"
#endif
#include "media/gpu/chromeos/fourcc.h"

using ::testing::_;
using ::testing::Return;

namespace media {
namespace {

class FakeAV1Picture : public AV1Picture {
 public:
  FakeAV1Picture() = default;

 protected:
  ~FakeAV1Picture() override = default;

 private:
  scoped_refptr<AV1Picture> CreateDuplicate() override {
    return base::MakeRefCounted<FakeAV1Picture>();
  }
};

bool IsYUV420(int8_t subsampling_x, int8_t subsampling_y, bool is_monochrome) {
  return subsampling_x == 1 && subsampling_y == 1 && !is_monochrome;
}

MATCHER_P(SameAV1PictureInstance, av1_picture, "") {
  return &arg == av1_picture.get();
}

MATCHER_P2(MatchesFrameSizeAndRenderSize, frame_size, render_size, "") {
  const auto& frame_header = arg.frame_header;
  return base::strict_cast<int>(frame_header.width) == frame_size.width() &&
         base::strict_cast<int>(frame_header.height) == frame_size.height() &&
         base::strict_cast<int>(frame_header.render_width) ==
             render_size.width() &&
         base::strict_cast<int>(frame_header.render_height) ==
             render_size.height();
}

MATCHER_P4(MatchesFrameHeader,
           frame_size,
           render_size,
           show_existing_frame,
           show_frame,
           "") {
  const auto& frame_header = arg.frame_header;
  return base::strict_cast<int>(frame_header.width) == frame_size.width() &&
         base::strict_cast<int>(frame_header.height) == frame_size.height() &&
         base::strict_cast<int>(frame_header.render_width) ==
             render_size.width() &&
         base::strict_cast<int>(frame_header.render_height) ==
             render_size.height() &&
         frame_header.show_existing_frame == show_existing_frame &&
         frame_header.show_frame == show_frame;
}

MATCHER_P4(MatchesYUV420SequenceHeader,
           profile,
           bitdepth,
           max_frame_size,
           film_grain_params_present,
           "") {
  return arg.profile == profile && arg.color_config.bitdepth == bitdepth &&
         base::strict_cast<int>(arg.max_frame_width) ==
             max_frame_size.width() &&
         base::strict_cast<int>(arg.max_frame_height) ==
             max_frame_size.height() &&
         arg.film_grain_params_present == film_grain_params_present &&
         IsYUV420(arg.color_config.subsampling_x,
                  arg.color_config.subsampling_y,
                  arg.color_config.is_monochrome);
}

MATCHER(NonEmptyTileBuffers, "") {
  return !arg.empty();
}

MATCHER_P(MatchesFrameData, decoder_buffer, "") {
  return arg.data() == decoder_buffer->data() &&
         arg.size() == decoder_buffer->data_size();
}

class MockAV1Accelerator : public AV1Decoder::AV1Accelerator {
 public:
  MockAV1Accelerator() = default;
  ~MockAV1Accelerator() override = default;

  MOCK_METHOD1(CreateAV1Picture, scoped_refptr<AV1Picture>(bool));
  MOCK_METHOD5(SubmitDecode,
               bool(const AV1Picture&,
                    const libgav1::ObuSequenceHeader&,
                    const AV1ReferenceFrameVector&,
                    const libgav1::Vector<libgav1::TileBuffer>&,
                    base::span<const uint8_t>));
  MOCK_METHOD1(OutputPicture, bool(const AV1Picture&));
};

class AV1DecoderTest : public ::testing::Test {
 public:
  using DecodeResult = AcceleratedVideoDecoder::DecodeResult;

  AV1DecoderTest() = default;
  ~AV1DecoderTest() override = default;
  void SetUp() override;
  std::vector<DecodeResult> Decode(scoped_refptr<DecoderBuffer> buffer);
  scoped_refptr<DecoderBuffer> ReadDecoderBuffer(const std::string& fname);
  std::vector<scoped_refptr<DecoderBuffer>> ReadIVF(const std::string& fname);
  std::vector<scoped_refptr<DecoderBuffer>> ReadWebm(const std::string& fname);

 protected:
  base::FilePath GetTestFilePath(const std::string& fname) {
    base::FilePath file_path(base::FilePath(base::FilePath::kCurrentDirectory)
                                 .Append(base::FilePath::StringType(fname)));
    if (base::PathExists(file_path)) {
      return file_path;
    }
    return GetTestDataFilePath(fname);
  }

  // Owned by |decoder_|.
  MockAV1Accelerator* mock_accelerator_;

  std::unique_ptr<AV1Decoder> decoder_;
  int32_t bitstream_id_ = 0;
};

void AV1DecoderTest::SetUp() {
  auto accelerator = std::make_unique<MockAV1Accelerator>();
  mock_accelerator_ = accelerator.get();
  decoder_ = std::make_unique<AV1Decoder>(std::move(accelerator),
                                          VIDEO_CODEC_PROFILE_UNKNOWN);
}

std::vector<AcceleratedVideoDecoder::DecodeResult> AV1DecoderTest::Decode(
    scoped_refptr<DecoderBuffer> buffer) {
  decoder_->SetStream(bitstream_id_++, *buffer);

  std::vector<DecodeResult> results;
  DecodeResult res;
  do {
    res = decoder_->Decode();
    results.push_back(res);
  } while (res != DecodeResult::kDecodeError &&
           res != DecodeResult::kRanOutOfStreamData);
  return results;
}  // namespace

scoped_refptr<DecoderBuffer> AV1DecoderTest::ReadDecoderBuffer(
    const std::string& fname) {
  auto input_file = GetTestFilePath(fname);
  std::string bitstream;

  EXPECT_TRUE(base::ReadFileToString(input_file, &bitstream));
  auto buffer = DecoderBuffer::CopyFrom(
      reinterpret_cast<const uint8_t*>(bitstream.data()), bitstream.size());
  EXPECT_TRUE(!!buffer);
  return buffer;
}

std::vector<scoped_refptr<DecoderBuffer>> AV1DecoderTest::ReadIVF(
    const std::string& fname) {
  std::string ivf_data;
  auto input_file = GetTestFilePath(fname);
  EXPECT_TRUE(base::ReadFileToString(input_file, &ivf_data));

  IvfParser ivf_parser;
  IvfFileHeader ivf_header{};
  EXPECT_TRUE(
      ivf_parser.Initialize(reinterpret_cast<const uint8_t*>(ivf_data.data()),
                            ivf_data.size(), &ivf_header));
  EXPECT_EQ(ivf_header.fourcc, ComposeFourcc('A', 'V', '0', '1'));

  std::vector<scoped_refptr<DecoderBuffer>> buffers;
  IvfFrameHeader ivf_frame_header{};
  const uint8_t* data;
  while (ivf_parser.ParseNextFrame(&ivf_frame_header, &data)) {
    buffers.push_back(DecoderBuffer::CopyFrom(
        reinterpret_cast<const uint8_t*>(data), ivf_frame_header.frame_size));
  }
  return buffers;
}

std::vector<scoped_refptr<DecoderBuffer>> AV1DecoderTest::ReadWebm(
    const std::string& fname) {
  std::string webm_data;
  auto input_file = GetTestFilePath(fname);
  EXPECT_TRUE(base::ReadFileToString(input_file, &webm_data));

  InMemoryUrlProtocol protocol(
      reinterpret_cast<const uint8_t*>(webm_data.data()), webm_data.size(),
      false);
  FFmpegGlue glue(&protocol);
  LOG_ASSERT(glue.OpenContext());
  int stream_index = -1;
  for (unsigned int i = 0; i < glue.format_context()->nb_streams; ++i) {
    const AVStream* stream = glue.format_context()->streams[i];
    const AVCodecParameters* codec_parameters = stream->codecpar;
    const AVMediaType codec_type = codec_parameters->codec_type;
    const AVCodecID codec_id = codec_parameters->codec_id;
    if (codec_type == AVMEDIA_TYPE_VIDEO && codec_id == AV_CODEC_ID_AV1) {
      stream_index = i;
      break;
    }
  }
  EXPECT_NE(stream_index, -1) << "No AV1 data found in " << input_file;

  std::vector<scoped_refptr<DecoderBuffer>> buffers;
  AVPacket packet{};
  while (av_read_frame(glue.format_context(), &packet) >= 0) {
    if (packet.stream_index == stream_index)
      buffers.push_back(DecoderBuffer::CopyFrom(packet.data, packet.size));
    av_packet_unref(&packet);
  }
  return buffers;
}

TEST_F(AV1DecoderTest, DecodeInvalidOBU) {
  std::string kInvalidData = "ThisIsInvalidData";
  auto kInvalidBuffer = DecoderBuffer::CopyFrom(
      reinterpret_cast<const uint8_t*>(kInvalidData.data()),
      kInvalidData.size());
  std::vector<DecodeResult> results = Decode(kInvalidBuffer);
  std::vector<DecodeResult> expected = {DecodeResult::kDecodeError};
  EXPECT_EQ(results, expected);
}

TEST_F(AV1DecoderTest, DecodeEmptyOBU) {
  auto kEmptyBuffer = base::MakeRefCounted<DecoderBuffer>(0);
  std::vector<DecodeResult> results = Decode(kEmptyBuffer);
  std::vector<DecodeResult> expected = {DecodeResult::kRanOutOfStreamData};
  EXPECT_EQ(results, expected);
}

TEST_F(AV1DecoderTest, DecodeOneIFrame) {
  constexpr gfx::Size kFrameSize(320, 240);
  constexpr gfx::Size kRenderSize(320, 240);
  constexpr auto kProfile = libgav1::BitstreamProfile::kProfile0;
  const std::string kIFrame("av1-I-frame-320x240");
  scoped_refptr<DecoderBuffer> i_frame_buffer = ReadDecoderBuffer(kIFrame);
  ASSERT_TRUE(!!i_frame_buffer);
  auto av1_picture = base::MakeRefCounted<AV1Picture>();
  ::testing::InSequence s;
  EXPECT_CALL(*mock_accelerator_, CreateAV1Picture(/*apply_grain=*/false))
      .WillOnce(Return(av1_picture));
  EXPECT_CALL(
      *mock_accelerator_,
      SubmitDecode(
          MatchesFrameHeader(kFrameSize, kRenderSize,
                             /*show_existing_frame=*/false,
                             /*show_frame=*/true),
          MatchesYUV420SequenceHeader(kProfile, /*bitdepth=*/8, kFrameSize,
                                      /*film_grain_params_present=*/false),
          _, NonEmptyTileBuffers(), MatchesFrameData(i_frame_buffer)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_accelerator_,
              OutputPicture(SameAV1PictureInstance(av1_picture)))
      .WillOnce(Return(true));
  std::vector<DecodeResult> results = Decode(i_frame_buffer);
  std::vector<DecodeResult> expected = {DecodeResult::kConfigChange,
                                        DecodeResult::kRanOutOfStreamData};
  EXPECT_EQ(results, expected);
}

TEST_F(AV1DecoderTest, DecodeSimpleStream) {
  constexpr gfx::Size kFrameSize(320, 240);
  constexpr gfx::Size kRenderSize(320, 240);
  constexpr auto kProfile = libgav1::BitstreamProfile::kProfile0;
  const std::string kSimpleStream("bear-av1.webm");
  std::vector<scoped_refptr<DecoderBuffer>> buffers = ReadWebm(kSimpleStream);
  ASSERT_FALSE(buffers.empty());
  std::vector<DecodeResult> expected = {DecodeResult::kConfigChange};
  std::vector<DecodeResult> results;
  for (auto buffer : buffers) {
    ::testing::InSequence sequence;
    auto av1_picture = base::MakeRefCounted<AV1Picture>();
    EXPECT_CALL(*mock_accelerator_, CreateAV1Picture(/*apply_grain=*/false))
        .WillOnce(Return(av1_picture));
    EXPECT_CALL(
        *mock_accelerator_,
        SubmitDecode(
            MatchesFrameHeader(kFrameSize, kRenderSize,
                               /*show_existing_frame=*/false,
                               /*show_frame=*/true),
            MatchesYUV420SequenceHeader(kProfile, /*bitdepth=*/8, kFrameSize,
                                        /*film_grain_params_present=*/false),
            _, NonEmptyTileBuffers(), MatchesFrameData(buffer)))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_accelerator_,
                OutputPicture(SameAV1PictureInstance(av1_picture)))
        .WillOnce(Return(true));
    for (DecodeResult r : Decode(buffer))
      results.push_back(r);
    expected.push_back(DecodeResult::kRanOutOfStreamData);
    testing::Mock::VerifyAndClearExpectations(mock_accelerator_);
  }
  EXPECT_EQ(results, expected);
}

TEST_F(AV1DecoderTest, DecodeShowExistingPictureStream) {
  constexpr gfx::Size kFrameSize(208, 144);
  constexpr gfx::Size kRenderSize(208, 144);
  constexpr auto kProfile = libgav1::BitstreamProfile::kProfile0;
  constexpr size_t kDecodedFrames = 10;
  constexpr size_t kOutputFrames = 10;
  const std::string kShowExistingFrameStream("av1-show_existing_frame.ivf");
  std::vector<scoped_refptr<DecoderBuffer>> buffers =
      ReadIVF(kShowExistingFrameStream);
  ASSERT_FALSE(buffers.empty());

  // TODO(hiroh): Test what's unique about the show_existing_frame path.
  std::vector<DecodeResult> expected = {DecodeResult::kConfigChange};
  std::vector<DecodeResult> results;
  EXPECT_CALL(*mock_accelerator_, CreateAV1Picture(/*apply_grain=*/false))
      .Times(kDecodedFrames)
      .WillRepeatedly(Return(base::MakeRefCounted<FakeAV1Picture>()));
  EXPECT_CALL(
      *mock_accelerator_,
      SubmitDecode(
          MatchesFrameSizeAndRenderSize(kFrameSize, kRenderSize),
          MatchesYUV420SequenceHeader(kProfile, /*bitdepth=*/8, kFrameSize,
                                      /*film_grain_params_present=*/false),
          _, NonEmptyTileBuffers(), _))
      .Times(kDecodedFrames)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_accelerator_, OutputPicture(_))
      .Times(kOutputFrames)
      .WillRepeatedly(Return(true));

  for (auto buffer : buffers) {
    for (DecodeResult r : Decode(buffer))
      results.push_back(r);
    expected.push_back(DecodeResult::kRanOutOfStreamData);
  }
  EXPECT_EQ(results, expected);
}

TEST_F(AV1DecoderTest, Decode10bitStream) {
  const std::string k10bitStream("bear-av1-320x180-10bit.webm");
  std::vector<scoped_refptr<DecoderBuffer>> buffers = ReadWebm(k10bitStream);
  ASSERT_FALSE(buffers.empty());
  std::vector<DecodeResult> expected = {DecodeResult::kDecodeError};
  EXPECT_EQ(Decode(buffers[0]), expected);
  // Once AV1Decoder gets into an error state, Decode() returns kDecodeError
  // until Reset().
  EXPECT_EQ(Decode(buffers[1]), expected);
}

TEST_F(AV1DecoderTest, DecodeSVCStream) {
  const std::string kSVCStream("av1-svc-L2T2.ivf");
  std::vector<scoped_refptr<DecoderBuffer>> buffers = ReadIVF(kSVCStream);
  ASSERT_FALSE(buffers.empty());
  std::vector<DecodeResult> expected = {DecodeResult::kDecodeError};
  EXPECT_EQ(Decode(buffers[0]), expected);
  // Once AV1Decoder gets into an error state, Decode() returns kDecodeError
  // until Reset().
  EXPECT_EQ(Decode(buffers[1]), expected);
}

// TODO(hiroh): Add more tests, non-YUV420 stream, Reset() flow, mid-stream
// configuration change, and reference frame tracking.
}  // namespace
}  // namespace media
