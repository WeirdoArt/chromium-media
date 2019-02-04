// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/test/scoped_task_environment.h"
#include "base/test/test_timeouts.h"
#include "media/base/test_data_util.h"
#include "media/gpu/buildflags.h"
#include "media/gpu/test/video_frame_file_writer.h"
#include "media/gpu/test/video_frame_validator.h"
#include "media/gpu/test/video_player/frame_renderer_dummy.h"
#include "media/gpu/test/video_player/video.h"
#include "media/gpu/test/video_player/video_collection.h"
#include "media/gpu/test/video_player/video_decoder_client.h"
#include "media/gpu/test/video_player/video_player.h"
#include "mojo/core/embedder/embedder.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(USE_OZONE)
#include "ui/ozone/public/ozone_gpu_test_helper.h"
#include "ui/ozone/public/ozone_platform.h"
#endif

#if BUILDFLAG(USE_VAAPI)
#include "media/gpu/vaapi/vaapi_wrapper.h"
#endif

namespace media {
namespace test {

namespace {
// Test environment for video decode tests. Performs setup and teardown once for
// the entire test run.
class VideoDecoderTestEnvironment : public ::testing::Environment {
 public:
  explicit VideoDecoderTestEnvironment(const Video* video) : video_(video) {}
  virtual ~VideoDecoderTestEnvironment() {}

  // Set up the video decode test environment, only called once.
  void SetUp() override;
  // Tear down the video decode test environment, only called once.
  void TearDown() override;

  std::unique_ptr<base::test::ScopedTaskEnvironment> task_environment_;
  const Video* const video_;

