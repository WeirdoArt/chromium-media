// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/test/video_player/video_player.h"

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "media/gpu/macros.h"
#include "media/gpu/test/video.h"
#include "media/gpu/test/video_player/frame_renderer_dummy.h"
#include "media/gpu/test/video_player/video_decoder_client.h"

namespace media {
namespace test {

namespace {
// Get the name of the specified video player |event|.
const char* EventName(VideoPlayerEvent event) {
  switch (event) {
    case VideoPlayerEvent::kInitialized:
      return "Initialized";
    case VideoPlayerEvent::kFrameDecoded:
      return "FrameDecoded";
    case VideoPlayerEvent::kFlushing:
      return "Flushing";
    case VideoPlayerEvent::kFlushDone:
      return "FlushDone";
    case VideoPlayerEvent::kResetting:
      return "Resetting";
    case VideoPlayerEvent::kResetDone:
      return "ResetDone";
    case VideoPlayerEvent::kConfigInfo:
      return "ConfigInfo";
    default:
      return "Unknown";
  }
}
}  // namespace

VideoPlayer::VideoPlayer() : event_cv_(&event_lock_) {}

VideoPlayer::~VideoPlayer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOGF(4);

  decoder_client_.reset();
}

// static
std::unique_ptr<VideoPlayer> VideoPlayer::Create(
    const VideoDecoderClientConfig& config,
    std::unique_ptr<FrameRendererDummy> frame_renderer,
    std::vector<std::unique_ptr<VideoFrameProcessor>> frame_processors) {
  auto video_player = base::WrapUnique(new VideoPlayer());
  if (!video_player->CreateDecoderClient(config, std::move(frame_renderer),
                                         std::move(frame_processors))) {
    return nullptr;
  }
  return video_player;
}

bool VideoPlayer::CreateDecoderClient(
    const VideoDecoderClientConfig& config,
    std::unique_ptr<FrameRendererDummy> frame_renderer,
    std::vector<std::unique_ptr<VideoFrameProcessor>> frame_processors) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(video_player_state_, VideoPlayerState::kUninitialized);
  DCHECK(frame_renderer);
  DVLOGF(4);

  // base::Unretained is safe here because |decoder_client_| is fully owned.
  EventCallback event_cb =
      base::BindRepeating(&VideoPlayer::NotifyEvent, base::Unretained(this));

  decoder_client_ = VideoDecoderClient::Create(
      event_cb, std::move(frame_renderer), std::move(frame_processors), config);

  LOG_IF(ERROR, !decoder_client_) << __func__ << "(): "
                                  << "Failed to create video decoder client";
  return !!decoder_client_;
}

void VideoPlayer::SetEventWaitTimeout(base::TimeDelta timeout) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOGF(4);

  event_timeout_ = timeout;
}

bool VideoPlayer::Initialize(const Video* video) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(video_player_state_ == VideoPlayerState::kUninitialized ||
         video_player_state_ == VideoPlayerState::kIdle);
  DCHECK(video);
  DVLOGF(4);

  decoder_client_->Initialize(video);

  // Wait until the video decoder is initialized.
  if (!WaitForEvent(VideoPlayerEvent::kInitialized))
    return false;

  video_player_state_ = VideoPlayerState::kIdle;
  return true;
}

void VideoPlayer::Play() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(video_player_state_, VideoPlayerState::kIdle);
  DVLOGF(4);

  // Play until the end of the video.
  PlayUntil(VideoPlayerEvent::kNumEvents);
}

void VideoPlayer::PlayUntil(VideoPlayerEvent event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(video_player_state_, VideoPlayerState::kIdle);
  DVLOGF(4);

  play_until_ = event;
  video_player_state_ = VideoPlayerState::kDecoding;
  decoder_client_->Play();
}

void VideoPlayer::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOGF(4);

  decoder_client_->Reset();
}

void VideoPlayer::Flush() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOGF(4);

  decoder_client_->Flush();
}

bool VideoPlayer::WaitForEvent(VideoPlayerEvent sought_event, size_t times) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_GE(times, 1u);
  DVLOGF(4) << "Event: " << EventName(sought_event);

  base::TimeDelta time_waiting;
  base::AutoLock auto_lock(event_lock_);
  while (true) {
    while (!video_player_events_.empty()) {
      const auto received_event = video_player_events_.front();
      video_player_events_.pop();

      if (received_event == sought_event)
        times--;
      if (times == 0)
        return true;
    }

    // Check whether we've exceeded the maximum time we're allowed to wait.
    if (time_waiting >= event_timeout_) {
      LOG(ERROR) << "Timeout while waiting for '" << EventName(sought_event)
                 << "' event";
      return false;
    }

    const base::TimeTicks start_time = base::TimeTicks::Now();
    event_cv_.TimedWait(event_timeout_ - time_waiting);
    time_waiting += base::TimeTicks::Now() - start_time;
  }
}

bool VideoPlayer::WaitForFlushDone() {
  return WaitForEvent(VideoPlayerEvent::kFlushDone);
}

bool VideoPlayer::WaitForResetDone() {
  return WaitForEvent(VideoPlayerEvent::kResetDone);
}

bool VideoPlayer::WaitForFrameDecoded(size_t times) {
  return WaitForEvent(VideoPlayerEvent::kFrameDecoded, times);
}

size_t VideoPlayer::GetEventCount(VideoPlayerEvent event) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::AutoLock auto_lock(event_lock_);
  return video_player_event_counts_[static_cast<size_t>(event)];
}

bool VideoPlayer::WaitForFrameProcessors() {
  return !decoder_client_ || decoder_client_->WaitForFrameProcessors();
}

void VideoPlayer::WaitForRenderer() {
  if (decoder_client_)
    decoder_client_->WaitForRenderer();
}

size_t VideoPlayer::GetFlushDoneCount() const {
  return GetEventCount(VideoPlayerEvent::kFlushDone);
}

size_t VideoPlayer::GetResetDoneCount() const {
  return GetEventCount(VideoPlayerEvent::kResetDone);
}

size_t VideoPlayer::GetFrameDecodedCount() const {
  return GetEventCount(VideoPlayerEvent::kFrameDecoded);
}

bool VideoPlayer::NotifyEvent(VideoPlayerEvent event) {
  base::AutoLock auto_lock(event_lock_);
  if (event == VideoPlayerEvent::kFlushDone ||
      event == VideoPlayerEvent::kResetDone) {
    video_player_state_ = VideoPlayerState::kIdle;
  }

  video_player_events_.push(event);
  video_player_event_counts_[static_cast<size_t>(event)]++;
  event_cv_.Signal();

  // Check whether video playback should be paused after this event.
  if (play_until_.has_value() && play_until_ == event) {
    video_player_state_ = VideoPlayerState::kIdle;
    return false;
  }
  return true;
}

}  // namespace test
}  // namespace media
