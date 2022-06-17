// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_FORMATS_HLS_TAGS_H_
#define MEDIA_FORMATS_HLS_TAGS_H_

#include "base/time/time.h"
#include "media/base/media_export.h"
#include "media/formats/hls/parse_status.h"
#include "media/formats/hls/tag_name.h"
#include "media/formats/hls/types.h"
#include "media/formats/hls/variable_dictionary.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace media::hls {

class TagItem;

// Represents the contents of the #EXTM3U tag
struct MEDIA_EXPORT M3uTag {
  static constexpr auto kName = CommonTagName::kM3u;
  static ParseStatus::Or<M3uTag> Parse(TagItem);
};

// Represents the contents of the #EXT-X-VERSION tag
struct MEDIA_EXPORT XVersionTag {
  static constexpr auto kName = CommonTagName::kXVersion;
  static ParseStatus::Or<XVersionTag> Parse(TagItem);

  types::DecimalInteger version;
};

// Represents the contents of the #EXT-X-INDEPENDENT-SEGMENTS tag
struct MEDIA_EXPORT XIndependentSegmentsTag {
  static constexpr auto kName = CommonTagName::kXIndependentSegments;
  static ParseStatus::Or<XIndependentSegmentsTag> Parse(TagItem);
};

// Represents the contents of the #EXT-X-DEFINE tag
struct MEDIA_EXPORT XDefineTag {
  static constexpr auto kName = CommonTagName::kXDefine;
  static ParseStatus::Or<XDefineTag> Parse(TagItem);

  // Constructs an XDefineTag representing a variable definition.
  static XDefineTag CreateDefinition(types::VariableName name,
                                     base::StringPiece value);

  // Constructs an XDefineTag representing an imported variable definition.
  static XDefineTag CreateImport(types::VariableName name);

  // The name of the variable being defined.
  types::VariableName name;

  // The value of the variable. If this is `nullopt`, then the value
  // is being IMPORT-ed and must be defined in the parent playlist.
  absl::optional<base::StringPiece> value;
};

// Represents the contents of the #EXTINF tag
struct MEDIA_EXPORT InfTag {
  static constexpr auto kName = MediaPlaylistTagName::kInf;
  static ParseStatus::Or<InfTag> Parse(TagItem);

  // Target duration of the media segment.
  base::TimeDelta duration;

  // Human-readable title of the media segment.
  SourceString title;
};

// Represents the contents of the #EXT-X-ENDLIST tag
struct MEDIA_EXPORT XEndListTag {
  static constexpr auto kName = MediaPlaylistTagName::kXEndList;
  static ParseStatus::Or<XEndListTag> Parse(TagItem);
};

// Represents the contents of the #EXT-X-I-FRAMES-ONLY tag
struct MEDIA_EXPORT XIFramesOnlyTag {
  static constexpr auto kName = MediaPlaylistTagName::kXIFramesOnly;
  static ParseStatus::Or<XIFramesOnlyTag> Parse(TagItem);
};

// Represents the contents of the #EXT-X-DISCONTINUITY tag
struct MEDIA_EXPORT XDiscontinuityTag {
  static constexpr auto kName = MediaPlaylistTagName::kXDiscontinuity;
  static ParseStatus::Or<XDiscontinuityTag> Parse(TagItem);
};

// Represents the contents of the #EXT-X-GAP tag
struct MEDIA_EXPORT XGapTag {
  static constexpr auto kName = MediaPlaylistTagName::kXGap;
  static ParseStatus::Or<XGapTag> Parse(TagItem);
};

enum class PlaylistType {
  // Indicates that this playlist may have segments appended upon reloading
  // (until the #EXT-X-ENDLIST tag appears), but segments will not be removed.
  kEvent,

  // Indicates that this playlist is static, and will not have segments appended
  // or removed.
  kVOD,
};

// Represents the contents of the #EXT-X-PLAYLIST-TYPE tag
struct MEDIA_EXPORT XPlaylistTypeTag {
  static constexpr auto kName = MediaPlaylistTagName::kXPlaylistType;
  static ParseStatus::Or<XPlaylistTypeTag> Parse(TagItem);

  PlaylistType type;
};

// Represents the contents of the #EXT-X-STREAM-INF tag
struct MEDIA_EXPORT XStreamInfTag {
  static constexpr auto kName = MultivariantPlaylistTagName::kXStreamInf;
  static ParseStatus::Or<XStreamInfTag> Parse(
      TagItem,
      const VariableDictionary& variable_dict,
      VariableDictionary::SubstitutionBuffer& sub_buffer);

  XStreamInfTag();
  ~XStreamInfTag();
  XStreamInfTag(const XStreamInfTag&);
  XStreamInfTag(XStreamInfTag&&);
  XStreamInfTag& operator=(const XStreamInfTag&);
  XStreamInfTag& operator=(XStreamInfTag&&);

  // The peak segment bitrate of the stream this tag applies to, in bits per
  // second.
  types::DecimalInteger bandwidth = 0;

