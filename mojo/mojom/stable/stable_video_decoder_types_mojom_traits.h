// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MOJO_MOJOM_STABLE_STABLE_VIDEO_DECODER_TYPES_MOJOM_TRAITS_H_
#define MEDIA_MOJO_MOJOM_STABLE_STABLE_VIDEO_DECODER_TYPES_MOJOM_TRAITS_H_

#include "base/notreached.h"
#include "media/base/video_frame.h"
#include "media/base/video_frame_metadata.h"
#include "media/mojo/mojom/stable/stable_video_decoder_types.mojom.h"
#include "mojo/public/cpp/bindings/optional_as_pointer.h"

namespace mojo {

template <>
struct EnumTraits<media::stable::mojom::ColorSpacePrimaryID,
                  gfx::ColorSpace::PrimaryID> {
  static media::stable::mojom::ColorSpacePrimaryID ToMojom(
      gfx::ColorSpace::PrimaryID input) {
    switch (input) {
      case gfx::ColorSpace::PrimaryID::INVALID:
        return media::stable::mojom::ColorSpacePrimaryID::kInvalid;
      case gfx::ColorSpace::PrimaryID::BT709:
        return media::stable::mojom::ColorSpacePrimaryID::kBT709;
      case gfx::ColorSpace::PrimaryID::BT470M:
        return media::stable::mojom::ColorSpacePrimaryID::kBT470M;
      case gfx::ColorSpace::PrimaryID::BT470BG:
        return media::stable::mojom::ColorSpacePrimaryID::kBT470BG;
      case gfx::ColorSpace::PrimaryID::SMPTE170M:
        return media::stable::mojom::ColorSpacePrimaryID::kSMPTE170M;
      case gfx::ColorSpace::PrimaryID::SMPTE240M:
        return media::stable::mojom::ColorSpacePrimaryID::kSMPTE240M;
      case gfx::ColorSpace::PrimaryID::FILM:
        return media::stable::mojom::ColorSpacePrimaryID::kFilm;
      case gfx::ColorSpace::PrimaryID::BT2020:
        return media::stable::mojom::ColorSpacePrimaryID::kBT2020;
      case gfx::ColorSpace::PrimaryID::SMPTEST428_1:
        return media::stable::mojom::ColorSpacePrimaryID::kSMPTEST428_1;
      case gfx::ColorSpace::PrimaryID::SMPTEST431_2:
        return media::stable::mojom::ColorSpacePrimaryID::kSMPTEST431_2;
      case gfx::ColorSpace::PrimaryID::P3:
        return media::stable::mojom::ColorSpacePrimaryID::kSMPTEST432_1;
      case gfx::ColorSpace::PrimaryID::XYZ_D50:
        return media::stable::mojom::ColorSpacePrimaryID::kXYZ_D50;
      case gfx::ColorSpace::PrimaryID::ADOBE_RGB:
        return media::stable::mojom::ColorSpacePrimaryID::kAdobeRGB;
      case gfx::ColorSpace::PrimaryID::APPLE_GENERIC_RGB:
        return media::stable::mojom::ColorSpacePrimaryID::kAppleGenericRGB;
      case gfx::ColorSpace::PrimaryID::WIDE_GAMUT_COLOR_SPIN:
        return media::stable::mojom::ColorSpacePrimaryID::kWideGamutColorSpin;
      case gfx::ColorSpace::PrimaryID::CUSTOM:
        return media::stable::mojom::ColorSpacePrimaryID::kCustom;
    }

    NOTREACHED();
    return media::stable::mojom::ColorSpacePrimaryID::kInvalid;
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::stable::mojom::ColorSpacePrimaryID input,
                        gfx::ColorSpace::PrimaryID* output) {
    switch (input) {
      case media::stable::mojom::ColorSpacePrimaryID::kInvalid:
        *output = gfx::ColorSpace::PrimaryID::INVALID;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kBT709:
        *output = gfx::ColorSpace::PrimaryID::BT709;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kBT470M:
        *output = gfx::ColorSpace::PrimaryID::BT470M;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kBT470BG:
        *output = gfx::ColorSpace::PrimaryID::BT470BG;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kSMPTE170M:
        *output = gfx::ColorSpace::PrimaryID::SMPTE170M;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kSMPTE240M:
        *output = gfx::ColorSpace::PrimaryID::SMPTE240M;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kFilm:
        *output = gfx::ColorSpace::PrimaryID::FILM;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kBT2020:
        *output = gfx::ColorSpace::PrimaryID::BT2020;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kSMPTEST428_1:
        *output = gfx::ColorSpace::PrimaryID::SMPTEST428_1;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kSMPTEST431_2:
        *output = gfx::ColorSpace::PrimaryID::SMPTEST431_2;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kSMPTEST432_1:
        *output = gfx::ColorSpace::PrimaryID::P3;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kXYZ_D50:
        *output = gfx::ColorSpace::PrimaryID::XYZ_D50;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kAdobeRGB:
        *output = gfx::ColorSpace::PrimaryID::ADOBE_RGB;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kAppleGenericRGB:
        *output = gfx::ColorSpace::PrimaryID::APPLE_GENERIC_RGB;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kWideGamutColorSpin:
        *output = gfx::ColorSpace::PrimaryID::WIDE_GAMUT_COLOR_SPIN;
        return true;
      case media::stable::mojom::ColorSpacePrimaryID::kCustom:
        *output = gfx::ColorSpace::PrimaryID::CUSTOM;
        return true;
    }

    NOTREACHED();
    return false;
  }
};

template <>
struct EnumTraits<media::stable::mojom::ColorSpaceTransferID,
                  gfx::ColorSpace::TransferID> {
  static media::stable::mojom::ColorSpaceTransferID ToMojom(
      gfx::ColorSpace::TransferID input) {
    switch (input) {
      case gfx::ColorSpace::TransferID::INVALID:
        return media::stable::mojom::ColorSpaceTransferID::kInvalid;
      case gfx::ColorSpace::TransferID::BT709:
        return media::stable::mojom::ColorSpaceTransferID::kBT709;
      case gfx::ColorSpace::TransferID::BT709_APPLE:
        return media::stable::mojom::ColorSpaceTransferID::kBT709Apple;
      case gfx::ColorSpace::TransferID::GAMMA18:
        return media::stable::mojom::ColorSpaceTransferID::kGamma18;
      case gfx::ColorSpace::TransferID::GAMMA22:
        return media::stable::mojom::ColorSpaceTransferID::kGamma22;
      case gfx::ColorSpace::TransferID::GAMMA24:
        return media::stable::mojom::ColorSpaceTransferID::kGamma24;
      case gfx::ColorSpace::TransferID::GAMMA28:
        return media::stable::mojom::ColorSpaceTransferID::kGamma28;
      case gfx::ColorSpace::TransferID::SMPTE170M:
        return media::stable::mojom::ColorSpaceTransferID::kSMPTE170M;
      case gfx::ColorSpace::TransferID::SMPTE240M:
        return media::stable::mojom::ColorSpaceTransferID::kSMPTE240M;
      case gfx::ColorSpace::TransferID::LINEAR:
        return media::stable::mojom::ColorSpaceTransferID::kLinear;
      case gfx::ColorSpace::TransferID::LOG:
        return media::stable::mojom::ColorSpaceTransferID::kLog;
      case gfx::ColorSpace::TransferID::LOG_SQRT:
        return media::stable::mojom::ColorSpaceTransferID::kLogSqrt;
      case gfx::ColorSpace::TransferID::IEC61966_2_4:
        return media::stable::mojom::ColorSpaceTransferID::kIEC61966_2_4;
      case gfx::ColorSpace::TransferID::BT1361_ECG:
        return media::stable::mojom::ColorSpaceTransferID::kBT1361_ECG;
      case gfx::ColorSpace::TransferID::SRGB:
        return media::stable::mojom::ColorSpaceTransferID::kIEC61966_2_1;
      case gfx::ColorSpace::TransferID::BT2020_10:
        return media::stable::mojom::ColorSpaceTransferID::kBT2020_10;
      case gfx::ColorSpace::TransferID::BT2020_12:
        return media::stable::mojom::ColorSpaceTransferID::kBT2020_12;
      case gfx::ColorSpace::TransferID::PQ:
        return media::stable::mojom::ColorSpaceTransferID::kSMPTEST2084;
      case gfx::ColorSpace::TransferID::SMPTEST428_1:
        return media::stable::mojom::ColorSpaceTransferID::kSMPTEST428_1;
      case gfx::ColorSpace::TransferID::HLG:
        return media::stable::mojom::ColorSpaceTransferID::kARIB_STD_B67;
      case gfx::ColorSpace::TransferID::SRGB_HDR:
        return media::stable::mojom::ColorSpaceTransferID::kIEC61966_2_1_HDR;
      case gfx::ColorSpace::TransferID::LINEAR_HDR:
        return media::stable::mojom::ColorSpaceTransferID::kLinearHDR;
      case gfx::ColorSpace::TransferID::CUSTOM:
        return media::stable::mojom::ColorSpaceTransferID::kCustom;
      case gfx::ColorSpace::TransferID::CUSTOM_HDR:
        return media::stable::mojom::ColorSpaceTransferID::kCustomHDR;
      case gfx::ColorSpace::TransferID::PIECEWISE_HDR:
        return media::stable::mojom::ColorSpaceTransferID::kPiecewiseHDR;
    }

    NOTREACHED();
    return media::stable::mojom::ColorSpaceTransferID::kInvalid;
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::stable::mojom::ColorSpaceTransferID input,
                        gfx::ColorSpace::TransferID* output) {
    switch (input) {
      case media::stable::mojom::ColorSpaceTransferID::kInvalid:
        *output = gfx::ColorSpace::TransferID::INVALID;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kBT709:
        *output = gfx::ColorSpace::TransferID::BT709;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kBT709Apple:
        *output = gfx::ColorSpace::TransferID::BT709_APPLE;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kGamma18:
        *output = gfx::ColorSpace::TransferID::GAMMA18;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kGamma22:
        *output = gfx::ColorSpace::TransferID::GAMMA22;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kGamma24:
        *output = gfx::ColorSpace::TransferID::GAMMA24;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kGamma28:
        *output = gfx::ColorSpace::TransferID::GAMMA28;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kSMPTE170M:
        *output = gfx::ColorSpace::TransferID::SMPTE170M;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kSMPTE240M:
        *output = gfx::ColorSpace::TransferID::SMPTE240M;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kLinear:
        *output = gfx::ColorSpace::TransferID::LINEAR;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kLog:
        *output = gfx::ColorSpace::TransferID::LOG;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kLogSqrt:
        *output = gfx::ColorSpace::TransferID::LOG_SQRT;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kIEC61966_2_4:
        *output = gfx::ColorSpace::TransferID::IEC61966_2_4;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kBT1361_ECG:
        *output = gfx::ColorSpace::TransferID::BT1361_ECG;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kIEC61966_2_1:
        *output = gfx::ColorSpace::TransferID::SRGB;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kBT2020_10:
        *output = gfx::ColorSpace::TransferID::BT2020_10;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kBT2020_12:
        *output = gfx::ColorSpace::TransferID::BT2020_12;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kSMPTEST2084:
        *output = gfx::ColorSpace::TransferID::PQ;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kSMPTEST428_1:
        *output = gfx::ColorSpace::TransferID::SMPTEST428_1;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kARIB_STD_B67:
        *output = gfx::ColorSpace::TransferID::HLG;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kIEC61966_2_1_HDR:
        *output = gfx::ColorSpace::TransferID::SRGB_HDR;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kLinearHDR:
        *output = gfx::ColorSpace::TransferID::LINEAR_HDR;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kCustom:
        *output = gfx::ColorSpace::TransferID::CUSTOM;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kCustomHDR:
        *output = gfx::ColorSpace::TransferID::CUSTOM_HDR;
        return true;
      case media::stable::mojom::ColorSpaceTransferID::kPiecewiseHDR:
        *output = gfx::ColorSpace::TransferID::PIECEWISE_HDR;
        return true;
    }

    NOTREACHED();
    return false;
  }
};

template <>
struct EnumTraits<media::stable::mojom::ColorSpaceMatrixID,
                  gfx::ColorSpace::MatrixID> {
  static media::stable::mojom::ColorSpaceMatrixID ToMojom(
      gfx::ColorSpace::MatrixID input) {
    switch (input) {
      case gfx::ColorSpace::MatrixID::INVALID:
        return media::stable::mojom::ColorSpaceMatrixID::kInvalid;
      case gfx::ColorSpace::MatrixID::RGB:
        return media::stable::mojom::ColorSpaceMatrixID::kRGB;
      case gfx::ColorSpace::MatrixID::BT709:
        return media::stable::mojom::ColorSpaceMatrixID::kBT709;
      case gfx::ColorSpace::MatrixID::FCC:
        return media::stable::mojom::ColorSpaceMatrixID::kFCC;
      case gfx::ColorSpace::MatrixID::BT470BG:
        return media::stable::mojom::ColorSpaceMatrixID::kBT470BG;
      case gfx::ColorSpace::MatrixID::SMPTE170M:
        return media::stable::mojom::ColorSpaceMatrixID::kSMPTE170M;
      case gfx::ColorSpace::MatrixID::SMPTE240M:
        return media::stable::mojom::ColorSpaceMatrixID::kSMPTE240M;
      case gfx::ColorSpace::MatrixID::YCOCG:
        return media::stable::mojom::ColorSpaceMatrixID::kYCOCG;
      case gfx::ColorSpace::MatrixID::BT2020_NCL:
        return media::stable::mojom::ColorSpaceMatrixID::kBT2020_NCL;
      case gfx::ColorSpace::MatrixID::BT2020_CL:
        return media::stable::mojom::ColorSpaceMatrixID::kBT2020_CL;
      case gfx::ColorSpace::MatrixID::YDZDX:
        return media::stable::mojom::ColorSpaceMatrixID::kYDZDX;
      case gfx::ColorSpace::MatrixID::GBR:
        return media::stable::mojom::ColorSpaceMatrixID::kGBR;
    }

    NOTREACHED();
    return media::stable::mojom::ColorSpaceMatrixID::kInvalid;
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::stable::mojom::ColorSpaceMatrixID input,
                        gfx::ColorSpace::MatrixID* output) {
    switch (input) {
      case media::stable::mojom::ColorSpaceMatrixID::kInvalid:
        *output = gfx::ColorSpace::MatrixID::INVALID;
        return true;
      case media::stable::mojom::ColorSpaceMatrixID::kRGB:
        *output = gfx::ColorSpace::MatrixID::RGB;
        return true;
      case media::stable::mojom::ColorSpaceMatrixID::kBT709:
        *output = gfx::ColorSpace::MatrixID::BT709;
        return true;
      case media::stable::mojom::ColorSpaceMatrixID::kFCC:
        *output = gfx::ColorSpace::MatrixID::FCC;
        return true;
      case media::stable::mojom::ColorSpaceMatrixID::kBT470BG:
        *output = gfx::ColorSpace::MatrixID::BT470BG;
        return true;
      case media::stable::mojom::ColorSpaceMatrixID::kSMPTE170M:
        *output = gfx::ColorSpace::MatrixID::SMPTE170M;
        return true;
      case media::stable::mojom::ColorSpaceMatrixID::kSMPTE240M:
        *output = gfx::ColorSpace::MatrixID::SMPTE240M;
        return true;
      case media::stable::mojom::ColorSpaceMatrixID::kYCOCG:
        *output = gfx::ColorSpace::MatrixID::YCOCG;
        return true;
      case media::stable::mojom::ColorSpaceMatrixID::kBT2020_NCL:
        *output = gfx::ColorSpace::MatrixID::BT2020_NCL;
        return true;
      case media::stable::mojom::ColorSpaceMatrixID::kBT2020_CL:
        *output = gfx::ColorSpace::MatrixID::BT2020_CL;
        return true;
      case media::stable::mojom::ColorSpaceMatrixID::kYDZDX:
        *output = gfx::ColorSpace::MatrixID::YDZDX;
        return true;
      case media::stable::mojom::ColorSpaceMatrixID::kGBR:
        *output = gfx::ColorSpace::MatrixID::GBR;
        return true;
    }

    NOTREACHED();
    return false;
  }
};

template <>
struct EnumTraits<media::stable::mojom::ColorSpaceRangeID,
                  gfx::ColorSpace::RangeID> {
  static media::stable::mojom::ColorSpaceRangeID ToMojom(
      gfx::ColorSpace::RangeID input) {
    switch (input) {
      case gfx::ColorSpace::RangeID::INVALID:
        return media::stable::mojom::ColorSpaceRangeID::kInvalid;
      case gfx::ColorSpace::RangeID::LIMITED:
        return media::stable::mojom::ColorSpaceRangeID::kLimited;
      case gfx::ColorSpace::RangeID::FULL:
        return media::stable::mojom::ColorSpaceRangeID::kFull;
      case gfx::ColorSpace::RangeID::DERIVED:
        return media::stable::mojom::ColorSpaceRangeID::kDerived;
    }

    NOTREACHED();
    return media::stable::mojom::ColorSpaceRangeID::kInvalid;
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::stable::mojom::ColorSpaceRangeID input,
                        gfx::ColorSpace::RangeID* output) {
    switch (input) {
      case media::stable::mojom::ColorSpaceRangeID::kInvalid:
        *output = gfx::ColorSpace::RangeID::INVALID;
        return true;
      case media::stable::mojom::ColorSpaceRangeID::kLimited:
        *output = gfx::ColorSpace::RangeID::LIMITED;
        return true;
      case media::stable::mojom::ColorSpaceRangeID::kFull:
        *output = gfx::ColorSpace::RangeID::FULL;
        return true;
      case media::stable::mojom::ColorSpaceRangeID::kDerived:
        *output = gfx::ColorSpace::RangeID::DERIVED;
        return true;
    }

    NOTREACHED();
    return false;
  }
};

template <>
struct StructTraits<media::stable::mojom::ColorSpaceDataView, gfx::ColorSpace> {
  static gfx::ColorSpace::PrimaryID primaries(const gfx::ColorSpace& input);