  // An exit manager is required to run callbacks on shutdown.
  base::AtExitManager at_exit_manager;

#if defined(USE_OZONE)
  std::unique_ptr<ui::OzoneGpuTestHelper> gpu_helper_;
#endif
};

void VideoDecoderTestEnvironment::SetUp() {
  // Setting up a task environment will create a task runner for the current
  // thread and allow posting tasks to other threads. This is required for the
  // test video player to function correctly.
  TestTimeouts::Initialize();
  task_environment_ = std::make_unique<base::test::ScopedTaskEnvironment>(
      base::test::ScopedTaskEnvironment::MainThreadType::UI);

  // Set the default test data path.
  media::test::Video::SetTestDataPath(media::GetTestDataPath());

  // Perform all static initialization that is required when running video
  // decoders in a test environment.
#if BUILDFLAG(USE_VAAPI)
  media::VaapiWrapper::PreSandboxInitialization();
#endif

#if defined(USE_OZONE)
  // Initialize Ozone. This is necessary to gain access to the GPU for hardware
  // video decode acceleration.
  LOG(WARNING) << "Initializing Ozone Platform...\n"
                  "If this hangs indefinitely please call 'stop ui' first!";
  ui::OzonePlatform::InitParams params = {.single_process = false};
  ui::OzonePlatform::InitializeForUI(params);
  ui::OzonePlatform::InitializeForGPU(params);
  ui::OzonePlatform::GetInstance()->AfterSandboxEntry();

  // Initialize the Ozone GPU helper. If this is not done an error will occur:
  // "Check failed: drm. No devices available for buffer allocation."
  // Note: If a task environment is not set up initialization will hang
  // indefinitely here.
  gpu_helper_.reset(new ui::OzoneGpuTestHelper());
  gpu_helper_->Initialize(base::ThreadTaskRunnerHandle::Get());
#endif
}

void VideoDecoderTestEnvironment::TearDown() {
  task_environment_.reset();
}

media::test::VideoDecoderTestEnvironment* g_env;

// Video decode test class. Performs setup and teardown for each single test.
class VideoDecoderTest : public ::testing::Test {
 public:
  std::unique_ptr<VideoPlayer> CreateVideoPlayer(
      const Video* video,
      const VideoDecoderClientConfig& config = VideoDecoderClientConfig()) {
    std::vector<std::unique_ptr<VideoFrameProcessor>> frame_processors;

    // Validate decoded video frames.
    frame_processors.push_back(
        media::test::VideoFrameValidator::Create(video->FrameChecksums()));

    // Write decoded video frames to the 'video_frames/<test_name/>' folder.
    const ::testing::TestInfo* const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    base::FilePath output_folder =
        base::FilePath("video_frames")
            .Append(base::FilePath(test_info->name()));
    frame_processors.push_back(VideoFrameFileWriter::Create(output_folder));

    return VideoPlayer::Create(video, FrameRendererDummy::Create(),
                               std::move(frame_processors), config);
  }
};

}  // namespace

// Play video from start to end. Wait for the kFlushDone event at the end of the
// stream, that notifies us all frames have been decoded.
TEST_F(VideoDecoderTest, FlushAtEndOfStream) {
  auto tvp = CreateVideoPlayer(g_env->video_);

  tvp->Play();
  EXPECT_TRUE(tvp->WaitForFlushDone());

  EXPECT_EQ(tvp->GetFlushDoneCount(), 1u);
  EXPECT_EQ(tvp->GetFrameDecodedCount(), g_env->video_->NumFrames());
  EXPECT_TRUE(tvp->WaitForFrameProcessors());
}

// Flush the decoder immediately after initialization.
TEST_F(VideoDecoderTest, FlushAfterInitialize) {
  auto tvp = CreateVideoPlayer(g_env->video_);

  tvp->Flush();
  EXPECT_TRUE(tvp->WaitForFlushDone());
  tvp->Play();
  EXPECT_TRUE(tvp->WaitForFlushDone());

  EXPECT_EQ(tvp->GetFlushDoneCount(), 2u);
  EXPECT_EQ(tvp->GetFrameDecodedCount(), g_env->video_->NumFrames());
  EXPECT_TRUE(tvp->WaitForFrameProcessors());
}

// Flush the decoder immediately after doing a mid-stream reset, without waiting
// for a kResetDone event.
TEST_F(VideoDecoderTest, FlushBeforeResetDone) {
  auto tvp = CreateVideoPlayer(g_env->video_);

  tvp->Play();
  EXPECT_TRUE(tvp->WaitForFrameDecoded(g_env->video_->NumFrames() / 2));
  tvp->Reset();
  tvp->Flush();
  EXPECT_TRUE(tvp->WaitForResetDone());
  EXPECT_TRUE(tvp->WaitForFlushDone());

  // As flush doesn't cancel reset, we should have received a single kResetDone
  // and kFlushDone event. We didn't decode the entire video, but more frames
  // might be decoded by the time we called reset, so we can only check whether
  // the decoded frame count is <= the total number of frames.
  EXPECT_EQ(tvp->GetResetDoneCount(), 1u);
  EXPECT_EQ(tvp->GetFlushDoneCount(), 1u);
  EXPECT_LE(tvp->GetFrameDecodedCount(), g_env->video_->NumFrames());
  EXPECT_TRUE(tvp->WaitForFrameProcessors());
}

// Reset the decoder immediately after initialization.
TEST_F(VideoDecoderTest, ResetAfterInitialize) {
  auto tvp = CreateVideoPlayer(g_env->video_);

  tvp->Reset();
  EXPECT_TRUE(tvp->WaitForResetDone());
  tvp->Play();
  EXPECT_TRUE(tvp->WaitForFlushDone());

  EXPECT_EQ(tvp->GetResetDoneCount(), 1u);
  EXPECT_EQ(tvp->GetFlushDoneCount(), 1u);
  EXPECT_EQ(tvp->GetFrameDecodedCount(), g_env->video_->NumFrames());
  EXPECT_TRUE(tvp->WaitForFrameProcessors());
}

// Reset the decoder when the middle of the stream is reached.
TEST_F(VideoDecoderTest, ResetMidStream) {
  auto tvp = CreateVideoPlayer(g_env->video_);

  tvp->Play();
  EXPECT_TRUE(tvp->WaitForFrameDecoded(g_env->video_->NumFrames() / 2));
  tvp->Reset();
  EXPECT_TRUE(tvp->WaitForResetDone());
  size_t numFramesDecoded = tvp->GetFrameDecodedCount();
  tvp->Play();
  EXPECT_TRUE(tvp->WaitForFlushDone());

  EXPECT_EQ(tvp->GetResetDoneCount(), 1u);
  EXPECT_EQ(tvp->GetFlushDoneCount(), 1u);
  EXPECT_EQ(tvp->GetFrameDecodedCount(),
            numFramesDecoded + g_env->video_->NumFrames());
  EXPECT_TRUE(tvp->WaitForFrameProcessors());
}

// Reset the decoder when the end of the stream is reached.
TEST_F(VideoDecoderTest, ResetEndOfStream) {
  auto tvp = CreateVideoPlayer(g_env->video_);

  tvp->Play();
  EXPECT_TRUE(tvp->WaitForFlushDone());
  EXPECT_EQ(tvp->GetFrameDecodedCount(), g_env->video_->NumFrames());
  tvp->Reset();
  EXPECT_TRUE(tvp->WaitForResetDone());
  tvp->Play();
  EXPECT_TRUE(tvp->WaitForFlushDone());

  EXPECT_EQ(tvp->GetResetDoneCount(), 1u);
  EXPECT_EQ(tvp->GetFlushDoneCount(), 2u);
  EXPECT_EQ(tvp->GetFrameDecodedCount(), g_env->video_->NumFrames() * 2);
  EXPECT_TRUE(tvp->WaitForFrameProcessors());
}

// Reset the decoder immediately when the end-of-stream flush starts, without
// waiting for a kFlushDone event.
TEST_F(VideoDecoderTest, ResetBeforeFlushDone) {
  auto tvp = CreateVideoPlayer(g_env->video_);

  // Reset when a kFlushing event is received.
  tvp->Play();
  EXPECT_TRUE(tvp->WaitForFlushDone());
  tvp->Reset();
  EXPECT_TRUE(tvp->WaitForResetDone());

  // Reset will cause the decoder to drop everything it's doing, including the
  // ongoing flush operation. However the flush might have been completed
  // already by the time reset is called. So depending on the timing of the
  // calls we should see 0 or 1 flushes, and the last few video frames might
  // have been dropped.
  EXPECT_LE(tvp->GetFlushDoneCount(), 1u);
  EXPECT_EQ(tvp->GetResetDoneCount(), 1u);
  EXPECT_LE(tvp->GetFrameDecodedCount(), g_env->video_->NumFrames());
  EXPECT_TRUE(tvp->WaitForFrameProcessors());
}

// Reset the decoder immediately when encountering the first config info in a
// H.264 video stream. After resetting the video is played until the end.
TEST_F(VideoDecoderTest, ResetAfterFirstConfigInfo) {
  // This test is only relevant for H.264 video streams.
  if (g_env->video_->Profile() < H264PROFILE_MIN ||
      g_env->video_->Profile() > H264PROFILE_MAX)
    GTEST_SKIP();

  auto tvp = CreateVideoPlayer(g_env->video_);

  tvp->PlayUntil(VideoPlayerEvent::kConfigInfo);
  EXPECT_TRUE(tvp->WaitForEvent(VideoPlayerEvent::kConfigInfo));
  tvp->Reset();
  EXPECT_TRUE(tvp->WaitForResetDone());
  size_t numFramesDecoded = tvp->GetFrameDecodedCount();
  tvp->Play();
  EXPECT_TRUE(tvp->WaitForFlushDone());

  EXPECT_EQ(tvp->GetResetDoneCount(), 1u);
  EXPECT_EQ(tvp->GetFlushDoneCount(), 1u);
  EXPECT_EQ(tvp->GetFrameDecodedCount(),
            numFramesDecoded + g_env->video_->NumFrames());
  EXPECT_GE(tvp->GetEventCount(VideoPlayerEvent::kConfigInfo), 1u);
  EXPECT_TRUE(tvp->WaitForFrameProcessors());
}

// Play video from start to end. Multiple buffer decodes will be queued in the
// decoder, without waiting for the result of the previous decode requests.
TEST_F(VideoDecoderTest, FlushAtEndOfStream_MultipleOutstandingDecodes) {
  VideoDecoderClientConfig config;
  config.max_outstanding_decode_requests = 5;
  auto tvp = CreateVideoPlayer(g_env->video_, config);

  tvp->Play();
  EXPECT_TRUE(tvp->WaitForFlushDone());

  EXPECT_EQ(tvp->GetFlushDoneCount(), 1u);
  EXPECT_EQ(tvp->GetFrameDecodedCount(), g_env->video_->NumFrames());
  EXPECT_TRUE(tvp->WaitForFrameProcessors());
}

// Play multiple videos simultaneously from start to finish.
TEST_F(VideoDecoderTest, FlushAtEndOfStream_MultipleConcurrentDecodes) {
  // The minimal number of concurrent decoders we expect to be supported.
  constexpr size_t kMinSupportedConcurrentDecoders = 3;

  std::vector<std::unique_ptr<VideoPlayer>> tvps(
      kMinSupportedConcurrentDecoders);
  for (size_t i = 0; i < kMinSupportedConcurrentDecoders; ++i)
    tvps[i] = CreateVideoPlayer(g_env->video_);

  for (size_t i = 0; i < kMinSupportedConcurrentDecoders; ++i)
    tvps[i]->Play();

  for (size_t i = 0; i < kMinSupportedConcurrentDecoders; ++i) {
    EXPECT_TRUE(tvps[i]->WaitForFlushDone());
    EXPECT_EQ(tvps[i]->GetFlushDoneCount(), 1u);
    EXPECT_EQ(tvps[i]->GetFrameDecodedCount(), g_env->video_->NumFrames());
    EXPECT_TRUE(tvps[i]->WaitForFrameProcessors());
  }
}

}  // namespace test
}  // namespace media

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  base::CommandLine::Init(argc, argv);

  // Using shared memory requires mojo to be initialized (crbug.com/849207).
  mojo::core::Init();

  // Needed to enable DVLOG through --vmodule.
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  LOG_ASSERT(logging::InitLogging(settings));

  // Set up our test environment
  const media::test::Video* video =
      &media::test::kDefaultTestVideoCollection[0];
  media::test::g_env = static_cast<media::test::VideoDecoderTestEnvironment*>(
      testing::AddGlobalTestEnvironment(
          new media::test::VideoDecoderTestEnvironment(video)));

  return RUN_ALL_TESTS();
}
