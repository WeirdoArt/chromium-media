// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_TEST_VIDEO_PLAYER_FRAME_RENDERER_H_
#define MEDIA_GPU_TEST_VIDEO_PLAYER_FRAME_RENDERER_H_

#include <vector>

#include "base/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "media/base/video_types.h"
#include "media/video/picture.h"
#include "ui/gfx/geometry/size.h"

namespace gl {

class GLContext;

}  // namespace gl

namespace media {

class VideoFrame;

namespace test {

// The frame renderer interface can be used to render decoded frames to screen,
// file,... It is responsible for creating picture buffers and maintaining a GL
// context.
class FrameRenderer {
 public:
  virtual ~FrameRenderer() = default;
  // Acquire the GL context for the current thread. This is needed if the
  // context is shared between multiple threads.
  virtual void AcquireGLContext() = 0;
  // Release the GL context on the current thread.
  virtual void ReleaseGLContext() = 0;
  // Get the current GL context.
  virtual gl::GLContext* GetGLContext() = 0;

  // Render the specified video frame. Once rendering is done the reference to
  // the |video_frame| should be dropped so the video frame can be reused.
  virtual void RenderFrame(scoped_refptr<VideoFrame> video_frame) = 0;
};

}  // namespace test
}  // namespace media

#endif  // MEDIA_GPU_TEST_VIDEO_PLAYER_FRAME_RENDERER_H_
