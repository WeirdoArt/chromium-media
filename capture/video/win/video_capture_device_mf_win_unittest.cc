// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mfapi.h>
#include <mferror.h>
#include <stddef.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/test/task_environment.h"
#include "media/capture/video/win/sink_filter_win.h"
#include "media/capture/video/win/video_capture_device_factory_win.h"
#include "media/capture/video/win/video_capture_device_mf_win.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;
using ::testing::AtLeast;
using ::testing::Mock;
using Microsoft::WRL::ComPtr;

namespace media {

namespace {
constexpr int kArcSecondsInDegree = 3600;
constexpr int kHundredMicrosecondsInSecond = 10000;

constexpr long kCameraControlCurrentBase = 10;
constexpr long kCameraControlMinBase = -20;
constexpr long kCameraControlMaxBase = 20;
constexpr long kCameraControlStep = 2;
constexpr long kVideoProcAmpCurrentBase = 25;
constexpr long kVideoProcAmpMinBase = -50;
constexpr long kVideoProcAmpMaxBase = 50;
constexpr long kVideoProcAmpStep = 1;

class MockClient : public VideoCaptureDevice::Client {
 public:
  void OnIncomingCapturedData(const uint8_t* data,
                              int length,
                              const VideoCaptureFormat& frame_format,
                              const gfx::ColorSpace& color_space,
                              int clockwise_rotation,
                              bool flip_y,
                              base::TimeTicks reference_time,
                              base::TimeDelta timestamp,
                              int frame_feedback_id = 0) override {}

  void OnIncomingCapturedGfxBuffer(gfx::GpuMemoryBuffer* buffer,
                                   const VideoCaptureFormat& frame_format,
                                   int clockwise_rotation,
                                   base::TimeTicks reference_time,
                                   base::TimeDelta timestamp,
                                   int frame_feedback_id = 0) override {}

  MOCK_METHOD4(ReserveOutputBuffer,
               ReserveResult(const gfx::Size&, VideoPixelFormat, int, Buffer*));

  void OnIncomingCapturedBuffer(Buffer buffer,
                                const VideoCaptureFormat& format,
                                base::TimeTicks reference_,
                                base::TimeDelta timestamp) override {}

  void OnIncomingCapturedBufferExt(
      Buffer buffer,
      const VideoCaptureFormat& format,
      const gfx::ColorSpace& color_space,
      base::TimeTicks reference_time,
      base::TimeDelta timestamp,
      gfx::Rect visible_rect,
      const VideoFrameMetadata& additional_metadata) override {}

  MOCK_METHOD3(OnError,
               void(VideoCaptureError,
                    const base::Location&,
                    const std::string&));

  MOCK_METHOD1(OnFrameDropped, void(VideoCaptureFrameDropReason));

  double GetBufferPoolUtilization() const override { return 0.0; }