  static gfx::ColorSpace::TransferID transfer(const gfx::ColorSpace& input);

  static gfx::ColorSpace::MatrixID matrix(const gfx::ColorSpace& input);

  static gfx::ColorSpace::RangeID range(const gfx::ColorSpace& input);

  static base::span<const float> custom_primary_matrix(
      const gfx::ColorSpace& input);

  static base::span<const float> transfer_params(const gfx::ColorSpace& input);

  static bool Read(media::stable::mojom::ColorSpaceDataView data,
                   gfx::ColorSpace* output);
};

template <>
struct StructTraits<media::stable::mojom::ColorVolumeMetadataDataView,
                    gfx::ColorVolumeMetadata> {
  static const gfx::PointF& primary_r(const gfx::ColorVolumeMetadata& input);

  static const gfx::PointF& primary_g(const gfx::ColorVolumeMetadata& input);

  static const gfx::PointF& primary_b(const gfx::ColorVolumeMetadata& input);

  static const gfx::PointF& white_point(const gfx::ColorVolumeMetadata& input);

  static float luminance_max(const gfx::ColorVolumeMetadata& input);

  static float luminance_min(const gfx::ColorVolumeMetadata& input);

  static bool Read(media::stable::mojom::ColorVolumeMetadataDataView data,
                   gfx::ColorVolumeMetadata* output);
};

template <>
struct StructTraits<media::stable::mojom::DecoderBufferDataView,
                    scoped_refptr<media::DecoderBuffer>> {
  static bool IsNull(const scoped_refptr<media::DecoderBuffer>& input) {
    return !input;
  }

  static void SetToNull(scoped_refptr<media::DecoderBuffer>* input) {
    *input = nullptr;
  }

  static base::TimeDelta timestamp(
      const scoped_refptr<media::DecoderBuffer>& input);

  static base::TimeDelta duration(
      const scoped_refptr<media::DecoderBuffer>& input);

  static bool is_end_of_stream(
      const scoped_refptr<media::DecoderBuffer>& input);

  static uint32_t data_size(const scoped_refptr<media::DecoderBuffer>& input);

  static bool is_key_frame(const scoped_refptr<media::DecoderBuffer>& input);

  static std::vector<uint8_t> side_data(
      const scoped_refptr<media::DecoderBuffer>& input);

  static std::unique_ptr<media::DecryptConfig> decrypt_config(
      const scoped_refptr<media::DecoderBuffer>& input);

  static base::TimeDelta front_discard(
      const scoped_refptr<media::DecoderBuffer>& input);

  static base::TimeDelta back_discard(
      const scoped_refptr<media::DecoderBuffer>& input);

  static bool Read(media::stable::mojom::DecoderBufferDataView input,
                   scoped_refptr<media::DecoderBuffer>* output);
};

template <>
struct StructTraits<media::stable::mojom::DecryptConfigDataView,
                    std::unique_ptr<media::DecryptConfig>> {
  static bool IsNull(const std::unique_ptr<media::DecryptConfig>& input) {
    return !input;
  }

  static void SetToNull(std::unique_ptr<media::DecryptConfig>* output) {
    output->reset();
  }

  static media::EncryptionScheme encryption_scheme(
      const std::unique_ptr<media::DecryptConfig>& input);

  static const std::string& key_id(
      const std::unique_ptr<media::DecryptConfig>& input);

  static const std::string& iv(
      const std::unique_ptr<media::DecryptConfig>& input);

  static const std::vector<media::SubsampleEntry>& subsamples(
      const std::unique_ptr<media::DecryptConfig>& input);

  static const absl::optional<media::EncryptionPattern>& encryption_pattern(
      const std::unique_ptr<media::DecryptConfig>& input);

  static bool Read(media::stable::mojom::DecryptConfigDataView input,
                   std::unique_ptr<media::DecryptConfig>* output);
};

template <>
struct EnumTraits<media::stable::mojom::EncryptionScheme,
                  ::media::EncryptionScheme> {
  static media::stable::mojom::EncryptionScheme ToMojom(
      ::media::EncryptionScheme input) {
    switch (input) {
      case ::media::EncryptionScheme::kUnencrypted:
        return media::stable::mojom::EncryptionScheme::kUnencrypted;
      case ::media::EncryptionScheme::kCenc:
        return media::stable::mojom::EncryptionScheme::kCenc;
      case ::media::EncryptionScheme::kCbcs:
        return media::stable::mojom::EncryptionScheme::kCbcs;
    }

    NOTREACHED();
    return media::stable::mojom::EncryptionScheme::kUnencrypted;
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::stable::mojom::EncryptionScheme input,
                        media::EncryptionScheme* output) {
    switch (input) {
      case media::stable::mojom::EncryptionScheme::kUnencrypted:
        *output = ::media::EncryptionScheme::kUnencrypted;
        return true;
      case media::stable::mojom::EncryptionScheme::kCenc:
        *output = ::media::EncryptionScheme::kCenc;
        return true;
      case media::stable::mojom::EncryptionScheme::kCbcs:
        *output = ::media::EncryptionScheme::kCbcs;
        return true;
    }

    NOTREACHED();
    return false;
  }
};

template <>
struct StructTraits<media::stable::mojom::HDRMetadataDataView,
                    gfx::HDRMetadata> {
  static uint32_t max_content_light_level(const gfx::HDRMetadata& input);

  static uint32_t max_frame_average_light_level(const gfx::HDRMetadata& input);

  static const gfx::ColorVolumeMetadata& color_volume_metadata(
      const gfx::HDRMetadata& input);

  static bool Read(media::stable::mojom::HDRMetadataDataView data,
                   gfx::HDRMetadata* output);
};

template <>
struct EnumTraits<media::stable::mojom::MediaLogRecord_Type,
                  media::MediaLogRecord::Type> {
  static media::stable::mojom::MediaLogRecord_Type ToMojom(
      media::MediaLogRecord::Type input) {
    switch (input) {
      case media::MediaLogRecord::Type::kMessage:
        return media::stable::mojom::MediaLogRecord_Type::kMessage;
      case media::MediaLogRecord::Type::kMediaPropertyChange:
        return media::stable::mojom::MediaLogRecord_Type::kMediaPropertyChange;
      case media::MediaLogRecord::Type::kMediaEventTriggered:
        return media::stable::mojom::MediaLogRecord_Type::kMediaEventTriggered;
      case media::MediaLogRecord::Type::kMediaStatus:
        return media::stable::mojom::MediaLogRecord_Type::kMediaStatus;
    }

    NOTREACHED();
    return media::stable::mojom::MediaLogRecord_Type::kMessage;
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::stable::mojom::MediaLogRecord_Type input,
                        media::MediaLogRecord::Type* output) {
    switch (input) {
      case media::stable::mojom::MediaLogRecord_Type::kMessage:
        *output = media::MediaLogRecord::Type::kMessage;
        return true;
      case media::stable::mojom::MediaLogRecord_Type::kMediaPropertyChange:
        *output = media::MediaLogRecord::Type::kMediaPropertyChange;
        return true;
      case media::stable::mojom::MediaLogRecord_Type::kMediaEventTriggered:
        *output = media::MediaLogRecord::Type::kMediaEventTriggered;
        return true;
      case media::stable::mojom::MediaLogRecord_Type::kMediaStatus:
        *output = media::MediaLogRecord::Type::kMediaStatus;
        return true;
    }

    NOTREACHED();
    return false;
  }
};

template <>
struct StructTraits<media::stable::mojom::MediaLogRecordDataView,
                    media::MediaLogRecord> {
  static int32_t id(const media::MediaLogRecord& input);

  static media::MediaLogRecord::Type type(const media::MediaLogRecord& input);

  static const base::Value& params(const media::MediaLogRecord& input);

  static base::TimeTicks time(const media::MediaLogRecord& input);

  static bool Read(media::stable::mojom::MediaLogRecordDataView input,
                   media::MediaLogRecord* output);
};

template <>
struct StructTraits<media::stable::mojom::NativeGpuMemoryBufferHandleDataView,
                    gfx::GpuMemoryBufferHandle> {
  static const gfx::GpuMemoryBufferId& id(
      const gfx::GpuMemoryBufferHandle& input);

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  static gfx::NativePixmapHandle platform_handle(
      gfx::GpuMemoryBufferHandle& input);
#else
  static media::stable::mojom::NativePixmapHandlePtr platform_handle(
      gfx::GpuMemoryBufferHandle& input) {
    // We should not be trying to serialize a gfx::GpuMemoryBufferHandle for the
    // purposes of this interface outside of Linux and Chrome OS.
    CHECK(false);
    return media::stable::mojom::NativePixmapHandle::New();
  }
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

  static bool Read(
      media::stable::mojom::NativeGpuMemoryBufferHandleDataView data,
      gfx::GpuMemoryBufferHandle* output);
};

template <>
struct StructTraits<media::stable::mojom::StatusDataDataView,
                    media::internal::StatusData> {
  static media::stable::mojom::StatusCode code(
      const media::internal::StatusData& input);

  static std::string group(const media::internal::StatusData& input);

  static std::string message(const media::internal::StatusData& input);

  static base::span<const base::Value> frames(
      const media::internal::StatusData& input);

  static absl::optional<media::internal::StatusData> cause(
      const media::internal::StatusData& input);

  static const base::Value& data(const media::internal::StatusData& input);

  static bool Read(media::stable::mojom::StatusDataDataView data,
                   media::internal::StatusData* output);
};

template <>
struct StructTraits<media::stable::mojom::StatusDataView,
                    media::DecoderStatus> {
  static mojo::OptionalAsPointer<const media::internal::StatusData> internal(
      const media::DecoderStatus& input);

  static bool Read(media::stable::mojom::StatusDataView data,
                   media::DecoderStatus* output);
};

template <>
struct StructTraits<media::stable::mojom::SupportedVideoDecoderConfigDataView,
                    media::SupportedVideoDecoderConfig> {
  static media::VideoCodecProfile profile_min(
      const media::SupportedVideoDecoderConfig& input);

  static media::VideoCodecProfile profile_max(
      const media::SupportedVideoDecoderConfig& input);

  static const gfx::Size& coded_size_min(
      const media::SupportedVideoDecoderConfig& input);

  static const gfx::Size& coded_size_max(
      const media::SupportedVideoDecoderConfig& input);

  static bool allow_encrypted(const media::SupportedVideoDecoderConfig& input);

  static bool require_encrypted(
      const media::SupportedVideoDecoderConfig& input);

  static bool Read(
      media::stable::mojom::SupportedVideoDecoderConfigDataView input,
      media::SupportedVideoDecoderConfig* output);
};

template <>
struct EnumTraits<media::stable::mojom::VideoCodec, ::media::VideoCodec> {
  static media::stable::mojom::VideoCodec ToMojom(::media::VideoCodec input) {
    switch (input) {
      case ::media::VideoCodec::kUnknown:
        return media::stable::mojom::VideoCodec::kUnknown;
      case ::media::VideoCodec::kH264:
        return media::stable::mojom::VideoCodec::kH264;
      case ::media::VideoCodec::kVC1:
        return media::stable::mojom::VideoCodec::kVC1;
      case ::media::VideoCodec::kMPEG2:
        return media::stable::mojom::VideoCodec::kMPEG2;
      case ::media::VideoCodec::kMPEG4:
        return media::stable::mojom::VideoCodec::kMPEG4;
      case ::media::VideoCodec::kTheora:
        return media::stable::mojom::VideoCodec::kTheora;
      case ::media::VideoCodec::kVP8:
        return media::stable::mojom::VideoCodec::kVP8;
      case ::media::VideoCodec::kVP9:
        return media::stable::mojom::VideoCodec::kVP9;
      case ::media::VideoCodec::kHEVC:
        return media::stable::mojom::VideoCodec::kHEVC;
      case ::media::VideoCodec::kDolbyVision:
        return media::stable::mojom::VideoCodec::kDolbyVision;
      case ::media::VideoCodec::kAV1:
        return media::stable::mojom::VideoCodec::kAV1;
    }

    NOTREACHED();
    return media::stable::mojom::VideoCodec::kUnknown;
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::stable::mojom::VideoCodec input,
                        media::VideoCodec* output) {
    switch (input) {
      case media::stable::mojom::VideoCodec::kUnknown:
        *output = ::media::VideoCodec::kUnknown;
        return true;
      case media::stable::mojom::VideoCodec::kH264:
        *output = ::media::VideoCodec::kH264;
        return true;
      case media::stable::mojom::VideoCodec::kVC1:
        *output = ::media::VideoCodec::kVC1;
        return true;
      case media::stable::mojom::VideoCodec::kMPEG2:
        *output = ::media::VideoCodec::kMPEG2;
        return true;
      case media::stable::mojom::VideoCodec::kMPEG4:
        *output = ::media::VideoCodec::kMPEG4;
        return true;
      case media::stable::mojom::VideoCodec::kTheora:
        *output = ::media::VideoCodec::kTheora;
        return true;
      case media::stable::mojom::VideoCodec::kVP8:
        *output = ::media::VideoCodec::kVP8;
        return true;
      case media::stable::mojom::VideoCodec::kVP9:
        *output = ::media::VideoCodec::kVP9;
        return true;
      case media::stable::mojom::VideoCodec::kHEVC:
        *output = ::media::VideoCodec::kHEVC;
        return true;
      case media::stable::mojom::VideoCodec::kDolbyVision:
        *output = ::media::VideoCodec::kDolbyVision;
        return true;
      case media::stable::mojom::VideoCodec::kAV1:
        *output = ::media::VideoCodec::kAV1;
        return true;
    }

    NOTREACHED();
    return false;
  }
};

template <>
struct EnumTraits<media::stable::mojom::VideoCodecProfile,
                  ::media::VideoCodecProfile> {
  static media::stable::mojom::VideoCodecProfile ToMojom(
      ::media::VideoCodecProfile input) {
    switch (input) {
      case ::media::VideoCodecProfile::VIDEO_CODEC_PROFILE_UNKNOWN:
        return media::stable::mojom::VideoCodecProfile::
            kVideoCodecProfileUnknown;
      case ::media::VideoCodecProfile::H264PROFILE_BASELINE:
        return media::stable::mojom::VideoCodecProfile::kH264ProfileBaseline;
      case ::media::VideoCodecProfile::H264PROFILE_MAIN:
        return media::stable::mojom::VideoCodecProfile::kH264ProfileMain;
      case ::media::VideoCodecProfile::H264PROFILE_EXTENDED:
        return media::stable::mojom::VideoCodecProfile::kH264ProfileExtended;
      case ::media::VideoCodecProfile::H264PROFILE_HIGH:
        return media::stable::mojom::VideoCodecProfile::kH264ProfileHigh;
      case ::media::VideoCodecProfile::H264PROFILE_HIGH10PROFILE:
        return media::stable::mojom::VideoCodecProfile::kH264ProfileHigh10;
      case ::media::VideoCodecProfile::H264PROFILE_HIGH422PROFILE:
        return media::stable::mojom::VideoCodecProfile::kH264ProfileHigh422;
      case ::media::VideoCodecProfile::H264PROFILE_HIGH444PREDICTIVEPROFILE:
        return media::stable::mojom::VideoCodecProfile::
            kH264ProfileHigh444Predictive;
      case ::media::VideoCodecProfile::H264PROFILE_SCALABLEBASELINE:
        return media::stable::mojom::VideoCodecProfile::
            kH264ProfileScalableBaseline;
      case ::media::VideoCodecProfile::H264PROFILE_SCALABLEHIGH:
        return media::stable::mojom::VideoCodecProfile::
            kH264ProfileScalableHigh;
      case ::media::VideoCodecProfile::H264PROFILE_STEREOHIGH:
        return media::stable::mojom::VideoCodecProfile::kH264ProfileStereoHigh;
      case ::media::VideoCodecProfile::H264PROFILE_MULTIVIEWHIGH:
        return media::stable::mojom::VideoCodecProfile::
            kH264ProfileMultiviewHigh;
      case ::media::VideoCodecProfile::VP8PROFILE_ANY:
        return media::stable::mojom::VideoCodecProfile::kVP8ProfileAny;
      case ::media::VideoCodecProfile::VP9PROFILE_PROFILE0:
        return media::stable::mojom::VideoCodecProfile::kVP9Profile0;
      case ::media::VideoCodecProfile::VP9PROFILE_PROFILE1:
        return media::stable::mojom::VideoCodecProfile::kVP9Profile1;
      case ::media::VideoCodecProfile::VP9PROFILE_PROFILE2:
        return media::stable::mojom::VideoCodecProfile::kVP9Profile2;
      case ::media::VideoCodecProfile::VP9PROFILE_PROFILE3:
        return media::stable::mojom::VideoCodecProfile::kVP9Profile3;
      case ::media::VideoCodecProfile::HEVCPROFILE_MAIN:
        return media::stable::mojom::VideoCodecProfile::kHEVCProfileMain;
      case ::media::VideoCodecProfile::HEVCPROFILE_MAIN10:
        return media::stable::mojom::VideoCodecProfile::kHEVCProfileMain10;
      case ::media::VideoCodecProfile::HEVCPROFILE_MAIN_STILL_PICTURE:
        return media::stable::mojom::VideoCodecProfile::
            kHEVCProfileMainStillPicture;
      case ::media::VideoCodecProfile::DOLBYVISION_PROFILE0:
        return media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile0;
      case ::media::VideoCodecProfile::DOLBYVISION_PROFILE4:
        return media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile4;
      case ::media::VideoCodecProfile::DOLBYVISION_PROFILE5:
        return media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile5;
      case ::media::VideoCodecProfile::DOLBYVISION_PROFILE7:
        return media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile7;
      case ::media::VideoCodecProfile::THEORAPROFILE_ANY:
        return media::stable::mojom::VideoCodecProfile::kTheoraProfileAny;
      case ::media::VideoCodecProfile::AV1PROFILE_PROFILE_MAIN:
        return media::stable::mojom::VideoCodecProfile::kAV1ProfileMain;
      case ::media::VideoCodecProfile::AV1PROFILE_PROFILE_HIGH:
        return media::stable::mojom::VideoCodecProfile::kAV1ProfileHigh;
      case ::media::VideoCodecProfile::AV1PROFILE_PROFILE_PRO:
        return media::stable::mojom::VideoCodecProfile::kAV1ProfilePro;
      case ::media::VideoCodecProfile::DOLBYVISION_PROFILE8:
        return media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile8;
      case ::media::VideoCodecProfile::DOLBYVISION_PROFILE9:
        return media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile9;
    }

    NOTREACHED();
    return media::stable::mojom::VideoCodecProfile::kVideoCodecProfileUnknown;
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::stable::mojom::VideoCodecProfile input,
                        media::VideoCodecProfile* output) {
    switch (input) {
      case media::stable::mojom::VideoCodecProfile::kVideoCodecProfileUnknown:
        *output = ::media::VideoCodecProfile::VIDEO_CODEC_PROFILE_UNKNOWN;
        return true;
      case media::stable::mojom::VideoCodecProfile::kH264ProfileBaseline:
        *output = ::media::VideoCodecProfile::H264PROFILE_BASELINE;
        return true;
      case media::stable::mojom::VideoCodecProfile::kH264ProfileMain:
        *output = ::media::VideoCodecProfile::H264PROFILE_MAIN;
        return true;
      case media::stable::mojom::VideoCodecProfile::kH264ProfileExtended:
        *output = ::media::VideoCodecProfile::H264PROFILE_EXTENDED;
        return true;
      case media::stable::mojom::VideoCodecProfile::kH264ProfileHigh:
        *output = ::media::VideoCodecProfile::H264PROFILE_HIGH;
        return true;
      case media::stable::mojom::VideoCodecProfile::kH264ProfileHigh10:
        *output = ::media::VideoCodecProfile::H264PROFILE_HIGH10PROFILE;
        return true;
      case media::stable::mojom::VideoCodecProfile::kH264ProfileHigh422:
        *output = ::media::VideoCodecProfile::H264PROFILE_HIGH422PROFILE;
        return true;
      case media::stable::mojom::VideoCodecProfile::
          kH264ProfileHigh444Predictive:
        *output =
            ::media::VideoCodecProfile::H264PROFILE_HIGH444PREDICTIVEPROFILE;
        return true;
      case media::stable::mojom::VideoCodecProfile::
          kH264ProfileScalableBaseline:
        *output = ::media::VideoCodecProfile::H264PROFILE_SCALABLEBASELINE;
        return true;
      case media::stable::mojom::VideoCodecProfile::kH264ProfileScalableHigh:
        *output = ::media::VideoCodecProfile::H264PROFILE_SCALABLEHIGH;
        return true;
      case media::stable::mojom::VideoCodecProfile::kH264ProfileStereoHigh:
        *output = ::media::VideoCodecProfile::H264PROFILE_STEREOHIGH;
        return true;
      case media::stable::mojom::VideoCodecProfile::kH264ProfileMultiviewHigh:
        *output = ::media::VideoCodecProfile::H264PROFILE_MULTIVIEWHIGH;
        return true;
      case media::stable::mojom::VideoCodecProfile::kVP8ProfileAny:
        *output = ::media::VideoCodecProfile::VP8PROFILE_ANY;
        return true;
      case media::stable::mojom::VideoCodecProfile::kVP9Profile0:
        *output = ::media::VideoCodecProfile::VP9PROFILE_PROFILE0;
        return true;
      case media::stable::mojom::VideoCodecProfile::kVP9Profile1:
        *output = ::media::VideoCodecProfile::VP9PROFILE_PROFILE1;
        return true;
      case media::stable::mojom::VideoCodecProfile::kVP9Profile2:
        *output = ::media::VideoCodecProfile::VP9PROFILE_PROFILE2;
        return true;
      case media::stable::mojom::VideoCodecProfile::kVP9Profile3:
        *output = ::media::VideoCodecProfile::VP9PROFILE_PROFILE3;
        return true;
      case media::stable::mojom::VideoCodecProfile::kHEVCProfileMain:
        *output = ::media::VideoCodecProfile::HEVCPROFILE_MAIN;
        return true;
      case media::stable::mojom::VideoCodecProfile::kHEVCProfileMain10:
        *output = ::media::VideoCodecProfile::HEVCPROFILE_MAIN10;
        return true;
      case media::stable::mojom::VideoCodecProfile::
          kHEVCProfileMainStillPicture:
        *output = ::media::VideoCodecProfile::HEVCPROFILE_MAIN_STILL_PICTURE;
        return true;
      case media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile0:
        *output = ::media::VideoCodecProfile::DOLBYVISION_PROFILE0;
        return true;
      case media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile4:
        *output = ::media::VideoCodecProfile::DOLBYVISION_PROFILE4;
        return true;
      case media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile5:
        *output = ::media::VideoCodecProfile::DOLBYVISION_PROFILE5;
        return true;
      case media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile7:
        *output = ::media::VideoCodecProfile::DOLBYVISION_PROFILE7;
        return true;
      case media::stable::mojom::VideoCodecProfile::kTheoraProfileAny:
        *output = ::media::VideoCodecProfile::THEORAPROFILE_ANY;
        return true;
      case media::stable::mojom::VideoCodecProfile::kAV1ProfileMain:
        *output = ::media::VideoCodecProfile::AV1PROFILE_PROFILE_MAIN;
        return true;
      case media::stable::mojom::VideoCodecProfile::kAV1ProfileHigh:
        *output = ::media::VideoCodecProfile::AV1PROFILE_PROFILE_HIGH;
        return true;
      case media::stable::mojom::VideoCodecProfile::kAV1ProfilePro:
        *output = ::media::VideoCodecProfile::AV1PROFILE_PROFILE_PRO;
        return true;
      case media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile8:
        *output = ::media::VideoCodecProfile::DOLBYVISION_PROFILE8;
        return true;
      case media::stable::mojom::VideoCodecProfile::kDolbyVisionProfile9:
        *output = ::media::VideoCodecProfile::DOLBYVISION_PROFILE9;
        return true;
    }

    NOTREACHED();
    return false;
  }
};

template <>
struct StructTraits<media::stable::mojom::VideoDecoderConfigDataView,
                    media::VideoDecoderConfig> {
  static media::VideoCodec codec(const media::VideoDecoderConfig& input);

  static media::VideoCodecProfile profile(
      const media::VideoDecoderConfig& input);

  static bool has_alpha(const media::VideoDecoderConfig& input);

  static const gfx::Size& coded_size(const media::VideoDecoderConfig& input);

  static const gfx::Rect& visible_rect(const media::VideoDecoderConfig& input);

  static const gfx::Size& natural_size(const media::VideoDecoderConfig& input);

  static const std::vector<uint8_t>& extra_data(
      const media::VideoDecoderConfig& input);

  static media::EncryptionScheme encryption_scheme(
      const media::VideoDecoderConfig& input);

  static const gfx::ColorSpace color_space_info(
      const media::VideoDecoderConfig& input);

  static const absl::optional<gfx::HDRMetadata>& hdr_metadata(
      const media::VideoDecoderConfig& input);

  static uint32_t level(const media::VideoDecoderConfig& input);

  static bool Read(media::stable::mojom::VideoDecoderConfigDataView input,
                   media::VideoDecoderConfig* output);
};

template <>
struct EnumTraits<media::stable::mojom::VideoDecoderType,
                  ::media::VideoDecoderType> {
  static media::stable::mojom::VideoDecoderType ToMojom(
      ::media::VideoDecoderType input) {
    switch (input) {
      case ::media::VideoDecoderType::kVaapi:
        return media::stable::mojom::VideoDecoderType::kVaapi;
      case ::media::VideoDecoderType::kVda:
        return media::stable::mojom::VideoDecoderType::kVda;
      case ::media::VideoDecoderType::kV4L2:
        return media::stable::mojom::VideoDecoderType::kV4L2;
      case ::media::VideoDecoderType::kTesting:
        return media::stable::mojom::VideoDecoderType::kTesting;
      case ::media::VideoDecoderType::kUnknown:
      case ::media::VideoDecoderType::kFFmpeg:
      case ::media::VideoDecoderType::kVpx:
      case ::media::VideoDecoderType::kAom:
      case ::media::VideoDecoderType::kMojo:
      case ::media::VideoDecoderType::kDecrypting:
      case ::media::VideoDecoderType::kDav1d:
      case ::media::VideoDecoderType::kFuchsia:
      case ::media::VideoDecoderType::kMediaCodec:
      case ::media::VideoDecoderType::kGav1:
      case ::media::VideoDecoderType::kD3D11:
      case ::media::VideoDecoderType::kBroker:
        return media::stable::mojom::VideoDecoderType::kUnknown;
    }

    NOTREACHED();
    return media::stable::mojom::VideoDecoderType::kUnknown;
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::stable::mojom::VideoDecoderType input,
                        ::media::VideoDecoderType* output) {
    switch (input) {
      case media::stable::mojom::VideoDecoderType::kVaapi:
        *output = ::media::VideoDecoderType::kVaapi;
        return true;
      case media::stable::mojom::VideoDecoderType::kVda:
        *output = ::media::VideoDecoderType::kVda;
        return true;
      case media::stable::mojom::VideoDecoderType::kV4L2:
        *output = ::media::VideoDecoderType::kV4L2;
        return true;
      case media::stable::mojom::VideoDecoderType::kTesting:
        *output = ::media::VideoDecoderType::kTesting;
        return true;
      case media::stable::mojom::VideoDecoderType::kUnknown:
        *output = ::media::VideoDecoderType::kUnknown;
        return true;
    }

    NOTREACHED();
    return false;
  }
};

template <>
struct StructTraits<media::stable::mojom::VideoFrameDataView,
                    scoped_refptr<media::VideoFrame>> {
  static bool IsNull(const scoped_refptr<media::VideoFrame>& input) {
    return !input;
  }

  static void SetToNull(scoped_refptr<media::VideoFrame>* input) {
    *input = nullptr;
  }

  static media::VideoPixelFormat format(
      const scoped_refptr<media::VideoFrame>& input);

  static const gfx::Size& coded_size(
      const scoped_refptr<media::VideoFrame>& input);

  static const gfx::Rect& visible_rect(
      const scoped_refptr<media::VideoFrame>& input);

  static const gfx::Size& natural_size(
      const scoped_refptr<media::VideoFrame>& input);

  static base::TimeDelta timestamp(
      const scoped_refptr<media::VideoFrame>& input);

  static gfx::ColorSpace color_space(
      const scoped_refptr<media::VideoFrame>& input);

  static const absl::optional<gfx::HDRMetadata>& hdr_metadata(
      const scoped_refptr<media::VideoFrame>& input);

  static media::stable::mojom::VideoFrameDataPtr data(
      const scoped_refptr<media::VideoFrame>& input);

  static const media::VideoFrameMetadata& metadata(
      const scoped_refptr<media::VideoFrame>& input);

  static bool Read(media::stable::mojom::VideoFrameDataView input,
                   scoped_refptr<media::VideoFrame>* output);
};

template <>
struct StructTraits<media::stable::mojom::VideoFrameMetadataDataView,
                    media::VideoFrameMetadata> {
  static bool allow_overlay(const media::VideoFrameMetadata& input);

  static bool end_of_stream(const media::VideoFrameMetadata& input);

  static bool read_lock_fences_enabled(const media::VideoFrameMetadata& input);

  static bool protected_video(const media::VideoFrameMetadata& input);

  static bool hw_protected(const media::VideoFrameMetadata& input);

  static bool power_efficient(const media::VideoFrameMetadata& input);

  static bool Read(media::stable::mojom::VideoFrameMetadataDataView input,
                   media::VideoFrameMetadata* output);
};

template <>
struct EnumTraits<media::stable::mojom::VideoPixelFormat,
                  ::media::VideoPixelFormat> {
  static media::stable::mojom::VideoPixelFormat ToMojom(
      ::media::VideoPixelFormat input) {
    switch (input) {
      case ::media::VideoPixelFormat::PIXEL_FORMAT_UNKNOWN:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatUnknown;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_I420:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatI420;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YV12:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYV12;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_I422:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatI422;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_I420A:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatI420A;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_I444:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatI444;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_NV12:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatNV12;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_NV21:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatNV21;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_UYVY:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatUYVY;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUY2:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUY2;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_ARGB:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatARGB;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_XRGB:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatXRGB;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_RGB24:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatRGB24;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_MJPEG:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatMJPEG;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV420P9:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV420P9;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV420P10:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV420P10;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV422P9:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV422P9;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV422P10:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV422P10;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV444P9:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV444P9;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV444P10:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV444P10;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV420P12:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV420P12;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV422P12:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV422P12;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV444P12:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV444P12;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_Y16:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatY16;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_ABGR:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatABGR;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_XBGR:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatXBGR;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_P016LE:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatP016LE;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_XR30:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatXR30;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_XB30:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatXB30;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_BGRA:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatBGRA;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_RGBAF16:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatRGBAF16;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_I422A:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatI422A;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_I444A:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatI444A;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV420AP10:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV420AP10;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV422AP10:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV422AP10;
      case ::media::VideoPixelFormat::PIXEL_FORMAT_YUV444AP10:
        return media::stable::mojom::VideoPixelFormat::kPixelFormatYUV444AP10;
    }

    NOTREACHED();
    return media::stable::mojom::VideoPixelFormat::kPixelFormatUnknown;
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::stable::mojom::VideoPixelFormat input,
                        ::media::VideoPixelFormat* output) {
    switch (input) {
      case media::stable::mojom::VideoPixelFormat::kPixelFormatUnknown:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_UNKNOWN;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatI420:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_I420;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYV12:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YV12;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatI422:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_I422;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatI420A:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_I420A;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatI444:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_I444;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatNV12:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_NV12;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatNV21:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_NV21;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatUYVY:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_UYVY;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUY2:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUY2;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatARGB:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_ARGB;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatXRGB:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_XRGB;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatRGB24:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_RGB24;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatMJPEG:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_MJPEG;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV420P9:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV420P9;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV420P10:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV420P10;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV422P9:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV422P9;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV422P10:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV422P10;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV444P9:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV444P9;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV444P10:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV444P10;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV420P12:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV420P12;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV422P12:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV422P12;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV444P12:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV444P12;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatY16:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_Y16;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatABGR:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_ABGR;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatXBGR:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_XBGR;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatP016LE:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_P016LE;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatXR30:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_XR30;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatXB30:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_XB30;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatBGRA:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_BGRA;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatRGBAF16:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_RGBAF16;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatI422A:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_I422A;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatI444A:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_I444A;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV420AP10:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV420AP10;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV422AP10:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV422AP10;
        return true;
      case media::stable::mojom::VideoPixelFormat::kPixelFormatYUV444AP10:
        *output = ::media::VideoPixelFormat::PIXEL_FORMAT_YUV444AP10;
        return true;
    }

    NOTREACHED();
    return false;
  }
};

template <>
struct EnumTraits<media::stable::mojom::WaitingReason, media::WaitingReason> {
  static media::stable::mojom::WaitingReason ToMojom(
      media::WaitingReason input) {
    switch (input) {
      case media::WaitingReason::kNoCdm:
        return media::stable::mojom::WaitingReason::kNoCdm;
      case media::WaitingReason::kNoDecryptionKey:
        return media::stable::mojom::WaitingReason::kNoDecryptionKey;
      case media::WaitingReason::kDecoderStateLost:
        return media::stable::mojom::WaitingReason::kDecoderStateLost;
    }

    NOTREACHED();
    return media::stable::mojom::WaitingReason::kNoCdm;
  }

