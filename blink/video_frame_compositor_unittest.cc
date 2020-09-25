// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/blink/video_frame_compositor.h"
#include "base/bind.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/simple_test_tick_clock.h"
#include "components/viz/common/frame_sinks/begin_frame_args.h"
#include "components/viz/common/surfaces/frame_sink_id.h"
#include "media/base/video_frame.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/web_video_frame_submitter.h"

using base::test::RunClosure;
using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Eq;
using testing::Return;
using testing::StrictMock;

namespace media {

class MockWebVideoFrameSubmitter : public blink::WebVideoFrameSubmitter {
 public:
  // blink::WebVideoFrameSubmitter implementation.
  void StopUsingProvider() override {}
  MOCK_METHOD1(EnableSubmission, void(viz::SurfaceId));
  MOCK_METHOD0(StartRendering, void());
  MOCK_METHOD0(StopRendering, void());
  MOCK_CONST_METHOD0(IsDrivingFrameUpdates, bool(void));
  MOCK_METHOD2(Initialize, void(cc::VideoFrameProvider*, bool));
  MOCK_METHOD1(SetRotation, void(media::VideoRotation));
  MOCK_METHOD1(SetIsSurfaceVisible, void(bool));
  MOCK_METHOD1(SetIsPageVisible, void(bool));
  MOCK_METHOD1(SetForceSubmit, void(bool));
  MOCK_METHOD1(SetForceBeginFrames, void(bool));
  void DidReceiveFrame() override { ++did_receive_frame_count_; }

  int did_receive_frame_count() { return did_receive_frame_count_; }