  MOCK_METHOD0(OnStarted, void());
};

class MockImageCaptureClient
    : public base::RefCountedThreadSafe<MockImageCaptureClient> {
 public:
  // GMock doesn't support move-only arguments, so we use this forward method.
  void DoOnGetPhotoState(mojom::PhotoStatePtr received_state) {
    state = std::move(received_state);
  }

  MOCK_METHOD1(OnCorrectSetPhotoOptions, void(bool));

  // GMock doesn't support move-only arguments, so we use this forward method.
  void DoOnPhotoTaken(mojom::BlobPtr blob) {
    EXPECT_TRUE(blob);
    OnCorrectPhotoTaken();
  }
  MOCK_METHOD0(OnCorrectPhotoTaken, void(void));

  mojom::PhotoStatePtr state;

 private:
  friend class base::RefCountedThreadSafe<MockImageCaptureClient>;
  virtual ~MockImageCaptureClient() = default;
};

template <class Interface>
Interface* AddReference(Interface* object) {
  DCHECK(object);
  object->AddRef();
  return object;
}

template <class Interface>
class MockInterface
    : public base::RefCountedThreadSafe<MockInterface<Interface>>,
      public Interface {
 public:
  // IUnknown
  IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override {
    if (riid == __uuidof(this) || riid == __uuidof(IUnknown)) {
      *object = AddReference(this);
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  IFACEMETHODIMP_(ULONG) AddRef() override {
    base::RefCountedThreadSafe<MockInterface>::AddRef();
    return 1U;
  }
  IFACEMETHODIMP_(ULONG) Release() override {
    base::RefCountedThreadSafe<MockInterface>::Release();
    return 1U;
  }

 protected:
  friend class base::RefCountedThreadSafe<MockInterface<Interface>>;
  virtual ~MockInterface() = default;
};

class MockAMCameraControl final : public MockInterface<IAMCameraControl> {
 public:
  IFACEMETHODIMP Get(long property, long* value, long* flags) override {
    switch (property) {
      case CameraControl_Pan:
      case CameraControl_Tilt:
      case CameraControl_Roll:
      case CameraControl_Zoom:
      case CameraControl_Exposure:
      case CameraControl_Iris:
      case CameraControl_Focus:
        *value = kCameraControlCurrentBase + property;
        *flags = CameraControl_Flags_Auto;
        return S_OK;
      default:
        NOTREACHED();
        return E_NOTIMPL;
    }
  }
  IFACEMETHODIMP GetRange(long property,
                          long* min,
                          long* max,
                          long* step,
                          long* default_value,
                          long* caps_flags) override {
    switch (property) {
      case CameraControl_Pan:
      case CameraControl_Tilt:
      case CameraControl_Roll:
      case CameraControl_Zoom:
      case CameraControl_Exposure:
      case CameraControl_Iris:
      case CameraControl_Focus:
        *min = kCameraControlMinBase + property;
        *max = kCameraControlMaxBase + property;
        *step = kCameraControlStep;
        *default_value = (*min + *max) / 2;
        *caps_flags = CameraControl_Flags_Auto | CameraControl_Flags_Manual;
        return S_OK;
      default:
        NOTREACHED();
        return E_NOTIMPL;
    }
  }
  IFACEMETHODIMP Set(long property, long value, long flags) override {
    return E_NOTIMPL;
  }

 protected:
  ~MockAMCameraControl() override = default;
};

class MockAMVideoProcAmp final : public MockInterface<IAMVideoProcAmp> {
 public:
  IFACEMETHODIMP Get(long property, long* value, long* flags) override {
    switch (property) {
      case VideoProcAmp_Brightness:
      case VideoProcAmp_Contrast:
      case VideoProcAmp_Hue:
      case VideoProcAmp_Saturation:
      case VideoProcAmp_Sharpness:
      case VideoProcAmp_Gamma:
      case VideoProcAmp_ColorEnable:
      case VideoProcAmp_WhiteBalance:
      case VideoProcAmp_BacklightCompensation:
      case VideoProcAmp_Gain:
        *value = kVideoProcAmpCurrentBase + property;
        *flags = VideoProcAmp_Flags_Auto;
        return S_OK;
      default:
        NOTREACHED();
        return E_NOTIMPL;
    }
  }
  IFACEMETHODIMP GetRange(long property,
                          long* min,
                          long* max,
                          long* step,
                          long* default_value,
                          long* caps_flags) override {
    switch (property) {
      case VideoProcAmp_Brightness:
      case VideoProcAmp_Contrast:
      case VideoProcAmp_Hue:
      case VideoProcAmp_Saturation:
      case VideoProcAmp_Sharpness:
      case VideoProcAmp_Gamma:
      case VideoProcAmp_ColorEnable:
      case VideoProcAmp_WhiteBalance:
      case VideoProcAmp_BacklightCompensation:
      case VideoProcAmp_Gain:
        *min = kVideoProcAmpMinBase + property;
        *max = kVideoProcAmpMaxBase + property;
        *step = kVideoProcAmpStep;
        *default_value = (*min + *max) / 2;
        *caps_flags = VideoProcAmp_Flags_Auto | VideoProcAmp_Flags_Manual;
        return S_OK;
      default:
        NOTREACHED();
        return E_NOTIMPL;
    }
  }
  IFACEMETHODIMP Set(long property, long value, long flags) override {
    return E_NOTIMPL;
  }

 protected:
  ~MockAMVideoProcAmp() override = default;
};

class MockMFMediaSource : public MockInterface<IMFMediaSource> {
 public:
  // IUnknown
  IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override {
    if (riid == __uuidof(IAMCameraControl)) {
      *object = AddReference(new MockAMCameraControl);
      return S_OK;
    }
    if (riid == __uuidof(IAMVideoProcAmp)) {
      *object = AddReference(new MockAMVideoProcAmp);
      return S_OK;
    }
    return MockInterface::QueryInterface(riid, object);
  }
  // IMFMediaEventGenerator
  IFACEMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback,
                               IUnknown* punkState) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP EndGetEvent(IMFAsyncResult* pResult,
                             IMFMediaEvent** ppEvent) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP QueueEvent(MediaEventType met,
                            REFGUID guidExtendedType,
                            HRESULT hrStatus,
                            const PROPVARIANT* pvValue) override {
    return E_NOTIMPL;
  }
  // IMFMediaSource
  IFACEMETHODIMP GetCharacteristics(DWORD* pdwCharacteristics) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP CreatePresentationDescriptor(
      IMFPresentationDescriptor** ppPresentationDescriptor) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Start(IMFPresentationDescriptor* pPresentationDescriptor,
                       const GUID* pguidTimeFormat,
                       const PROPVARIANT* pvarStartPosition) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Stop(void) override { return E_NOTIMPL; }
  IFACEMETHODIMP Pause(void) override { return E_NOTIMPL; }
  IFACEMETHODIMP Shutdown(void) override { return E_NOTIMPL; }

 private:
  ~MockMFMediaSource() override = default;
};

class MockMFCaptureSource : public MockInterface<IMFCaptureSource> {
 public:
  IFACEMETHODIMP GetCaptureDeviceSource(
      MF_CAPTURE_ENGINE_DEVICE_TYPE mfCaptureEngineDeviceType,
      IMFMediaSource** ppMediaSource) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetCaptureDeviceActivate(
      MF_CAPTURE_ENGINE_DEVICE_TYPE mfCaptureEngineDeviceType,
      IMFActivate** ppActivate) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetService(REFIID rguidService,
                            REFIID riid,
                            IUnknown** ppUnknown) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP AddEffect(DWORD dwSourceStreamIndex,
                           IUnknown* pUnknown) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP RemoveEffect(DWORD dwSourceStreamIndex,
                              IUnknown* pUnknown) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP RemoveAllEffects(DWORD dwSourceStreamIndex) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetAvailableDeviceMediaType(
      DWORD stream_index,
      DWORD media_type_index,
      IMFMediaType** media_type) override {
    return DoGetAvailableDeviceMediaType(stream_index, media_type_index,
                                         media_type);
  }

  MOCK_METHOD3(DoGetAvailableDeviceMediaType,
               HRESULT(DWORD, DWORD, IMFMediaType**));

  IFACEMETHODIMP SetCurrentDeviceMediaType(DWORD dwSourceStreamIndex,
                                           IMFMediaType* pMediaType) override {
    return DoSetCurrentDeviceMediaType(dwSourceStreamIndex, pMediaType);
  }

  MOCK_METHOD2(DoSetCurrentDeviceMediaType, HRESULT(DWORD, IMFMediaType*));

  IFACEMETHODIMP GetCurrentDeviceMediaType(DWORD stream_index,
                                           IMFMediaType** media_type) {
    return DoGetCurrentDeviceMediaType(stream_index, media_type);
  }
  MOCK_METHOD2(DoGetCurrentDeviceMediaType, HRESULT(DWORD, IMFMediaType**));

  IFACEMETHODIMP GetDeviceStreamCount(DWORD* count) {
    return DoGetDeviceStreamCount(count);
  }
  MOCK_METHOD1(DoGetDeviceStreamCount, HRESULT(DWORD*));

  IFACEMETHODIMP GetDeviceStreamCategory(
      DWORD stream_index,
      MF_CAPTURE_ENGINE_STREAM_CATEGORY* category) {
    return DoGetDeviceStreamCategory(stream_index, category);
  }
  MOCK_METHOD2(DoGetDeviceStreamCategory,
               HRESULT(DWORD, MF_CAPTURE_ENGINE_STREAM_CATEGORY*));

  IFACEMETHODIMP GetMirrorState(DWORD dwStreamIndex,
                                BOOL* pfMirrorState) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetMirrorState(DWORD dwStreamIndex,
                                BOOL fMirrorState) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetStreamIndexFromFriendlyName(
      UINT32 uifriendlyName,
      DWORD* pdwActualStreamIndex) override {
    return E_NOTIMPL;
  }

 private:
  ~MockMFCaptureSource() override = default;
};

class MockCapturePreviewSink : public MockInterface<IMFCapturePreviewSink> {
 public:
  IFACEMETHODIMP GetOutputMediaType(DWORD dwSinkStreamIndex,
                                    IMFMediaType** ppMediaType) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetService(DWORD dwSinkStreamIndex,
                            REFGUID rguidService,
                            REFIID riid,
                            IUnknown** ppUnknown) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP AddStream(DWORD stream_index,
                           IMFMediaType* media_type,
                           IMFAttributes* attributes,
                           DWORD* sink_stream_index) override {
    return DoAddStream(stream_index, media_type, attributes, sink_stream_index);
  }

  MOCK_METHOD4(DoAddStream,
               HRESULT(DWORD, IMFMediaType*, IMFAttributes*, DWORD*));

  IFACEMETHODIMP Prepare(void) override { return E_NOTIMPL; }
  IFACEMETHODIMP RemoveAllStreams(void) override { return S_OK; }
  IFACEMETHODIMP SetRenderHandle(HANDLE handle) override { return E_NOTIMPL; }
  IFACEMETHODIMP SetRenderSurface(IUnknown* pSurface) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP UpdateVideo(const MFVideoNormalizedRect* pSrc,
                             const RECT* pDst,
                             const COLORREF* pBorderClr) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetSampleCallback(
      DWORD dwStreamSinkIndex,
      IMFCaptureEngineOnSampleCallback* pCallback) override {
    sample_callback = pCallback;
    return S_OK;
  }
  IFACEMETHODIMP GetMirrorState(BOOL* pfMirrorState) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetMirrorState(BOOL fMirrorState) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetRotation(DWORD dwStreamIndex,
                             DWORD* pdwRotationValue) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetRotation(DWORD dwStreamIndex,
                             DWORD dwRotationValue) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetCustomSink(IMFMediaSink* pMediaSink) override {
    return E_NOTIMPL;
  }

  scoped_refptr<IMFCaptureEngineOnSampleCallback> sample_callback;

 private:
  ~MockCapturePreviewSink() override = default;
};

class MockCapturePhotoSink : public MockInterface<IMFCapturePhotoSink> {
 public:
  IFACEMETHODIMP GetOutputMediaType(DWORD dwSinkStreamIndex,
                                    IMFMediaType** ppMediaType) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetService(DWORD dwSinkStreamIndex,
                            REFGUID rguidService,
                            REFIID riid,
                            IUnknown** ppUnknown) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP AddStream(DWORD dwSourceStreamIndex,
                           IMFMediaType* pMediaType,
                           IMFAttributes* pAttributes,
                           DWORD* pdwSinkStreamIndex) override {
    return S_OK;
  }
  IFACEMETHODIMP Prepare(void) override { return E_NOTIMPL; }
  IFACEMETHODIMP RemoveAllStreams(void) override { return S_OK; }

  IFACEMETHODIMP SetOutputFileName(LPCWSTR fileName) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetSampleCallback(
      IMFCaptureEngineOnSampleCallback* pCallback) override {
    sample_callback = pCallback;
    return S_OK;
  }
  IFACEMETHODIMP SetOutputByteStream(IMFByteStream* pByteStream) override {
    return E_NOTIMPL;
  }

  scoped_refptr<IMFCaptureEngineOnSampleCallback> sample_callback;

 private:
  ~MockCapturePhotoSink() override = default;
};

class MockMFCaptureEngine : public MockInterface<IMFCaptureEngine> {
 public:
  IFACEMETHODIMP Initialize(IMFCaptureEngineOnEventCallback* pEventCallback,
                            IMFAttributes* pAttributes,
                            IUnknown* pAudioSource,
                            IUnknown* pVideoSource) override {
    EXPECT_TRUE(pEventCallback);
    EXPECT_TRUE(pAttributes);
    EXPECT_TRUE(pVideoSource);
    event_callback = pEventCallback;
    OnCorrectInitializeQueued();

    ON_CALL(*this, OnInitStatus).WillByDefault(Return(S_OK));
    ON_CALL(*this, OnInitEventGuid)
        .WillByDefault(Return(MF_CAPTURE_ENGINE_INITIALIZED));
    // HW Cameras usually add about 500ms latency on init
    ON_CALL(*this, InitEventDelay)
        .WillByDefault(Return(base::TimeDelta::FromMilliseconds(500)));

    base::TimeDelta event_delay = InitEventDelay();

    base::ThreadPool::PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&MockMFCaptureEngine::FireCaptureEvent, this,
                       OnInitEventGuid(), OnInitStatus()),
        event_delay);
    // if zero is passed ensure event fires before wait starts
    if (event_delay == base::TimeDelta::FromMilliseconds(0)) {
      base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(200));
    }

