// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/test/video_encoder/bitstream_file_writer.h"

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "media/gpu/test/video_test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {
namespace test {

// A helper class to write each frame to disk.
class BitstreamFileWriter::FrameFileWriter {
 public:
  FrameFileWriter(std::unique_ptr<IvfWriter> ivf_writer)
      : ivf_writer_(std::move(ivf_writer)) {}
  FrameFileWriter(base::File output_file)
      : output_file_(std::move(output_file)) {}

  bool WriteFrame(uint32_t data_size, uint64_t timestamp, const uint8_t* data) {
    if (ivf_writer_) {
      return ivf_writer_->WriteFrame(data_size, timestamp, data);
    }
    // For H.264.
    LOG_ASSERT(output_file_.IsValid());
    return output_file_.WriteAtCurrentPos(reinterpret_cast<const char*>(data),
                                          data_size) ==
           static_cast<int>(data_size);
  }

 private:
  const std::unique_ptr<IvfWriter> ivf_writer_;
  base::File output_file_;
};

BitstreamFileWriter::BitstreamFileWriter(
    std::unique_ptr<FrameFileWriter> frame_file_writer,
    absl::optional<size_t> vp9_spatial_layer_index_to_write,
    absl::optional<size_t> num_vp9_temporal_layers_to_write)
    : frame_file_writer_(std::move(frame_file_writer)),
      vp9_spatial_layer_index_to_write_(vp9_spatial_layer_index_to_write),
      num_vp9_temporal_layers_to_write_(num_vp9_temporal_layers_to_write),
      num_buffers_writing_(0),
      num_errors_(0),
      writer_thread_("BitstreamFileWriterThread"),
      writer_cv_(&writer_lock_) {}

BitstreamFileWriter::~BitstreamFileWriter() {
  base::AutoLock auto_lock(writer_lock_);
  DCHECK_EQ(0u, num_buffers_writing_);

  writer_thread_.Stop();
}

// static
std::unique_ptr<BitstreamFileWriter> BitstreamFileWriter::Create(
    const base::FilePath& output_filepath,
    VideoCodec codec,
    const gfx::Size& resolution,
    uint32_t frame_rate,
    uint32_t num_frames,
    absl::optional<size_t> vp9_spatial_layer_index_to_write,
    absl::optional<size_t> num_vp9_temporal_layers_to_write) {
  std::unique_ptr<FrameFileWriter> frame_file_writer;
  if (!base::DirectoryExists(output_filepath.DirName()))
    base::CreateDirectory(output_filepath.DirName());

  if (codec == kCodecH264) {
    base::File output_file(output_filepath, base::File::FLAG_CREATE_ALWAYS |
                                                base::File::FLAG_WRITE);
    LOG_ASSERT(output_file.IsValid());
    frame_file_writer =
        std::make_unique<FrameFileWriter>(std::move(output_file));
  } else {
    auto ivf_writer = std::make_unique<IvfWriter>(output_filepath);
    if (!ivf_writer->WriteFileHeader(codec, resolution, frame_rate,
                                     num_frames)) {
      LOG(ERROR) << "Failed writing ivf header";
      return nullptr;
    }

    frame_file_writer =
        std::make_unique<FrameFileWriter>(std::move(ivf_writer));
  }

  auto bitstream_file_writer = base::WrapUnique(new BitstreamFileWriter(
      std::move(frame_file_writer), vp9_spatial_layer_index_to_write,
      num_vp9_temporal_layers_to_write));
  if (!bitstream_file_writer->writer_thread_.Start()) {
    LOG(ERROR) << "Failed to start file writer thread";
    return nullptr;
  }

  return bitstream_file_writer;
}

void BitstreamFileWriter::ProcessBitstream(
    scoped_refptr<BitstreamRef> bitstream,
    size_t frame_index) {
  if (vp9_spatial_layer_index_to_write_ &&
      (bitstream->metadata.vp9->spatial_idx >
           *vp9_spatial_layer_index_to_write_ ||
       (bitstream->metadata.vp9->spatial_idx <
            *vp9_spatial_layer_index_to_write_ &&
        !bitstream->metadata.vp9->referenced_by_upper_spatial_layers))) {
    // Skip |bitstream| because it contains a frame not needed by desired
    // spatial layers.
    return;
  }
  if (num_vp9_temporal_layers_to_write_ &&
      bitstream->metadata.vp9->temporal_idx >=
          *num_vp9_temporal_layers_to_write_) {
    // Skip |bitstream| because it contains a frame in upper layers than layers
    // to be saved.
    return;
  }

  base::AutoLock auto_lock(writer_lock_);
  num_buffers_writing_++;
  writer_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&BitstreamFileWriter::WriteBitstreamTask,
                                base::Unretained(this), std::move(bitstream),
                                frame_index));
}

void BitstreamFileWriter::WriteBitstreamTask(
    scoped_refptr<BitstreamRef> bitstream,
    size_t frame_index) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(writer_thread_sequence_checker_);
  const DecoderBuffer& buffer = *bitstream->buffer.get();
  bool success = frame_file_writer_->WriteFrame(
      static_cast<uint32_t>(buffer.data_size()),
      static_cast<uint64_t>(frame_index), buffer.data());

  base::AutoLock auto_lock(writer_lock_);
  num_errors_ += !success;
  num_buffers_writing_--;
  writer_cv_.Signal();
}

bool BitstreamFileWriter::WaitUntilDone() {
  base::AutoLock auto_lock(writer_lock_);
  while (num_buffers_writing_ > 0u)
    writer_cv_.Wait();
  LOG_IF(ERROR, num_errors_ > 0u) << "Detected error count: " << num_errors_;
  return num_errors_ == 0;
}
}  // namespace test
}  // namespace media
