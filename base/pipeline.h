// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_PIPELINE_H_
#define MEDIA_BASE_PIPELINE_H_

#include <memory>

#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/buffering_state.h"
#include "media/base/cdm_context.h"
#include "media/base/media_export.h"
#include "media/base/media_status.h"
#include "media/base/media_track.h"
#include "media/base/pipeline_metadata.h"
#include "media/base/pipeline_status.h"
#include "media/base/ranges.h"
#include "media/base/text_track.h"
#include "media/base/video_decoder_config.h"
#include "media/base/video_rotation.h"
#include "media/base/waiting.h"
#include "ui/gfx/geometry/size.h"

namespace media {

class Demuxer;
class Renderer;

class MEDIA_EXPORT Pipeline {
 public:
  class Client {
   public:
    // Executed whenever an error occurs except when the error occurs during
    // Start/Seek/Resume or Suspend. Those errors are reported via |seek_cb|
    // and |suspend_cb| respectively.
    // NOTE: The client is responsible for calling Pipeline::Stop().
    virtual void OnError(PipelineStatus status) = 0;

    // Executed whenever the media reaches the end.
    virtual void OnEnded() = 0;

    // Executed when the content duration, container video size, start time,
    // and whether the content has audio and/or video in supported formats are
    // known.
    virtual void OnMetadata(PipelineMetadata metadata) = 0;

    // Executed whenever there are changes in the buffering state of the
    // pipeline.
    virtual void OnBufferingStateChange(BufferingState state) = 0;

    // Executed whenever the presentation duration changes.
    virtual void OnDurationChange() = 0;

    // Executed whenever a text track is added.
    // The client is expected to create a TextTrack and call |done_cb|.
    virtual void OnAddTextTrack(const TextTrackConfig& config,
                                const AddTextTrackDoneCB& done_cb) = 0;

    // Executed whenever the pipeline is waiting because of |reason|.
    virtual void OnWaiting(WaitingReason reason) = 0;

    // Executed for the first video frame and whenever natural size changes.
    virtual void OnVideoNaturalSizeChange(const gfx::Size& size) = 0;

    // Executed for the first video frame and whenever opacity changes.
    virtual void OnVideoOpacityChange(bool opaque) = 0;

    // Executed when the average keyframe distance for the video changes.
    virtual void OnVideoAverageKeyframeDistanceUpdate() = 0;

    // Executed whenever DemuxerStream status returns kConfigChange. Initial
    // configs provided by OnMetadata.
    virtual void OnAudioConfigChange(const AudioDecoderConfig& config) = 0;
    virtual void OnVideoConfigChange(const VideoDecoderConfig& config) = 0;

    // Executed whenever the underlying AudioDecoder or VideoDecoder changes
    // during playback.
    virtual void OnAudioDecoderChange(const std::string& name) = 0;
    virtual void OnVideoDecoderChange(const std::string& name) = 0;

    // Executed whenever an important status change has happened, and that this
    // change was not initiated by Pipeline or Pipeline::Client.
    // Only used with FlingingRenderer, when an external device pauses/resumes
    // a video that is playing remotely.
    virtual void OnRemotePlayStateChange(MediaStatus::State state) = 0;
  };

  virtual ~Pipeline() {}

  // StartType provides the option to start the pipeline without a renderer;
  // pipeline initialization will stop once metadata has been retrieved. The
  // flags below indicate when suspended start will be invoked.
  enum class StartType {
    kNormal,                            // Follow the normal startup path.
    kSuspendAfterMetadataForAudioOnly,  // Suspend after metadata for audio
                                        // only.
    kSuspendAfterMetadata,              // Always suspend after metadata.
  };

  // Build a pipeline to using the given |demuxer| and |renderer| to construct
  // a filter chain, executing |seek_cb| when the initial seek has completed.
  // Methods on PipelineClient may be called up until Stop() has completed.
  // It is an error to call this method after the pipeline has already started.
  //
  // If a |start_type| is specified which allows suspension, pipeline startup
  // will halt after metadata has been retrieved and the pipeline will be in a
  // suspended state.
  virtual void Start(StartType start_type,
                     Demuxer* demuxer,
                     std::unique_ptr<Renderer> renderer,
                     Client* client,
                     const PipelineStatusCB& seek_cb) = 0;

