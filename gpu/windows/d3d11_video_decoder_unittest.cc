// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/windows/d3d11_video_decoder.h"

#include <d3d11.h>
#include <d3d11_1.h>
#include <initguid.h>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_task_environment.h"
#include "base/threading/thread_task_runner_handle.h"
#include "media/base/decoder_buffer.h"
#include "media/base/media_log.h"
#include "media/base/media_switches.h"
#include "media/base/media_util.h"
#include "media/base/test_helpers.h"
#include "media/base/win/d3d11_mocks.h"
#include "media/gpu/windows/d3d11_video_decoder_impl.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

namespace media {

class MockD3D11VideoDecoderImpl : public D3D11VideoDecoderImpl {
 public:
  MockD3D11VideoDecoderImpl()
      : D3D11VideoDecoderImpl(
            nullptr,
            base::RepeatingCallback<scoped_refptr<CommandBufferHelper>()>()) {}

  void Initialize(InitCB init_cb,
                  ReturnPictureBufferCB return_picture_buffer_cb) override {
    MockInitialize();
  }

  MOCK_METHOD0(MockInitialize, void());
};

class D3D11VideoDecoderTest : public ::testing::Test {
 public:
  void SetUp() override {
    // Set up some sane defaults.
    gpu_preferences_.enable_zero_copy_dxgi_video = true;
    gpu_preferences_.use_passthrough_cmd_decoder = false;
    gpu_workarounds_.disable_dxgi_zero_copy_video = false;

    // Create a mock D3D11 device that supports 11.0.  Note that if you change
    // this, then you probably also want VideoDevice1 and friends, below.
    mock_d3d11_device_ = CreateD3D11Mock<D3D11DeviceMock>();
    ON_CALL(*mock_d3d11_device_.Get(), GetFeatureLevel)
        .WillByDefault(Return(D3D_FEATURE_LEVEL_11_0));

    mock_d3d11_device_context_ = CreateD3D11Mock<D3D11DeviceContextMock>();
    ON_CALL(*mock_d3d11_device_.Get(), GetImmediateContext(_))
        .WillByDefault(SetComPointee<0>(mock_d3d11_device_context_.Get()));

    // Set up an D3D11VideoDevice rather than ...Device1, since Initialize uses
    // Device for checking decoder GUIDs.
    // TODO(liberato): Try to use Device1 more often.
    mock_d3d11_video_device_ = CreateD3D11Mock<D3D11VideoDeviceMock>();
    ON_CALL(*mock_d3d11_device_.Get(), QueryInterface(IID_ID3D11VideoDevice, _))
        .WillByDefault(DoAll(SetComPointee<1>(mock_d3d11_video_device_.Get()),
                             Return(S_OK)));

    EnableDecoder(D3D11_DECODER_PROFILE_H264_VLD_NOFGT);

    mock_d3d11_video_decoder_ = CreateD3D11Mock<D3D11VideoDecoderMock>();
    ON_CALL(*mock_d3d11_video_device_.Get(), CreateVideoDecoder(_, _, _))
        .WillByDefault(
            SetComPointeeAndReturnOk<2>(mock_d3d11_video_decoder_.Get()));

    mock_d3d11_video_context_ = CreateD3D11Mock<D3D11VideoContextMock>();
    ON_CALL(*mock_d3d11_device_context_.Get(),
            QueryInterface(IID_ID3D11VideoContext, _))
        .WillByDefault(
            SetComPointeeAndReturnOk<1>(mock_d3d11_video_context_.Get()));
  }

  // Enable a decoder for the given GUID.  Only one decoder may be enabled at a
  // time.  GUIDs are things like D3D11_DECODER_PROFILE_H264_VLD_NOFGT or
  // D3D11_DECODER_PROFILE_VP9_VLD_PROFILE0, etc.
  void EnableDecoder(GUID decoder_profile) {
    ON_CALL(*mock_d3d11_video_device_.Get(), GetVideoDecoderProfileCount())
        .WillByDefault(Return(1));

    // Note that we don't check if the guid in the config actually matches
    // |decoder_profile|.  Perhaps we should.
    ON_CALL(*mock_d3d11_video_device_.Get(), GetVideoDecoderProfile(0, _))
        .WillByDefault(DoAll(SetArgPointee<1>(decoder_profile), Return(S_OK)));

    ON_CALL(*mock_d3d11_video_device_.Get(), GetVideoDecoderConfigCount(_, _))
        .WillByDefault(DoAll(
            Invoke(this, &D3D11VideoDecoderTest::GetVideoDecoderConfigCount),
            Return(S_OK)));

    video_decoder_config_.ConfigBitstreamRaw =
        decoder_profile == D3D11_DECODER_PROFILE_H264_VLD_NOFGT ? 2 : 1;

    ON_CALL(*mock_d3d11_video_device_.Get(), GetVideoDecoderConfig(_, 0, _))
        .WillByDefault(
            DoAll(SetArgPointee<2>(video_decoder_config_), Return(S_OK)));
  }

