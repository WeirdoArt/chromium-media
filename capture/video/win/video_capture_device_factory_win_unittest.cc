// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mfidl.h>

#include <ks.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mferror.h>
#include <stddef.h>
#include <vidcap.h>
#include <wrl.h>
#include <wrl/client.h>

#include "base/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/sys_string_conversions.h"
#include "media/capture/video/win/video_capture_device_factory_win.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

namespace {

// MediaFoundation devices
const wchar_t* kMFDeviceId0 = L"\\\\?\\usb#vid_0000&pid_0000&mi_00";
const wchar_t* kMFDeviceName0 = L"Device 0";

const wchar_t* kMFDeviceId1 = L"\\\\?\\usb#vid_0001&pid_0001&mi_00";
const wchar_t* kMFDeviceName1 = L"Device 1";

const wchar_t* kMFDeviceId2 = L"\\\\?\\usb#vid_0002&pid_0002&mi_00";
const wchar_t* kMFDeviceName2 = L"Device 2";

const wchar_t* kMFDeviceId5 = L"\\\\?\\usb#vid_0005&pid_0005&mi_00";
const wchar_t* kMFDeviceName5 = L"Dazzle";

const wchar_t* kMFDeviceId6 = L"\\\\?\\usb#vid_eb1a&pid_2860&mi_00";
const wchar_t* kMFDeviceName6 = L"Empia Device";

// DirectShow devices
const wchar_t* kDirectShowDeviceId0 = L"\\\\?\\usb#vid_0000&pid_0000&mi_00";
const wchar_t* kDirectShowDeviceName0 = L"Device 0";

const wchar_t* kDirectShowDeviceId1 = L"\\\\?\\usb#vid_0001&pid_0001&mi_00#1";
const wchar_t* kDirectShowDeviceName1 = L"Device 1";

const wchar_t* kDirectShowDeviceId3 = L"Virtual Camera 3";
const wchar_t* kDirectShowDeviceName3 = L"Virtual Camera";

const wchar_t* kDirectShowDeviceId4 = L"Virtual Camera 4";
const wchar_t* kDirectShowDeviceName4 = L"Virtual Camera";

const wchar_t* kDirectShowDeviceId5 = L"\\\\?\\usb#vid_0005&pid_0005&mi_00#5";
const wchar_t* kDirectShowDeviceName5 = L"Dazzle";

const wchar_t* kDirectShowDeviceId6 = L"\\\\?\\usb#vid_eb1a&pid_2860&mi_00";
const wchar_t* kDirectShowDeviceName6 = L"Empia Device";

using iterator = VideoCaptureDeviceDescriptors::const_iterator;
iterator FindDescriptorInRange(iterator begin,
                               iterator end,
                               const std::string& device_id) {
  return std::find_if(
      begin, end, [device_id](const VideoCaptureDeviceDescriptor& descriptor) {
        return device_id == descriptor.device_id;
      });
}

template <class Interface>
Interface* AddReference(Interface* object) {
  DCHECK(object);
  object->AddRef();
  return object;
}

template <class Interface>
class StubInterface
    : public base::RefCountedThreadSafe<StubInterface<Interface>>,
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
    base::RefCountedThreadSafe<StubInterface>::AddRef();
    return 1U;
  }
  IFACEMETHODIMP_(ULONG) Release() override {
    base::RefCountedThreadSafe<StubInterface>::Release();
    return 1U;
  }

 protected:
  friend class base::RefCountedThreadSafe<StubInterface<Interface>>;
  virtual ~StubInterface() = default;
};

template <class Interface>
class StubDeviceInterface : public StubInterface<Interface> {
 public:
  StubDeviceInterface(std::string device_id)
      : device_id_(std::move(device_id)) {}
  const std::string& device_id() const { return device_id_; }

 protected:
  ~StubDeviceInterface() override = default;

 private:
  std::string device_id_;
};

// Stub IAMCameraControl with pan, tilt and zoom ranges for all devices except
// from Device 1.
class StubAMCameraControl final : public StubDeviceInterface<IAMCameraControl> {
 public:
  using StubDeviceInterface::StubDeviceInterface;
  // IAMCameraControl
  IFACEMETHODIMP Get(long property, long* value, long* flags) override {
    return E_NOTIMPL;
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
      case CameraControl_Zoom:
        if (device_id() != base::SysWideToUTF8(kMFDeviceId1)) {
          *min = 100;
          *max = 400;
          *step = 1;
          *default_value = 100;
          *caps_flags = CameraControl_Flags_Manual;
          return S_OK;
        }
        break;
    }
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Set(long property, long value, long flags) override {
    return E_NOTIMPL;
  }

