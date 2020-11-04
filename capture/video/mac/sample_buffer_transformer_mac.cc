// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/mac/sample_buffer_transformer_mac.h"

#include <utility>

#include "base/logging.h"
#include "base/notreached.h"
#include "media/base/video_frame.h"
#include "media/base/video_types.h"
#include "third_party/libyuv/include/libyuv.h"
#include "third_party/libyuv/include/libyuv/scale.h"

namespace media {

namespace {

// NV12 a.k.a. 420v
constexpr OSType kPixelFormatNv12 =
    kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
// I420 a.k.a. y420
constexpr OSType kPixelFormatI420 = kCVPixelFormatType_420YpCbCr8Planar;
// MJPEG a.k.a. dmb1
constexpr OSType kPixelFormatMjpeg = kCMVideoCodecType_JPEG_OpenDML;

// Pixel formats mappings to allow these pixel formats to be converted to I420
// with libyuv.
libyuv::FourCC MacFourCCToLibyuvFourCC(OSType mac_fourcc) {
  switch (mac_fourcc) {
    // NV12 a.k.a. 420v
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
      return libyuv::FOURCC_NV12;
    // UYVY a.k.a. 2vuy
    case kCVPixelFormatType_422YpCbCr8:
      return libyuv::FOURCC_UYVY;
    // YUY2 a.k.a. yuvs
    case kCMPixelFormat_422YpCbCr8_yuvs:
      return libyuv::FOURCC_YUY2;
    // MJPEG a.k.a. dmb1
    case kCMVideoCodecType_JPEG_OpenDML:
      return libyuv::FOURCC_MJPG;
    // I420 a.k.a. y420
    case kCVPixelFormatType_420YpCbCr8Planar:
      return libyuv::FOURCC_I420;
    default:
      NOTREACHED();
  }
  return libyuv::FOURCC_ANY;
}

std::pair<uint8_t*, size_t> GetPixelBufferBaseAddressAndSize(
    CVPixelBufferRef pixel_buffer) {
  uint8_t* data_base_address;
  if (!CVPixelBufferIsPlanar(pixel_buffer)) {
    data_base_address =
        static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixel_buffer));
  } else {
    data_base_address = static_cast<uint8_t*>(
        CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 0));
  }
  size_t data_size = CVPixelBufferGetDataSize(pixel_buffer);
  DCHECK(data_base_address);
  DCHECK(data_size);
  return std::make_pair(data_base_address, data_size);
}

std::pair<uint8_t*, size_t> GetSampleBufferBaseAddressAndSize(
    CMSampleBufferRef sample_buffer) {
  // Access source sample buffer bytes.
  CMBlockBufferRef block_buffer = CMSampleBufferGetDataBuffer(sample_buffer);
  DCHECK(block_buffer);
  char* data_base_address;
  size_t data_size;
  size_t length_at_offset;
  OSStatus status = CMBlockBufferGetDataPointer(
      block_buffer, 0, &length_at_offset, &data_size, &data_base_address);
  DCHECK_EQ(status, noErr);
  DCHECK(data_base_address);
  DCHECK(data_size);
  DCHECK_EQ(length_at_offset, data_size);  // Buffer must be contiguous.
  return std::make_pair(reinterpret_cast<uint8_t*>(data_base_address),
                        data_size);
}

struct I420Planes {
  size_t width;
  size_t height;
  uint8_t* y_plane_data;
  uint8_t* u_plane_data;
  uint8_t* v_plane_data;
  size_t y_plane_stride;
  size_t u_plane_stride;
  size_t v_plane_stride;
};

size_t GetContiguousI420BufferSize(size_t width, size_t height) {
  gfx::Size dimensions(width, height);
  return VideoFrame::PlaneSize(PIXEL_FORMAT_I420, VideoFrame::kYPlane,
                               dimensions)
             .GetArea() +
         VideoFrame::PlaneSize(PIXEL_FORMAT_I420, VideoFrame::kUPlane,
                               dimensions)
             .GetArea() +
         VideoFrame::PlaneSize(PIXEL_FORMAT_I420, VideoFrame::kVPlane,
                               dimensions)
             .GetArea();
}