  void GetVideoDecoderConfigCount(const D3D11_VIDEO_DECODER_DESC* desc,
                                  UINT* config_count_out) {
    last_video_decoder_desc_ = *desc;
    *config_count_out = 1;
  }

  // Most recently provided video decoder desc.
  base::Optional<D3D11_VIDEO_DECODER_DESC> last_video_decoder_desc_;
  D3D11_VIDEO_DECODER_CONFIG video_decoder_config_;

  void TearDown() override {
    decoder_.reset();
    // Run the gpu thread runner to tear down |impl_|.
    base::RunLoop().RunUntilIdle();
  }

  void EnableFeature(const base::Feature& feature) {
    scoped_feature_list_.emplace();
    scoped_feature_list_->InitAndEnableFeature(feature);
  }

  void DisableFeature(const base::Feature& feature) {
    scoped_feature_list_.emplace();
    scoped_feature_list_->InitAndDisableFeature(feature);
  }

  void CreateDecoder() {
    std::unique_ptr<MockD3D11VideoDecoderImpl> impl =
        std::make_unique<NiceMock<MockD3D11VideoDecoderImpl>>();
    impl_ = impl.get();

    gpu_task_runner_ = base::ThreadTaskRunnerHandle::Get();

    // We store it in a std::unique_ptr<VideoDecoder> so that the default
    // deleter works.  The dtor is protected.
    decoder_ = base::WrapUnique<VideoDecoder>(
        d3d11_decoder_raw_ = new D3D11VideoDecoder(
            gpu_task_runner_, std::make_unique<NullMediaLog>(),
            gpu_preferences_, gpu_workarounds_, std::move(impl),
            base::RepeatingCallback<scoped_refptr<CommandBufferHelper>()>()));
    d3d11_decoder_raw_->SetCreateDeviceCallbackForTesting(base::BindRepeating(
        [](Microsoft::WRL::ComPtr<ID3D11Device> device) { return device; },
        mock_d3d11_device_));
  }

  enum InitExpectation {
    kExpectFailure = false,
    kExpectSuccess = true,
  };

  void InitializeDecoder(const VideoDecoderConfig& config,
                         InitExpectation expectation) {
    const bool low_delay = false;
    CdmContext* cdm_context = nullptr;

    if (expectation == kExpectSuccess) {
      EXPECT_CALL(*this, MockInitCB(_)).Times(0);
      EXPECT_CALL(*impl_, MockInitialize());
    } else {
      EXPECT_CALL(*this, MockInitCB(false));
    }
    decoder_->Initialize(config, low_delay, cdm_context,
                         base::BindRepeating(&D3D11VideoDecoderTest::MockInitCB,
                                             base::Unretained(this)),
                         base::DoNothing(), base::DoNothing());
    base::RunLoop().RunUntilIdle();
  }

  MOCK_METHOD1(MockInitCB, void(bool));

  base::test::ScopedTaskEnvironment env_;

  scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner_;

  std::unique_ptr<VideoDecoder> decoder_;
  D3D11VideoDecoder* d3d11_decoder_raw_ = nullptr;
  gpu::GpuPreferences gpu_preferences_;
  gpu::GpuDriverBugWorkarounds gpu_workarounds_;
  MockD3D11VideoDecoderImpl* impl_ = nullptr;

  Microsoft::WRL::ComPtr<D3D11DeviceMock> mock_d3d11_device_;
  Microsoft::WRL::ComPtr<D3D11DeviceContextMock> mock_d3d11_device_context_;
  Microsoft::WRL::ComPtr<D3D11VideoDeviceMock> mock_d3d11_video_device_;
  Microsoft::WRL::ComPtr<D3D11VideoDecoderMock> mock_d3d11_video_decoder_;
  Microsoft::WRL::ComPtr<D3D11VideoContextMock> mock_d3d11_video_context_;