 private:
  ~StubAMCameraControl() override = default;
};

class StubAMVideoProcAmp final : public StubDeviceInterface<IAMVideoProcAmp> {
 public:
  using StubDeviceInterface::StubDeviceInterface;
  // IAMVideoProcAmp
  IFACEMETHODIMP Get(long property, long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetRange(long property,
                          long* min,
                          long* max,
                          long* step,
                          long* default_value,
                          long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Set(long property, long value, long flags) override {
    return E_NOTIMPL;
  }

 private:
  ~StubAMVideoProcAmp() override = default;
};

class StubMFActivate final : public StubInterface<IMFActivate> {
 public:
  StubMFActivate(const std::wstring& symbolic_link,
                 const std::wstring& name,
                 bool kscategory_video_camera,
                 bool kscategory_sensor_camera)
      : symbolic_link_(symbolic_link),
        name_(name),
        kscategory_video_camera_(kscategory_video_camera),
        kscategory_sensor_camera_(kscategory_sensor_camera) {}

  bool MatchesQuery(IMFAttributes* query, HRESULT* status) {
    UINT32 count;
    *status = query->GetCount(&count);
    if (FAILED(*status))
      return false;
    GUID value;
    *status = query->GetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &value);
    if (FAILED(*status))
      return false;
    if (value != MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)
      return false;
    *status = query->GetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY,
                             &value);
    if (SUCCEEDED(*status)) {
      if ((value == KSCATEGORY_SENSOR_CAMERA && kscategory_sensor_camera_) ||
          (value == KSCATEGORY_VIDEO_CAMERA && kscategory_video_camera_))
        return true;
    } else if (*status == MF_E_ATTRIBUTENOTFOUND) {
      // When no category attribute is specified, it should behave the same as
      // if KSCATEGORY_VIDEO_CAMERA is specified.
      *status = S_OK;
      if (kscategory_video_camera_)
        return true;
    }
    return false;
  }

  // IMFAttributes
  IFACEMETHODIMP GetItem(REFGUID key, PROPVARIANT* value) override {
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
    return E_FAIL;
  }
  IFACEMETHODIMP GetDouble(REFGUID guidKey, double* pfValue) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetGUID(REFGUID key, GUID* value) override { return E_FAIL; }
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
    std::wstring value;
    if (guidKey == MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK) {
      value = symbolic_link_;
    } else if (guidKey == MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME) {
      value = name_;
    } else {
      return E_NOTIMPL;
    }
    *ppwszValue = static_cast<wchar_t*>(
        CoTaskMemAlloc((value.size() + 1) * sizeof(wchar_t)));
    wcscpy(*ppwszValue, value.c_str());
    *pcchLength = value.length();
    return S_OK;
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
  // IMFActivate
  IFACEMETHODIMP ActivateObject(REFIID riid, void** ppv) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP DetachObject(void) override { return E_NOTIMPL; }
  IFACEMETHODIMP ShutdownObject(void) override { return E_NOTIMPL; }

 private:
  ~StubMFActivate() override = default;

  const std::wstring symbolic_link_;
  const std::wstring name_;
  const bool kscategory_video_camera_;
  const bool kscategory_sensor_camera_;
};

