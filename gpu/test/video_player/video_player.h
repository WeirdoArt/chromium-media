// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_TEST_VIDEO_PLAYER_VIDEO_PLAYER_H_
#define MEDIA_GPU_TEST_VIDEO_PLAYER_VIDEO_PLAYER_H_

#include <limits.h>
#include <memory>
#include <utility>

#include "base/callback.h"
#include "base/containers/queue.h"
#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "base/time/time.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace media {
namespace test {

class FrameRendererDummy;
class Video;
class VideoFrameProcessor;
class DecoderWrapper;
struct DecoderWrapperConfig;

// Default timeout used when waiting for events.
constexpr base::TimeDelta kDefaultEventWaitTimeout = base::Seconds(30);

enum class VideoPlayerEvent : size_t {
  kInitialized,
  kFrameDecoded,
  kFlushing,
  kFlushDone,
  kResetting,
  kResetDone,
  kConfigInfo,  // A config info was encountered in an H.264/HEVC video stream.
  kNewBuffersRequested,
  kNumEvents,
};

// The video player provides a framework to build video decode accelerator tests
// upon. It provides methods to manipulate video playback, and wait for specific
// events to occur.
class VideoPlayer {
 public:
  using EventCallback = base::RepeatingCallback<bool(VideoPlayerEvent)>;

  VideoPlayer(const VideoPlayer&) = delete;
  VideoPlayer& operator=(const VideoPlayer&) = delete;

  ~VideoPlayer();

  // Create an instance of this class. Must be Initialize()d before use.
  static std::unique_ptr<VideoPlayer> Create(
      const DecoderWrapperConfig& config,
      std::unique_ptr<FrameRendererDummy> frame_renderer,
      std::vector<std::unique_ptr<VideoFrameProcessor>> frame_processors = {});

  // Wait until all frame processors have finished processing. Returns whether
  // processing was successful.
  bool WaitForFrameProcessors();
  // Wait until the renderer has finished rendering all queued frames.
  void WaitForRenderer();

  // Set the maximum time we will wait for an event to finish.
  void SetEventWaitTimeout(base::TimeDelta timeout);

  // Initialize the video player for the specified |video|. This function can be
  // called multiple times and needs to be called before Play(). The |video|
  // will not be owned by the video player, the caller should guarantee it
  // outlives the video player.
  bool Initialize(const Video* video);
  // Play the video asynchronously.
  void Play();
  // Play the video asynchronously. Automatically pause decoding when |event|
  // occurs.
  void PlayUntil(VideoPlayerEvent event);
  // Reset the decoder to the beginning of the video stream.
  void Reset();
  // Flush the decoder.
  void Flush();

  // Wait for an event to occur the specified number of times. All events that
  // occurred since last calling this function will be taken into account. All
  // events with different types will be consumed. Will return false if the
  // specified timeout is exceeded while waiting for the events.
  bool WaitForEvent(VideoPlayerEvent event, size_t times = 1);
  // Helper function to wait for a FlushDone event.
  bool WaitForFlushDone();
  // Helper function to wait for a ResetDone event.
  bool WaitForResetDone();
  // Helper function to wait for the specified number of FrameDecoded events.
  bool WaitForFrameDecoded(size_t times);

  // Get the number of times the specified event occurred.
  size_t GetEventCount(VideoPlayerEvent event) const;
  // Helper function to get the number of ResetDone events thrown.
  size_t GetResetDoneCount() const;
  // Helper function to get the number of FlushDone events thrown.
  size_t GetFlushDoneCount() const;
  // Helper function to get the number of FrameDecoded events thrown.
  size_t GetFrameDecodedCount() const;

 private:
  VideoPlayer();

  bool CreateWrapper(
      const DecoderWrapperConfig& config,
      std::unique_ptr<FrameRendererDummy> frame_renderer,
      std::vector<std::unique_ptr<VideoFrameProcessor>> frame_processors);

  // Notify the video player an event has occurred (e.g. frame decoded). Returns
  // whether the decoder client should continue decoding frames.
  bool NotifyEvent(VideoPlayerEvent event);

  std::unique_ptr<DecoderWrapper> decoder_wrapper_;

  // The timeout used when waiting for events.
  base::TimeDelta event_timeout_ = kDefaultEventWaitTimeout;
  mutable base::Lock event_lock_;
  base::ConditionVariable event_cv_;

  // NotifyEvent() will store events here for WaitForEvent() to process.
  base::queue<VideoPlayerEvent> video_player_events_ GUARDED_BY(event_lock_);

  size_t video_player_event_counts_[static_cast<size_t>(
      VideoPlayerEvent::kNumEvents)] GUARDED_BY(event_lock_){};

  // Set by PlayUntil() to automatically pause decoding once this event occurs.
  absl::optional<VideoPlayerEvent> play_until_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace test
}  // namespace media

#endif  // MEDIA_GPU_TEST_VIDEO_PLAYER_VIDEO_PLAYER_H_