  base::Optional<base::test::ScopedFeatureList> scoped_feature_list_;
};

TEST_F(D3D11VideoDecoderTest, SupportsVP9WithFlagAndDecoderEnabled) {
  CreateDecoder();

  VideoDecoderConfig configuration =
      TestVideoConfig::NormalCodecProfile(kCodecVP9, VP9PROFILE_PROFILE0);

  EnableFeature(kD3D11VP9Decoder);
  EnableDecoder(D3D11_DECODER_PROFILE_VP9_VLD_PROFILE0);
  InitializeDecoder(configuration, kExpectSuccess);
}

TEST_F(D3D11VideoDecoderTest, DoesNotSupportVP9WithoutDecoderEnabled) {
  CreateDecoder();

  VideoDecoderConfig configuration =
      TestVideoConfig::NormalCodecProfile(kCodecVP9, VP9PROFILE_PROFILE0);

  EnableFeature(kD3D11VP9Decoder);
  EnableDecoder(D3D11_DECODER_PROFILE_H264_VLD_NOFGT);  // Paranoia, not VP9.
  InitializeDecoder(configuration, kExpectFailure);
}

TEST_F(D3D11VideoDecoderTest, DoesNotSupportVP9WithoutFlagEnabled) {
  CreateDecoder();

  VideoDecoderConfig configuration =
      TestVideoConfig::NormalCodecProfile(kCodecVP9, VP9PROFILE_PROFILE0);

  DisableFeature(kD3D11VP9Decoder);
  InitializeDecoder(configuration, kExpectFailure);
}

TEST_F(D3D11VideoDecoderTest, DoesNotSupportsH264HIGH10Profile) {
  CreateDecoder();

  VideoDecoderConfig high10 = TestVideoConfig::NormalCodecProfile(
      kCodecH264, H264PROFILE_HIGH10PROFILE);

  InitializeDecoder(high10, kExpectFailure);
}

TEST_F(D3D11VideoDecoderTest, SupportsH264) {
  CreateDecoder();

  VideoDecoderConfig normal =
      TestVideoConfig::NormalCodecProfile(kCodecH264, H264PROFILE_MAIN);

  InitializeDecoder(normal, kExpectSuccess);
  // TODO(liberato): Check |last_video_decoder_desc_| for sanity.
}

TEST_F(D3D11VideoDecoderTest, RequiresZeroCopyPreference) {
  gpu_preferences_.enable_zero_copy_dxgi_video = false;
  CreateDecoder();
  InitializeDecoder(
      TestVideoConfig::NormalCodecProfile(kCodecH264, H264PROFILE_MAIN),
      kExpectFailure);
}

TEST_F(D3D11VideoDecoderTest, FailsIfZeroCopyWorkaround) {
  gpu_workarounds_.disable_dxgi_zero_copy_video = true;
  CreateDecoder();
  InitializeDecoder(
      TestVideoConfig::NormalCodecProfile(kCodecH264, H264PROFILE_MAIN),
      kExpectFailure);
}

TEST_F(D3D11VideoDecoderTest, DoesNotSupportEncryptionWithoutFlag) {
  CreateDecoder();
  VideoDecoderConfig encrypted_config =
      TestVideoConfig::NormalCodecProfile(kCodecH264, H264PROFILE_MAIN);
  encrypted_config.SetIsEncrypted(true);

  DisableFeature(kHardwareSecureDecryption);
  InitializeDecoder(encrypted_config, kExpectFailure);
}

TEST_F(D3D11VideoDecoderTest, DoesNotSupportEncryptionWithFlagOn11_0) {
  CreateDecoder();
  VideoDecoderConfig encrypted_config =
      TestVideoConfig::NormalEncrypted(kCodecH264, H264PROFILE_MAIN);
  // TODO(liberato): Provide a CdmContext, so that this test is identical to the
  // 11.1 version, except for the D3D11 version.

  EnableFeature(kHardwareSecureDecryption);
  InitializeDecoder(encrypted_config, kExpectFailure);
}

TEST_F(D3D11VideoDecoderTest, DISABLED_SupportsEncryptionWithFlagOn11_1) {
  // This test fails, probably because we don't provide a CdmContext.
  CreateDecoder();
  VideoDecoderConfig encrypted_config =
      TestVideoConfig::NormalEncrypted(kCodecH264, H264PROFILE_MAIN);
  encrypted_config.SetIsEncrypted(true);
  ON_CALL(*mock_d3d11_device_.Get(), GetFeatureLevel)
      .WillByDefault(Return(D3D_FEATURE_LEVEL_11_1));
  EnableFeature(kHardwareSecureDecryption);
  InitializeDecoder(encrypted_config, kExpectSuccess);
}

// TODO(xhwang): Add tests to cover kWaitingForNewKey and kWaitingForReset.

}  // namespace media
