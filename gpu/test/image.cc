// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/test/image.h"

#include <memory>

#include "base/files/file_util.h"
#include "base/hash/md5.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "media/base/test_data_util.h"
#include "media/gpu/macros.h"

namespace media {
namespace test {

namespace {

// Resolve the specified test file path to an absolute path. The path can be
// either an absolute path, a path relative to the current directory, or a path
// relative to the test data path.
base::Optional<base::FilePath> ResolveFilePath(
    const base::FilePath& file_path) {
  base::FilePath resolved_path = file_path;

  // Try to resolve the path into an absolute path. If the path doesn't exist,
  // it might be relative to the test data dir.
  if (!resolved_path.IsAbsolute()) {
    resolved_path = base::MakeAbsoluteFilePath(
        PathExists(resolved_path)
            ? resolved_path
            : media::GetTestDataPath().Append(resolved_path));
  }

  return PathExists(resolved_path)
             ? base::Optional<base::FilePath>(resolved_path)
             : base::nullopt;
}

// Converts the |pixel_format| string into a VideoPixelFormat.
VideoPixelFormat ConvertStringtoPixelFormat(const std::string& pixel_format) {
  if (pixel_format == "BGRA") {
    return PIXEL_FORMAT_ARGB;
  } else if (pixel_format == "I420") {
    return PIXEL_FORMAT_I420;
  } else if (pixel_format == "NV12") {
    return PIXEL_FORMAT_NV12;
  } else if (pixel_format == "YV12") {
    return PIXEL_FORMAT_YV12;
  } else if (pixel_format == "RGBA") {
    return PIXEL_FORMAT_ABGR;
  } else {
    VLOG(2) << pixel_format << " is not supported.";
    return PIXEL_FORMAT_UNKNOWN;
  }
}

}  // namespace

// Suffix to append to the image file path to get the metadata file path.
constexpr const base::FilePath::CharType* kMetadataSuffix =
    FILE_PATH_LITERAL(".json");

Image::Image(const base::FilePath& file_path) : file_path_(file_path) {}

Image::~Image() {}

bool Image::Load() {
  DCHECK(!file_path_.empty());
  DCHECK(!IsLoaded());

  base::Optional<base::FilePath> resolved_path = ResolveFilePath(file_path_);
  if (!resolved_path) {
    LOG(ERROR) << "Image file not found: " << file_path_;
    return false;
  }
  file_path_ = resolved_path.value();
  DVLOGF(2) << "File path: " << file_path_;

  if (!mapped_file_.Initialize(file_path_)) {
    LOG(ERROR) << "Failed to read file: " << file_path_;
    return false;
  }

  if (!LoadMetadata()) {
    LOG(ERROR) << "Failed to load metadata";
    return false;
  }

  // Verify that the image's checksum matches the checksum in the metadata.
  base::MD5Digest digest;
  base::MD5Sum(mapped_file_.data(), mapped_file_.length(), &digest);
  if (base::MD5DigestToBase16(digest) != checksum_) {
    LOG(ERROR) << "Image checksum not matching metadata";
    return false;
  }

  return true;
}

bool Image::IsLoaded() const {
  return mapped_file_.IsValid();
}

bool Image::LoadMetadata() {
  if (IsMetadataLoaded()) {
    return true;
  }

  base::FilePath json_path = file_path_.AddExtension(kMetadataSuffix);
  base::Optional<base::FilePath> resolved_path = ResolveFilePath(json_path);
  if (!resolved_path) {
    LOG(ERROR) << "Image metadata file not found: " << json_path;
    return false;
  }
  json_path = resolved_path.value();

  if (!base::PathExists(json_path)) {
    VLOGF(1) << "Image metadata file not found: " << json_path.BaseName();
    return false;
  }

  std::string json_data;
  if (!base::ReadFileToString(json_path, &json_data)) {
    VLOGF(1) << "Failed to read image metadata file: " << json_path;
    return false;
  }

  base::JSONReader reader;
  std::unique_ptr<base::Value> metadata(
      reader.ReadToValueDeprecated(json_data));
  if (!metadata) {
    VLOGF(1) << "Failed to parse image metadata: " << json_path << ": "
             << reader.GetErrorMessage();
    return false;
  }

  // Get the pixel format from the json data.
  const base::Value* pixel_format =
      metadata->FindKeyOfType("pixel_format", base::Value::Type::STRING);
  if (!pixel_format) {
    VLOGF(1) << "Key \"pixel_format\" is not found in " << json_path;
    return false;
  }
  pixel_format_ = ConvertStringtoPixelFormat(pixel_format->GetString());
  if (pixel_format_ == PIXEL_FORMAT_UNKNOWN) {
    VLOGF(1) << pixel_format->GetString() << " is not supported";
    return false;
  }

  // Get the image dimensions from the json data.
  const base::Value* width =
      metadata->FindKeyOfType("width", base::Value::Type::INTEGER);
  if (!width) {
    VLOGF(1) << "Key \"width\" is not found in " << json_path;
    return false;
  }
  const base::Value* height =
      metadata->FindKeyOfType("height", base::Value::Type::INTEGER);
  if (!height) {
    VLOGF(1) << "Key \"height\" is not found in " << json_path;
    return false;
  }
  size_ = gfx::Size(width->GetInt(), height->GetInt());

  // Try to get the visible rectangle of the image from the json data.
  // These values are not in json data if all the image data is in the visible
  // area.
  visible_rect_ = gfx::Rect(size_);
  const base::Value* visible_rect_info =
      metadata->FindKeyOfType("visible_rect", base::Value::Type::LIST);
  if (visible_rect_info) {
    base::Value::ConstListView values = visible_rect_info->GetList();
    if (values.size() != 4) {
      VLOGF(1) << "unexpected json format for visible rectangle";
      return false;
    }
    int origin_x = values[0].GetInt();
    int origin_y = values[1].GetInt();
    int visible_width = values[2].GetInt();
    int visible_height = values[3].GetInt();
    visible_rect_ =
        gfx::Rect(origin_x, origin_y, visible_width, visible_height);
  }

  // Get the image checksum from the json data.
  const base::Value* checksum =
      metadata->FindKeyOfType("checksum", base::Value::Type::STRING);
  if (!checksum) {
    VLOGF(1) << "Key \"checksum\" is not found in " << json_path;
    return false;
  }
  checksum_ = checksum->GetString();

  return true;
}

bool Image::IsMetadataLoaded() const {
  return pixel_format_ != PIXEL_FORMAT_UNKNOWN;
}

uint8_t* Image::Data() const {
  return mapped_file_.data();
}

size_t Image::DataSize() const {
  return mapped_file_.length();
}

VideoPixelFormat Image::PixelFormat() const {
  return pixel_format_;
}

const gfx::Size& Image::Size() const {
  return size_;
}

const gfx::Rect& Image::VisibleRect() const {
  return visible_rect_;
}

const char* Image::Checksum() const {
  return checksum_.data();
}

}  // namespace test
}  // namespace media
