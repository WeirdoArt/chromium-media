// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/image_processor.h"

#include "media/base/bind_to_current_loop.h"

namespace media {

ImageProcessor::PortConfig::PortConfig(
    const VideoFrameLayout& layout,
    const gfx::Size& visible_size,
    const std::vector<VideoFrame::StorageType>& preferred_storage_types)
    : layout(layout),
      visible_size(visible_size),
      preferred_storage_types(preferred_storage_types) {}

ImageProcessor::PortConfig::~PortConfig() {}

ImageProcessor::ImageProcessor(const VideoFrameLayout& input_layout,
                               VideoFrame::StorageType input_storage_type,
                               const VideoFrameLayout& output_layout,
                               VideoFrame::StorageType output_storage_type,
                               OutputMode output_mode)
    : input_layout_(input_layout),
      input_storage_type_(input_storage_type),
      output_layout_(output_layout),
      output_storage_type_(output_storage_type),
      output_mode_(output_mode) {}

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
bool ImageProcessor::Process(scoped_refptr<VideoFrame> frame,
                             int output_buffer_index,
                             std::vector<base::ScopedFD> output_dmabuf_fds,
                             FrameReadyCB cb) {
  return ProcessInternal(std::move(frame), output_buffer_index,
                         std::move(output_dmabuf_fds),
                         BindToCurrentLoop(std::move(cb)));
}
#endif

bool ImageProcessor::Process(scoped_refptr<VideoFrame> input_frame,
                             scoped_refptr<VideoFrame> output_frame,
                             FrameReadyCB cb) {
  return ProcessInternal(std::move(input_frame), std::move(output_frame),
                         BindToCurrentLoop(std::move(cb)));
}

}  // namespace media