I420Planes GetI420PlanesFromContiguousBuffer(uint8_t* data_base_address,
                                             size_t width,
                                             size_t height) {
  gfx::Size dimensions(width, height);
  gfx::Size y_plane_size =
      VideoFrame::PlaneSize(PIXEL_FORMAT_I420, VideoFrame::kYPlane, dimensions);
  gfx::Size u_plane_size =
      VideoFrame::PlaneSize(PIXEL_FORMAT_I420, VideoFrame::kUPlane, dimensions);
  gfx::Size v_plane_size =
      VideoFrame::PlaneSize(PIXEL_FORMAT_I420, VideoFrame::kUPlane, dimensions);
  I420Planes i420_planes;
  i420_planes.width = width;
  i420_planes.height = height;
  i420_planes.y_plane_data = data_base_address;
  i420_planes.u_plane_data = i420_planes.y_plane_data + y_plane_size.GetArea();
  i420_planes.v_plane_data = i420_planes.u_plane_data + u_plane_size.GetArea();
  i420_planes.y_plane_stride = y_plane_size.width();
  i420_planes.u_plane_stride = u_plane_size.width();
  i420_planes.v_plane_stride = v_plane_size.width();
  return i420_planes;
}

I420Planes EnsureI420BufferSizeAndGetPlanes(size_t width,
                                            size_t height,
                                            std::vector<uint8_t>* i420_buffer) {
  size_t required_size = GetContiguousI420BufferSize(width, height);
  if (i420_buffer->size() < required_size)
    i420_buffer->resize(required_size);
  return GetI420PlanesFromContiguousBuffer(&(*i420_buffer)[0], width, height);
}

I420Planes GetI420PlanesFromPixelBuffer(CVPixelBufferRef pixel_buffer) {
  DCHECK_EQ(CVPixelBufferGetPlaneCount(pixel_buffer), 3u);
  I420Planes i420_planes;
  i420_planes.width = CVPixelBufferGetWidth(pixel_buffer);
  i420_planes.height = CVPixelBufferGetHeight(pixel_buffer);
  i420_planes.y_plane_data = static_cast<uint8_t*>(
      CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 0));
  i420_planes.u_plane_data = static_cast<uint8_t*>(
      CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 1));
  i420_planes.v_plane_data = static_cast<uint8_t*>(
      CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 2));
  i420_planes.y_plane_stride =
      CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0);
  i420_planes.u_plane_stride =
      CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 1);
  i420_planes.v_plane_stride =
      CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 2);
  return i420_planes;
}

struct NV12Planes {
  size_t width;
  size_t height;
  uint8_t* y_plane_data;
  uint8_t* uv_plane_data;
  size_t y_plane_stride;
  size_t uv_plane_stride;
};

size_t GetContiguousNV12BufferSize(size_t width, size_t height) {
  gfx::Size dimensions(width, height);
  return VideoFrame::PlaneSize(PIXEL_FORMAT_NV12, VideoFrame::kYPlane,
                               dimensions)
             .GetArea() +
         VideoFrame::PlaneSize(PIXEL_FORMAT_NV12, VideoFrame::kUVPlane,
                               dimensions)
             .GetArea();
}

NV12Planes GetNV12PlanesFromContiguousBuffer(uint8_t* data_base_address,
                                             size_t width,
                                             size_t height) {
  gfx::Size dimensions(width, height);
  gfx::Size y_plane_size =
      VideoFrame::PlaneSize(PIXEL_FORMAT_NV12, VideoFrame::kYPlane, dimensions);
  gfx::Size uv_plane_size = VideoFrame::PlaneSize(
      PIXEL_FORMAT_NV12, VideoFrame::kUVPlane, dimensions);
  NV12Planes nv12_planes;
  nv12_planes.width = width;
  nv12_planes.height = height;
  nv12_planes.y_plane_data = data_base_address;
  nv12_planes.uv_plane_data = nv12_planes.y_plane_data + y_plane_size.GetArea();
  nv12_planes.y_plane_stride = y_plane_size.width();
  nv12_planes.uv_plane_stride = uv_plane_size.width();
  return nv12_planes;
}