  // Returning false results in deserialization failure and causes the
  // message pipe receiving it to be disconnected.
  static bool FromMojom(media::stable::mojom::WaitingReason input,
                        media::WaitingReason* output) {
    switch (input) {
      case media::stable::mojom::WaitingReason::kNoCdm:
        *output = media::WaitingReason::kNoCdm;
        return true;
      case media::stable::mojom::WaitingReason::kNoDecryptionKey:
        *output = media::WaitingReason::kNoDecryptionKey;
        return true;
      case media::stable::mojom::WaitingReason::kDecoderStateLost:
        *output = media::WaitingReason::kDecoderStateLost;
        return true;
    }

    NOTREACHED();
    return false;
  }
};

template <>
struct StructTraits<media::stable::mojom::SubsampleEntryDataView,
                    ::media::SubsampleEntry> {
  static uint32_t clear_bytes(const ::media::SubsampleEntry& input);

  static uint32_t cypher_bytes(const ::media::SubsampleEntry& input);

  static bool Read(media::stable::mojom::SubsampleEntryDataView input,
                   ::media::SubsampleEntry* output);
};

}  // namespace mojo

#endif  // MEDIA_MOJO_MOJOM_STABLE_STABLE_VIDEO_DECODER_TYPES_MOJOM_TRAITS_H_