    return S_OK;
  }

  MOCK_METHOD0(OnCorrectInitializeQueued, void(void));
  MOCK_METHOD0(OnInitEventGuid, GUID(void));
  MOCK_METHOD0(OnInitStatus, HRESULT(void));
  MOCK_METHOD0(InitEventDelay, base::TimeDelta(void));

  IFACEMETHODIMP StartPreview(void) override {
    OnStartPreview();
    return S_OK;
  }

  MOCK_METHOD0(OnStartPreview, void(void));

  IFACEMETHODIMP StopPreview(void) override {
    OnStopPreview();
    return S_OK;
  }

  MOCK_METHOD0(OnStopPreview, void(void));

  IFACEMETHODIMP StartRecord(void) override { return E_NOTIMPL; }
  IFACEMETHODIMP StopRecord(BOOL bFinalize, BOOL bFlushUnprocessedSamples) {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP TakePhoto(void) override {
    OnTakePhoto();
    return S_OK;
  }
  MOCK_METHOD0(OnTakePhoto, void(void));

  IFACEMETHODIMP GetSink(MF_CAPTURE_ENGINE_SINK_TYPE type,
                         IMFCaptureSink** sink) {
    return DoGetSink(type, sink);
  }
  MOCK_METHOD2(DoGetSink,
               HRESULT(MF_CAPTURE_ENGINE_SINK_TYPE, IMFCaptureSink**));

  IFACEMETHODIMP GetSource(IMFCaptureSource** source) {
    *source = DoGetSource();
    return source ? S_OK : E_FAIL;
  }
  MOCK_METHOD0(DoGetSource, IMFCaptureSource*());

  void FireCaptureEvent(GUID event, HRESULT hrStatus) {
    ComPtr<IMFMediaEvent> captureEvent;
    MFCreateMediaEvent(MEExtendedType, event, hrStatus, nullptr, &captureEvent);
    if (event_callback) {
      event_callback->OnEvent(captureEvent.Get());
    }
  }
  scoped_refptr<IMFCaptureEngineOnEventCallback> event_callback;
 private:
  ~MockMFCaptureEngine() override = default;
};

class StubMFMediaType : public MockInterface<IMFMediaType> {
 public:
  StubMFMediaType(GUID major_type,
                  GUID sub_type,
                  int frame_width,
                  int frame_height,
                  int frame_rate)
      : major_type_(major_type),
        sub_type_(sub_type),
        frame_width_(frame_width),
        frame_height_(frame_height),
        frame_rate_(frame_rate) {}