NV12Planes EnsureNV12BufferSizeAndGetPlanes(size_t width,
                                            size_t height,
                                            std::vector<uint8_t>* nv12_buffer) {
  size_t required_size = GetContiguousNV12BufferSize(width, height);
  if (nv12_buffer->size() < required_size)
    nv12_buffer->resize(required_size);
  return GetNV12PlanesFromContiguousBuffer(&(*nv12_buffer)[0], width, height);
}

NV12Planes GetNV12PlanesFromPixelBuffer(CVPixelBufferRef pixel_buffer) {
  DCHECK_EQ(CVPixelBufferGetPlaneCount(pixel_buffer), 2u);
  NV12Planes nv12_planes;
  nv12_planes.width = CVPixelBufferGetWidth(pixel_buffer);
  nv12_planes.height = CVPixelBufferGetHeight(pixel_buffer);
  nv12_planes.y_plane_data = static_cast<uint8_t*>(
      CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 0));
  nv12_planes.uv_plane_data = static_cast<uint8_t*>(
      CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 1));
  nv12_planes.y_plane_stride =
      CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0);
  nv12_planes.uv_plane_stride =
      CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 1);
  return nv12_planes;
}

void ConvertFromAnyToI420(OSType source_pixel_format,
                          uint8_t* source_buffer_base_address,
                          size_t source_buffer_size,
                          const I420Planes& destination) {
  int result = libyuv::ConvertToI420(
      source_buffer_base_address, source_buffer_size, destination.y_plane_data,
      destination.y_plane_stride, destination.u_plane_data,
      destination.u_plane_stride, destination.v_plane_data,
      destination.v_plane_stride,
      /*crop_x*/ 0,
      /*crop_y*/ 0, destination.width, destination.height,
      /*crop_width*/ destination.width,
      /*crop_height*/ destination.height, libyuv::kRotate0,
      MacFourCCToLibyuvFourCC(source_pixel_format));
  DCHECK_EQ(result, 0);
}

void ConvertFromI420ToNV12(const I420Planes& source,
                           const NV12Planes& destination) {
  DCHECK_EQ(source.width, destination.width);
  DCHECK_EQ(source.height, destination.height);
  int result = libyuv::I420ToNV12(
      source.y_plane_data, source.y_plane_stride, source.u_plane_data,
      source.u_plane_stride, source.v_plane_data, source.v_plane_stride,
      destination.y_plane_data, destination.y_plane_stride,
      destination.uv_plane_data, destination.uv_plane_stride, source.width,
      source.height);
  DCHECK_EQ(result, 0);
}

void ConvertFromMjpegToNV12(uint8_t* source_buffer_data_base_address,
                            size_t source_buffer_data_size,
                            const NV12Planes& destination) {
  // Despite libyuv::MJPGToNV12() taking both source and destination sizes as
  // arguments, this function is only successful if the sizes match. So here we
  // require the destination buffer's size to match the source's.
  int result = libyuv::MJPGToNV12(
      source_buffer_data_base_address, source_buffer_data_size,
      destination.y_plane_data, destination.y_plane_stride,
      destination.uv_plane_data, destination.uv_plane_stride, destination.width,
      destination.height, destination.width, destination.height);
  DCHECK_EQ(result, 0);
}

void ScaleI420(const I420Planes& source, const I420Planes& destination) {
  int result = libyuv::I420Scale(
      source.y_plane_data, source.y_plane_stride, source.u_plane_data,
      source.u_plane_stride, source.v_plane_data, source.v_plane_stride,
      source.width, source.height, destination.y_plane_data,
      destination.y_plane_stride, destination.u_plane_data,
      destination.u_plane_stride, destination.v_plane_data,
      destination.v_plane_stride, destination.width, destination.height,
      libyuv::kFilterBilinear);
  DCHECK_EQ(result, 0);
}

