// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAST_SENDER_FRAME_SENDER_H_
#define MEDIA_CAST_SENDER_FRAME_SENDER_H_

#include <stdint.h>

#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "media/cast/cast_config.h"
#include "media/cast/cast_environment.h"
#include "media/cast/net/cast_transport.h"
#include "media/cast/net/rtcp/rtcp_defines.h"
#include "media/cast/sender/congestion_control.h"

namespace media::cast {

struct SenderEncodedFrame;
class CastEnvironment;
class CastTransport;

// This is the pure virtual interface for an object that sends encoded frames
// to a receiver.
class FrameSender {
 public:
  // The client is responsible for implementing some encoder-specific methods
  // as well as having the option to subscribe to frame cancellation events.
  class Client {
   public:
    virtual ~Client();

    // Returns the number of frames in the encoder's backlog.
    virtual int GetNumberOfFramesInEncoder() const = 0;

    // Should return the amount of playback time that is in the encoder's
    // backlog. Assuming that the encoder emits frames consecutively, this is
    // the same as the difference between the smallest and largest presentation
    // timestamps in the backlog.
    virtual base::TimeDelta GetEncoderBacklogDuration() const = 0;

    // The frame associated with |frame_id| was canceled and not sent.
    virtual void OnFrameCanceled(FrameId frame_id) {}
  };

  static std::unique_ptr<FrameSender> Create(
      scoped_refptr<CastEnvironment> cast_environment,
      const FrameSenderConfig& config,
      CastTransport* const transport_sender,
      Client* client);

  FrameSender();
  FrameSender(FrameSender&&) = delete;
  FrameSender(const FrameSender&) = delete;
  FrameSender& operator=(const FrameSender&) = delete;
  FrameSender& operator=(FrameSender&&) = delete;
  virtual ~FrameSender();

  // Setting of the target playout delay. It should be communicated to the
  // receiver on the next encoded frame.
  // NOTE: Calling this function is only valid if the receiver supports the
  // "extra_playout_delay", rtp extension.
  virtual void SetTargetPlayoutDelay(
      base::TimeDelta new_target_playout_delay) = 0;
  virtual base::TimeDelta GetTargetPlayoutDelay() const = 0;

  // Whether a key frame is needed, typically caused by a picture loss
  // indication event.
  virtual bool NeedsKeyFrame() const = 0;

  // Called by the encoder with the next encoded frame to send.
  virtual void EnqueueFrame(
      std::unique_ptr<SenderEncodedFrame> encoded_frame) = 0;

  // Returns true if too many frames would be in-flight by encoding and sending
  // the next frame having the given |frame_duration|.
  //
  // Callers are recommended to compute the frame duration based on the
  // difference between the next and last frames' reference times, or the period
  // between frames of the configured max frame rate if the reference times are
  // unavailable.
  virtual bool ShouldDropNextFrame(base::TimeDelta frame_duration) const = 0;

  // Returns the RTP timestamp on the frame associated with |frame_id|.
  virtual RtpTimeTicks GetRecordedRtpTimestamp(FrameId frame_id) const = 0;

  // Returns the number of frames that were sent but not yet acknowledged.
  virtual int GetUnacknowledgedFrameCount() const = 0;

  // Returns the suggested bitrate the next frame should be encoded at.
  virtual int GetSuggestedBitrate(base::TimeTicks playout_time,
                                  base::TimeDelta playout_delay) = 0;

  // Configuration specific methods.

  // The maximum frame rate.
  virtual double MaxFrameRate() const = 0;
  virtual void SetMaxFrameRate(double max_frame_rate) = 0;

  // The current target playout delay.
  virtual base::TimeDelta TargetPlayoutDelay() const = 0;

  // The current, estimated round trip time.
  virtual base::TimeDelta CurrentRoundTripTime() const = 0;

  // When the last frame was sent.
  virtual base::TimeTicks LastSendTime() const = 0;

  // The latest acknowledged frame ID.
  virtual FrameId LatestAckedFrameId() const = 0;

  // RTCP client-specific methods.
  virtual void OnReceivedCastFeedback(const RtcpCastMessage& cast_feedback) = 0;
  virtual void OnReceivedPli() = 0;
  virtual void OnMeasuredRoundTripTime(base::TimeDelta rtt) = 0;
};

}  // namespace media::cast

#endif  // MEDIA_CAST_SENDER_FRAME_SENDER_H_
