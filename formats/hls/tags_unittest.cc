// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/formats/hls/tags.h"

#include <utility>

#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "media/formats/hls/items.h"
#include "media/formats/hls/parse_status.h"
#include "media/formats/hls/source_string.h"
#include "media/formats/hls/test_util.h"
#include "media/formats/hls/variable_dictionary.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace media::hls {

namespace {

// Returns the maximum whole quantity of seconds that can be represented by this
// implementation.
types::DecimalInteger MaxSeconds() {
  return base::TimeDelta::FiniteMax().InSeconds();
}

template <typename T>
void ErrorTest(absl::optional<base::StringPiece> content,
               ParseStatusCode expected_status,
               const base::Location& from = base::Location::Current()) {
  auto tag = content ? TagItem::Create(ToTagName(T::kName),
                                       SourceString::CreateForTesting(*content))
                     : TagItem::CreateEmpty(ToTagName(T::kName), 1);
  auto result = T::Parse(tag);
  ASSERT_TRUE(result.has_error()) << from.ToString();
  auto error = std::move(result).error();
  EXPECT_EQ(error.code(), expected_status) << from.ToString();
}

template <typename T>
void ErrorTest(absl::optional<base::StringPiece> content,
               const VariableDictionary& variable_dict,
               VariableDictionary::SubstitutionBuffer& sub_buffer,
               ParseStatusCode expected_status,
               const base::Location& from = base::Location::Current()) {
  auto tag = content ? TagItem::Create(ToTagName(T::kName),
                                       SourceString::CreateForTesting(*content))
                     : TagItem::CreateEmpty(ToTagName(T::kName), 1);
  auto result = T::Parse(tag, variable_dict, sub_buffer);
  ASSERT_TRUE(result.has_error()) << from.ToString();
  auto error = std::move(result).error();
  EXPECT_EQ(error.code(), expected_status) << from.ToString();
}

template <typename T>
struct OkTestResult {
  T tag;

  // `tag` may have references to this string. To avoid UAF when moving
  // small strings we wrap it in a `std::unique_ptr`.
  std::unique_ptr<std::string> source;
};

template <typename T>
OkTestResult<T> OkTest(absl::optional<std::string> content,
                       const base::Location& from = base::Location::Current()) {
  std::unique_ptr<std::string> source =
      content ? std::make_unique<std::string>(std::move(*content)) : nullptr;
  auto tag = source ? TagItem::Create(ToTagName(T::kName),
                                      SourceString::CreateForTesting(*source))
                    : TagItem::CreateEmpty(ToTagName(T::kName), 1);
  auto result = T::Parse(tag);
  EXPECT_TRUE(result.has_value()) << from.ToString();
  return OkTestResult<T>{.tag = std::move(result).value(),
                         .source = std::move(source)};
}

template <typename T>
OkTestResult<T> OkTest(absl::optional<std::string> content,
                       const VariableDictionary& variable_dict,
                       VariableDictionary::SubstitutionBuffer& sub_buffer,
                       const base::Location& from = base::Location::Current()) {
  auto source =
      content ? std::make_unique<std::string>(std::move(*content)) : nullptr;
  auto tag = source ? TagItem::Create(ToTagName(T::kName),
                                      SourceString::CreateForTesting(*source))
                    : TagItem::CreateEmpty(ToTagName(T::kName), 1);
  auto result = T::Parse(tag, variable_dict, sub_buffer);
  CHECK(result.has_value()) << from.ToString();
  return OkTestResult<T>{.tag = std::move(result).value(),
                         .source = std::move(source)};
}

// Helper to test identification of this tag in a manifest.
// `line` must be a sample line containing this tag, and must end with a
// newline. This DOES NOT parse the item content (only that the item content
// matches what was expected), use `OkTest` and `ErrorTest` for that.
template <typename T>
void RunTagIdenficationTest(
    base::StringPiece line,
    absl::optional<base::StringPiece> expected_content,
    const base::Location& from = base::Location::Current()) {
  auto iter = SourceLineIterator(line);
  auto item_result = GetNextLineItem(&iter);
  ASSERT_TRUE(item_result.has_value()) << from.ToString();

  auto item = std::move(item_result).value();
  auto* tag = absl::get_if<TagItem>(&item);
  ASSERT_NE(tag, nullptr) << from.ToString();
  EXPECT_EQ(tag->GetName(), ToTagName(T::kName)) << from.ToString();
  EXPECT_EQ(tag->GetContent().has_value(), expected_content.has_value())
      << from.ToString();
  if (tag->GetContent().has_value() && expected_content.has_value()) {
    EXPECT_EQ(tag->GetContent()->Str(), *expected_content) << from.ToString();
  }
}

// Test helper for tags which are expected to have no content
template <typename T>
void RunEmptyTagTest() {
  // Empty content is the only allowed content
  OkTest<T>(absl::nullopt);

  // Test with non-empty content
  ErrorTest<T>("", ParseStatusCode::kMalformedTag);
  ErrorTest<T>(" ", ParseStatusCode::kMalformedTag);
  ErrorTest<T>("a", ParseStatusCode::kMalformedTag);
  ErrorTest<T>("1234", ParseStatusCode::kMalformedTag);
  ErrorTest<T>("\t", ParseStatusCode::kMalformedTag);
}

// There are a couple of tags that are defined simply as `#EXT-X-TAG:n` where
// `n` must be a valid DecimalInteger. This helper provides coverage for those
// tags.
template <typename T>
void RunDecimalIntegerTagTest(types::DecimalInteger T::*field) {
  // Content is required
  ErrorTest<T>(absl::nullopt, ParseStatusCode::kMalformedTag);
  ErrorTest<T>("", ParseStatusCode::kMalformedTag);

  // Content must be a valid decimal-integer
  ErrorTest<T>("-1", ParseStatusCode::kMalformedTag);
  ErrorTest<T>("-1.5", ParseStatusCode::kMalformedTag);
  ErrorTest<T>("-.5", ParseStatusCode::kMalformedTag);
  ErrorTest<T>(".5", ParseStatusCode::kMalformedTag);
  ErrorTest<T>("0.5", ParseStatusCode::kMalformedTag);
  ErrorTest<T>("one", ParseStatusCode::kMalformedTag);
  ErrorTest<T>(" 1 ", ParseStatusCode::kMalformedTag);
  ErrorTest<T>("1,", ParseStatusCode::kMalformedTag);
  ErrorTest<T>("{$X}", ParseStatusCode::kMalformedTag);

  auto result = OkTest<T>("0");
  EXPECT_EQ(result.tag.*field, 0u);
  result = OkTest<T>("1");
  EXPECT_EQ(result.tag.*field, 1u);
  result = OkTest<T>("10");
  EXPECT_EQ(result.tag.*field, 10u);
  result = OkTest<T>("14");
  EXPECT_EQ(result.tag.*field, 14u);
}

VariableDictionary CreateBasicDictionary(
    const base::Location& from = base::Location::Current()) {
  VariableDictionary dict;
  EXPECT_TRUE(dict.Insert(CreateVarName("FOO"), "bar")) << from.ToString();
  EXPECT_TRUE(dict.Insert(CreateVarName("BAR"), "baz")) << from.ToString();

  return dict;
}

}  // namespace

