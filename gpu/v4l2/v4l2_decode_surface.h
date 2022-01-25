// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_V4L2_V4L2_DECODE_SURFACE_H_
#define MEDIA_GPU_V4L2_V4L2_DECODE_SURFACE_H_

#include <vector>

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/sequence_checker.h"
#include "media/base/video_color_space.h"
#include "media/gpu/v4l2/v4l2_device.h"
#include "ui/gfx/geometry/rect.h"

struct v4l2_ext_controls;
struct v4l2_buffer;

namespace media {

// A V4L2-specific decode surface generated by V4L2DecodeSurfaceHandler.
// It is used to store common picture metadata (e.g. visible_rect) and
// platform-specific metadata (e.g. {input,output}_record). All the methods
// should be called on the same sequence.
class V4L2DecodeSurface : public base::RefCounted<V4L2DecodeSurface> {
 public:
  // V4L2DecodeSurfaceHandler maintains a list of InputRecords, which records
  // the status and metadata of input buffers.
  // |input_buffer| and |output_buffer| are the buffers to be used as input and
  // output in this transaction.
  // |frame| is optional, and allows the caller to keep a reference to a
  // VideoFrame for as long as this decode surface exists.
  V4L2DecodeSurface(V4L2WritableBufferRef input_buffer,
                    V4L2WritableBufferRef output_buffer,
                    scoped_refptr<VideoFrame> frame);

  V4L2DecodeSurface(const V4L2DecodeSurface&) = delete;
  V4L2DecodeSurface& operator=(const V4L2DecodeSurface&) = delete;

  // Mark the surface as decoded. This will also release all surfaces used for
  // reference, as they are not needed anymore and execute the done callback,
  // if not null.
  void SetDecoded();
  void SetVisibleRect(const gfx::Rect& visible_rect);
  void SetColorSpace(const VideoColorSpace& color_space);
  // Take references to each reference surface and keep them until the
  // target surface is decoded.
  void SetReferenceSurfaces(
      std::vector<scoped_refptr<V4L2DecodeSurface>> ref_surfaces);
  // If provided via this method, |done_cb| callback will be executed after
  // decoding into this surface is finished. The callback is reset afterwards,
  // so it needs to be set again before each decode operation.
  void SetDecodeDoneCallback(base::OnceClosure done_cb);
  // Set the callback that will be called when the surface is released. This
  // method must be called one time at most.
  void SetReleaseCallback(base::OnceClosure release_cb);

  // Update the passed v4l2_ext_controls structure to add the request or
  // config store information.
  virtual void PrepareSetCtrls(struct v4l2_ext_controls* ctrls) const = 0;
  // Return the ID to use in order to reference this frame.
  virtual uint64_t GetReferenceID() const = 0;
  // Set controls, queue buffers and submit the request corresponding to this
  // surface.
  virtual bool Submit() = 0;

  bool decoded() const { return decoded_; }
  int input_record() const { return input_record_; }
  V4L2WritableBufferRef& input_buffer() {
    return input_buffer_;
  }
  int output_record() const { return output_record_; }
  V4L2WritableBufferRef& output_buffer() {
    return output_buffer_;
  }
  scoped_refptr<VideoFrame> video_frame() const { return video_frame_; }
  gfx::Rect visible_rect() const { return visible_rect_; }
  const VideoColorSpace& color_space() const { return color_space_; }

  std::string ToString() const;

 protected:
  virtual ~V4L2DecodeSurface();
  friend class base::RefCounted<V4L2DecodeSurface>;

  SEQUENCE_CHECKER(sequence_checker_);

 private:
  V4L2WritableBufferRef input_buffer_;
  V4L2WritableBufferRef output_buffer_;
  scoped_refptr<VideoFrame> video_frame_;
  // The index of the corresponding input record.
  const int input_record_;
  // The index of the corresponding output record.
  const int output_record_;
  // The visible size of the buffer.
  gfx::Rect visible_rect_;
  // The color space of the buffer.
  VideoColorSpace color_space_;

  // Indicate whether the surface is decoded or not.
  bool decoded_;
  // Callback function which is called when the instance is destroyed.
  base::OnceClosure release_cb_;
  // Callback function which is called after the surface has been decoded.
  base::OnceClosure done_cb_;

  // The decoded surfaces of the reference frames, which is kept until the
  // surface has been decoded.
  std::vector<scoped_refptr<V4L2DecodeSurface>> reference_surfaces_;
};

// An implementation of V4L2DecodeSurface that uses the config store to
// associate controls/buffers to frames.
class V4L2ConfigStoreDecodeSurface : public V4L2DecodeSurface {
 public:
  V4L2ConfigStoreDecodeSurface(V4L2WritableBufferRef input_buffer,
                               V4L2WritableBufferRef output_buffer,
                               scoped_refptr<VideoFrame> frame)
      : V4L2DecodeSurface(std::move(input_buffer),
                          std::move(output_buffer),
                          std::move(frame)),
        // config store IDs are arbitrarily defined to be buffer ID + 1
        config_store_(this->input_buffer().BufferId() + 1) {}

  V4L2ConfigStoreDecodeSurface(const V4L2ConfigStoreDecodeSurface&) = delete;
  V4L2ConfigStoreDecodeSurface& operator=(const V4L2ConfigStoreDecodeSurface&) =
      delete;

  void PrepareSetCtrls(struct v4l2_ext_controls* ctrls) const override;
  uint64_t GetReferenceID() const override;
  bool Submit() override;

 private:
  ~V4L2ConfigStoreDecodeSurface() override = default;

  // The configuration store of the input buffer.
  uint32_t config_store_;
};

// An implementation of V4L2DecodeSurface that uses requests to associate
// controls/buffers to frames
class V4L2RequestDecodeSurface : public V4L2DecodeSurface {
 public:
  V4L2RequestDecodeSurface(V4L2WritableBufferRef input_buffer,
                           V4L2WritableBufferRef output_buffer,
                           scoped_refptr<VideoFrame> frame,
                           V4L2RequestRef request_ref)
      : V4L2DecodeSurface(std::move(input_buffer),
                          std::move(output_buffer),
                          std::move(frame)),
        request_ref_(std::move(request_ref)) {}

  V4L2RequestDecodeSurface(const V4L2RequestDecodeSurface&) = delete;
  V4L2RequestDecodeSurface& operator=(const V4L2RequestDecodeSurface&) = delete;

  void PrepareSetCtrls(struct v4l2_ext_controls* ctrls) const override;
  uint64_t GetReferenceID() const override;
  bool Submit() override;

 private:
  ~V4L2RequestDecodeSurface() override = default;

  // Request reference used for the surface.
  V4L2RequestRef request_ref_;
};

}  // namespace media

#endif  // MEDIA_GPU_V4L2_V4L2_DECODE_SURFACE_H_