// Stub IMFMediaSource with IAMCameraControl and IAMVideoProcAmp interfaces for
// all devices except from Device 0.
class StubMFMediaSource final : public StubDeviceInterface<IMFMediaSource> {
 public:
  using StubDeviceInterface::StubDeviceInterface;
  // IUnknown
  IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override {
    if (device_id() != base::SysWideToUTF8(kMFDeviceId0)) {
      if (riid == __uuidof(IAMCameraControl)) {
        *object = AddReference(new StubAMCameraControl(device_id()));
        return S_OK;
      }
      if (riid == __uuidof(IAMVideoProcAmp)) {
        *object = AddReference(new StubAMVideoProcAmp(device_id()));
        return S_OK;
      }
    }
    return StubDeviceInterface::QueryInterface(riid, object);
  }
  // IMFMediaEventGenerator
  IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback,
                               IUnknown* state) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result,
                             IMFMediaEvent** event) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP QueueEvent(MediaEventType met,
                            REFGUID extended_type,
                            HRESULT status,
                            const PROPVARIANT* value) override {
    return E_NOTIMPL;
  }
  // IMFMediaSource
  IFACEMETHODIMP CreatePresentationDescriptor(
      IMFPresentationDescriptor** presentation_descriptor) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetCharacteristics(DWORD* characteristics) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Pause() override { return E_NOTIMPL; }
  IFACEMETHODIMP Shutdown() override { return E_NOTIMPL; }
  IFACEMETHODIMP Start(IMFPresentationDescriptor* presentation_descriptor,
                       const GUID* time_format,
                       const PROPVARIANT* start_position) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Stop() override { return E_NOTIMPL; }

 private:
  ~StubMFMediaSource() override = default;
};