TEST(HlsTagsTest, TagNameIdentity) {
  std::set<base::StringPiece> names;

  for (TagName name = kMinTagName; name <= kMaxTagName; ++name) {
    auto name_str = TagNameToString(name);

    // Name must be unique
    EXPECT_EQ(names.find(name_str), names.end());
    names.insert(name_str);

    // Name must parse to the original constant
    EXPECT_EQ(ParseTagName(name_str), name);
  }
}

TEST(HlsTagsTest, ParseM3uTag) {
  RunTagIdenficationTest<M3uTag>("#EXTM3U\n", absl::nullopt);
  RunEmptyTagTest<M3uTag>();
}

TEST(HlsTagsTest, ParseXVersionTag) {
  RunTagIdenficationTest<XVersionTag>("#EXT-X-VERSION:123\n", "123");

  // Test valid versions
  auto result = OkTest<XVersionTag>("1");
  EXPECT_EQ(result.tag.version, 1u);
  result = OkTest<XVersionTag>("2");
  EXPECT_EQ(result.tag.version, 2u);
  result = OkTest<XVersionTag>("3");
  EXPECT_EQ(result.tag.version, 3u);
  result = OkTest<XVersionTag>("4");
  EXPECT_EQ(result.tag.version, 4u);
  result = OkTest<XVersionTag>("5");
  EXPECT_EQ(result.tag.version, 5u);
  result = OkTest<XVersionTag>("6");
  EXPECT_EQ(result.tag.version, 6u);
  result = OkTest<XVersionTag>("7");
  EXPECT_EQ(result.tag.version, 7u);
  result = OkTest<XVersionTag>("8");
  EXPECT_EQ(result.tag.version, 8u);
  result = OkTest<XVersionTag>("9");
  EXPECT_EQ(result.tag.version, 9u);
  result = OkTest<XVersionTag>("10");
  EXPECT_EQ(result.tag.version, 10u);

  // While unsupported playlist versions are rejected, that's NOT the
  // responsibility of this tag parsing function. The playlist should be
  // rejected at a higher level.
  result = OkTest<XVersionTag>("99999");
  EXPECT_EQ(result.tag.version, 99999u);

  // Test invalid versions
  ErrorTest<XVersionTag>(absl::nullopt, ParseStatusCode::kMalformedTag);
  ErrorTest<XVersionTag>("", ParseStatusCode::kMalformedTag);
  ErrorTest<XVersionTag>("0", ParseStatusCode::kInvalidPlaylistVersion);
  ErrorTest<XVersionTag>("-1", ParseStatusCode::kMalformedTag);
  ErrorTest<XVersionTag>("1.0", ParseStatusCode::kMalformedTag);
  ErrorTest<XVersionTag>("asdf", ParseStatusCode::kMalformedTag);
  ErrorTest<XVersionTag>("  1 ", ParseStatusCode::kMalformedTag);
}

