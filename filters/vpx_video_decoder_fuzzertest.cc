// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <random>

#include "base/at_exit.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "media/base/decoder_buffer.h"
#include "media/base/media.h"
#include "media/base/media_util.h"
#include "media/filters/vpx_video_decoder.h"

using namespace media;

struct Env {
  Env() {
    InitializeMediaLibrary();
    base::CommandLine::Init(0, nullptr);
    logging::SetMinLogLevel(logging::LOG_FATAL);
  }

  base::AtExitManager at_exit_manager;
  base::MessageLoop message_loop;
};

void OnDecodeComplete(const base::Closure& quit_closure, DecodeStatus status) {
  quit_closure.Run();
}

void OnInitDone(const base::Closure& quit_closure,
                bool* success_dest,
                bool success) {
  *success_dest = success;
  quit_closure.Run();
}

void OnOutputComplete(scoped_refptr<VideoFrame> frame) {}

// Entry point for LibFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Create Env on the first run of LLVMFuzzerTestOneInput otherwise
  // message_loop will be created before this process forks when used with AFL,
  // causing hangs.
  static Env* env = new Env();
  ALLOW_UNUSED_LOCAL(env);
  std::mt19937_64 rng;

  {  // Seed rng from data.
    std::string str = std::string(reinterpret_cast<const char*>(data), size);
    std::size_t data_hash = std::hash<std::string>()(str);
    rng.seed(data_hash);
  }

  // Compute randomized constants. Put all rng() usages here.
  // Use only values that pass DCHECK in VpxVideoDecoder::ConfigureDecoder().
  VideoCodec codec;

  bool has_alpha = false;
  if (rng() & 1) {
    codec = kCodecVP8;
    // non-Alpha VP8 decoding isn't supported by VpxVideoDecoder on Linux.
    has_alpha = true;
  } else {
    codec = kCodecVP9;
    has_alpha = rng() & 1;
  }

  auto profile =
      static_cast<VideoCodecProfile>(rng() % VIDEO_CODEC_PROFILE_MAX);
  auto color_space =
      VideoColorSpace(rng() % 256, rng() % 256, rng() % 256,
                      (rng() & 1) ? gfx::ColorSpace::RangeID::LIMITED
                                  : gfx::ColorSpace::RangeID::FULL);
  auto rotation = static_cast<VideoRotation>(rng() % VIDEO_ROTATION_MAX);
  auto coded_size = gfx::Size(1 + (rng() % 127), 1 + (rng() % 127));
  auto visible_rect = gfx::Rect(coded_size);
  auto natural_size = gfx::Size(1 + (rng() % 127), 1 + (rng() % 127));
  uint8_t reflection = rng() % 4;

  VideoDecoderConfig config(
      codec, profile,
      has_alpha ? VideoDecoderConfig::AlphaMode::kHasAlpha
                : VideoDecoderConfig::AlphaMode::kIsOpaque,
      color_space, VideoTransformation(rotation, reflection), coded_size,
      visible_rect, natural_size, EmptyExtraData(), Unencrypted());

  if (!config.IsValidConfig())
    return 0;

  VpxVideoDecoder decoder;

  {
    base::RunLoop run_loop;
    bool success = false;
    decoder.Initialize(
        config, true /* low_delay */, nullptr /* cdm_context */,
        base::Bind(&OnInitDone, run_loop.QuitClosure(), &success),
        base::Bind(&OnOutputComplete), base::NullCallback());
    run_loop.Run();
    if (!success)
      return 0;
  }

  {
    base::RunLoop run_loop;
    auto buffer = DecoderBuffer::CopyFrom(data, size);
    decoder.Decode(buffer,
                   base::Bind(&OnDecodeComplete, run_loop.QuitClosure()));
    run_loop.Run();
  }

  return 0;
}