// Stub ICameraControl with pan range for all devices except from Device 5.
class StubCameraControl final : public StubDeviceInterface<ICameraControl> {
 public:
  using StubDeviceInterface::StubDeviceInterface;
  // ICameraControl
  IFACEMETHODIMP get_Exposure(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_ExposureRelative(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_FocalLengths(long* ocular_focal_length,
                                  long* objective_focal_length_min,
                                  long* objective_focal_length_max) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Focus(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_FocusRelative(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Iris(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_IrisRelative(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Pan(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_PanRelative(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_PanTilt(long* pan_value,
                             long* tilt_value,
                             long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_PanTiltRelative(long* pan_value,
                                     long* tilt_value,
                                     long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_PrivacyMode(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Roll(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_RollRelative(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_ScanMode(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Tilt(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_TiltRelative(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Zoom(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_ZoomRelative(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Exposure(long* min,
                                   long* max,
                                   long* step,
                                   long* default_value,
                                   long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_ExposureRelative(long* min,
                                           long* max,
                                           long* step,
                                           long* default_value,
                                           long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Focus(long* min,
                                long* max,
                                long* step,
                                long* default_value,
                                long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_FocusRelative(long* min,
                                        long* max,
                                        long* step,
                                        long* default_value,
                                        long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Iris(long* min,
                               long* max,
                               long* step,
                               long* default_value,
                               long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_IrisRelative(long* min,
                                       long* max,
                                       long* step,
                                       long* default_value,
                                       long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Pan(long* min,
                              long* max,
                              long* step,
                              long* default_value,
                              long* caps_flags) override {
    if (device_id() != base::SysWideToUTF8(kDirectShowDeviceId5)) {
      *min = 100;
      *max = 400;
      *step = 1;
      *default_value = 100;
      *caps_flags = CameraControl_Flags_Manual;
      return S_OK;
    }
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_PanRelative(long* min,
                                      long* max,
                                      long* step,
                                      long* default_value,
                                      long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Roll(long* min,
                               long* max,
                               long* step,
                               long* default_value,
                               long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_RollRelative(long* min,
                                       long* max,
                                       long* step,
                                       long* default_value,
                                       long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Tilt(long* min,
                               long* max,
                               long* step,
                               long* default_value,
                               long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_TiltRelative(long* min,
                                       long* max,
                                       long* step,
                                       long* default_value,
                                       long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Zoom(long* min,
                               long* max,
                               long* step,
                               long* default_value,
                               long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_ZoomRelative(long* min,
                                       long* max,
                                       long* step,
                                       long* default_value,
                                       long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Exposure(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_ExposureRelative(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Focus(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_FocusRelative(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Iris(long value, long flags) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_IrisRelative(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Pan(long value, long flags) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_PanRelative(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_PanTilt(long pan_value,
                             long tilt_value,
                             long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_PanTiltRelative(long pan_value,
                                     long tilt_value,
                                     long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_PrivacyMode(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Roll(long value, long flags) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_RollRelative(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_ScanMode(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Tilt(long value, long flags) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_TiltRelative(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Zoom(long value, long flags) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_ZoomRelative(long value, long flags) override {
    return E_NOTIMPL;
  }

 private:
  ~StubCameraControl() override = default;
};

class StubVideoProcAmp final : public StubDeviceInterface<IVideoProcAmp> {
 public:
  using StubDeviceInterface::StubDeviceInterface;
  // IVideoProcAmp
  IFACEMETHODIMP get_BacklightCompensation(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Brightness(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_ColorEnable(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Contrast(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_DigitalMultiplier(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Gain(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Gamma(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Hue(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_PowerlineFrequency(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Saturation(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Sharpness(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_WhiteBalance(long* value, long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_WhiteBalanceComponent(long* value1,
                                           long* value2,
                                           long* flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_BacklightCompensation(long* min,
                                                long* max,
                                                long* step,
                                                long* default_value,
                                                long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Brightness(long* min,
                                     long* max,
                                     long* step,
                                     long* default_value,
                                     long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_ColorEnable(long* min,
                                      long* max,
                                      long* step,
                                      long* default_value,
                                      long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Contrast(long* min,
                                   long* max,
                                   long* step,
                                   long* default_value,
                                   long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_DigitalMultiplier(long* min,
                                            long* max,
                                            long* step,
                                            long* default_value,
                                            long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Gain(long* min,
                               long* max,
                               long* step,
                               long* default_value,
                               long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Gamma(long* min,
                                long* max,
                                long* step,
                                long* default_value,
                                long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Hue(long* min,
                              long* max,
                              long* step,
                              long* default_value,
                              long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_PowerlineFrequency(long* min,
                                             long* max,
                                             long* step,
                                             long* default_value,
                                             long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Saturation(long* min,
                                     long* max,
                                     long* step,
                                     long* default_value,
                                     long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_Sharpness(long* min,
                                    long* max,
                                    long* step,
                                    long* default_value,
                                    long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_WhiteBalance(long* min,
                                       long* max,
                                       long* step,
                                       long* default_value,
                                       long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP getRange_WhiteBalanceComponent(long* min,
                                                long* max,
                                                long* step,
                                                long* default_value,
                                                long* caps_flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_BacklightCompensation(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Brightness(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_ColorEnable(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Contrast(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_DigitalMultiplier(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Gain(long value, long flags) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_Gamma(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Hue(long value, long flags) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_PowerlineFrequency(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Saturation(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_Sharpness(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_WhiteBalance(long value, long flags) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP put_WhiteBalanceComponent(long value1,
                                           long value2,
                                           long flags) override {
    return E_NOTIMPL;
  }

 private:
  ~StubVideoProcAmp() override = default;
};

// Stub IKsTopologyInfo with 2 nodes.
// For all devices except from Device 3, the first node is
// a KSNODETYPE_VIDEO_CAMERA_TERMINAL (ICameraControl) node.
// For all devices except from Device 4, the second node is
// a KSNODETYPE_VIDEO_PROCESSING (IVideoProcAmp) node.
class StubKsTopologyInfo final : public StubDeviceInterface<IKsTopologyInfo> {
 public:
  enum { kNumNodes = 2 };
  using StubDeviceInterface::StubDeviceInterface;
  // IKsTopologyInfo
  IFACEMETHODIMP CreateNodeInstance(DWORD node_id,
                                    REFIID iid,
                                    void** object) override {
    GUID node_type;
    HRESULT hr = get_NodeType(node_id, &node_type);
    if (FAILED(hr))
      return hr;
    if (node_type == KSNODETYPE_VIDEO_CAMERA_TERMINAL) {
      EXPECT_EQ(iid, __uuidof(ICameraControl));
      *object = AddReference(new StubCameraControl(device_id()));
      return S_OK;
    }
    if (node_type == KSNODETYPE_VIDEO_PROCESSING) {
      EXPECT_EQ(iid, __uuidof(IVideoProcAmp));
      *object = AddReference(new StubVideoProcAmp(device_id()));
      return S_OK;
    }
    NOTREACHED();
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_Category(DWORD index, GUID* category) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_ConnectionInfo(
      DWORD index,
      KSTOPOLOGY_CONNECTION* connection_info) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_NodeName(DWORD node_id,
                              WCHAR* node_name,
                              DWORD buf_size,
                              DWORD* name_len) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_NodeType(DWORD node_id, GUID* node_type) override {
    EXPECT_LT(node_id, kNumNodes);
    switch (node_id) {
      case 0:
        *node_type = device_id() != base::SysWideToUTF8(kDirectShowDeviceId3)
                         ? KSNODETYPE_VIDEO_CAMERA_TERMINAL
                         : KSNODETYPE_DEV_SPECIFIC;
        return S_OK;
      case 1:
        *node_type = device_id() != base::SysWideToUTF8(kDirectShowDeviceId4)
                         ? KSNODETYPE_VIDEO_PROCESSING
                         : KSNODETYPE_DEV_SPECIFIC;
        return S_OK;
    }
    NOTREACHED();
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_NumCategories(DWORD* num_categories) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_NumConnections(DWORD* num_connections) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP get_NumNodes(DWORD* num_nodes) override {
    *num_nodes = kNumNodes;
    return S_OK;
  }

 private:
  ~StubKsTopologyInfo() override = default;
};

// Stub IBaseFilter with IKsTopologyInfo interface.
class StubBaseFilter final : public StubDeviceInterface<IBaseFilter> {
 public:
  using StubDeviceInterface::StubDeviceInterface;
  // IUnknown
  IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override {
    if (riid == __uuidof(IKsTopologyInfo)) {
      *object = AddReference(new StubKsTopologyInfo(device_id()));
      return S_OK;
    }
    return StubDeviceInterface::QueryInterface(riid, object);
  }
  // IPersist
  IFACEMETHODIMP GetClassID(CLSID* class_id) override { return E_NOTIMPL; }
  // IMediaFilter
  IFACEMETHODIMP GetState(DWORD milli_secs_timeout,
                          FILTER_STATE* State) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetSyncSource(IReferenceClock** clock) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Pause() override { return E_NOTIMPL; }
  IFACEMETHODIMP Run(REFERENCE_TIME start) override { return E_NOTIMPL; }
  IFACEMETHODIMP SetSyncSource(IReferenceClock* clock) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Stop() override { return E_NOTIMPL; }
  // IBaseFilter
  IFACEMETHODIMP EnumPins(IEnumPins** enum_pins) override { return E_NOTIMPL; }
  IFACEMETHODIMP FindPin(LPCWSTR id, IPin** pin) override { return E_NOTIMPL; }
  IFACEMETHODIMP JoinFilterGraph(IFilterGraph* graph, LPCWSTR name) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP QueryFilterInfo(FILTER_INFO* info) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP QueryVendorInfo(LPWSTR* vendor_info) override {
    return E_NOTIMPL;
  }

 private:
  ~StubBaseFilter() override = default;
};

class StubPropertyBag final : public StubInterface<IPropertyBag> {
 public:
  StubPropertyBag(const wchar_t* device_path, const wchar_t* description)
      : device_path_(device_path), description_(description) {}

  IFACEMETHODIMP Read(LPCOLESTR pszPropName,
                      VARIANT* pVar,
                      IErrorLog* pErrorLog) override {
    if (pszPropName == std::wstring(L"Description")) {
      pVar->vt = VT_BSTR;
      pVar->bstrVal = SysAllocString(description_);
      return S_OK;
    }
    if (pszPropName == std::wstring(L"DevicePath")) {
      pVar->vt = VT_BSTR;
      pVar->bstrVal = SysAllocString(device_path_);
      return S_OK;
    }
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Write(LPCOLESTR pszPropName, VARIANT* pVar) override {
    return E_NOTIMPL;
  }

 private:
  ~StubPropertyBag() override = default;

  const wchar_t* device_path_;
  const wchar_t* description_;
};

class StubMoniker final : public StubInterface<IMoniker> {
 public:
  StubMoniker(const wchar_t* device_path, const wchar_t* description)
      : device_path_(device_path), description_(description) {}

  IFACEMETHODIMP GetClassID(CLSID* pClassID) override { return E_NOTIMPL; }
  IFACEMETHODIMP IsDirty(void) override { return E_NOTIMPL; }
  IFACEMETHODIMP Load(IStream* pStm) override { return E_NOTIMPL; }
  IFACEMETHODIMP Save(IStream* pStm, BOOL fClearDirty) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetSizeMax(ULARGE_INTEGER* pcbSize) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP BindToObject(IBindCtx* pbc,
                              IMoniker* pmkToLeft,
                              REFIID riidResult,
                              void** ppvResult) override {
    if (riidResult == __uuidof(IBaseFilter)) {
      *ppvResult =
          AddReference(new StubBaseFilter(base::SysWideToUTF8(device_path_)));
      return S_OK;
    }
    return MK_E_NOOBJECT;
  }
  IFACEMETHODIMP BindToStorage(IBindCtx* pbc,
                               IMoniker* pmkToLeft,
                               REFIID riid,
                               void** ppvObj) override {
    *ppvObj = AddReference(new StubPropertyBag(device_path_, description_));
    return S_OK;
  }
  IFACEMETHODIMP Reduce(IBindCtx* pbc,
                        DWORD dwReduceHowFar,
                        IMoniker** ppmkToLeft,
                        IMoniker** ppmkReduced) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP ComposeWith(IMoniker* pmkRight,
                             BOOL fOnlyIfNotGeneric,
                             IMoniker** ppmkComposite) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Enum(BOOL fForward, IEnumMoniker** ppenumMoniker) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP IsEqual(IMoniker* pmkOtherMoniker) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Hash(DWORD* pdwHash) override { return E_NOTIMPL; }
  IFACEMETHODIMP IsRunning(IBindCtx* pbc,
                           IMoniker* pmkToLeft,
                           IMoniker* pmkNewlyRunning) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetTimeOfLastChange(IBindCtx* pbc,
                                     IMoniker* pmkToLeft,
                                     FILETIME* pFileTime) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP Inverse(IMoniker** ppmk) override { return E_NOTIMPL; }
  IFACEMETHODIMP CommonPrefixWith(IMoniker* pmkOther,
                                  IMoniker** ppmkPrefix) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP RelativePathTo(IMoniker* pmkOther,
                                IMoniker** ppmkRelPath) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetDisplayName(IBindCtx* pbc,
                                IMoniker* pmkToLeft,
                                LPOLESTR* ppszDisplayName) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP ParseDisplayName(IBindCtx* pbc,
                                  IMoniker* pmkToLeft,
                                  LPOLESTR pszDisplayName,
                                  ULONG* pchEaten,
                                  IMoniker** ppmkOut) override {
    return E_NOTIMPL;
  }
  IFACEMETHODIMP IsSystemMoniker(DWORD* pdwMksys) override { return E_NOTIMPL; }

 private:
  ~StubMoniker() override = default;

  const wchar_t* device_path_;
  const wchar_t* description_;
};

class StubEnumMoniker : public StubInterface<IEnumMoniker> {
 public:
  StubEnumMoniker(std::vector<scoped_refptr<StubMoniker>> monikers)
      : monikers_(std::move(monikers)) {}

  IFACEMETHODIMP Next(ULONG celt,
                      IMoniker** rgelt,
                      ULONG* celt_fetched) override {
    if (cursor_position_ >= monikers_.size())
      return S_FALSE;
    const ULONG original_cursor_position = cursor_position_;
    while (celt-- > 0 && cursor_position_ < monikers_.size())
      *rgelt++ = AddReference(monikers_[cursor_position_++].get());
    if (celt_fetched)
      *celt_fetched = cursor_position_ - original_cursor_position;
    return S_OK;
  }
  IFACEMETHODIMP Skip(ULONG celt) override { return E_NOTIMPL; }
  IFACEMETHODIMP Reset(void) override { return E_NOTIMPL; }
  IFACEMETHODIMP Clone(IEnumMoniker** enum_moniker) override {
    return E_NOTIMPL;
  }

 private:
  ~StubEnumMoniker() override = default;

  std::vector<scoped_refptr<StubMoniker>> monikers_;
  ULONG cursor_position_ = 0;
};

class FakeVideoCaptureDeviceFactoryWin : public VideoCaptureDeviceFactoryWin {
 protected:
  bool CreateDeviceEnumMonikerDirectShow(IEnumMoniker** enum_moniker) override {
    *enum_moniker = AddReference(new StubEnumMoniker(
        {base::MakeRefCounted<StubMoniker>(kDirectShowDeviceId0,
                                           kDirectShowDeviceName0),
         base::MakeRefCounted<StubMoniker>(kDirectShowDeviceId1,
                                           kDirectShowDeviceName1),
         base::MakeRefCounted<StubMoniker>(kDirectShowDeviceId3,
                                           kDirectShowDeviceName3),
         base::MakeRefCounted<StubMoniker>(kDirectShowDeviceId4,
                                           kDirectShowDeviceName4),
         base::MakeRefCounted<StubMoniker>(kDirectShowDeviceId5,
                                           kDirectShowDeviceName5),
         base::MakeRefCounted<StubMoniker>(kDirectShowDeviceId6,
                                           kDirectShowDeviceName6)}));
    return true;
  }
  bool CreateDeviceSourceMediaFoundation(
      Microsoft::WRL::ComPtr<IMFAttributes> attributes,
      IMFMediaSource** source) override {
    UINT32 length;
    if (FAILED(attributes->GetStringLength(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &length))) {
      return false;
    }
    std::wstring symbolic_link(length, wchar_t());
    if (FAILED(attributes->GetString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &symbolic_link[0], length + 1, &length))) {
      return false;
    }
    *source =
        AddReference(new StubMFMediaSource(base::SysWideToUTF8(symbolic_link)));
    return true;
  }
  bool EnumerateDeviceSourcesMediaFoundation(
      Microsoft::WRL::ComPtr<IMFAttributes> attributes,
      IMFActivate*** devices,
      UINT32* count) override {
    std::vector<scoped_refptr<StubMFActivate>> stub_devices = {
        base::MakeRefCounted<StubMFActivate>(kMFDeviceId0, kMFDeviceName0, true,
                                             false),
        base::MakeRefCounted<StubMFActivate>(kMFDeviceId1, kMFDeviceName1, true,
                                             true),
        base::MakeRefCounted<StubMFActivate>(kMFDeviceId2, kMFDeviceName2,
                                             false, true),
        base::MakeRefCounted<StubMFActivate>(kMFDeviceId5, kMFDeviceName5, true,
                                             false),
        base::MakeRefCounted<StubMFActivate>(kMFDeviceId6, kMFDeviceName6, true,
                                             false)};
    // Iterate once to get the match count and check for errors.
    *count = 0U;
    HRESULT hr;
    for (auto& device : stub_devices) {
      if (device->MatchesQuery(attributes.Get(), &hr))
        (*count)++;
      if (FAILED(hr))
        return false;
    }
    // Second iteration packs the returned devices and increments their
    // reference count.
    *devices = static_cast<IMFActivate**>(
        CoTaskMemAlloc(sizeof(IMFActivate*) * (*count)));
    int offset = 0;
    for (auto& device : stub_devices) {
      if (!device->MatchesQuery(attributes.Get(), &hr))
        continue;
      *(*devices + offset++) = AddReference(device.get());
    }
    return true;
  }
  void GetSupportedFormatsDirectShow(
      const VideoCaptureDeviceDescriptor& device_descriptor,
      VideoCaptureFormats* supported_formats) override {
    if (device_descriptor.device_id ==
        base::SysWideToUTF8(kDirectShowDeviceId5)) {
      VideoCaptureFormat arbitrary_format;
      supported_formats->emplace_back(arbitrary_format);
    }
  }
  void GetSupportedFormatsMediaFoundation(
      const VideoCaptureDeviceDescriptor& device_descriptor,
      VideoCaptureFormats* supported_formats) override {
    if (device_descriptor.device_id == base::SysWideToUTF8(kMFDeviceId6)) {
      VideoCaptureFormat arbitrary_format;
      supported_formats->emplace_back(arbitrary_format);
    }
  }
};

}  // namespace

class VideoCaptureDeviceFactoryWinTest : public ::testing::Test {
 protected:
  VideoCaptureDeviceFactoryWinTest()
      : media_foundation_supported_(
            VideoCaptureDeviceFactoryWin::PlatformSupportsMediaFoundation()) {}

  bool ShouldSkipMFTest() {
    if (media_foundation_supported_)
      return false;
    DVLOG(1) << "Media foundation is not supported by the current platform. "
                "Skipping test.";
    return true;
  }

  FakeVideoCaptureDeviceFactoryWin factory_;
  const bool media_foundation_supported_;
};

class VideoCaptureDeviceFactoryMFWinTest
    : public VideoCaptureDeviceFactoryWinTest {
  void SetUp() override {
    VideoCaptureDeviceFactoryWinTest::SetUp();
    factory_.set_use_media_foundation_for_testing(true);
  }
};

TEST_F(VideoCaptureDeviceFactoryMFWinTest, GetDeviceDescriptors) {
  if (ShouldSkipMFTest())
    return;
  VideoCaptureDeviceDescriptors descriptors;
  factory_.GetDeviceDescriptors(&descriptors);
  EXPECT_EQ(descriptors.size(), 7U);
  for (auto it = descriptors.begin(); it != descriptors.end(); it++) {
    // Verify that there are no duplicates.
    EXPECT_EQ(FindDescriptorInRange(descriptors.begin(), it, it->device_id),
              it);
  }
  iterator it = FindDescriptorInRange(descriptors.begin(), descriptors.end(),
                                      base::SysWideToUTF8(kMFDeviceId0));
  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->capture_api, VideoCaptureApi::WIN_MEDIA_FOUNDATION);
  EXPECT_EQ(it->display_name(), base::SysWideToUTF8(kMFDeviceName0));
  EXPECT_TRUE(it->pan_tilt_zoom_supported().has_value());
  // No IAMCameraControl and no IAMVideoProcAmp interfaces.
  EXPECT_FALSE(it->pan_tilt_zoom_supported().value());

  it = FindDescriptorInRange(descriptors.begin(), descriptors.end(),
                             base::SysWideToUTF8(kMFDeviceId1));
  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->capture_api, VideoCaptureApi::WIN_MEDIA_FOUNDATION);
  EXPECT_EQ(it->display_name(), base::SysWideToUTF8(kMFDeviceName1));
  EXPECT_TRUE(it->pan_tilt_zoom_supported().has_value());
  // No pan/tilt/zoom in IAMCameraControl interface.
  EXPECT_FALSE(it->pan_tilt_zoom_supported().value());

  it = FindDescriptorInRange(descriptors.begin(), descriptors.end(),
                             base::SysWideToUTF8(kMFDeviceId2));
  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->capture_api, VideoCaptureApi::WIN_MEDIA_FOUNDATION_SENSOR);
  EXPECT_EQ(it->display_name(), base::SysWideToUTF8(kMFDeviceName2));
  EXPECT_TRUE(it->pan_tilt_zoom_supported().has_value());
  EXPECT_TRUE(it->pan_tilt_zoom_supported().value());

  it = FindDescriptorInRange(descriptors.begin(), descriptors.end(),
                             base::SysWideToUTF8(kDirectShowDeviceId3));
  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->capture_api, VideoCaptureApi::WIN_DIRECT_SHOW);
  EXPECT_EQ(it->display_name(), base::SysWideToUTF8(kDirectShowDeviceName3));
  EXPECT_TRUE(it->pan_tilt_zoom_supported().has_value());
  // No ICameraControl interface.
  EXPECT_FALSE(it->pan_tilt_zoom_supported().value());

  it = FindDescriptorInRange(descriptors.begin(), descriptors.end(),
                             base::SysWideToUTF8(kDirectShowDeviceId4));
  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->capture_api, VideoCaptureApi::WIN_DIRECT_SHOW);
  EXPECT_EQ(it->display_name(), base::SysWideToUTF8(kDirectShowDeviceName4));
  EXPECT_TRUE(it->pan_tilt_zoom_supported().has_value());
  // No IVideoProcAmp interface.
  EXPECT_FALSE(it->pan_tilt_zoom_supported().value());

  // Devices that are listed in MediaFoundation but only report supported
  // formats in DirectShow are expected to get enumerated with
  // VideoCaptureApi::WIN_DIRECT_SHOW
  it = FindDescriptorInRange(descriptors.begin(), descriptors.end(),
                             base::SysWideToUTF8(kDirectShowDeviceId5));
  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->capture_api, VideoCaptureApi::WIN_DIRECT_SHOW);
  EXPECT_EQ(it->display_name(), base::SysWideToUTF8(kDirectShowDeviceName5));
  EXPECT_TRUE(it->pan_tilt_zoom_supported().has_value());
  // No pan, tilt, or zoom ranges in ICameraControl interface.
  EXPECT_FALSE(it->pan_tilt_zoom_supported().value());

  // Devices that are listed in both MediaFoundation and DirectShow but are
  // blacklisted for use with MediaFoundation are expected to get enumerated
  // with VideoCaptureApi::WIN_DIRECT_SHOW.
  it = FindDescriptorInRange(descriptors.begin(), descriptors.end(),
                             base::SysWideToUTF8(kDirectShowDeviceId6));
  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->capture_api, VideoCaptureApi::WIN_DIRECT_SHOW);
  EXPECT_EQ(it->display_name(), base::SysWideToUTF8(kDirectShowDeviceName6));
  EXPECT_TRUE(it->pan_tilt_zoom_supported().has_value());
  EXPECT_TRUE(it->pan_tilt_zoom_supported().value());
}

}  // namespace media