TEST(HlsTagsTest, ParseInfTag) {
  RunTagIdenficationTest<InfTag>("#EXTINF:123,\t\n", "123,\t");

  // Test some valid tags
  auto result = OkTest<InfTag>("123,");
  EXPECT_TRUE(RoughlyEqual(result.tag.duration, base::Seconds(123.0)));
  EXPECT_EQ(result.tag.title.Str(), "");

  result = OkTest<InfTag>("1.23,");
  EXPECT_TRUE(RoughlyEqual(result.tag.duration, base::Seconds(1.23)));
  EXPECT_EQ(result.tag.title.Str(), "");

  // The spec implies that whitespace characters like this usually aren't
  // permitted, but "\t" is a common occurrence for the title value.
  result = OkTest<InfTag>("99.5,\t");
  EXPECT_TRUE(RoughlyEqual(result.tag.duration, base::Seconds(99.5)));
  EXPECT_EQ(result.tag.title.Str(), "\t");

  result = OkTest<InfTag>("9.5,,,,");
  EXPECT_TRUE(RoughlyEqual(result.tag.duration, base::Seconds(9.5)));
  EXPECT_EQ(result.tag.title.Str(), ",,,");

  result = OkTest<InfTag>("12,asdfsdf   ");
  EXPECT_TRUE(RoughlyEqual(result.tag.duration, base::Seconds(12.0)));
  EXPECT_EQ(result.tag.title.Str(), "asdfsdf   ");

  // Test some invalid tags
  ErrorTest<InfTag>(absl::nullopt, ParseStatusCode::kMalformedTag);
  ErrorTest<InfTag>("", ParseStatusCode::kMalformedTag);
  ErrorTest<InfTag>(",", ParseStatusCode::kMalformedTag);
  ErrorTest<InfTag>("-123,", ParseStatusCode::kMalformedTag);
  ErrorTest<InfTag>("123", ParseStatusCode::kMalformedTag);
  ErrorTest<InfTag>("asdf,", ParseStatusCode::kMalformedTag);

  // Test max value
  result = OkTest<InfTag>(base::NumberToString(MaxSeconds()) + ",\t");
  EXPECT_TRUE(RoughlyEqual(result.tag.duration, base::Seconds(MaxSeconds())));
  ErrorTest<InfTag>(base::NumberToString(MaxSeconds() + 1) + ",\t",
                    ParseStatusCode::kValueOverflowsTimeDelta);
}

TEST(HlsTagsTest, ParseXIndependentSegmentsTag) {
  RunTagIdenficationTest<XIndependentSegmentsTag>(
      "#EXT-X-INDEPENDENT-SEGMENTS\n", absl::nullopt);
  RunEmptyTagTest<XIndependentSegmentsTag>();
}

TEST(HlsTagsTest, ParseXEndListTag) {
  RunTagIdenficationTest<XEndListTag>("#EXT-X-ENDLIST\n", absl::nullopt);
  RunEmptyTagTest<XEndListTag>();
}

TEST(HlsTagsTest, ParseXIFramesOnlyTag) {
  RunTagIdenficationTest<XIFramesOnlyTag>("#EXT-X-I-FRAMES-ONLY\n",
                                          absl::nullopt);
  RunEmptyTagTest<XIFramesOnlyTag>();
}

TEST(HlsTagsTest, ParseXDiscontinuityTag) {
  RunTagIdenficationTest<XDiscontinuityTag>("#EXT-X-DISCONTINUITY\n",
                                            absl::nullopt);
  RunEmptyTagTest<XDiscontinuityTag>();
}

TEST(HlsTagsTest, ParseXGapTag) {
  RunTagIdenficationTest<XGapTag>("#EXT-X-GAP\n", absl::nullopt);
  RunEmptyTagTest<XGapTag>();
}