 private:
  int did_receive_frame_count_ = 0;
};

class VideoFrameCompositorTest : public VideoRendererSink::RenderCallback,
                                 public ::testing::TestWithParam<bool> {
 public:
  VideoFrameCompositorTest()
      : client_(new StrictMock<MockWebVideoFrameSubmitter>()) {}

  void SetUp() override {
    if (IsSurfaceLayerForVideoEnabled()) {
      feature_list_.InitFromCommandLine("UseSurfaceLayerForVideo", "");

      // When SurfaceLayerForVideo is enabled, |compositor_| owns the
      // |submitter_|. Otherwise, the |compositor_| treats the |submitter_| as
      // if it were a VideoFrameProviderClient in the VideoLayer code path,
      // holding only a bare pointer.
    }
    submitter_ = client_.get();

    if (!IsSurfaceLayerForVideoEnabled()) {
      compositor_ = std::make_unique<VideoFrameCompositor>(
          base::ThreadTaskRunnerHandle::Get(), nullptr);
      compositor_->SetVideoFrameProviderClient(client_.get());
    } else {
      EXPECT_CALL(*submitter_, Initialize(_, _));
      compositor_ = std::make_unique<VideoFrameCompositor>(
          base::ThreadTaskRunnerHandle::Get(), std::move(client_));
      base::RunLoop().RunUntilIdle();
      EXPECT_CALL(*submitter_,
                  SetRotation(Eq(media::VideoRotation::VIDEO_ROTATION_90)));
      EXPECT_CALL(*submitter_, SetForceSubmit(false));
      EXPECT_CALL(*submitter_, EnableSubmission(Eq(viz::SurfaceId())));
      compositor_->EnableSubmission(
          viz::SurfaceId(), media::VideoRotation::VIDEO_ROTATION_90, false);
    }

    compositor_->set_tick_clock_for_testing(&tick_clock_);
    // Disable background rendering by default.
    compositor_->set_background_rendering_for_testing(false);
  }

  ~VideoFrameCompositorTest() override {
    compositor_->SetVideoFrameProviderClient(nullptr);
  }

  scoped_refptr<VideoFrame> CreateOpaqueFrame() {
    return CreateOpaqueFrame(8, 8);
  }

  scoped_refptr<VideoFrame> CreateOpaqueFrame(int width, int height) {
    gfx::Size size(width, height);
    return VideoFrame::CreateFrame(PIXEL_FORMAT_I420, size, gfx::Rect(size),
                                   size, base::TimeDelta());
  }

  VideoFrameCompositor* compositor() { return compositor_.get(); }

  VideoFrameCompositor::OnNewFramePresentedCB GetNewFramePresentedCB() {
    return base::BindOnce(&VideoFrameCompositorTest::OnNewFramePresented,
                          base::Unretained(this));
  }

 protected:
  bool IsSurfaceLayerForVideoEnabled() { return GetParam(); }

  // VideoRendererSink::RenderCallback implementation.
  MOCK_METHOD3(Render,
               scoped_refptr<VideoFrame>(base::TimeTicks,
                                         base::TimeTicks,
                                         bool));
  MOCK_METHOD0(OnFrameDropped, void());
  MOCK_METHOD0(OnNewFramePresented, void());

  base::TimeDelta GetPreferredRenderInterval() override {
    return preferred_render_interval_;
  }

  void StartVideoRendererSink() {
    EXPECT_CALL(*submitter_, StartRendering());
    const bool had_current_frame = !!compositor_->GetCurrentFrame();
    compositor()->Start(this);
    // If we previously had a frame, we should still have one now.
    EXPECT_EQ(had_current_frame, !!compositor_->GetCurrentFrame());
    base::RunLoop().RunUntilIdle();
  }

  void StopVideoRendererSink(bool have_client) {
    if (have_client)
      EXPECT_CALL(*submitter_, StopRendering());
    const bool had_current_frame = !!compositor_->GetCurrentFrame();
    compositor()->Stop();
    // If we previously had a frame, we should still have one now.
    EXPECT_EQ(had_current_frame, !!compositor_->GetCurrentFrame());
    base::RunLoop().RunUntilIdle();
  }

  void RenderFrame() {
    compositor()->GetCurrentFrame();
    compositor()->PutCurrentFrame();
  }

  base::TimeDelta preferred_render_interval_;
  base::SimpleTestTickClock tick_clock_;
  StrictMock<MockWebVideoFrameSubmitter>* submitter_;
  std::unique_ptr<StrictMock<MockWebVideoFrameSubmitter>> client_;
  std::unique_ptr<VideoFrameCompositor> compositor_;

 private:
  base::test::ScopedFeatureList feature_list_;

  DISALLOW_COPY_AND_ASSIGN(VideoFrameCompositorTest);
};

TEST_P(VideoFrameCompositorTest, InitialValues) {
  EXPECT_FALSE(compositor()->GetCurrentFrame().get());
}

TEST_P(VideoFrameCompositorTest, SetIsSurfaceVisible) {
  if (!IsSurfaceLayerForVideoEnabled())
    return;

  auto cb = compositor()->GetUpdateSubmissionStateCallback();

  EXPECT_CALL(*submitter_, SetIsSurfaceVisible(true));
  cb.Run(true);
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(*submitter_, SetIsSurfaceVisible(false));
  cb.Run(false);
  base::RunLoop().RunUntilIdle();
}

TEST_P(VideoFrameCompositorTest, SetIsPageVisible) {
  if (!IsSurfaceLayerForVideoEnabled())
    return;

  EXPECT_CALL(*submitter_, SetIsPageVisible(true));
  compositor()->SetIsPageVisible(true);

  EXPECT_CALL(*submitter_, SetIsPageVisible(false));
  compositor()->SetIsPageVisible(false);
}

TEST_P(VideoFrameCompositorTest, PaintSingleFrame) {
  scoped_refptr<VideoFrame> expected = VideoFrame::CreateEOSFrame();

  // Should notify compositor synchronously.
  EXPECT_EQ(0, submitter_->did_receive_frame_count());
  compositor()->PaintSingleFrame(expected);
  scoped_refptr<VideoFrame> actual = compositor()->GetCurrentFrame();
  EXPECT_EQ(expected, actual);
  EXPECT_EQ(1, submitter_->did_receive_frame_count());
}

TEST_P(VideoFrameCompositorTest, RenderFiresPresentationCallback) {
  // Advance the clock so we can differentiate between base::TimeTicks::Now()
  // and base::TimeTicks().
  tick_clock_.Advance(base::TimeDelta::FromSeconds(1));

  scoped_refptr<VideoFrame> opaque_frame = CreateOpaqueFrame();
  EXPECT_CALL(*this, Render(_, _, true)).WillRepeatedly(Return(opaque_frame));
  EXPECT_CALL(*this, OnNewFramePresented());
  EXPECT_CALL(*submitter_, SetForceBeginFrames(true)).Times(AnyNumber());
  compositor()->SetOnFramePresentedCallback(GetNewFramePresentedCB());
  StartVideoRendererSink();
  StopVideoRendererSink(true);

  auto metadata = compositor()->GetLastPresentedFrameMetadata();
  EXPECT_NE(base::TimeTicks(), metadata->presentation_time);
  EXPECT_NE(base::TimeTicks(), metadata->expected_display_time);
}

TEST_P(VideoFrameCompositorTest, PresentationCallbackForcesBeginFrames) {
  if (!IsSurfaceLayerForVideoEnabled())
    return;

  // A call to the requestVideoFrameCallback() API should set ForceBeginFrames.
  EXPECT_CALL(*submitter_, SetForceBeginFrames(true));
  compositor()->SetOnFramePresentedCallback(GetNewFramePresentedCB());
  base::RunLoop().RunUntilIdle();

  testing::Mock::VerifyAndClear(submitter_);

  // The flag should be un-set when stop receiving callbacks.
  base::RunLoop run_loop;
  EXPECT_CALL(*submitter_, SetForceBeginFrames(false))
      .WillOnce(RunClosure(run_loop.QuitClosure()));
  run_loop.Run();

  testing::Mock::VerifyAndClear(submitter_);
}

TEST_P(VideoFrameCompositorTest, MultiplePresentationCallbacks) {
  // Advance the clock so we can differentiate between base::TimeTicks::Now()
  // and base::TimeTicks().
  tick_clock_.Advance(base::TimeDelta::FromSeconds(1));

  // Create frames of different sizes so we can differentiate them.
  constexpr int kSize1 = 8;
  constexpr int kSize2 = 16;
  constexpr int kSize3 = 24;
  scoped_refptr<VideoFrame> opaque_frame_1 = CreateOpaqueFrame(kSize1, kSize1);
  scoped_refptr<VideoFrame> opaque_frame_2 = CreateOpaqueFrame(kSize2, kSize2);
  scoped_refptr<VideoFrame> opaque_frame_3 = CreateOpaqueFrame(kSize3, kSize3);

  EXPECT_CALL(*this, OnNewFramePresented()).Times(1);
  EXPECT_CALL(*submitter_, SetForceBeginFrames(_)).Times(AnyNumber());
  compositor()->SetOnFramePresentedCallback(GetNewFramePresentedCB());
  compositor()->PaintSingleFrame(opaque_frame_1);

  auto metadata = compositor()->GetLastPresentedFrameMetadata();
  EXPECT_EQ(metadata->width, kSize1);
  uint32_t first_presented_frames = metadata->presented_frames;

  // Callbacks are one-shot, and shouldn't fire if they are not re-queued.
  EXPECT_CALL(*this, OnNewFramePresented()).Times(0);
  compositor()->PaintSingleFrame(opaque_frame_2);

  // We should get the 2nd frame's metadata when we query for it.
  metadata = compositor()->GetLastPresentedFrameMetadata();
  EXPECT_EQ(first_presented_frames + 1, metadata->presented_frames);
  EXPECT_EQ(metadata->width, kSize2);

  EXPECT_CALL(*this, OnNewFramePresented()).Times(1);
  compositor()->SetOnFramePresentedCallback(GetNewFramePresentedCB());
  compositor()->PaintSingleFrame(opaque_frame_3);

  // The presentated frames counter should have gone up twice by now.
  metadata = compositor()->GetLastPresentedFrameMetadata();
  EXPECT_EQ(first_presented_frames + 2, metadata->presented_frames);
  EXPECT_EQ(metadata->width, kSize3);
}

TEST_P(VideoFrameCompositorTest, VideoRendererSinkFrameDropped) {
  scoped_refptr<VideoFrame> opaque_frame = CreateOpaqueFrame();

  EXPECT_CALL(*this, Render(_, _, _)).WillRepeatedly(Return(opaque_frame));
  StartVideoRendererSink();

  EXPECT_TRUE(
      compositor()->UpdateCurrentFrame(base::TimeTicks(), base::TimeTicks()));

  // Another call should trigger a dropped frame callback.
  EXPECT_CALL(*this, OnFrameDropped());
  EXPECT_FALSE(
      compositor()->UpdateCurrentFrame(base::TimeTicks(), base::TimeTicks()));

  // Ensure it always happens until the frame is rendered.
  EXPECT_CALL(*this, OnFrameDropped());
  EXPECT_FALSE(
      compositor()->UpdateCurrentFrame(base::TimeTicks(), base::TimeTicks()));

  // Call GetCurrentFrame() but not PutCurrentFrame()
  compositor()->GetCurrentFrame();

  // The frame should still register as dropped until PutCurrentFrame is called.
  EXPECT_CALL(*this, OnFrameDropped());
  EXPECT_FALSE(
      compositor()->UpdateCurrentFrame(base::TimeTicks(), base::TimeTicks()));

  RenderFrame();
  EXPECT_FALSE(
      compositor()->UpdateCurrentFrame(base::TimeTicks(), base::TimeTicks()));

  StopVideoRendererSink(true);
}

TEST_P(VideoFrameCompositorTest, VideoLayerShutdownWhileRendering) {
  if (!IsSurfaceLayerForVideoEnabled()) {
    EXPECT_CALL(*this, Render(_, _, true)).WillOnce(Return(nullptr));
    StartVideoRendererSink();
    compositor_->SetVideoFrameProviderClient(nullptr);
    StopVideoRendererSink(false);
  }
}

TEST_P(VideoFrameCompositorTest, StartFiresBackgroundRender) {
  scoped_refptr<VideoFrame> opaque_frame = CreateOpaqueFrame();
  EXPECT_CALL(*this, Render(_, _, true)).WillRepeatedly(Return(opaque_frame));
  StartVideoRendererSink();
  StopVideoRendererSink(true);
}

TEST_P(VideoFrameCompositorTest, BackgroundRenderTicks) {
  scoped_refptr<VideoFrame> opaque_frame = CreateOpaqueFrame();
  compositor_->set_background_rendering_for_testing(true);

  base::RunLoop run_loop;
  EXPECT_CALL(*this, Render(_, _, true))
      .WillOnce(Return(opaque_frame))
      .WillOnce(
          DoAll(RunClosure(run_loop.QuitClosure()), Return(opaque_frame)));
  StartVideoRendererSink();
  run_loop.Run();

  // UpdateCurrentFrame() calls should indicate they are not synthetic.
  EXPECT_CALL(*this, Render(_, _, false)).WillOnce(Return(opaque_frame));
  EXPECT_FALSE(
      compositor()->UpdateCurrentFrame(base::TimeTicks(), base::TimeTicks()));

  // Background rendering should tick another render callback.
  StopVideoRendererSink(true);
}

TEST_P(VideoFrameCompositorTest,
       UpdateCurrentFrameWorksWhenBackgroundRendered) {
  scoped_refptr<VideoFrame> opaque_frame = CreateOpaqueFrame();
  compositor_->set_background_rendering_for_testing(true);

  // Background render a frame that succeeds immediately.
  EXPECT_CALL(*this, Render(_, _, true)).WillOnce(Return(opaque_frame));
  StartVideoRendererSink();

  // The background render completes immediately, so the next call to
  // UpdateCurrentFrame is expected to return true to account for the frame
  // rendered in the background.
  EXPECT_CALL(*this, Render(_, _, false))
      .WillOnce(Return(scoped_refptr<VideoFrame>(opaque_frame)));
  EXPECT_TRUE(
      compositor()->UpdateCurrentFrame(base::TimeTicks(), base::TimeTicks()));
  RenderFrame();

  // Second call to UpdateCurrentFrame will return false as no new frame has
  // been created since the last call.
  EXPECT_CALL(*this, Render(_, _, false))
      .WillOnce(Return(scoped_refptr<VideoFrame>(opaque_frame)));
  EXPECT_FALSE(
      compositor()->UpdateCurrentFrame(base::TimeTicks(), base::TimeTicks()));

  StopVideoRendererSink(true);
}

TEST_P(VideoFrameCompositorTest, UpdateCurrentFrameIfStale) {
  scoped_refptr<VideoFrame> opaque_frame_1 = CreateOpaqueFrame();
  scoped_refptr<VideoFrame> opaque_frame_2 = CreateOpaqueFrame();
  compositor_->set_background_rendering_for_testing(true);

  EXPECT_CALL(*submitter_, IsDrivingFrameUpdates)
      .Times(AnyNumber())
      .WillRepeatedly(Return(true));

  // Starting the video renderer should return a single frame.
  EXPECT_CALL(*this, Render(_, _, true)).WillOnce(Return(opaque_frame_1));
  StartVideoRendererSink();
  EXPECT_EQ(opaque_frame_1, compositor()->GetCurrentFrame());

  // Since we have a client, this call should not call background render, even
  // if a lot of time has elapsed between calls.
  tick_clock_.Advance(base::TimeDelta::FromSeconds(1));
  EXPECT_CALL(*this, Render(_, _, _)).Times(0);
  compositor()->UpdateCurrentFrameIfStale();

  // Have the client signal that it will not drive the frame clock, so that
  // calling UpdateCurrentFrameIfStale may update the frame.
  EXPECT_CALL(*submitter_, IsDrivingFrameUpdates)
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));

  // Wait for background rendering to tick.
  base::RunLoop run_loop;
  EXPECT_CALL(*this, Render(_, _, true))
      .WillOnce(
          DoAll(RunClosure(run_loop.QuitClosure()), Return(opaque_frame_2)));
  run_loop.Run();

  // This call should still not call background render, because not enough time
  // has elapsed since the last background render call.
  EXPECT_CALL(*this, Render(_, _, true)).Times(0);
  compositor()->UpdateCurrentFrameIfStale();
  EXPECT_EQ(opaque_frame_2, compositor()->GetCurrentFrame());

  // Advancing the tick clock should allow a new frame to be requested.
  tick_clock_.Advance(base::TimeDelta::FromMilliseconds(10));
  EXPECT_CALL(*this, Render(_, _, true)).WillOnce(Return(opaque_frame_1));
  compositor()->UpdateCurrentFrameIfStale();
  EXPECT_EQ(opaque_frame_1, compositor()->GetCurrentFrame());

  // Clear our client, which means no mock function calls for Client.  It will
  // also permit UpdateCurrentFrameIfStale to update the frame.
  compositor()->SetVideoFrameProviderClient(nullptr);

  // Advancing the tick clock should allow a new frame to be requested.
  tick_clock_.Advance(base::TimeDelta::FromMilliseconds(10));
  EXPECT_CALL(*this, Render(_, _, true)).WillOnce(Return(opaque_frame_2));
  compositor()->UpdateCurrentFrameIfStale();
  EXPECT_EQ(opaque_frame_2, compositor()->GetCurrentFrame());

  // Background rendering should tick another render callback.
  StopVideoRendererSink(false);
}

TEST_P(VideoFrameCompositorTest, PreferredRenderInterval) {
  preferred_render_interval_ = base::TimeDelta::FromSeconds(1);
  compositor_->Start(this);
  EXPECT_EQ(compositor_->GetPreferredRenderInterval(),
            preferred_render_interval_);
  compositor_->Stop();
  EXPECT_EQ(compositor_->GetPreferredRenderInterval(),
            viz::BeginFrameArgs::MinInterval());
}

INSTANTIATE_TEST_SUITE_P(SubmitterEnabled,
                         VideoFrameCompositorTest,
                         ::testing::Bool());

}  // namespace media