void ScaleNV12(const NV12Planes& source, const NV12Planes& destination) {
  int result = libyuv::NV12Scale(
      source.y_plane_data, source.y_plane_stride, source.uv_plane_data,
      source.uv_plane_stride, source.width, source.height,
      destination.y_plane_data, destination.y_plane_stride,
      destination.uv_plane_data, destination.uv_plane_stride, destination.width,
      destination.height, libyuv::kFilterBilinear);
  DCHECK_EQ(result, 0);
}

}  // namespace

SampleBufferTransformer::SampleBufferTransformer()
    : transformer_(Transformer::kNotConfigured),
      destination_pixel_format_(0x0),
      destination_width_(0),
      destination_height_(0) {}

SampleBufferTransformer::~SampleBufferTransformer() {}

SampleBufferTransformer::Transformer SampleBufferTransformer::transformer()
    const {
  return transformer_;
}

OSType SampleBufferTransformer::destination_pixel_format() const {
  return destination_pixel_format_;
}

void SampleBufferTransformer::Reconfigure(
    Transformer transformer,
    OSType destination_pixel_format,
    size_t destination_width,
    size_t destination_height,
    base::Optional<size_t> buffer_pool_size) {
  DCHECK(transformer != Transformer::kLibyuv ||
         destination_pixel_format == kPixelFormatI420 ||
         destination_pixel_format == kPixelFormatNv12)
      << "Destination format is unsupported when running libyuv";
  transformer_ = transformer;
  destination_pixel_format_ = destination_pixel_format;
  destination_width_ = destination_width;
  destination_height_ = destination_height;
  destination_pixel_buffer_pool_ =
      PixelBufferPool::Create(destination_pixel_format_, destination_width_,
                              destination_height_, buffer_pool_size);
  if (transformer == Transformer::kPixelBufferTransfer) {
    pixel_buffer_transferer_ = std::make_unique<PixelBufferTransferer>();
  } else {
    pixel_buffer_transferer_.reset();
  }
  intermediate_i420_buffer_.resize(0);
  intermediate_nv12_buffer_.resize(0);
}

base::ScopedCFTypeRef<CVPixelBufferRef> SampleBufferTransformer::Transform(
    CMSampleBufferRef sample_buffer) {
  DCHECK(transformer_ != Transformer::kNotConfigured);
  CVPixelBufferRef source_pixel_buffer =
      CMSampleBufferGetImageBuffer(sample_buffer);
  // Fast path: If source and destination formats are identical, return the
  // source pixel buffer.
  if (source_pixel_buffer &&
      destination_width_ == CVPixelBufferGetWidth(source_pixel_buffer) &&
      destination_height_ == CVPixelBufferGetHeight(source_pixel_buffer) &&
      destination_pixel_format_ ==
          CVPixelBufferGetPixelFormatType(source_pixel_buffer)) {
    return base::ScopedCFTypeRef<CVPixelBufferRef>(source_pixel_buffer,
                                                   base::scoped_policy::RETAIN);
  }
  // Create destination buffer from pool.
  base::ScopedCFTypeRef<CVPixelBufferRef> destination_pixel_buffer =
      destination_pixel_buffer_pool_->CreateBuffer();
  if (!destination_pixel_buffer) {
    // Maximum destination buffers exceeded. Old buffers are not being released
    // (and thus not returned to the pool) in time.
    LOG(ERROR) << "Maximum destination buffers exceeded";
    return base::ScopedCFTypeRef<CVPixelBufferRef>();
  }
  if (source_pixel_buffer) {
    // Pixel buffer path. Do pixel transfer or libyuv conversion + rescale.
    TransformPixelBuffer(source_pixel_buffer, destination_pixel_buffer);
    return destination_pixel_buffer;
  }
  // Sample buffer path - it's MJPEG. Do libyuv conversion + rescale.
  TransformSampleBuffer(sample_buffer, destination_pixel_buffer);
  return destination_pixel_buffer;
}