TEST(HlsTagsTest, ParseXDefineTag) {
  RunTagIdenficationTest<XDefineTag>(
      "#EXT-X-DEFINE:NAME=\"FOO\",VALUE=\"Bar\",\n",
      "NAME=\"FOO\",VALUE=\"Bar\",");

  // Test some valid inputs
  auto result = OkTest<XDefineTag>(R"(NAME="Foo",VALUE="bar",)");
  EXPECT_EQ(result.tag.name.GetName(), "Foo");
  EXPECT_TRUE(result.tag.value.has_value());
  EXPECT_EQ(result.tag.value.value(), "bar");

  result = OkTest<XDefineTag>(R"(VALUE="90/12#%)(zx./",NAME="Hello12_-")");
  EXPECT_EQ(result.tag.name.GetName(), "Hello12_-");
  EXPECT_TRUE(result.tag.value.has_value());
  EXPECT_EQ(result.tag.value.value(), "90/12#%)(zx./");

  result = OkTest<XDefineTag>(R"(IMPORT="-F90_Baz")");
  EXPECT_EQ(result.tag.name.GetName(), "-F90_Baz");
  EXPECT_FALSE(result.tag.value.has_value());

  // IMPORT and VALUE are not currently considered an error
  result = OkTest<XDefineTag>(R"(IMPORT="F00_Bar",VALUE="Test")");
  EXPECT_EQ(result.tag.name.GetName(), "F00_Bar");
  EXPECT_FALSE(result.tag.value.has_value());

  // NAME with empty value is allowed
  result = OkTest<XDefineTag>(R"(NAME="HELLO",VALUE="")");
  EXPECT_EQ(result.tag.name.GetName(), "HELLO");
  EXPECT_TRUE(result.tag.value.has_value());
  EXPECT_EQ(result.tag.value.value(), "");

  // Empty content is not allowed
  ErrorTest<XDefineTag>(absl::nullopt, ParseStatusCode::kMalformedTag);
  ErrorTest<XDefineTag>("", ParseStatusCode::kMalformedTag);

  // NAME and IMPORT are NOT allowed
  ErrorTest<XDefineTag>(R"(NAME="Foo",IMPORT="Foo")",
                        ParseStatusCode::kMalformedTag);

  // Name without VALUE is NOT allowed
  ErrorTest<XDefineTag>(R"(NAME="Foo",)", ParseStatusCode::kMalformedTag);

  // Empty NAME is not allowed
  ErrorTest<XDefineTag>(R"(NAME="",VALUE="Foo")",
                        ParseStatusCode::kMalformedTag);

  // Empty IMPORT is not allowed
  ErrorTest<XDefineTag>(R"(IMPORT="")", ParseStatusCode::kMalformedTag);

  // Non-valid NAME is not allowed
  ErrorTest<XDefineTag>(R"(NAME=".FOO",VALUE="Foo")",
                        ParseStatusCode::kMalformedTag);
  ErrorTest<XDefineTag>(R"(NAME="F++OO",VALUE="Foo")",
                        ParseStatusCode::kMalformedTag);
  ErrorTest<XDefineTag>(R"(NAME=" FOO",VALUE="Foo")",
                        ParseStatusCode::kMalformedTag);
  ErrorTest<XDefineTag>(R"(NAME="FOO ",VALUE="Foo")",
                        ParseStatusCode::kMalformedTag);
}

TEST(HlsTagsTest, ParseXPlaylistTypeTag) {
  RunTagIdenficationTest<XPlaylistTypeTag>("#EXT-X-PLAYLIST-TYPE:VOD\n", "VOD");
  RunTagIdenficationTest<XPlaylistTypeTag>("#EXT-X-PLAYLIST-TYPE:EVENT\n",
                                           "EVENT");

  auto result = OkTest<XPlaylistTypeTag>("EVENT");
  EXPECT_EQ(result.tag.type, PlaylistType::kEvent);
  result = OkTest<XPlaylistTypeTag>("VOD");
  EXPECT_EQ(result.tag.type, PlaylistType::kVOD);

  ErrorTest<XPlaylistTypeTag>("FOOBAR", ParseStatusCode::kUnknownPlaylistType);
  ErrorTest<XPlaylistTypeTag>("EEVENT", ParseStatusCode::kUnknownPlaylistType);
  ErrorTest<XPlaylistTypeTag>(" EVENT", ParseStatusCode::kUnknownPlaylistType);
  ErrorTest<XPlaylistTypeTag>("EVENT ", ParseStatusCode::kUnknownPlaylistType);
  ErrorTest<XPlaylistTypeTag>("", ParseStatusCode::kMalformedTag);
  ErrorTest<XPlaylistTypeTag>(absl::nullopt, ParseStatusCode::kMalformedTag);
}

TEST(HlsTagsTest, ParseXStreamInfTag) {
  RunTagIdenficationTest<XStreamInfTag>(
      "#EXT-X-STREAM-INF:BANDWIDTH=1010,CODECS=\"foo,bar\"\n",
      "BANDWIDTH=1010,CODECS=\"foo,bar\"");

  VariableDictionary variable_dict = CreateBasicDictionary();
  VariableDictionary::SubstitutionBuffer sub_buffer;

  auto result = OkTest<XStreamInfTag>(
      R"(BANDWIDTH=1010,AVERAGE-BANDWIDTH=1000,CODECS="foo,bar",SCORE=12.2)",
      variable_dict, sub_buffer);
  EXPECT_EQ(result.tag.bandwidth, 1010u);
  EXPECT_EQ(result.tag.average_bandwidth, 1000u);
  EXPECT_DOUBLE_EQ(result.tag.score.value(), 12.2);
  EXPECT_EQ(result.tag.codecs, "foo,bar");
  EXPECT_EQ(result.tag.resolution, absl::nullopt);
  EXPECT_EQ(result.tag.frame_rate, absl::nullopt);

  result = OkTest<XStreamInfTag>(
      R"(BANDWIDTH=1010,RESOLUTION=1920x1080,FRAME-RATE=29.97)", variable_dict,
      sub_buffer);
  EXPECT_EQ(result.tag.bandwidth, 1010u);
  EXPECT_EQ(result.tag.average_bandwidth, absl::nullopt);
  EXPECT_EQ(result.tag.score, absl::nullopt);
  EXPECT_EQ(result.tag.codecs, absl::nullopt);
  ASSERT_TRUE(result.tag.resolution.has_value());
  EXPECT_EQ(result.tag.resolution->width, 1920u);
  EXPECT_EQ(result.tag.resolution->height, 1080u);
  EXPECT_DOUBLE_EQ(result.tag.frame_rate.value(), 29.97);

  // "BANDWIDTH" is the only required attribute
  result =
      OkTest<XStreamInfTag>(R"(BANDWIDTH=5050)", variable_dict, sub_buffer);
  EXPECT_EQ(result.tag.bandwidth, 5050u);
  EXPECT_EQ(result.tag.average_bandwidth, absl::nullopt);
  EXPECT_EQ(result.tag.score, absl::nullopt);
  EXPECT_EQ(result.tag.codecs, absl::nullopt);
  EXPECT_EQ(result.tag.resolution, absl::nullopt);
  EXPECT_EQ(result.tag.frame_rate, absl::nullopt);

  ErrorTest<XStreamInfTag>(absl::nullopt, variable_dict, sub_buffer,
                           ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>("", variable_dict, sub_buffer,
                           ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(CODECS="foo,bar")", variable_dict, sub_buffer,
                           ParseStatusCode::kMalformedTag);

  // "BANDWIDTH" must be a valid DecimalInteger (non-negative)
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH="111")", variable_dict, sub_buffer,
                           ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=-1)", variable_dict, sub_buffer,
                           ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1.5)", variable_dict, sub_buffer,
                           ParseStatusCode::kMalformedTag);

  // "AVERAGE-BANDWIDTH" must be a valid DecimalInteger (non-negative)
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,AVERAGE-BANDWIDTH="111")",
                           variable_dict, sub_buffer,
                           ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,AVERAGE-BANDWIDTH=-1)",
                           variable_dict, sub_buffer,
                           ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,AVERAGE-BANDWIDTH=1.5)",
                           variable_dict, sub_buffer,
                           ParseStatusCode::kMalformedTag);

  // "SCORE" must be a valid DecimalFloatingPoint (non-negative)
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,SCORE="1")", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,SCORE=-1)", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,SCORE=ONE)", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);

  // "CODECS" must be a valid string
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,CODECS=abc,123)", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,CODECS=abc)", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,CODECS=123)", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,CODECS="")", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);

  // "CODECS" is subject to variable substitution
  result = OkTest<XStreamInfTag>(R"(BANDWIDTH=1010,CODECS="{$FOO},{$BAR}")",
                                 variable_dict, sub_buffer);
  EXPECT_EQ(result.tag.bandwidth, 1010u);
  EXPECT_EQ(result.tag.average_bandwidth, absl::nullopt);
  EXPECT_EQ(result.tag.score, absl::nullopt);
  EXPECT_EQ(result.tag.codecs, "bar,baz");
  EXPECT_EQ(result.tag.resolution, absl::nullopt);

  // "RESOLUTION" must be a valid decimal-resolution
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,RESOLUTION=1920x)", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,RESOLUTION=x123)", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);

  // "FRAME-RATE" must be a valid decimal-floating-point (unsigned)
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,FRAME-RATE=-1)", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,FRAME-RATE=One)", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);
  ErrorTest<XStreamInfTag>(R"(BANDWIDTH=1010,FRAME-RATE=30.0.0)", variable_dict,
                           sub_buffer, ParseStatusCode::kMalformedTag);
}

