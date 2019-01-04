// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_FILTERS_DECRYPTING_MEDIA_RESOURCE_H_
#define MEDIA_FILTERS_DECRYPTING_MEDIA_RESOURCE_H_

#include <vector>

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "media/base/media_resource.h"
#include "media/base/pipeline.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace media {

class CdmContext;
class DemuxerStream;
class DecryptingDemuxerStream;

// DecryptingMediaResource is used as a proxy for a MediaResource
// implementation. This wrapper is only created when the decryptor
// implementation always supports decrypt-only and will decrypt the streams
// that it retrieves from the internal MediaResource. These clear streams are
// then passed downstream, allowing renderer implementations to no longer need
// to worry about decryption.
class MEDIA_EXPORT DecryptingMediaResource : public MediaResource {
 public:
  using InitCB = base::OnceCallback<void(bool success)>;

  DecryptingMediaResource(
      MediaResource* media_resource,
      CdmContext* cdm_context,
      MediaLog* media_log,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);
  ~DecryptingMediaResource() override;

  // MediaResource implementation:
  MediaResource::Type GetType() const override;
  std::vector<DemuxerStream*> GetAllStreams() override;
  MediaUrlParams GetMediaUrlParams() const override;

  void Initialize(InitCB init_cb);

  // Returns the number of DecryptingDemuxerStreams that were created.
  virtual int DecryptingDemuxerStreamCountForTesting() const;

 private:
  void OnDecryptingDemuxerInitialized(PipelineStatus status);

  MediaResource* const media_resource_;
  CdmContext* const cdm_context_;
  MediaLog* const media_log_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  // Number of DecryptingDemuxerStreams that have yet to be initialized.
  int num_dds_pending_init_ = 0;

  // |streams_| is the set of streams that this implementation does not own and
  // will be returned when GetAllStreams() is invoked. |owned_streams_| is the
  // set of DecryptingDemuxerStreams that we have created and own (i.e.
  // responsible for destructing).
  std::vector<DemuxerStream*> streams_;
  std::vector<std::unique_ptr<DecryptingDemuxerStream>> owned_streams_;

  // Called when the final DecryptingDemuxerStream has been initialized *or*
  // if one of the DecryptingDemuxerStreams failed to initialize correctly.
  InitCB init_cb_;
  base::WeakPtrFactory<DecryptingMediaResource> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(DecryptingMediaResource);
};

}  // namespace media

#endif  // MEDIA_FILTERS_DECRYPTING_MEDIA_RESOURCE_H_