void SampleBufferTransformer::TransformPixelBuffer(
    CVPixelBufferRef source_pixel_buffer,
    CVPixelBufferRef destination_pixel_buffer) {
  switch (transformer_) {
    case Transformer::kPixelBufferTransfer:
      return TransformPixelBufferWithPixelTransfer(source_pixel_buffer,
                                                   destination_pixel_buffer);
    case Transformer::kLibyuv:
      return TransformPixelBufferWithLibyuv(source_pixel_buffer,
                                            destination_pixel_buffer);
    case Transformer::kNotConfigured:
      NOTREACHED();
  }
}

void SampleBufferTransformer::TransformPixelBufferWithPixelTransfer(
    CVPixelBufferRef source_pixel_buffer,
    CVPixelBufferRef destination_pixel_buffer) {
  DCHECK(transformer_ == Transformer::kPixelBufferTransfer);
  DCHECK(pixel_buffer_transferer_);
  bool success = pixel_buffer_transferer_->TransferImage(
      source_pixel_buffer, destination_pixel_buffer);
  DCHECK(success);
}

void SampleBufferTransformer::TransformPixelBufferWithLibyuv(
    CVPixelBufferRef source_pixel_buffer,
    CVPixelBufferRef destination_pixel_buffer) {
  DCHECK(transformer_ == Transformer::kLibyuv);
  // Lock source and destination pixel buffers.
  CVReturn lock_status = CVPixelBufferLockBaseAddress(
      source_pixel_buffer, kCVPixelBufferLock_ReadOnly);
  DCHECK_EQ(lock_status, kCVReturnSuccess);
  lock_status = CVPixelBufferLockBaseAddress(destination_pixel_buffer, 0);
  DCHECK_EQ(lock_status, kCVReturnSuccess);

  // Perform transform with libyuv.
  switch (destination_pixel_format_) {
    case kPixelFormatI420:
      TransformPixelBufferWithLibyuvFromAnyToI420(source_pixel_buffer,
                                                  destination_pixel_buffer);
      break;
    case kPixelFormatNv12:
      TransformPixelBufferWithLibyuvFromAnyToNV12(source_pixel_buffer,
                                                  destination_pixel_buffer);
      break;
    default:
      NOTREACHED();
  }

  // Unlock source and destination pixel buffers.
  lock_status = CVPixelBufferUnlockBaseAddress(destination_pixel_buffer, 0);
  DCHECK_EQ(lock_status, kCVReturnSuccess);
  lock_status = CVPixelBufferUnlockBaseAddress(source_pixel_buffer,
                                               kCVPixelBufferLock_ReadOnly);
  DCHECK_EQ(lock_status, kCVReturnSuccess);
}

void SampleBufferTransformer::TransformPixelBufferWithLibyuvFromAnyToI420(
    CVPixelBufferRef source_pixel_buffer,
    CVPixelBufferRef destination_pixel_buffer) {
  // Get source pixel format and bytes.
  size_t source_width = CVPixelBufferGetWidth(source_pixel_buffer);
  size_t source_height = CVPixelBufferGetHeight(source_pixel_buffer);
  OSType source_pixel_format =
      CVPixelBufferGetPixelFormatType(source_pixel_buffer);
  uint8_t* source_buffer_data_base_address;
  size_t source_buffer_data_size;
  std::tie(source_buffer_data_base_address, source_buffer_data_size) =
      GetPixelBufferBaseAddressAndSize(source_pixel_buffer);

  // Rescaling has to be done in a separate step.
  const bool rescale_needed = destination_width_ != source_width ||
                              destination_height_ != source_height;

  // Step 1: Convert to I420.
  I420Planes i420_fullscale_buffer;
  if (source_pixel_format == kPixelFormatI420) {
    // We are already at I420.
    i420_fullscale_buffer = GetI420PlanesFromPixelBuffer(source_pixel_buffer);
    DCHECK(rescale_needed);
  } else {
    // Convert X -> I420.
    if (!rescale_needed) {
      i420_fullscale_buffer =
          GetI420PlanesFromPixelBuffer(destination_pixel_buffer);
    } else {
      i420_fullscale_buffer = EnsureI420BufferSizeAndGetPlanes(
          source_width, source_height, &intermediate_i420_buffer_);
    }
    ConvertFromAnyToI420(source_pixel_format, source_buffer_data_base_address,
                         source_buffer_data_size, i420_fullscale_buffer);
  }

  // Step 2: Rescale I420.
  if (rescale_needed) {
    I420Planes i420_destination_buffer =
        GetI420PlanesFromPixelBuffer(destination_pixel_buffer);
    ScaleI420(i420_fullscale_buffer, i420_destination_buffer);
  }
}