TEST(HlsTagsTest, ParseXTargetDurationTag) {
  RunTagIdenficationTest<XTargetDurationTag>("#EXT-X-TARGETDURATION:10\n",
                                             "10");

  // Content must be a valid decimal-integer
  ErrorTest<XTargetDurationTag>(absl::nullopt, ParseStatusCode::kMalformedTag);
  ErrorTest<XTargetDurationTag>("", ParseStatusCode::kMalformedTag);
  ErrorTest<XTargetDurationTag>("-1", ParseStatusCode::kMalformedTag);
  ErrorTest<XTargetDurationTag>("1.5", ParseStatusCode::kMalformedTag);
  ErrorTest<XTargetDurationTag>(" 1", ParseStatusCode::kMalformedTag);
  ErrorTest<XTargetDurationTag>("1 ", ParseStatusCode::kMalformedTag);
  ErrorTest<XTargetDurationTag>("one", ParseStatusCode::kMalformedTag);
  ErrorTest<XTargetDurationTag>("{$ONE}", ParseStatusCode::kMalformedTag);
  ErrorTest<XTargetDurationTag>("1,", ParseStatusCode::kMalformedTag);

  auto result = OkTest<XTargetDurationTag>("0");
  EXPECT_TRUE(RoughlyEqual(result.tag.duration, base::Seconds(0)));

  result = OkTest<XTargetDurationTag>("1");
  EXPECT_TRUE(RoughlyEqual(result.tag.duration, base::Seconds(1)));

  result = OkTest<XTargetDurationTag>("99");
  EXPECT_TRUE(RoughlyEqual(result.tag.duration, base::Seconds(99)));

  // Test max value
  result = OkTest<XTargetDurationTag>(base::NumberToString(MaxSeconds()));
  EXPECT_EQ(result.tag.duration, base::Seconds(MaxSeconds()));
  ErrorTest<XTargetDurationTag>(base::NumberToString(MaxSeconds() + 1),
                                ParseStatusCode::kValueOverflowsTimeDelta);
}

