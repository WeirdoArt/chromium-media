// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/shared_memory_buffer_tracker.h"

#include "base/memory/ptr_util.h"
#include "mojo/public/cpp/system/platform_handle.h"

namespace media {

SharedMemoryBufferTracker::SharedMemoryBufferTracker()
    : VideoCaptureBufferTracker() {}

bool SharedMemoryBufferTracker::Init(const gfx::Size& dimensions,
                                     VideoPixelFormat format,
                                     VideoPixelStorage storage_type,
                                     base::Lock* lock) {
  DVLOG(2) << __func__ << "allocating ShMem of " << dimensions.ToString();
  set_dimensions(dimensions);
  // |dimensions| can be 0x0 for trackers that do not require memory backing.
  set_max_pixel_count(dimensions.GetArea());
  set_pixel_format(format);
  set_storage_type(storage_type);
  mapped_size_ = VideoCaptureFormat(dimensions, 0.0f, format, storage_type)
                     .ImageAllocationSize();
  if (!mapped_size_)
    return true;
  return shared_memory_.CreateAndMapAnonymous(mapped_size_);
}

std::unique_ptr<VideoCaptureBufferHandle>
SharedMemoryBufferTracker::GetBufferHandle() {
  return base::MakeUnique<SharedMemoryBufferHandle>(this);
}

mojo::ScopedSharedBufferHandle
SharedMemoryBufferTracker::GetHandleForTransit() {
  return mojo::WrapSharedMemoryHandle(
      base::SharedMemory::DuplicateHandle(shared_memory_.handle()),
      mapped_size_, false /* read_only */);
}

SharedMemoryBufferHandle::SharedMemoryBufferHandle(
    SharedMemoryBufferTracker* tracker)
    : tracker_(tracker) {}

SharedMemoryBufferHandle::~SharedMemoryBufferHandle() = default;

gfx::Size SharedMemoryBufferHandle::dimensions() const {
  return tracker_->dimensions();
}

size_t SharedMemoryBufferHandle::mapped_size() const {
  return tracker_->mapped_size_;
}

void* SharedMemoryBufferHandle::data(int plane) {
  DCHECK_EQ(0, plane);
  return tracker_->shared_memory_.memory();
}

#if defined(OS_POSIX) && !defined(OS_MACOSX)
base::FileDescriptor SharedMemoryBufferHandle::AsPlatformFile() {
  return tracker_->shared_memory_.handle();
}
#endif

bool SharedMemoryBufferHandle::IsBackedByVideoFrame() const {
  return false;
}

scoped_refptr<VideoFrame> SharedMemoryBufferHandle::GetVideoFrame() {
  return scoped_refptr<VideoFrame>();
}

}  // namespace media