  IFACEMETHODIMP GetItem(REFGUID key, PROPVARIANT* value) override {
    if (key == MF_MT_FRAME_SIZE) {
      value->vt = VT_UI8;
      value->uhVal.QuadPart = Pack2UINT32AsUINT64(frame_width_, frame_height_);
      return S_OK;
    }
    if (key == MF_MT_FRAME_RATE) {
      value->vt = VT_UI8;
      value->uhVal.QuadPart = Pack2UINT32AsUINT64(frame_rate_, 1);
      return S_OK;
    }
    if (key == MF_MT_PIXEL_ASPECT_RATIO) {
      value->vt = VT_UI8;
      value->uhVal.QuadPart = Pack2UINT32AsUINT64(1, 1);
      return S_OK;
    }
    if (key == MF_MT_INTERLACE_MODE) {
      value->vt = VT_UI4;
      value->uintVal = MFVideoInterlace_Progressive;
      return S_OK;
    }
    return E_FAIL;
  }
  IFACEMETHODIMP GetItemType(REFGUID guidKey,
                             MF_ATTRIBUTE_TYPE* pType) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP CompareItem(REFGUID guidKey,
                             REFPROPVARIANT Value,
                             BOOL* pbResult) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Compare(IMFAttributes* pTheirs,
                         MF_ATTRIBUTES_MATCH_TYPE MatchType,
                         BOOL* pbResult) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetUINT32(REFGUID key, UINT32* value) override {
    if (key == MF_MT_INTERLACE_MODE) {
      *value = MFVideoInterlace_Progressive;
      return S_OK;
    }
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetUINT64(REFGUID key, UINT64* value) override {
    if (key == MF_MT_FRAME_SIZE) {
      *value = (long long)frame_width_ << 32 | frame_height_;
      return S_OK;
    }
    if (key == MF_MT_FRAME_RATE) {
      *value = (long long)frame_rate_ << 32 | 1;
      return S_OK;
    }
    if (key == MF_MT_PIXEL_ASPECT_RATIO) {
      *value = (long long)1 << 32 | 1;
      return S_OK;
    }
    return E_FAIL;
  }
  IFACEMETHODIMP GetDouble(REFGUID guidKey, double* pfValue) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetGUID(REFGUID key, GUID* value) override {
    if (key == MF_MT_MAJOR_TYPE) {
      *value = major_type_;
      return S_OK;
    }
    if (key == MF_MT_SUBTYPE) {
      *value = sub_type_;
      return S_OK;
    }
    return E_FAIL;
  }
  IFACEMETHODIMP GetStringLength(REFGUID guidKey, UINT32* pcchLength) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetString(REFGUID guidKey,
                           LPWSTR pwszValue,
                           UINT32 cchBufSize,
                           UINT32* pcchLength) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetAllocatedString(REFGUID guidKey,
                                    LPWSTR* ppwszValue,
                                    UINT32* pcchLength) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetBlob(REFGUID guidKey,
                         UINT8* pBuf,
                         UINT32 cbBufSize,
                         UINT32* pcbBlobSize) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetAllocatedBlob(REFGUID guidKey,
                                  UINT8** ppBuf,
                                  UINT32* pcbSize) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetUnknown(REFGUID guidKey,
                            REFIID riid,
                            LPVOID* ppv) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetItem(REFGUID guidKey, REFPROPVARIANT Value) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP DeleteItem(REFGUID guidKey) override { return E_NOTIMPL; }
  IFACEMETHODIMP DeleteAllItems(void) override { return E_NOTIMPL; }
  IFACEMETHODIMP SetUINT32(REFGUID guidKey, UINT32 unValue) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetUINT64(REFGUID guidKey, UINT64 unValue) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetDouble(REFGUID guidKey, double fValue) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetGUID(REFGUID guidKey, REFGUID guidValue) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetString(REFGUID guidKey, LPCWSTR wszValue) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetBlob(REFGUID guidKey,
                         const UINT8* pBuf,
                         UINT32 cbBufSize) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP SetUnknown(REFGUID guidKey, IUnknown* pUnknown) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP LockStore(void) override { return E_NOTIMPL; }
  IFACEMETHODIMP UnlockStore(void) override { return E_NOTIMPL; }
  IFACEMETHODIMP GetCount(UINT32* pcItems) override { return E_NOTIMPL; }
  IFACEMETHODIMP GetItemByIndex(UINT32 unIndex,
                                GUID* pguidKey,
                                PROPVARIANT* pValue) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP CopyAllItems(IMFAttributes* pDest) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetMajorType(GUID* pguidMajorType) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP IsCompressedFormat(BOOL* pfCompressed) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP IsEqual(IMFMediaType* pIMediaType, DWORD* pdwFlags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetRepresentation(GUID guidRepresentation,
                                   LPVOID* ppvRepresentation) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP FreeRepresentation(GUID guidRepresentation,
                                    LPVOID pvRepresentation) override {
    return E_NOTIMPL;
  }

 private:
  ~StubMFMediaType() override = default;

  const GUID major_type_;
  const GUID sub_type_;
  const int frame_width_;
  const int frame_height_;
  const int frame_rate_;
};

class MockMFMediaEvent : public MockInterface<IMFMediaEvent> {
 public:
  IFACEMETHODIMP GetItem(REFGUID guidKey, PROPVARIANT* pValue) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetItemType(REFGUID guidKey,
                             MF_ATTRIBUTE_TYPE* pType) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP CompareItem(REFGUID guidKey,
                             REFPROPVARIANT Value,
                             BOOL* pbResult) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP Compare(IMFAttributes* pTheirs,
                         MF_ATTRIBUTES_MATCH_TYPE MatchType,
                         BOOL* pbResult) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetUINT32(REFGUID guidKey, UINT32* punValue) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetUINT64(REFGUID guidKey, UINT64* punValue) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetDouble(REFGUID guidKey, double* pfValue) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetGUID(REFGUID guidKey, GUID* pguidValue) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetStringLength(REFGUID guidKey, UINT32* pcchLength) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetString(REFGUID guidKey,
                           LPWSTR pwszValue,
                           UINT32 cchBufSize,
                           UINT32* pcchLength) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetAllocatedString(REFGUID guidKey,
                                    LPWSTR* ppwszValue,
                                    UINT32* pcchLength) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetBlob(REFGUID guidKey,
                         UINT8* pBuf,
                         UINT32 cbBufSize,
                         UINT32* pcbBlobSize) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetAllocatedBlob(REFGUID guidKey,
                                  UINT8** ppBuf,
                                  UINT32* pcbSize) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetUnknown(REFGUID guidKey,
                            REFIID riid,
                            LPVOID* ppv) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP SetItem(REFGUID guidKey, REFPROPVARIANT Value) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP DeleteItem(REFGUID guidKey) override { return E_NOTIMPL; }

  IFACEMETHODIMP DeleteAllItems(void) override { return E_NOTIMPL; }

  IFACEMETHODIMP SetUINT32(REFGUID guidKey, UINT32 unValue) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP SetUINT64(REFGUID guidKey, UINT64 unValue) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP SetDouble(REFGUID guidKey, double fValue) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP SetGUID(REFGUID guidKey, REFGUID guidValue) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP SetString(REFGUID guidKey, LPCWSTR wszValue) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP SetBlob(REFGUID guidKey,
                         const UINT8* pBuf,
                         UINT32 cbBufSize) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP SetUnknown(REFGUID guidKey, IUnknown* pUnknown) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP LockStore(void) override { return E_NOTIMPL; }

  IFACEMETHODIMP UnlockStore(void) override { return E_NOTIMPL; }

  IFACEMETHODIMP GetCount(UINT32* pcItems) override { return E_NOTIMPL; }

  IFACEMETHODIMP GetItemByIndex(UINT32 unIndex,
                                GUID* pguidKey,
                                PROPVARIANT* pValue) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP CopyAllItems(IMFAttributes* pDest) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetType(MediaEventType* pmet) override {
    *pmet = DoGetType();
    return S_OK;
  }
  MOCK_METHOD0(DoGetType, MediaEventType());

  IFACEMETHODIMP GetExtendedType(GUID* pguidExtendedType) override {
    *pguidExtendedType = DoGetExtendedType();
    return S_OK;
  }
  MOCK_METHOD0(DoGetExtendedType, GUID());

  IFACEMETHODIMP GetStatus(HRESULT* status) override {
    *status = DoGetStatus();
    return S_OK;
  }
  MOCK_METHOD0(DoGetStatus, HRESULT());

  IFACEMETHODIMP GetValue(PROPVARIANT* pvValue) override { return E_NOTIMPL; }

 private:
  ~MockMFMediaEvent() override = default;
};

struct DepthDeviceParams {
  GUID depth_video_stream_subtype;
  // Depth stream could offer other (e.g. I420) formats, in addition to 16-bit.
  bool additional_i420_formats_in_depth_stream;
  // Depth device sometimes provides multiple video streams.
  bool additional_i420_video_stream;
};
}  // namespace

const int kArbitraryValidVideoWidth = 1920;
const int kArbitraryValidVideoHeight = 1080;

const int kArbitraryValidPhotoWidth = 3264;
const int kArbitraryValidPhotoHeight = 2448;

class VideoCaptureDeviceMFWinTest : public ::testing::Test {
 protected:
  VideoCaptureDeviceMFWinTest()
      : descriptor_(VideoCaptureDeviceDescriptor()),
        media_source_(new MockMFMediaSource()),
        engine_(new MockMFCaptureEngine()),
        client_(new MockClient()),
        image_capture_client_(new MockImageCaptureClient()),
        device_(
            new VideoCaptureDeviceMFWin(descriptor_, media_source_, engine_)),
        capture_source_(new MockMFCaptureSource()),
        capture_preview_sink_(new MockCapturePreviewSink()),
        media_foundation_supported_(
            VideoCaptureDeviceFactoryWin::PlatformSupportsMediaFoundation()) {}

  void SetUp() override {
    if (!media_foundation_supported_)
      return;
    device_->set_max_retry_count_for_testing(3);
    device_->set_retry_delay_in_ms_for_testing(1);

    EXPECT_CALL(*(engine_.Get()), OnCorrectInitializeQueued());
    EXPECT_TRUE(device_->Init());
    EXPECT_CALL(*(engine_.Get()), DoGetSource())
        .WillRepeatedly(Invoke([this]() {
          this->capture_source_->AddRef();
          return this->capture_source_.get();
        }));
  }

  bool ShouldSkipTest() {
    if (media_foundation_supported_)
      return false;
    DVLOG(1) << "Media foundation is not supported by the current platform. "
                "Skipping test.";
    return true;
  }

  void PrepareMFDeviceWithOneVideoStream(GUID mf_video_subtype) {
    EXPECT_CALL(*capture_source_, DoGetDeviceStreamCount(_))
        .WillRepeatedly(Invoke([](DWORD* stream_count) {
          *stream_count = 1;
          return S_OK;
        }));
    EXPECT_CALL(*capture_source_, DoGetDeviceStreamCategory(0, _))
        .WillRepeatedly(Invoke([](DWORD stream_index,
                                  MF_CAPTURE_ENGINE_STREAM_CATEGORY* category) {
          *category = MF_CAPTURE_ENGINE_STREAM_CATEGORY_VIDEO_PREVIEW;
          return S_OK;
        }));

    EXPECT_CALL(*capture_source_, DoGetAvailableDeviceMediaType(0, _, _))
        .WillRepeatedly(Invoke([mf_video_subtype](DWORD stream_index,
                                                  DWORD media_type_index,
                                                  IMFMediaType** media_type) {
          if (media_type_index != 0)
            return MF_E_NO_MORE_TYPES;

          *media_type = new StubMFMediaType(MFMediaType_Video, mf_video_subtype,
                                            kArbitraryValidVideoWidth,
                                            kArbitraryValidVideoHeight, 30);
          (*media_type)->AddRef();

          return S_OK;
        }));

    EXPECT_CALL(*(engine_.Get()),
                DoGetSink(MF_CAPTURE_ENGINE_SINK_TYPE_PREVIEW, _))
        .WillRepeatedly(Invoke([this](MF_CAPTURE_ENGINE_SINK_TYPE sink_type,
                                      IMFCaptureSink** sink) {
          *sink = this->capture_preview_sink_.get();
          this->capture_preview_sink_->AddRef();
          return S_OK;
        }));

    EXPECT_CALL(*capture_source_, DoGetCurrentDeviceMediaType(_, _))
        .WillRepeatedly(Invoke([mf_video_subtype](DWORD stream_index,
                                                  IMFMediaType** media_type) {
          *media_type = new StubMFMediaType(MFMediaType_Video, mf_video_subtype,
                                            kArbitraryValidVideoWidth,
                                            kArbitraryValidVideoHeight, 30);
          (*media_type)->AddRef();
          return S_OK;
        }));
  }