TEST(HlsTagsTest, ParseXMediaSequenceTag) {
  RunTagIdenficationTest<XMediaSequenceTag>("#EXT-X-MEDIA-SEQUENCE:3\n", "3");
  RunDecimalIntegerTagTest(&XMediaSequenceTag::number);
}

TEST(HlsTagsTest, ParseXDiscontinuitySequenceTag) {
  RunTagIdenficationTest<XDiscontinuitySequenceTag>(
      "#EXT-X-DISCONTINUITY-SEQUENCE:3\n", "3");
  RunDecimalIntegerTagTest(&XDiscontinuitySequenceTag::number);
}

TEST(HlsTagsTest, ParseXByteRangeTag) {
  RunTagIdenficationTest<XByteRangeTag>("#EXT-X-BYTERANGE:12@34\n", "12@34");

  auto result = OkTest<XByteRangeTag>("12");
  EXPECT_EQ(result.tag.range.length, 12u);
  EXPECT_EQ(result.tag.range.offset, absl::nullopt);
  result = OkTest<XByteRangeTag>("12@34");
  EXPECT_EQ(result.tag.range.length, 12u);
  EXPECT_EQ(result.tag.range.offset, 34u);

  ErrorTest<XByteRangeTag>("FOOBAR", ParseStatusCode::kMalformedTag);
  ErrorTest<XByteRangeTag>("12@", ParseStatusCode::kMalformedTag);
  ErrorTest<XByteRangeTag>("@34", ParseStatusCode::kMalformedTag);
  ErrorTest<XByteRangeTag>("@", ParseStatusCode::kMalformedTag);
  ErrorTest<XByteRangeTag>(" 12@34", ParseStatusCode::kMalformedTag);
  ErrorTest<XByteRangeTag>("12@34 ", ParseStatusCode::kMalformedTag);
  ErrorTest<XByteRangeTag>("", ParseStatusCode::kMalformedTag);
  ErrorTest<XByteRangeTag>(absl::nullopt, ParseStatusCode::kMalformedTag);
}

TEST(HlsTagsTest, ParseXBitrateTag) {
  RunTagIdenficationTest<XBitrateTag>("#EXT-X-BITRATE:3\n", "3");
  RunDecimalIntegerTagTest(&XBitrateTag::bitrate);
}

TEST(HlsTagsTest, ParseXPartInfTag) {
  RunTagIdenficationTest<XPartInfTag>("#EXT-X-PART-INF:PART-TARGET=1.0\n",
                                      "PART-TARGET=1.0");

  // PART-TARGET is required, and must be a valid DecimalFloatingPoint
  ErrorTest<XPartInfTag>(absl::nullopt, ParseStatusCode::kMalformedTag);
  ErrorTest<XPartInfTag>("", ParseStatusCode::kMalformedTag);
  ErrorTest<XPartInfTag>("1", ParseStatusCode::kMalformedTag);
  ErrorTest<XPartInfTag>("PART-TARGET=-1", ParseStatusCode::kMalformedTag);
  ErrorTest<XPartInfTag>("PART-TARGET={$part-target}",
                         ParseStatusCode::kMalformedTag);
  ErrorTest<XPartInfTag>("PART-TARGET=\"1\"", ParseStatusCode::kMalformedTag);
  ErrorTest<XPartInfTag>("PART-TARGET=one", ParseStatusCode::kMalformedTag);
  ErrorTest<XPartInfTag>("FOO=BAR", ParseStatusCode::kMalformedTag);
  ErrorTest<XPartInfTag>("PART-TARGET=10,PART-TARGET=10",
                         ParseStatusCode::kMalformedTag);

  auto result = OkTest<XPartInfTag>("PART-TARGET=1.2");
  EXPECT_TRUE(RoughlyEqual(result.tag.target_duration, base::Seconds(1.2)));
  result = OkTest<XPartInfTag>("PART-TARGET=1");
  EXPECT_TRUE(RoughlyEqual(result.tag.target_duration, base::Seconds(1)));
  result = OkTest<XPartInfTag>("PART-TARGET=0");
  EXPECT_TRUE(RoughlyEqual(result.tag.target_duration, base::Seconds(0)));
  result = OkTest<XPartInfTag>("FOO=BAR,PART-TARGET=100,BAR=BAZ");
  EXPECT_TRUE(RoughlyEqual(result.tag.target_duration, base::Seconds(100)));

  // Test the max value
  result =
      OkTest<XPartInfTag>("PART-TARGET=" + base::NumberToString(MaxSeconds()));
  EXPECT_TRUE(
      RoughlyEqual(result.tag.target_duration, base::Seconds(MaxSeconds())));
  ErrorTest<XPartInfTag>(
      "PART-TARGET=" + base::NumberToString(MaxSeconds() + 1),
      ParseStatusCode::kValueOverflowsTimeDelta);
}