  // The average segment bitrate of the stream this tag applies to, in bits per
  // second.
  absl::optional<types::DecimalInteger> average_bandwidth;

  // An abstract, relative measure of the quality-of-experience of the stream
  // this tag applies to. The determination of this number is up to the playlist
  // author, however higher scores must indicate a better playback experience.
  absl::optional<types::DecimalFloatingPoint> score;

  // A comma-separated list of formats, where each format specifies a media
  // sample type that is present is one or more renditions of the variant stream
  // this tag applies to. According to the spec this *should* be present on
  // every instance of this tag, but in practice it's not. It's represented as
  // optional here so that the caller may decide how they wish to handle its
  // absence.
  absl::optional<std::string> codecs;

  // The optimal pixel resolution at which to display all video in this variant
  // stream.
  absl::optional<types::DecimalResolution> resolution;

  // This describes the maximum framerate for all video in this variant stream.
  absl::optional<types::DecimalFloatingPoint> frame_rate;
};

// Represents the contents of the #EXT-X-TARGETDURATION tag.
struct MEDIA_EXPORT XTargetDurationTag {
  static constexpr auto kName = MediaPlaylistTagName::kXTargetDuration;
  static ParseStatus::Or<XTargetDurationTag> Parse(TagItem);

  // The upper bound on the duration of all media segments in the
  // media playlist. The EXTINF duration of each Media Segment in a Playlist
  // file, when rounded to the nearest integer, MUST be less than or equal to
  // this duration.
  base::TimeDelta duration;
};

// Represents the contents of the #EXT-PART-INF tag.
struct MEDIA_EXPORT XPartInfTag {
  static constexpr auto kName = MediaPlaylistTagName::kXPartInf;
  static ParseStatus::Or<XPartInfTag> Parse(TagItem);

  // This value indicates the target duration for partial media segments.
  base::TimeDelta target_duration;
};

// Represents the contents of the #EXT-X-SERVER-CONTROL tag.
struct MEDIA_EXPORT XServerControlTag {
  static constexpr auto kName = MediaPlaylistTagName::kXServerControl;
  static ParseStatus::Or<XServerControlTag> Parse(TagItem);

  // This value (given by the 'CAN-SKIP-UNTIL' attribute) represents the
  // distance from the last media segment that the server is able
  // to produce a playlist delta update.
  absl::optional<base::TimeDelta> skip_boundary;

  // This indicates whether the server supports skipping EXT-X-DATERANGE tags
  // older than the skip boundary when producing playlist delta updates.
  bool can_skip_dateranges = false;

  // This indicates the distance from the end of the playlist
  // at which clients should begin playback. This MUST be at least three times
  // the playlist's target duration.
  absl::optional<base::TimeDelta> hold_back;

  // This indicates the distance from the end of the playlist
  // at which clients should begin playback when playing in low-latency mode.
  // This value MUST be at least twice the playlist's partial segment target
  // duration, and SHOULD be at least three times that.
  absl::optional<base::TimeDelta> part_hold_back;

  // This indicates whether the server supports blocking playlist reloads.
  bool can_block_reload = false;
};

// Represents the contents of the #EXT-X-MEDIA-SEQUENCE tag.
struct MEDIA_EXPORT XMediaSequenceTag {
  static constexpr auto kName = MediaPlaylistTagName::kXMediaSequence;
  static ParseStatus::Or<XMediaSequenceTag> Parse(TagItem);

  // Indicates the media sequence number to assign to the first media segment in
  // this playlist. These numbers are useful for validating the same media
  // playlist across reloads, but not for synchronizing media segments between
  // playlists.
  types::DecimalInteger number;
};

// Represents the contents of the #EXT-X-DISCONTINUITY-SEQUENCE tag.
struct MEDIA_EXPORT XDiscontinuitySequenceTag {
  static constexpr auto kName = MediaPlaylistTagName::kXDiscontinuitySequence;
  static ParseStatus::Or<XDiscontinuitySequenceTag> Parse(TagItem);

  // Indicates the discontinuity sequence number to assign to the first media
  // segment in this playlist. These numbers are useful for synchronizing
  // between variant stream timelines.
  types::DecimalInteger number;
};

// Represents the contents of the #EXT-X-BYTERANGE tag.
struct MEDIA_EXPORT XByteRangeTag {
  static constexpr auto kName = MediaPlaylistTagName::kXByteRange;
  static ParseStatus::Or<XByteRangeTag> Parse(TagItem);

  types::ByteRangeExpression range;
};

// Represents the contents of the #EXT-X-BITRATE tag.
struct MEDIA_EXPORT XBitrateTag {
  static constexpr auto kName = MediaPlaylistTagName::kXBitrate;
  static ParseStatus::Or<XBitrateTag> Parse(TagItem);

  // The approximate bitrate of the following media segments, (except those that
  // have the EXT-X-BYTERANGE tag) expressed in kilobits per second. The value
  // must be within +-10% of the actual segment bitrate.
  types::DecimalInteger bitrate;
};

}  // namespace media::hls

#endif  // MEDIA_FORMATS_HLS_TAGS_H_