  // Track switching works similarly for both audio and video. Callbacks are
  // used to notify when it is time to procede to the next step, since many of
  // the operations are asynchronous.
  // ──────────────────── Track Switch Control Flow ───────────────────────
  //  pipeline | demuxer | demuxer_stream | renderer | video/audio_renderer
  //           |         |                |          |
  //           |         |                |          |
  //           |         |                |          |
  //     switch track    |                |          |
  //      --------->     |                |          |
  //           | disable/enable stream    |          |
  //           |      ----------->        |          |
  //    active streams   |                |          |
  //      <---------     |                |          |
  //           |        switch track      |          |
  //      -------------------------------------->    |
  //           |         |                |    Flush/Restart/Reset
  //           |         |                |     --------------->
  //     Notify pipeline of completed track change (via callback)
  //      <-----------------------------------------------------
  // ──────────────────── Sometime in the future ──────────────────────────
  //           |         |                | OnBufferingStateChange
  //           |         |                |    <----------------
  //           | OnBufferingStateChange   |          |
  //     <--------------------------------------     |
  //           |         |                |          |
  //           |         |                |          |
  // |enabled_track_ids| contains track ids of enabled audio tracks.
  virtual void OnEnabledAudioTracksChanged(
      const std::vector<MediaTrack::Id>& enabled_track_ids,
      base::OnceClosure change_completed_cb) = 0;

  // |selected_track_id| is either empty, which means no video track is
  // selected, or contains the selected video track id.
  virtual void OnSelectedVideoTrackChanged(
      base::Optional<MediaTrack::Id> selected_track_id,
      base::OnceClosure change_completed_cb) = 0;

  // Stops the pipeline. This is a blocking function.
  // If the pipeline is started, it must be stopped before destroying it.
  // It it permissible to call Stop() at any point during the lifetime of the
  // pipeline.
  //
  // Once Stop is called any outstanding completion callbacks
  // for Start/Seek/Suspend/Resume or Client methods will *not* be called.
  virtual void Stop() = 0;

  // Attempt to seek to the position specified by time.  |seek_cb| will be
  // executed when the all filters in the pipeline have processed the seek.
  //
  // Clients are expected to call GetMediaTime() to check whether the seek
  // succeeded.
  //
  // It is an error to call this method if the pipeline has not started or
  // has been suspended.
  virtual void Seek(base::TimeDelta time, const PipelineStatusCB& seek_cb) = 0;

  // Suspends the pipeline, discarding the current renderer.
  //
  // While suspended, GetMediaTime() returns the presentation timestamp of the
  // last rendered frame.
  //
  // It is an error to call this method if the pipeline has not started or is
  // seeking.
  virtual void Suspend(const PipelineStatusCB& suspend_cb) = 0;

  // Resume the pipeline with a new renderer, and initialize it with a seek.
  //
  // It is an error to call this method if the pipeline has not finished
  // suspending.
  virtual void Resume(std::unique_ptr<Renderer> renderer,
                      base::TimeDelta timestamp,
                      const PipelineStatusCB& seek_cb) = 0;

  // Returns true if the pipeline has been started via Start().  If IsRunning()
  // returns true, it is expected that Stop() will be called before destroying
  // the pipeline.
  virtual bool IsRunning() const = 0;

  // Returns true if the pipeline has been suspended via Suspend() or during
  // Start(). If IsSuspended() returns true, it is expected that Resume() will
  // be called to resume playback.
  virtual bool IsSuspended() const = 0;

  // Gets the current playback rate of the pipeline.  When the pipeline is
  // started, the playback rate will be 0.0.  A rate of 1.0 indicates
  // that the pipeline is rendering the media at the standard rate.  Valid
  // values for playback rate are >= 0.0.
  virtual double GetPlaybackRate() const = 0;

  // Attempt to adjust the playback rate. Setting a playback rate of 0.0 pauses
  // all rendering of the media.  A rate of 1.0 indicates a normal playback
  // rate.  Values for the playback rate must be greater than or equal to 0.0.
  //
  // TODO(scherkus): What about maximum rate?  Does HTML5 specify a max?
  virtual void SetPlaybackRate(double playback_rate) = 0;

  // Gets the current volume setting being used by the audio renderer.  When
  // the pipeline is started, this value will be 1.0f.  Valid values range
  // from 0.0f to 1.0f.
  virtual float GetVolume() const = 0;

  // Attempt to set the volume of the audio renderer.  Valid values for volume
  // range from 0.0f (muted) to 1.0f (full volume).  This value affects all
  // channels proportionately for multi-channel audio streams.
  virtual void SetVolume(float volume) = 0;

  // Returns the current media playback time, which progresses from 0 until
  // GetMediaDuration().
  virtual base::TimeDelta GetMediaTime() const = 0;

  // Get approximate time ranges of buffered media.
  virtual Ranges<base::TimeDelta> GetBufferedTimeRanges() const = 0;

  // Get the duration of the media in microseconds.  If the duration has not
  // been determined yet, then returns 0.
  virtual base::TimeDelta GetMediaDuration() const = 0;

  // Return true if loading progress has been made since the last time this
  // method was called.
  virtual bool DidLoadingProgress() = 0;

  // Gets the current pipeline statistics.
  virtual PipelineStatistics GetStatistics() const = 0;

  virtual void SetCdm(CdmContext* cdm_context,
                      const CdmAttachedCB& cdm_attached_cb) = 0;
};

}  // namespace media

#endif  // MEDIA_BASE_PIPELINE_H_