TEST(HlsTagsTest, ParseXServerControlTag) {
  RunTagIdenficationTest<XServerControlTag>(
      "#EXT-X-SERVER-CONTROL:SKIP-UNTIL=10\n", "SKIP-UNTIL=10");

  // Tag requires content
  ErrorTest<XServerControlTag>(absl::nullopt, ParseStatusCode::kMalformedTag);

  // Content is allowed to be empty
  auto result = OkTest<XServerControlTag>("");
  EXPECT_EQ(result.tag.skip_boundary, absl::nullopt);
  EXPECT_EQ(result.tag.can_skip_dateranges, false);
  EXPECT_EQ(result.tag.hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.part_hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.can_block_reload, false);

  result = OkTest<XServerControlTag>(
      "CAN-SKIP-UNTIL=50,CAN-SKIP-DATERANGES=YES,HOLD-BACK=60,PART-HOLD-BACK="
      "40,CAN-BLOCK-RELOAD=YES,FUTURE-PROOF=YES");
  EXPECT_TRUE(RoughlyEqual(result.tag.skip_boundary, base::Seconds(50)));
  EXPECT_EQ(result.tag.can_skip_dateranges, true);
  EXPECT_TRUE(RoughlyEqual(result.tag.hold_back, base::Seconds(60)));
  EXPECT_TRUE(RoughlyEqual(result.tag.part_hold_back, base::Seconds(40)));
  EXPECT_EQ(result.tag.can_block_reload, true);

  ErrorTest<XServerControlTag>("CAN-SKIP-UNTIL=-5",
                               ParseStatusCode::kMalformedTag);
  ErrorTest<XServerControlTag>("CAN-SKIP-UNTIL={$B}",
                               ParseStatusCode::kMalformedTag);
  ErrorTest<XServerControlTag>("CAN-SKIP-UNTIL=\"5\"",
                               ParseStatusCode::kMalformedTag);

  result = OkTest<XServerControlTag>("CAN-SKIP-UNTIL=5");
  EXPECT_TRUE(RoughlyEqual(result.tag.skip_boundary, base::Seconds(5)));
  EXPECT_EQ(result.tag.can_skip_dateranges, false);
  EXPECT_EQ(result.tag.hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.part_hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.can_block_reload, false);

  // Test max value
  result = OkTest<XServerControlTag>("CAN-SKIP-UNTIL=" +
                                     base::NumberToString(MaxSeconds()));
  EXPECT_TRUE(
      RoughlyEqual(result.tag.skip_boundary, base::Seconds(MaxSeconds())));
  EXPECT_EQ(result.tag.can_skip_dateranges, false);
  EXPECT_EQ(result.tag.hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.part_hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.can_block_reload, false);

  ErrorTest<XServerControlTag>(
      "CAN-SKIP-UNTIL=" + base::NumberToString(MaxSeconds() + 1),
      ParseStatusCode::kValueOverflowsTimeDelta);

  // 'CAN-SKIP-DATERANGES' requires the presence of 'CAN-SKIP-UNTIL'
  ErrorTest<XServerControlTag>("CAN-SKIP-DATERANGES=YES",
                               ParseStatusCode::kMalformedTag);
  result =
      OkTest<XServerControlTag>("CAN-SKIP-DATERANGES=YES,CAN-SKIP-UNTIL=50");
  EXPECT_TRUE(RoughlyEqual(result.tag.skip_boundary, base::Seconds(50)));
  EXPECT_EQ(result.tag.can_skip_dateranges, true);
  EXPECT_EQ(result.tag.hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.part_hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.can_block_reload, false);

  // The only value that results in `true` is "YES"
  for (std::string x : {"NO", "Y", "TRUE", "1", "yes"}) {
    result = OkTest<XServerControlTag>("CAN-SKIP-DATERANGES=" + x +
                                       ",CAN-SKIP-UNTIL=50");
    EXPECT_TRUE(RoughlyEqual(result.tag.skip_boundary, base::Seconds(50)));
    EXPECT_EQ(result.tag.can_skip_dateranges, false);
    EXPECT_EQ(result.tag.hold_back, absl::nullopt);
    EXPECT_EQ(result.tag.part_hold_back, absl::nullopt);
    EXPECT_EQ(result.tag.can_block_reload, false);
  }

  // 'HOLD-BACK' must be a valid DecimalFloatingPoint
  ErrorTest<XServerControlTag>("HOLD-BACK=-5", ParseStatusCode::kMalformedTag);
  ErrorTest<XServerControlTag>("HOLD-BACK={$B}",
                               ParseStatusCode::kMalformedTag);
  ErrorTest<XServerControlTag>("HOLD-BACK=\"5\"",
                               ParseStatusCode::kMalformedTag);

  result = OkTest<XServerControlTag>("HOLD-BACK=50");
  EXPECT_EQ(result.tag.skip_boundary, absl::nullopt);
  EXPECT_EQ(result.tag.can_skip_dateranges, false);
  EXPECT_TRUE(RoughlyEqual(result.tag.hold_back, base::Seconds(50)));
  EXPECT_EQ(result.tag.part_hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.can_block_reload, false);

  // Test max value
  result = OkTest<XServerControlTag>("HOLD-BACK=" +
                                     base::NumberToString(MaxSeconds()));
  EXPECT_EQ(result.tag.skip_boundary, absl::nullopt);
  EXPECT_EQ(result.tag.can_skip_dateranges, false);
  EXPECT_TRUE(RoughlyEqual(result.tag.hold_back, base::Seconds(MaxSeconds())));
  EXPECT_EQ(result.tag.part_hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.can_block_reload, false);
  ErrorTest<XServerControlTag>(
      "HOLD-BACK=" + base::NumberToString(MaxSeconds() + 1),
      ParseStatusCode::kValueOverflowsTimeDelta);

  // 'PART-HOLD-BACK' must be a valid DecimalFloatingPoint
  ErrorTest<XServerControlTag>("PART-HOLD-BACK=-5",
                               ParseStatusCode::kMalformedTag);
  ErrorTest<XServerControlTag>("PART-HOLD-BACK={$B}",
                               ParseStatusCode::kMalformedTag);
  ErrorTest<XServerControlTag>("PART-HOLD-BACK=\"5\"",
                               ParseStatusCode::kMalformedTag);

  result = OkTest<XServerControlTag>("PART-HOLD-BACK=50");
  EXPECT_EQ(result.tag.skip_boundary, absl::nullopt);
  EXPECT_EQ(result.tag.can_skip_dateranges, false);
  EXPECT_EQ(result.tag.hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.part_hold_back, base::Seconds(50));
  EXPECT_EQ(result.tag.can_block_reload, false);

  // Test max value
  result = OkTest<XServerControlTag>("PART-HOLD-BACK=" +
                                     base::NumberToString(MaxSeconds()));
  EXPECT_EQ(result.tag.skip_boundary, absl::nullopt);
  EXPECT_EQ(result.tag.can_skip_dateranges, false);
  EXPECT_EQ(result.tag.hold_back, absl::nullopt);
  EXPECT_TRUE(
      RoughlyEqual(result.tag.part_hold_back, base::Seconds(MaxSeconds())));
  EXPECT_EQ(result.tag.can_block_reload, false);
  ErrorTest<XServerControlTag>(
      "PART-HOLD-BACK=" + base::NumberToString(MaxSeconds() + 1),
      ParseStatusCode::kValueOverflowsTimeDelta);

  // The only value that results in `true` is "YES"
  for (std::string x : {"NO", "Y", "TRUE", "1", "yes"}) {
    result = OkTest<XServerControlTag>("CAN-BLOCK-RELOAD=" + x);
    EXPECT_EQ(result.tag.skip_boundary, absl::nullopt);
    EXPECT_EQ(result.tag.can_skip_dateranges, false);
    EXPECT_EQ(result.tag.hold_back, absl::nullopt);
    EXPECT_EQ(result.tag.part_hold_back, absl::nullopt);
    EXPECT_EQ(result.tag.can_block_reload, false);
  }

  result = OkTest<XServerControlTag>("CAN-BLOCK-RELOAD=YES");
  EXPECT_EQ(result.tag.skip_boundary, absl::nullopt);
  EXPECT_EQ(result.tag.can_skip_dateranges, false);
  EXPECT_EQ(result.tag.hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.part_hold_back, absl::nullopt);
  EXPECT_EQ(result.tag.can_block_reload, true);
}

}  // namespace media::hls