void SampleBufferTransformer::TransformPixelBufferWithLibyuvFromAnyToNV12(
    CVPixelBufferRef source_pixel_buffer,
    CVPixelBufferRef destination_pixel_buffer) {
  // Get source pixel format and bytes.
  size_t source_width = CVPixelBufferGetWidth(source_pixel_buffer);
  size_t source_height = CVPixelBufferGetHeight(source_pixel_buffer);
  OSType source_pixel_format =
      CVPixelBufferGetPixelFormatType(source_pixel_buffer);
  uint8_t* source_buffer_data_base_address;
  size_t source_buffer_data_size;
  std::tie(source_buffer_data_base_address, source_buffer_data_size) =
      GetPixelBufferBaseAddressAndSize(source_pixel_buffer);

  // Rescaling has to be done in a separate step.
  const bool rescale_needed = destination_width_ != source_width ||
                              destination_height_ != source_height;

  // Step 1: Convert to NV12.
  NV12Planes nv12_fullscale_buffer;
  if (source_pixel_format == kPixelFormatNv12) {
    // We are already at NV12.
    nv12_fullscale_buffer = GetNV12PlanesFromPixelBuffer(source_pixel_buffer);
    DCHECK(rescale_needed);
  } else {
    // Convert X -> I420 -> NV12. (We don't know how to do X -> NV12.)
    I420Planes i420_fullscale_buffer;
    if (source_pixel_format == kPixelFormatI420) {
      // We are already at I420.
      i420_fullscale_buffer = GetI420PlanesFromPixelBuffer(source_pixel_buffer);
    } else {
      // Convert X -> I420.
      i420_fullscale_buffer = EnsureI420BufferSizeAndGetPlanes(
          source_width, source_height, &intermediate_i420_buffer_);
      ConvertFromAnyToI420(source_pixel_format, source_buffer_data_base_address,
                           source_buffer_data_size, i420_fullscale_buffer);
    }
    // Convert I420 -> NV12.
    if (!rescale_needed) {
      nv12_fullscale_buffer =
          GetNV12PlanesFromPixelBuffer(destination_pixel_buffer);
    } else {
      nv12_fullscale_buffer = EnsureNV12BufferSizeAndGetPlanes(
          source_width, source_height, &intermediate_nv12_buffer_);
    }
    ConvertFromI420ToNV12(i420_fullscale_buffer, nv12_fullscale_buffer);
  }

  // Step 2: Rescale NV12.
  if (rescale_needed) {
    NV12Planes nv12_destination_buffer =
        GetNV12PlanesFromPixelBuffer(destination_pixel_buffer);
    ScaleNV12(nv12_fullscale_buffer, nv12_destination_buffer);
  }
}