  void PrepareMFDeviceWithOneVideoStreamAndOnePhotoStream(
      GUID mf_video_subtype) {
    EXPECT_CALL(*capture_source_, DoGetDeviceStreamCount(_))
        .WillRepeatedly(Invoke([](DWORD* stream_count) {
          *stream_count = 2;
          return S_OK;
        }));
    EXPECT_CALL(*capture_source_, DoGetDeviceStreamCategory(_, _))
        .WillRepeatedly(Invoke([](DWORD stream_index,
                                  MF_CAPTURE_ENGINE_STREAM_CATEGORY* category) {
          if (stream_index == 0) {
            *category = MF_CAPTURE_ENGINE_STREAM_CATEGORY_VIDEO_PREVIEW;
            return S_OK;
          } else if (stream_index == 1) {
            *category = MF_CAPTURE_ENGINE_STREAM_CATEGORY_PHOTO_INDEPENDENT;
            return S_OK;
          }
          return E_FAIL;
        }));

    auto get_device_media_type = [mf_video_subtype](DWORD stream_index,
                                                    IMFMediaType** media_type) {
      if (stream_index == 0) {
        *media_type = new StubMFMediaType(MFMediaType_Video, mf_video_subtype,
                                          kArbitraryValidVideoWidth,
                                          kArbitraryValidVideoHeight, 30);
        (*media_type)->AddRef();
        return S_OK;
      } else if (stream_index == 1) {
        *media_type = new StubMFMediaType(
            MFMediaType_Image, GUID_ContainerFormatJpeg,
            kArbitraryValidPhotoWidth, kArbitraryValidPhotoHeight, 0);
        (*media_type)->AddRef();
        return S_OK;
      }
      return E_FAIL;
    };

    EXPECT_CALL(*capture_source_, DoGetAvailableDeviceMediaType(_, _, _))
        .WillRepeatedly(Invoke(
            [get_device_media_type](DWORD stream_index, DWORD media_type_index,
                                    IMFMediaType** media_type) {
              if (media_type_index != 0)
                return MF_E_NO_MORE_TYPES;
              return get_device_media_type(stream_index, media_type);
            }));

    EXPECT_CALL(*(engine_.Get()), DoGetSink(_, _))
        .WillRepeatedly(Invoke([this](MF_CAPTURE_ENGINE_SINK_TYPE sink_type,
                                      IMFCaptureSink** sink) {
          if (sink_type == MF_CAPTURE_ENGINE_SINK_TYPE_PREVIEW) {
            *sink = this->capture_preview_sink_.get();
            this->capture_preview_sink_->AddRef();
            return S_OK;
          } else if (sink_type == MF_CAPTURE_ENGINE_SINK_TYPE_PHOTO) {
            *sink = new MockCapturePhotoSink();
            (*sink)->AddRef();
            return S_OK;
          }
          return E_FAIL;
        }));

    EXPECT_CALL(*capture_source_, DoGetCurrentDeviceMediaType(_, _))
        .WillRepeatedly(Invoke(get_device_media_type));
  }

  void PrepareMFDepthDeviceWithCombinedFormatsAndStreams(
      DepthDeviceParams params) {
    EXPECT_CALL(*capture_source_, DoGetDeviceStreamCount(_))
        .WillRepeatedly(Invoke([params](DWORD* stream_count) {
          *stream_count = params.additional_i420_video_stream ? 2 : 1;
          return S_OK;
        }));
    EXPECT_CALL(*capture_source_, DoGetDeviceStreamCategory(_, _))
        .WillRepeatedly(Invoke([](DWORD stream_index,
                                  MF_CAPTURE_ENGINE_STREAM_CATEGORY* category) {
          if (stream_index <= 1) {
            *category = MF_CAPTURE_ENGINE_STREAM_CATEGORY_VIDEO_PREVIEW;
            return S_OK;
          }
          return E_FAIL;
        }));

    auto get_device_media_type = [params](DWORD stream_index,
                                          IMFMediaType** media_type) {
      if (stream_index == 0) {
        *media_type = new StubMFMediaType(
            MFMediaType_Video, params.depth_video_stream_subtype,
            kArbitraryValidVideoWidth, kArbitraryValidVideoHeight, 30);
        (*media_type)->AddRef();
        return S_OK;
      } else if (stream_index == 1 && params.additional_i420_video_stream) {
        *media_type = new StubMFMediaType(MFMediaType_Video, MFVideoFormat_I420,
                                          kArbitraryValidVideoWidth,
                                          kArbitraryValidVideoHeight, 30);
        (*media_type)->AddRef();
        return S_OK;
      }
      return E_FAIL;
    };

    EXPECT_CALL(*capture_source_, DoGetAvailableDeviceMediaType(_, _, _))
        .WillRepeatedly(Invoke([params, get_device_media_type](
                                   DWORD stream_index, DWORD media_type_index,
                                   IMFMediaType** media_type) {
          if (stream_index == 0 &&
              params.additional_i420_formats_in_depth_stream &&
              media_type_index == 1) {
            *media_type = new StubMFMediaType(
                MFMediaType_Video, MFVideoFormat_I420,
                kArbitraryValidVideoWidth, kArbitraryValidVideoHeight, 30);
            (*media_type)->AddRef();
            return S_OK;
          }
          if (media_type_index != 0)
            return MF_E_NO_MORE_TYPES;
          return get_device_media_type(stream_index, media_type);
        }));

    EXPECT_CALL(*(engine_.Get()),
                DoGetSink(MF_CAPTURE_ENGINE_SINK_TYPE_PREVIEW, _))
        .WillRepeatedly(Invoke([this](MF_CAPTURE_ENGINE_SINK_TYPE sink_type,
                                      IMFCaptureSink** sink) {
          *sink = this->capture_preview_sink_.get();
          this->capture_preview_sink_->AddRef();
          return S_OK;
        }));

    EXPECT_CALL(*capture_source_, DoGetCurrentDeviceMediaType(_, _))
        .WillRepeatedly(Invoke(get_device_media_type));
  }

  VideoCaptureDeviceDescriptor descriptor_;
  Microsoft::WRL::ComPtr<MockMFMediaSource> media_source_;
  Microsoft::WRL::ComPtr<MockMFCaptureEngine> engine_;
  std::unique_ptr<MockClient> client_;
  scoped_refptr<MockImageCaptureClient> image_capture_client_;
  std::unique_ptr<VideoCaptureDeviceMFWin> device_;
  VideoCaptureFormat last_format_;

  scoped_refptr<MockMFCaptureSource> capture_source_;
  scoped_refptr<MockCapturePreviewSink> capture_preview_sink_;
  base::test::TaskEnvironment task_environment_;

