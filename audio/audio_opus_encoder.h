// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_AUDIO_AUDIO_OPUS_ENCODER_H_
#define MEDIA_AUDIO_AUDIO_OPUS_ENCODER_H_

#include <memory>
#include <vector>

#include "base/containers/circular_deque.h"
#include "media/base/audio_bus.h"
#include "media/base/audio_converter.h"
#include "media/base/audio_encoder.h"
#include "media/base/audio_timestamp_helper.h"
#include "third_party/opus/src/include/opus.h"

namespace media {

using OpusEncoderDeleterType = void (*)(OpusEncoder* encoder_ptr);
using OwnedOpusEncoder = std::unique_ptr<OpusEncoder, OpusEncoderDeleterType>;

// Performs Opus encoding of the input audio. The input audio is converted to a
// a format suitable for Opus before it is passed to the libopus encoder
// instance to do the actual encoding.
class MEDIA_EXPORT AudioOpusEncoder : public AudioEncoder {
 public:
  AudioOpusEncoder();
  AudioOpusEncoder(const AudioOpusEncoder&) = delete;
  AudioOpusEncoder& operator=(const AudioOpusEncoder&) = delete;
  ~AudioOpusEncoder() override;

  // AudioEncoder:
  void Initialize(const Options& options,
                  OutputCB output_callback,
                  EncoderStatusCB done_cb) override;

  void Encode(std::unique_ptr<AudioBus> audio_bus,
              base::TimeTicks capture_time,
              EncoderStatusCB done_cb) override;

  void Flush(EncoderStatusCB done_cb) override;

  static constexpr int kMinBitrate = 6000;

 private:
  class InputFramesFifo;

  // Called synchronously by Encode() once enough audio frames have been
  // buffered in |fifo_|. Calls libopus to do actual encoding.
  void OnEnoughInputFrames();

  CodecDescription PrepareExtraData();

  EncoderStatus::Or<OwnedOpusEncoder> CreateOpusEncoder();

  AudioParameters input_params_;

  // Output parameters after audio conversion. This may differ from the input
  // params in the number of channels, sample rate, and the frames per buffer.
  // (See CreateOpusInputParams() in the .cc file for details).
  AudioParameters converted_params_;

  // Minimal amount of frames needed to satisfy one convert call.
  int min_input_frames_needed_;

  // Sample rate adapter from the input audio to what OpusEncoder desires.
  // Note: Must outlive |fifo_|.
  std::unique_ptr<AudioConverter> converter_;

  // Fifo for holding the original input audio before it goes to the
  // converter.
  // Note: Must be destroyed before |converter_|.
  std::unique_ptr<InputFramesFifo> fifo_;

  // This is the destination AudioBus where the |converter_| teh audio into.
  std::unique_ptr<AudioBus> converted_audio_bus_;

  // Buffer for passing AudioBus data from the converter to the encoder.
  std::vector<float> buffer_;

  // The actual libopus encoder instance. This is nullptr if creating the
  // encoder fails.
  OwnedOpusEncoder opus_encoder_;

  // Keeps track of the timestamps for the each |output_callback_|
  std::unique_ptr<AudioTimestampHelper> timestamp_tracker_;

  // Callback for reporting completion and status of the current Flush() or
  // Encoder()
  EncoderStatusCB current_done_cb_;

  // True if the next output needs to have extra_data in it, only happens once.
  bool need_to_emit_extra_data_ = true;
};

}  // namespace media

#endif  // MEDIA_AUDIO_AUDIO_OPUS_ENCODER_H_