void SampleBufferTransformer::TransformSampleBuffer(
    CMSampleBufferRef source_sample_buffer,
    CVPixelBufferRef destination_pixel_buffer) {
  DCHECK(transformer_ == Transformer::kLibyuv);
  // Ensure source pixel format is MJPEG and get width and height.
  CMFormatDescriptionRef source_format_description =
      CMSampleBufferGetFormatDescription(source_sample_buffer);
  FourCharCode source_pixel_format =
      CMFormatDescriptionGetMediaSubType(source_format_description);
  DCHECK(source_pixel_format == kPixelFormatMjpeg);
  CMVideoDimensions source_dimensions =
      CMVideoFormatDescriptionGetDimensions(source_format_description);

  // Access source pixel buffer bytes.
  uint8_t* source_buffer_data_base_address;
  size_t source_buffer_data_size;
  std::tie(source_buffer_data_base_address, source_buffer_data_size) =
      GetSampleBufferBaseAddressAndSize(source_sample_buffer);

  // Lock destination pixel buffer.
  CVReturn lock_status =
      CVPixelBufferLockBaseAddress(destination_pixel_buffer, 0);
  DCHECK_EQ(lock_status, kCVReturnSuccess);
  // Convert to I420 or NV12.
  switch (destination_pixel_format_) {
    case kPixelFormatI420:
      TransformSampleBufferFromMjpegToI420(
          source_buffer_data_base_address, source_buffer_data_size,
          source_dimensions.width, source_dimensions.height,
          destination_pixel_buffer);
      break;
    case kPixelFormatNv12:
      TransformSampleBufferFromMjpegToNV12(
          source_buffer_data_base_address, source_buffer_data_size,
          source_dimensions.width, source_dimensions.height,
          destination_pixel_buffer);
      break;
    default:
      NOTREACHED();
  }
  // Unlock destination pixel buffer.
  lock_status = CVPixelBufferUnlockBaseAddress(destination_pixel_buffer, 0);
  DCHECK_EQ(lock_status, kCVReturnSuccess);
}

void SampleBufferTransformer::TransformSampleBufferFromMjpegToI420(
    uint8_t* source_buffer_data_base_address,
    size_t source_buffer_data_size,
    size_t source_width,
    size_t source_height,
    CVPixelBufferRef destination_pixel_buffer) {
  DCHECK(destination_pixel_format_ == kPixelFormatI420);
  // Rescaling has to be done in a separate step.
  const bool rescale_needed = destination_width_ != source_width ||
                              destination_height_ != source_height;

  // Step 1: Convert MJPEG -> I420.
  I420Planes i420_fullscale_buffer;
  if (!rescale_needed) {
    i420_fullscale_buffer =
        GetI420PlanesFromPixelBuffer(destination_pixel_buffer);
  } else {
    i420_fullscale_buffer = EnsureI420BufferSizeAndGetPlanes(
        source_width, source_height, &intermediate_i420_buffer_);
  }
  ConvertFromAnyToI420(kPixelFormatMjpeg, source_buffer_data_base_address,
                       source_buffer_data_size, i420_fullscale_buffer);

  // Step 2: Rescale I420.
  if (rescale_needed) {
    I420Planes i420_destination_buffer =
        GetI420PlanesFromPixelBuffer(destination_pixel_buffer);
    ScaleI420(i420_fullscale_buffer, i420_destination_buffer);
  }
}

void SampleBufferTransformer::TransformSampleBufferFromMjpegToNV12(
    uint8_t* source_buffer_data_base_address,
    size_t source_buffer_data_size,
    size_t source_width,
    size_t source_height,
    CVPixelBufferRef destination_pixel_buffer) {
  DCHECK(destination_pixel_format_ == kPixelFormatNv12);
  // Rescaling has to be done in a separate step.
  const bool rescale_needed = destination_width_ != source_width ||
                              destination_height_ != source_height;

  // Step 1: Convert MJPEG -> NV12.
  NV12Planes nv12_fullscale_buffer;
  if (!rescale_needed) {
    nv12_fullscale_buffer =
        GetNV12PlanesFromPixelBuffer(destination_pixel_buffer);
  } else {
    nv12_fullscale_buffer = EnsureNV12BufferSizeAndGetPlanes(
        source_width, source_height, &intermediate_nv12_buffer_);
  }
  ConvertFromMjpegToNV12(source_buffer_data_base_address,
                         source_buffer_data_size, nv12_fullscale_buffer);

  // Step 2: Rescale NV12.
  if (rescale_needed) {
    NV12Planes nv12_destination_buffer =
        GetNV12PlanesFromPixelBuffer(destination_pixel_buffer);
    ScaleNV12(nv12_fullscale_buffer, nv12_destination_buffer);
  }
}

}  // namespace media
