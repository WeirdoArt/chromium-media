// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_VAAPI_VAAPI_VIDEO_DECODER_DELEGATE_H_
#define MEDIA_GPU_VAAPI_VAAPI_VIDEO_DECODER_DELEGATE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "build/chromeos_buildflags.h"
#include "media/base/decryptor.h"
#include "media/base/subsample_entry.h"
#include "third_party/libva_protected_content/va_protected_content.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chromeos/components/cdm_factory_daemon/chromeos_cdm_context.h"
#endif

namespace media {

class CdmContext;
template <class T>
class DecodeSurfaceHandler;
class DecryptConfig;
class VaapiWrapper;
class VASurface;

// The common part of each AcceleratedVideoDecoder's Accelerator for VA-API.
// This class allows clients to reset VaapiWrapper in case of a profile change.
// DecodeSurfaceHandler must stay alive for the lifetime of this class.
// This also handles all of the shared functionality relating to protected
// sessions in VA-API.
class VaapiVideoDecoderDelegate {
 public:
  // Callback when using protected mode to indicate that if waiting, the
  // decoder should resume again. If |success| is false, then decoding should
  // fail.
  using ProtectedSessionUpdateCB = base::RepeatingCallback<void(bool success)>;

  VaapiVideoDecoderDelegate(
      DecodeSurfaceHandler<VASurface>* const vaapi_dec,
      scoped_refptr<VaapiWrapper> vaapi_wrapper,
      ProtectedSessionUpdateCB on_protected_session_update_cb,
      CdmContext* cdm_context);
  virtual ~VaapiVideoDecoderDelegate();

  void set_vaapi_wrapper(scoped_refptr<VaapiWrapper> vaapi_wrapper);
  virtual void OnVAContextDestructionSoon();

  VaapiVideoDecoderDelegate(const VaapiVideoDecoderDelegate&) = delete;
  VaapiVideoDecoderDelegate& operator=(const VaapiVideoDecoderDelegate&) =
      delete;

 protected:
  // Sets the |decrypt_config| currently active for this stream. Returns true if
  // that config is compatible with the existing one (for example, you can't
  // change encryption schemes midstream).
  bool SetDecryptConfig(std::unique_ptr<DecryptConfig> decrypt_config);

  enum class ProtectedSessionState {
    kNotCreated,
    kInProcess,
    kCreated,
    kFailed
  };

  // Ensures we have a protected session setup and attached to the active
  // |vaapi_wrapper_| we are using. We are in the corresponding state returned
  // when this call returns. |full_sample| indicates if we are using full sample
  // encryption or not and must remain consistent for a session. If everything
  // is setup for a protected session, it will fill in the |crypto_params|.
  // |segments| must retain its memory until the frame is submitted.
  // |subsamples| is for the current slice.
  VaapiVideoDecoderDelegate::ProtectedSessionState SetupDecryptDecode(
      bool full_sample,
      VAEncryptionParameters* crypto_params,
      std::vector<VAEncryptionSegmentInfo>* segments,
      const std::vector<SubsampleEntry>& subsamples);

  // Both owned by caller.
  DecodeSurfaceHandler<VASurface>* const vaapi_dec_;
  scoped_refptr<VaapiWrapper> vaapi_wrapper_;

  SEQUENCE_CHECKER(sequence_checker_);

 private:
  void OnGetHwConfigData(bool success, const std::vector<uint8_t>& config_data);
  void OnGetHwKeyData(const std::string& key_id,
                      Decryptor::Status status,
                      const std::vector<uint8_t>& key_data);

  // All members below pertain to protected content playback.
  ProtectedSessionUpdateCB on_protected_session_update_cb_;
#if BUILDFLAG(IS_CHROMEOS_ASH)
  chromeos::ChromeOsCdmContext* chromeos_cdm_context_{nullptr};  // Not owned.
#endif
  ProtectedSessionState protected_session_state_;
  std::unique_ptr<DecryptConfig> decrypt_config_;
  bool full_sample_;
  std::vector<uint8_t> hw_identifier_;
  std::map<std::string, std::vector<uint8_t>> hw_key_data_map_;

  base::WeakPtrFactory<VaapiVideoDecoderDelegate> weak_factory_{this};
};

}  // namespace media

#endif  // MEDIA_GPU_VAAPI_VAAPI_VIDEO_DECODER_DELEGATE_H_
