// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_STATUS_H_
#define MEDIA_BASE_STATUS_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/location.h"
#include "base/strings/string_piece.h"
#include "base/values.h"
#include "media/base/media_export.h"
#include "media/base/media_serializers_base.h"
#include "media/base/status_codes.h"

// Mojo namespaces for serialization friend declarations.
namespace mojo {
template <typename T, typename U>
struct StructTraits;
}  // namespace mojo

namespace media {

namespace mojom {
class StatusDataView;
}

// Status is meant to be a relatively small (sizeof(void*) bytes) object
// that can be returned as a status value from functions or passed to callbacks
// that want a report of status. Status allows attaching of arbitrary named
// data, other Status' as causes, and stack frames, which can all be logged
// and reported throughout the media stack. The status code and message are
// immutable and can be used to give a stable numeric ID for any error
// generated by media code.
// There is also an OK state which can't hold any data and is only for
// successful returns.
class MEDIA_EXPORT Status {
 public:
  // Default constructor can be used for OkStatus();
  Status();

  // Constructor to create a new Status from a numeric code & message.
  // These are immutable; if you'd like to change them, then you likely should
  // create a new Status. {} or OkStatus() should be used to create a
  // success status.
  // NOTE: This should never be given a location parameter when called - It is
  // defaulted in order to grab the caller location.
  Status(StatusCode code,
         base::StringPiece message = "",
         const base::Location& location = base::Location::Current());

  // Copy Constructor & assignment. (Mojo uses both of these)
  Status(const Status&);
  Status& operator=(const Status&);

  // Allows move.
  Status(Status&&);
  Status& operator=(Status&&);

  // Needs an out of line destructor...
  ~Status();

  bool is_ok() const { return !data_; }

  // Getters for internal fields
  const std::string& message() const {
    DCHECK(data_);
    return data_->message;
  }

  StatusCode code() const { return data_ ? data_->code : StatusCode::kOk; }

  // Adds the current location to Status as it’s passed upwards.
  // This does not need to be called at every location that touches it, but
  // should be called for those locations where the path is ambiguous or
  // critical. This can be especially helpful across IPC boundaries. This will
  // fail on an OK status.
  // NOTE: This should never be given a parameter when called - It is defaulted
  // in order to grab the caller location.
  Status&& AddHere(
      const base::Location& location = base::Location::Current()) &&;

  // Add |cause| as the error that triggered this one.  For example,
  // DecoderStream might return kDecoderSelectionFailed with one or more causes
  // that are the specific errors from the decoders that it tried.
  Status&& AddCause(Status&& cause) &&;
  void AddCause(Status&& cause) &;

  // Allows us to append any datatype which can be converted to
  // an int/bool/string/base::Value. Any existing data associated with |key|
  // will be overwritten by |value|. This will fail on an OK status.
  template <typename T>
  Status&& WithData(const char* key, const T& value) && {
    DCHECK(data_);
    data_->data.SetKey(key, MediaSerialize(value));
    return std::move(*this);
  }

  template <typename T>
  void WithData(const char* key, const T& value) & {
    DCHECK(data_);
    data_->data.SetKey(key, MediaSerialize(value));
  }

 private:
  // Private helper to add the current stack frame to the error trace.
  void AddFrame(const base::Location& location);

  // Keep the internal data in a unique ptr to minimize size of OK errors.
  struct MEDIA_EXPORT StatusInternal {
    StatusInternal(StatusCode code, std::string message);
    ~StatusInternal();

    // The current error code
    StatusCode code = StatusCode::kOk;

    // The current error message (Can be used for
    // https://developer.mozilla.org/en-US/docs/Web/API/Status)
    std::string message;

    // Stack frames
    std::vector<base::Value> frames;

    // Causes
    std::vector<Status> causes;

    // Data attached to the error
    base::Value data;
  };

  // Allow self-serialization
  friend struct internal::MediaSerializer<Status>;

  // Allow mojo-serialization
  friend struct mojo::StructTraits<media::mojom::StatusDataView, Status>;

  // A null internals is an implicit OK.
  std::unique_ptr<StatusInternal> data_;
};

// Convenience function to return |kOk|.
// OK won't have a message, trace, or data associated with them, and DCHECK
// if they are added.
MEDIA_EXPORT Status OkStatus();

// Helper class to allow returning a |T| or a Status.  Typical usage:
//
// ErrorOr<std::unique_ptr<MyObject>> FactoryFn() {
//   if (success)
//     return std::make_unique<MyObject>();
//   return Status(StatusCodes::kSomethingBadHappened);
// }
//
// auto result = FactoryFn();
// if (result.has_error())  return std::move(result.error());
// my_object_ = std::move(result.value());
//
// Also useful if one would like to get an enum class return value, unless an
// error occurs:
//
// enum class ResultType { kNeedMoreInput, kOutputIsReady, kFormatChanged };
//
// ErrorOr<ResultType> Foo() { ... }
//
// auto result = Foo();
// if (result.has_error()) return std::move(result.error());
// switch (result.value()) {
//  case ResultType::kNeedMoreInput:
//   ...
// }
template <typename T>
class ErrorOr {
 public:
  // All of these may be implicit, so that one may just return Status or
  // the value in question.
  ErrorOr(Status&& error) : error_(std::move(error)) {}
  ErrorOr(const Status& error) : error_(error) {}
  ErrorOr(T&& value) : value_(std::move(value)) {}
  ErrorOr(const T& value) : value_(value) {}

  ~ErrorOr() = default;

  // Move- and copy- construction and assignment are okay.
  ErrorOr(const ErrorOr&) = default;
  ErrorOr(ErrorOr&&) = default;
  ErrorOr& operator=(ErrorOr&) = default;
  ErrorOr& operator=(ErrorOr&&) = default;

  // Do we have a value?
  bool has_value() const { return value_.has_value(); }

  // Since we often test for errors, provide this too.
  bool has_error() const { return !has_value(); }

  // Return the error, if we have one.  Up to the caller to make sure that we
  // have one via |!has_value()|.
  Status& error() { return *error_; }

  // Return a ref to the value.  It's up to the caller to verify that we have a
  // value before calling this.
  T& value() { return *value_; }

 private:
  base::Optional<Status> error_;
  base::Optional<T> value_;
};

}  // namespace media

#endif  // MEDIA_BASE_STATUS_H_
