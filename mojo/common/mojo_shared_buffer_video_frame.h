// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MOJO_COMMON_MOJO_SHARED_BUFFER_VIDEO_FRAME_H_
#define MEDIA_MOJO_COMMON_MOJO_SHARED_BUFFER_VIDEO_FRAME_H_

#include <stddef.h>
#include <stdint.h>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "media/base/media_export.h"
#include "media/base/video_frame.h"
#include "mojo/public/cpp/system/buffer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace media {

// A derived class of media::VideoFrame holding a mojo::SharedBufferHandle
// which is mapped on constructor and remains so for the lifetime of the
// object. These frames are ref-counted.
class MEDIA_EXPORT MojoSharedBufferVideoFrame : public VideoFrame {
 public:
  // Creates a new I420 frame in shared memory with provided parameters
  // (coded_size() == natural_size() == visible_rect()), or returns nullptr.
  // Buffers for the frame are allocated but not initialized. The caller must
  // not make assumptions about the actual underlying sizes, but check the
  // returned VideoFrame instead.
  static scoped_refptr<MojoSharedBufferVideoFrame> CreateDefaultI420(
      const gfx::Size& dimensions,
      base::TimeDelta timestamp);

  // Creates a MojoSharedBufferVideoFrame that uses the memory in |handle|.
  // This will take ownership of |handle|, so the caller can no longer use it.
  static scoped_refptr<MojoSharedBufferVideoFrame> Create(
      VideoPixelFormat format,
      const gfx::Size& coded_size,
      const gfx::Rect& visible_rect,
      const gfx::Size& natural_size,
      mojo::ScopedSharedBufferHandle handle,
      size_t mapped_size,
      size_t y_offset,
      size_t u_offset,
      size_t v_offset,
      int32_t y_stride,
      int32_t u_stride,
      int32_t v_stride,
      base::TimeDelta timestamp);

  // Returns the offsets relative to the start of |shared_buffer| for the
  // |plane| specified.
  size_t PlaneOffset(size_t plane) const;

 private:
  MojoSharedBufferVideoFrame(VideoPixelFormat format,
                             const gfx::Size& coded_size,
                             const gfx::Rect& visible_rect,
                             const gfx::Size& natural_size,
                             mojo::ScopedSharedBufferHandle handle,
                             size_t mapped_size,
                             base::TimeDelta timestamp);
  ~MojoSharedBufferVideoFrame() override;

  // Initializes the MojoSharedBufferVideoFrame by creating a mapping onto
  // the shared memory, and then setting the strides and offsets as specified.
  bool Init(int32_t y_stride,
            int32_t u_stride,
            int32_t v_stride,
            size_t y_offset,
            size_t u_offset,
            size_t v_offset);

  mojo::ScopedSharedBufferHandle shared_buffer_handle_;
  size_t shared_buffer_size_;
  uint8_t* shared_buffer_data_;
  size_t offsets_[kMaxPlanes];

  DISALLOW_COPY_AND_ASSIGN(MojoSharedBufferVideoFrame);
};

}  // namespace media

#endif  // MEDIA_MOJO_COMMON_MOJO_SHARED_BUFFER_VIDEO_FRAME_H_