 private:
  const bool media_foundation_supported_;
};

// Expects StartPreview() to be called on AllocateAndStart()
TEST_F(VideoCaptureDeviceMFWinTest, StartPreviewOnAllocateAndStart) {
  if (ShouldSkipTest())
    return;

  PrepareMFDeviceWithOneVideoStream(MFVideoFormat_MJPG);

  EXPECT_CALL(*(engine_.Get()), OnStartPreview());
  EXPECT_CALL(*client_, OnStarted());
  EXPECT_CALL(*(engine_.Get()), OnStopPreview());

  device_->AllocateAndStart(VideoCaptureParams(), std::move(client_));
  capture_preview_sink_->sample_callback->OnSample(nullptr);
  device_->StopAndDeAllocate();
}

// Expects OnError() to be called on an errored IMFMediaEvent
TEST_F(VideoCaptureDeviceMFWinTest, CallClientOnErrorMediaEvent) {
  if (ShouldSkipTest())
    return;

  PrepareMFDeviceWithOneVideoStream(MFVideoFormat_MJPG);

  EXPECT_CALL(*(engine_.Get()), OnStartPreview());
  EXPECT_CALL(*client_, OnStarted());
  EXPECT_CALL(*client_, OnError(_, _, _));
  scoped_refptr<MockMFMediaEvent> media_event_error = new MockMFMediaEvent();
  EXPECT_CALL(*media_event_error, DoGetStatus()).WillRepeatedly(Return(E_FAIL));

  device_->AllocateAndStart(VideoCaptureParams(), std::move(client_));
  capture_preview_sink_->sample_callback->OnSample(nullptr);
  engine_->event_callback->OnEvent(media_event_error.get());
}

// Expects Init to fail due to OnError() event
TEST_F(VideoCaptureDeviceMFWinTest, CallClientOnErrorDurringInit) {
  if (ShouldSkipTest())
    return;

  VideoCaptureDeviceDescriptor descriptor = VideoCaptureDeviceDescriptor();
  Microsoft::WRL::ComPtr<MockMFMediaSource> media_source =
      new MockMFMediaSource();
  Microsoft::WRL::ComPtr<MockMFCaptureEngine> engine =
      new MockMFCaptureEngine();
  std::unique_ptr<VideoCaptureDeviceMFWin> device =
      std::make_unique<VideoCaptureDeviceMFWin>(descriptor, media_source,
                                                engine);

  EXPECT_CALL(*(engine.Get()), OnInitEventGuid).WillOnce([]() {
    return MF_CAPTURE_ENGINE_INITIALIZED;
  });
  // E_ACCESSDENIED is thrown if application is denied access in settings UI
  EXPECT_CALL(*(engine.Get()), OnInitStatus).WillOnce([]() {
    return E_ACCESSDENIED;
  });

  EXPECT_CALL(*(engine.Get()), OnCorrectInitializeQueued());

  EXPECT_FALSE(device->Init());
}

// Expects Init to succeed but MF_CAPTURE_ENGINE_INITIALIZED fired before
// WaitOnCaptureEvent is called.
TEST_F(VideoCaptureDeviceMFWinTest, CallClientOnFireCaptureEngineInitEarly) {
  if (ShouldSkipTest())
    return;

  VideoCaptureDeviceDescriptor descriptor = VideoCaptureDeviceDescriptor();
  Microsoft::WRL::ComPtr<MockMFMediaSource> media_source =
      new MockMFMediaSource();
  Microsoft::WRL::ComPtr<MockMFCaptureEngine> engine =
      new MockMFCaptureEngine();
  std::unique_ptr<VideoCaptureDeviceMFWin> device =
      std::make_unique<VideoCaptureDeviceMFWin>(descriptor, media_source,
                                                engine);

  EXPECT_CALL(*(engine.Get()), OnInitEventGuid).WillOnce([]() {
    return MF_CAPTURE_ENGINE_INITIALIZED;
  });
  EXPECT_CALL(*(engine.Get()), InitEventDelay).WillOnce([]() {
    return base::TimeDelta::FromMilliseconds(0);
  });

  EXPECT_CALL(*(engine.Get()), OnCorrectInitializeQueued());

  EXPECT_TRUE(device->Init());
}

// Send MFVideoCallback::OnEvent when VideoCaptureDeviceMFWin has been destroyed
TEST_F(VideoCaptureDeviceMFWinTest,
       SendMFVideoCallbackAfterVideoCaptureDeviceMFWinDestructor) {
  if (ShouldSkipTest())
    return;

  VideoCaptureDeviceDescriptor descriptor = VideoCaptureDeviceDescriptor();
  Microsoft::WRL::ComPtr<MockMFMediaSource> media_source =
      new MockMFMediaSource();
  Microsoft::WRL::ComPtr<MockMFCaptureEngine> engine =
      new MockMFCaptureEngine();
  std::unique_ptr<VideoCaptureDeviceMFWin> device =
      std::make_unique<VideoCaptureDeviceMFWin>(descriptor, media_source,
                                                engine);

  EXPECT_CALL(*(engine.Get()), OnInitEventGuid).WillOnce([]() {
    return MF_CAPTURE_ENGINE_INITIALIZED;
  });

  EXPECT_CALL(*(engine.Get()), OnCorrectInitializeQueued());

  EXPECT_TRUE(device->Init());

  // Force ~VideoCaptureDeviceMFWin() which will invalidate
  // MFVideoCallback::observer_
  device.reset();
  // Send event to MFVideoCallback::OnEvent
  engine->FireCaptureEvent(MF_CAPTURE_ENGINE_ERROR,
                           MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED);
}

// Allocates device with flaky methods failing with MF_E_INVALIDREQUEST and
// expects the device to retry and start correctly
TEST_F(VideoCaptureDeviceMFWinTest, AllocateAndStartWithFlakyInvalidRequest) {
  if (ShouldSkipTest())
    return;

  EXPECT_CALL(*capture_source_, DoGetDeviceStreamCount(_))
      .Times(AtLeast(2))
      .WillOnce(Return(MF_E_INVALIDREQUEST))
      .WillRepeatedly(Invoke([](DWORD* stream_count) {
        *stream_count = 1;
        return S_OK;
      }));
  EXPECT_CALL(*capture_source_, DoGetDeviceStreamCategory(0, _))
      .Times(AtLeast(2))
      .WillOnce(Return(MF_E_INVALIDREQUEST))
      .WillRepeatedly(Invoke(
          [](DWORD stream_index, MF_CAPTURE_ENGINE_STREAM_CATEGORY* category) {
            *category = MF_CAPTURE_ENGINE_STREAM_CATEGORY_VIDEO_PREVIEW;
            return S_OK;
          }));

  EXPECT_CALL(*capture_source_, DoGetAvailableDeviceMediaType(0, _, _))
      .Times(AtLeast(2))
      .WillOnce(Return(MF_E_INVALIDREQUEST))
      .WillRepeatedly(Invoke([](DWORD stream_index, DWORD media_type_index,
                                IMFMediaType** media_type) {
        if (media_type_index != 0)
          return MF_E_NO_MORE_TYPES;

        *media_type = new StubMFMediaType(MFMediaType_Video, MFVideoFormat_MJPG,
                                          kArbitraryValidVideoWidth,
                                          kArbitraryValidVideoHeight, 30);
        (*media_type)->AddRef();

        return S_OK;
      }));

  auto mock_sink = base::MakeRefCounted<MockCapturePreviewSink>();
  EXPECT_CALL(*(engine_.Get()),
              DoGetSink(MF_CAPTURE_ENGINE_SINK_TYPE_PREVIEW, _))
      .WillRepeatedly(Invoke([&mock_sink](MF_CAPTURE_ENGINE_SINK_TYPE sink_type,
                                          IMFCaptureSink** sink) {
        *sink = mock_sink.get();
        (*sink)->AddRef();
        return S_OK;
      }));

  EXPECT_CALL(*(engine_.Get()), OnStartPreview());
  EXPECT_CALL(*client_, OnStarted());
  device_->AllocateAndStart(VideoCaptureParams(), std::move(client_));
  mock_sink->sample_callback->OnSample(nullptr);
}

// Allocates device with methods always failing with MF_E_INVALIDREQUEST and
// expects the device to give up and call OnError()
TEST_F(VideoCaptureDeviceMFWinTest, AllocateAndStartWithFailingInvalidRequest) {
  if (ShouldSkipTest())
    return;

  EXPECT_CALL(*capture_source_, DoGetDeviceStreamCount(_))
      .WillRepeatedly(Return(MF_E_INVALIDREQUEST));

  EXPECT_CALL(*client_, OnError(_, _, _));
  device_->AllocateAndStart(VideoCaptureParams(), std::move(client_));
}

TEST_F(VideoCaptureDeviceMFWinTest,
       SendsOnErrorWithoutOnStartedIfDeviceIsBusy) {
  if (ShouldSkipTest())
    return;

  PrepareMFDeviceWithOneVideoStream(MFVideoFormat_MJPG);

  EXPECT_CALL(*(engine_.Get()), OnStartPreview());
  EXPECT_CALL(*client_, OnStarted()).Times(0);
  EXPECT_CALL(*client_, OnError(_, _, _));

  scoped_refptr<MockMFMediaEvent> media_event_preview_started =
      new MockMFMediaEvent();
  ON_CALL(*media_event_preview_started, DoGetStatus())
      .WillByDefault(Return(S_OK));
  ON_CALL(*media_event_preview_started, DoGetType())
      .WillByDefault(Return(MEExtendedType));
  ON_CALL(*media_event_preview_started, DoGetExtendedType())
      .WillByDefault(Return(MF_CAPTURE_ENGINE_PREVIEW_STARTED));

  scoped_refptr<MockMFMediaEvent> media_event_error = new MockMFMediaEvent();
  EXPECT_CALL(*media_event_error, DoGetStatus()).WillRepeatedly(Return(E_FAIL));

  device_->AllocateAndStart(VideoCaptureParams(), std::move(client_));
  // Even if the device is busy, MediaFoundation sends
  // MF_CAPTURE_ENGINE_PREVIEW_STARTED before sending an error event.
  engine_->event_callback->OnEvent(media_event_preview_started.get());
  engine_->event_callback->OnEvent(media_event_error.get());
}

// Given an |IMFCaptureSource| offering a video stream without photo stream to
// |VideoCaptureDevice|, when asking the photo state from |VideoCaptureDevice|
// then expect the returned state to match the video resolution
TEST_F(VideoCaptureDeviceMFWinTest, GetPhotoStateViaVideoStream) {
  if (ShouldSkipTest())
    return;

  PrepareMFDeviceWithOneVideoStream(MFVideoFormat_MJPG);

  EXPECT_CALL(*(engine_.Get()), OnStartPreview());
  EXPECT_CALL(*client_, OnStarted());

  device_->AllocateAndStart(VideoCaptureParams(), std::move(client_));
  capture_preview_sink_->sample_callback->OnSample(nullptr);

  VideoCaptureDevice::GetPhotoStateCallback get_photo_state_callback =
      base::BindOnce(&MockImageCaptureClient::DoOnGetPhotoState,
                     image_capture_client_);
  device_->GetPhotoState(std::move(get_photo_state_callback));

  mojom::PhotoState* state = image_capture_client_->state.get();
  EXPECT_EQ(state->width->min, kArbitraryValidVideoWidth);
  EXPECT_EQ(state->width->current, kArbitraryValidVideoWidth);
  EXPECT_EQ(state->width->max, kArbitraryValidVideoWidth);

  EXPECT_EQ(state->height->min, kArbitraryValidVideoHeight);
  EXPECT_EQ(state->height->current, kArbitraryValidVideoHeight);
  EXPECT_EQ(state->height->max, kArbitraryValidVideoHeight);
}

// Given an |IMFCaptureSource| offering a video stream and a photo stream to
// |VideoCaptureDevice|, when asking the photo state from |VideoCaptureDevice|
// then expect the returned state to match the available photo resolution
TEST_F(VideoCaptureDeviceMFWinTest, GetPhotoStateViaPhotoStream) {
  if (ShouldSkipTest())
    return;

  PrepareMFDeviceWithOneVideoStreamAndOnePhotoStream(MFVideoFormat_MJPG);

  EXPECT_CALL(*(engine_.Get()), OnStartPreview());
  EXPECT_CALL(*client_, OnStarted());

  device_->AllocateAndStart(VideoCaptureParams(), std::move(client_));
  capture_preview_sink_->sample_callback->OnSample(nullptr);

  VideoCaptureDevice::GetPhotoStateCallback get_photo_state_callback =
      base::BindOnce(&MockImageCaptureClient::DoOnGetPhotoState,
                     image_capture_client_);
  device_->GetPhotoState(std::move(get_photo_state_callback));

  mojom::PhotoState* state = image_capture_client_->state.get();
  EXPECT_EQ(state->width->min, kArbitraryValidPhotoWidth);
  EXPECT_EQ(state->width->current, kArbitraryValidPhotoWidth);
  EXPECT_EQ(state->width->max, kArbitraryValidPhotoWidth);

  EXPECT_EQ(state->height->min, kArbitraryValidPhotoHeight);
  EXPECT_EQ(state->height->current, kArbitraryValidPhotoHeight);
  EXPECT_EQ(state->height->max, kArbitraryValidPhotoHeight);

  EXPECT_EQ(state->supported_white_balance_modes.size(), 2u);
  EXPECT_EQ(std::count(state->supported_white_balance_modes.begin(),
                       state->supported_white_balance_modes.end(),
                       mojom::MeteringMode::CONTINUOUS),
            1);
  EXPECT_EQ(std::count(state->supported_white_balance_modes.begin(),
                       state->supported_white_balance_modes.end(),
                       mojom::MeteringMode::MANUAL),
            1);
  EXPECT_EQ(state->current_white_balance_mode, mojom::MeteringMode::CONTINUOUS);
  EXPECT_EQ(state->supported_exposure_modes.size(), 2u);
  EXPECT_EQ(std::count(state->supported_exposure_modes.begin(),
                       state->supported_exposure_modes.end(),
                       mojom::MeteringMode::CONTINUOUS),
            1);
  EXPECT_EQ(std::count(state->supported_exposure_modes.begin(),
                       state->supported_exposure_modes.end(),
                       mojom::MeteringMode::MANUAL),
            1);
  EXPECT_EQ(state->current_exposure_mode, mojom::MeteringMode::CONTINUOUS);
  EXPECT_EQ(state->supported_focus_modes.size(), 2u);
  EXPECT_EQ(std::count(state->supported_focus_modes.begin(),
                       state->supported_focus_modes.end(),
                       mojom::MeteringMode::CONTINUOUS),
            1);
  EXPECT_EQ(std::count(state->supported_focus_modes.begin(),
                       state->supported_focus_modes.end(),
                       mojom::MeteringMode::MANUAL),
            1);
  EXPECT_EQ(state->current_focus_mode, mojom::MeteringMode::CONTINUOUS);
  EXPECT_EQ(state->points_of_interest.size(), 0u);

  EXPECT_EQ(state->exposure_compensation->current,
            kVideoProcAmpCurrentBase + VideoProcAmp_Gain);
  EXPECT_EQ(state->exposure_compensation->min,
            kVideoProcAmpMinBase + VideoProcAmp_Gain);
  EXPECT_EQ(state->exposure_compensation->max,
            kVideoProcAmpMaxBase + VideoProcAmp_Gain);
  EXPECT_EQ(state->exposure_compensation->step, kVideoProcAmpStep);
  EXPECT_DOUBLE_EQ(
      std::log2(state->exposure_time->current / kHundredMicrosecondsInSecond),
      kCameraControlCurrentBase + CameraControl_Exposure);
  EXPECT_DOUBLE_EQ(
      std::log2(state->exposure_time->min / kHundredMicrosecondsInSecond),
      kCameraControlMinBase + CameraControl_Exposure);
  EXPECT_DOUBLE_EQ(
      std::log2(state->exposure_time->max / kHundredMicrosecondsInSecond),
      kCameraControlMaxBase + CameraControl_Exposure);
  EXPECT_DOUBLE_EQ(state->exposure_time->step,
                   std::exp2(kCameraControlStep) * state->exposure_time->min -
                       state->exposure_time->min);
  EXPECT_EQ(state->color_temperature->current,
            kVideoProcAmpCurrentBase + VideoProcAmp_WhiteBalance);
  EXPECT_EQ(state->color_temperature->min,
            kVideoProcAmpMinBase + VideoProcAmp_WhiteBalance);
  EXPECT_EQ(state->color_temperature->max,
            kVideoProcAmpMaxBase + VideoProcAmp_WhiteBalance);
  EXPECT_EQ(state->color_temperature->step, kVideoProcAmpStep);
  EXPECT_EQ(state->iso->min, state->iso->max);

  EXPECT_EQ(state->brightness->current,
            kVideoProcAmpCurrentBase + VideoProcAmp_Brightness);
  EXPECT_EQ(state->brightness->min,
            kVideoProcAmpMinBase + VideoProcAmp_Brightness);
  EXPECT_EQ(state->brightness->max,
            kVideoProcAmpMaxBase + VideoProcAmp_Brightness);
  EXPECT_EQ(state->brightness->step, kVideoProcAmpStep);
  EXPECT_EQ(state->contrast->current,
            kVideoProcAmpCurrentBase + VideoProcAmp_Contrast);
  EXPECT_EQ(state->contrast->min, kVideoProcAmpMinBase + VideoProcAmp_Contrast);
  EXPECT_EQ(state->contrast->max, kVideoProcAmpMaxBase + VideoProcAmp_Contrast);
  EXPECT_EQ(state->contrast->step, kVideoProcAmpStep);
  EXPECT_EQ(state->saturation->current,
            kVideoProcAmpCurrentBase + VideoProcAmp_Saturation);
  EXPECT_EQ(state->saturation->min,
            kVideoProcAmpMinBase + VideoProcAmp_Saturation);
  EXPECT_EQ(state->saturation->max,
            kVideoProcAmpMaxBase + VideoProcAmp_Saturation);
  EXPECT_EQ(state->saturation->step, kVideoProcAmpStep);
  EXPECT_EQ(state->sharpness->current,
            kVideoProcAmpCurrentBase + VideoProcAmp_Sharpness);
  EXPECT_EQ(state->sharpness->min,
            kVideoProcAmpMinBase + VideoProcAmp_Sharpness);
  EXPECT_EQ(state->sharpness->max,
            kVideoProcAmpMaxBase + VideoProcAmp_Sharpness);
  EXPECT_EQ(state->sharpness->step, kVideoProcAmpStep);

  EXPECT_EQ(state->focus_distance->current,
            kCameraControlCurrentBase + CameraControl_Focus);
  EXPECT_EQ(state->focus_distance->min,
            kCameraControlMinBase + CameraControl_Focus);
  EXPECT_EQ(state->focus_distance->max,
            kCameraControlMaxBase + CameraControl_Focus);
  EXPECT_EQ(state->focus_distance->step, kCameraControlStep);

  EXPECT_DOUBLE_EQ(state->pan->current / kArcSecondsInDegree,
                   kCameraControlCurrentBase + CameraControl_Pan);
  EXPECT_DOUBLE_EQ(state->pan->min / kArcSecondsInDegree,
                   kCameraControlMinBase + CameraControl_Pan);
  EXPECT_DOUBLE_EQ(state->pan->max / kArcSecondsInDegree,
                   kCameraControlMaxBase + CameraControl_Pan);
  EXPECT_DOUBLE_EQ(state->pan->step / kArcSecondsInDegree, kCameraControlStep);
  EXPECT_DOUBLE_EQ(state->tilt->current / kArcSecondsInDegree,
                   kCameraControlCurrentBase + CameraControl_Tilt);
  EXPECT_DOUBLE_EQ(state->tilt->min / kArcSecondsInDegree,
                   kCameraControlMinBase + CameraControl_Tilt);
  EXPECT_DOUBLE_EQ(state->tilt->max / kArcSecondsInDegree,
                   kCameraControlMaxBase + CameraControl_Tilt);
  EXPECT_DOUBLE_EQ(state->tilt->step / kArcSecondsInDegree, kCameraControlStep);
  EXPECT_EQ(state->zoom->current,
            kCameraControlCurrentBase + CameraControl_Zoom);
  EXPECT_EQ(state->zoom->min, kCameraControlMinBase + CameraControl_Zoom);
  EXPECT_EQ(state->zoom->max, kCameraControlMaxBase + CameraControl_Zoom);
  EXPECT_EQ(state->zoom->step, kCameraControlStep);

  EXPECT_FALSE(state->supports_torch);
  EXPECT_FALSE(state->torch);

  EXPECT_EQ(state->red_eye_reduction, mojom::RedEyeReduction::NEVER);
  EXPECT_EQ(state->fill_light_mode.size(), 0u);
}

// Given an |IMFCaptureSource| offering a video stream and a photo stream to
// |VideoCaptureDevice|, when taking photo from |VideoCaptureDevice| then
// expect IMFCaptureEngine::TakePhoto() to be called
TEST_F(VideoCaptureDeviceMFWinTest, TakePhotoViaPhotoStream) {
  if (ShouldSkipTest())
    return;

  PrepareMFDeviceWithOneVideoStreamAndOnePhotoStream(MFVideoFormat_MJPG);

  EXPECT_CALL(*(engine_.Get()), OnStartPreview());
  EXPECT_CALL(*client_, OnStarted());

  EXPECT_CALL(*(engine_.Get()), OnTakePhoto());

  device_->AllocateAndStart(VideoCaptureParams(), std::move(client_));
  capture_preview_sink_->sample_callback->OnSample(nullptr);
  VideoCaptureDevice::TakePhotoCallback take_photo_callback = base::BindOnce(
      &MockImageCaptureClient::DoOnPhotoTaken, image_capture_client_);
  device_->TakePhoto(std::move(take_photo_callback));
}

class DepthCameraDeviceMFWinTest
    : public VideoCaptureDeviceMFWinTest,
      public testing::WithParamInterface<DepthDeviceParams> {};

const DepthDeviceParams kDepthCamerasParams[] = {
    {kMediaSubTypeY16, false, false},
    {kMediaSubTypeZ16, false, true},
    {kMediaSubTypeINVZ, true, false},
    {MFVideoFormat_D16, true, true}};

INSTANTIATE_TEST_SUITE_P(DepthCameraDeviceMFWinTests,
                         DepthCameraDeviceMFWinTest,
                         testing::ValuesIn(kDepthCamerasParams));

// Given an |IMFCaptureSource| offering a video stream with subtype Y16, Z16,
// INVZ or D16, when allocating and starting |VideoCaptureDevice| expect the MF
// source and the MF sink to be set to the same media subtype.
TEST_P(DepthCameraDeviceMFWinTest, AllocateAndStartDepthCamera) {
  if (ShouldSkipTest())
    return;

  DepthDeviceParams params = GetParam();
  if (!params.additional_i420_video_stream &&
      !params.additional_i420_formats_in_depth_stream) {
    PrepareMFDeviceWithOneVideoStream(params.depth_video_stream_subtype);
  } else {
    PrepareMFDepthDeviceWithCombinedFormatsAndStreams(params);
  }

  EXPECT_CALL(*(engine_.Get()), OnStartPreview());
  EXPECT_CALL(*client_, OnStarted());

  EXPECT_CALL(*(capture_source_.get()), DoSetCurrentDeviceMediaType(0, _))
      .WillOnce(Invoke([params](DWORD stream_index, IMFMediaType* media_type) {
        GUID source_video_media_subtype;
        media_type->GetGUID(MF_MT_SUBTYPE, &source_video_media_subtype);
        EXPECT_EQ(source_video_media_subtype,
                  params.depth_video_stream_subtype);
        return S_OK;
      }));

  EXPECT_CALL(*(capture_preview_sink_.get()), DoAddStream(0, _, _, _))
      .WillOnce(Invoke([params](DWORD stream_index, IMFMediaType* media_type,
                                IMFAttributes* attributes,
                                DWORD* sink_stream_index) {
        GUID sink_video_media_subtype;
        media_type->GetGUID(MF_MT_SUBTYPE, &sink_video_media_subtype);
        EXPECT_EQ(sink_video_media_subtype, params.depth_video_stream_subtype);
        return S_OK;
      }));

  VideoCaptureFormat format(gfx::Size(640, 480), 30, media::PIXEL_FORMAT_Y16);
  VideoCaptureParams video_capture_params;
  video_capture_params.requested_format = format;
  device_->AllocateAndStart(video_capture_params, std::move(client_));
  capture_preview_sink_->sample_callback->OnSample(nullptr);
}

}  // namespace media
