// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/remoting/receiver.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/notreached.h"
#include "base/task/bind_post_task.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/cast_streaming/public/remoting_message_factories.h"
#include "components/cast_streaming/public/remoting_proto_enum_utils.h"
#include "components/cast_streaming/public/remoting_proto_utils.h"
#include "media/base/decoder_buffer.h"
#include "media/base/renderer.h"
#include "media/remoting/receiver_controller.h"
#include "media/remoting/stream_provider.h"

using openscreen::cast::RpcMessenger;

namespace media {
namespace remoting {
namespace {

// The period to send the TimeUpdate RPC message to update the media time on
// sender side.
constexpr base::TimeDelta kTimeUpdateInterval = base::Milliseconds(250);

}  // namespace

Receiver::Receiver(
    int rpc_handle,
    int remote_handle,
    ReceiverController* receiver_controller,
    const scoped_refptr<base::SingleThreadTaskRunner>& media_task_runner,
    std::unique_ptr<Renderer> renderer,
    base::OnceCallback<void(int)> acquire_renderer_done_cb)
    : rpc_handle_(rpc_handle),
      remote_handle_(remote_handle),
      receiver_controller_(receiver_controller),
      rpc_messenger_(receiver_controller_->rpc_messenger()),
      main_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      media_task_runner_(media_task_runner),
      renderer_(std::move(renderer)),
      acquire_renderer_done_cb_(std::move(acquire_renderer_done_cb)) {
  DCHECK(rpc_handle_ != RpcMessenger::kInvalidHandle);
  DCHECK(receiver_controller_);
  DCHECK(rpc_messenger_);
  DCHECK(renderer_);

  // Note: The constructor is running on the main thread, but will be destroyed
  // on the media thread. Therefore, all weak pointers must be dereferenced on
  // the media thread.
  auto receive_callback = base::BindPostTask(
      media_task_runner_,
      BindRepeating(&Receiver::OnReceivedRpc, weak_factory_.GetWeakPtr()));

  // Listening all renderer rpc messages.
  rpc_messenger_->RegisterMessageReceiverCallback(
      rpc_handle_, [cb = receive_callback](
                       std::unique_ptr<openscreen::cast::RpcMessage> message) {
        cb.Run(std::move(message));
      });

  VerifyAcquireRendererDone();
}

Receiver::~Receiver() {
  rpc_messenger_->UnregisterMessageReceiverCallback(rpc_handle_);
}

// Receiver::Initialize() will be called by the local pipeline, it would only
// keep the |init_cb| in order to continue the initialization once it receives
// RPC_R_INITIALIZE, which means Receiver::RpcInitialize() is called.
void Receiver::Initialize(MediaResource* media_resource,
                          RendererClient* client,
                          PipelineStatusCallback init_cb) {
  demuxer_ = media_resource;
  init_cb_ = std::move(init_cb);
  ShouldInitializeRenderer();
}

/* CDM is not supported for remoting media */
void Receiver::SetCdm(CdmContext* cdm_context, CdmAttachedCB cdm_attached_cb) {
  NOTREACHED();
}

// No-op. Controlled by sender via RPC calls instead.
void Receiver::SetLatencyHint(absl::optional<base::TimeDelta> latency_hint) {}

// No-op. Controlled by sender via RPC calls instead.
void Receiver::Flush(base::OnceClosure flush_cb) {}

// No-op. Controlled by sender via RPC calls instead.
void Receiver::StartPlayingFrom(base::TimeDelta time) {}

// No-op. Controlled by sender via RPC calls instead.
void Receiver::SetPlaybackRate(double playback_rate) {}

// No-op. Controlled by sender via RPC calls instead.
void Receiver::SetVolume(float volume) {}

// No-op. Controlled by sender via RPC calls instead.
base::TimeDelta Receiver::GetMediaTime() {
  return base::TimeDelta();
}

void Receiver::SendRpcMessageOnMainThread(
    std::unique_ptr<openscreen::cast::RpcMessage> message) {
  // |rpc_messenger_| is owned by |receiver_controller_| which is a singleton
  // per process, so it's safe to use Unretained() here.
  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&RpcMessenger::SendMessageToRemote,
                                base::Unretained(rpc_messenger_), *message));
}

void Receiver::OnReceivedRpc(
    std::unique_ptr<openscreen::cast::RpcMessage> message) {
  DCHECK(media_task_runner_->BelongsToCurrentThread());
  DCHECK(message);
  switch (message->proc()) {
    case openscreen::cast::RpcMessage::RPC_R_INITIALIZE:
      RpcInitialize(std::move(message));
      break;
    case openscreen::cast::RpcMessage::RPC_R_FLUSHUNTIL:
      RpcFlushUntil(std::move(message));
      break;
    case openscreen::cast::RpcMessage::RPC_R_STARTPLAYINGFROM:
      RpcStartPlayingFrom(std::move(message));
      break;
    case openscreen::cast::RpcMessage::RPC_R_SETPLAYBACKRATE:
      RpcSetPlaybackRate(std::move(message));
      break;
    case openscreen::cast::RpcMessage::RPC_R_SETVOLUME:
      RpcSetVolume(std::move(message));
      break;
    default:
      VLOG(1) << __func__ << ": Unknown RPC message. proc=" << message->proc();
  }
}

void Receiver::SetRemoteHandle(int remote_handle) {
  DCHECK_NE(remote_handle, RpcMessenger::kInvalidHandle);
  DCHECK_EQ(remote_handle_, RpcMessenger::kInvalidHandle);
  remote_handle_ = remote_handle;
  VerifyAcquireRendererDone();
}

void Receiver::VerifyAcquireRendererDone() {
  if (remote_handle_ == RpcMessenger::kInvalidHandle)
    return;

  DCHECK(acquire_renderer_done_cb_);
  std::move(acquire_renderer_done_cb_).Run(rpc_handle_);
}

void Receiver::RpcInitialize(
    std::unique_ptr<openscreen::cast::RpcMessage> message) {
  DCHECK(renderer_);
  rpc_initialize_received_ = true;
  ShouldInitializeRenderer();
}

void Receiver::ShouldInitializeRenderer() {
  // ShouldInitializeRenderer() will be called from Initialize() and
  // RpcInitialize() in different orders.
  //
  // |renderer_| must be initialized when both Initialize() and
  // RpcInitialize() are called.
  if (!rpc_initialize_received_ || !init_cb_)
    return;

  DCHECK(media_task_runner_->BelongsToCurrentThread());
  DCHECK(renderer_);
  DCHECK(demuxer_);
  renderer_->Initialize(demuxer_, this,
                        base::BindOnce(&Receiver::OnRendererInitialized,
                                       weak_factory_.GetWeakPtr()));
}

void Receiver::OnRendererInitialized(PipelineStatus status) {
  DCHECK(media_task_runner_->BelongsToCurrentThread());
  DCHECK(init_cb_);
  std::move(init_cb_).Run(status);

  auto rpc = cast_streaming::remoting::CreateMessageForInitializationComplete(
      status == PIPELINE_OK);
  rpc->set_handle(remote_handle_);
  SendRpcMessageOnMainThread(std::move(rpc));
}

void Receiver::RpcSetPlaybackRate(
    std::unique_ptr<openscreen::cast::RpcMessage> message) {
  DCHECK(media_task_runner_->BelongsToCurrentThread());

  const double playback_rate = message->double_value();
  renderer_->SetPlaybackRate(playback_rate);

  if (playback_rate == 0.0) {
    if (time_update_timer_.IsRunning()) {
      time_update_timer_.Stop();
      // Send one final media time update since the sender will not get any
      // until playback resumes.
      SendMediaTimeUpdate();
    }
  } else {
    ScheduleMediaTimeUpdates();
  }
}

void Receiver::RpcFlushUntil(
    std::unique_ptr<openscreen::cast::RpcMessage> message) {
  DCHECK(media_task_runner_->BelongsToCurrentThread());
  DCHECK(message->has_renderer_flushuntil_rpc());

  const openscreen::cast::RendererFlushUntil flush_message =
      message->renderer_flushuntil_rpc();
  DCHECK_EQ(flush_message.callback_handle(), remote_handle_);

  receiver_controller_->OnRendererFlush(flush_message.audio_count(),
                                        flush_message.video_count());

  time_update_timer_.Stop();
  renderer_->Flush(
      base::BindOnce(&Receiver::OnFlushDone, weak_factory_.GetWeakPtr()));
}

void Receiver::OnFlushDone() {
  auto rpc = cast_streaming::remoting::CreateMessageForFlushComplete();
  rpc->set_handle(remote_handle_);
  SendRpcMessageOnMainThread(std::move(rpc));
}

void Receiver::RpcStartPlayingFrom(
    std::unique_ptr<openscreen::cast::RpcMessage> message) {
  DCHECK(media_task_runner_->BelongsToCurrentThread());

  base::TimeDelta time = base::Microseconds(message->integer64_value());
  renderer_->StartPlayingFrom(time);
  ScheduleMediaTimeUpdates();
}

void Receiver::ScheduleMediaTimeUpdates() {
  if (time_update_timer_.IsRunning())
    return;
  SendMediaTimeUpdate();
  time_update_timer_.Start(FROM_HERE, kTimeUpdateInterval,
                           base::BindRepeating(&Receiver::SendMediaTimeUpdate,
                                               weak_factory_.GetWeakPtr()));
}

void Receiver::RpcSetVolume(
    std::unique_ptr<openscreen::cast::RpcMessage> message) {
  DCHECK(media_task_runner_->BelongsToCurrentThread());
  renderer_->SetVolume(message->double_value());
}

void Receiver::SendMediaTimeUpdate() {
  // Issues RPC_RC_ONTIMEUPDATE RPC message.
  auto rpc = cast_streaming::remoting::CreateMessageForMediaTimeUpdate(
      renderer_->GetMediaTime());
  rpc->set_handle(remote_handle_);
  SendRpcMessageOnMainThread(std::move(rpc));
}

void Receiver::OnError(PipelineStatus status) {
  auto rpc = cast_streaming::remoting::CreateMessageForError();
  rpc->set_handle(remote_handle_);
  SendRpcMessageOnMainThread(std::move(rpc));
}

void Receiver::OnEnded() {
  auto rpc = cast_streaming::remoting::CreateMessageForMediaEnded();
  rpc->set_handle(remote_handle_);
  SendRpcMessageOnMainThread(std::move(rpc));
  time_update_timer_.Stop();
}

void Receiver::OnStatisticsUpdate(const PipelineStatistics& stats) {
  auto rpc = cast_streaming::remoting::CreateMessageForStatisticsUpdate(stats);
  rpc->set_handle(remote_handle_);
  SendRpcMessageOnMainThread(std::move(rpc));
}

void Receiver::OnBufferingStateChange(BufferingState state,
                                      BufferingStateChangeReason reason) {
  auto rpc =
      cast_streaming::remoting::CreateMessageForBufferingStateChange(state);
  rpc->set_handle(remote_handle_);
  SendRpcMessageOnMainThread(std::move(rpc));
}

void Receiver::OnWaiting(WaitingReason reason) {
  // Media Remoting has not implemented this concept.
  NOTIMPLEMENTED();
}

void Receiver::OnAudioConfigChange(const AudioDecoderConfig& config) {
  auto rpc =
      cast_streaming::remoting::CreateMessageForAudioConfigChange(config);
  rpc->set_handle(remote_handle_);
  SendRpcMessageOnMainThread(std::move(rpc));
}

void Receiver::OnVideoConfigChange(const VideoDecoderConfig& config) {
  auto rpc =
      cast_streaming::remoting::CreateMessageForVideoConfigChange(config);
  rpc->set_handle(remote_handle_);
  SendRpcMessageOnMainThread(std::move(rpc));
}

void Receiver::OnVideoNaturalSizeChange(const gfx::Size& size) {
  auto rpc =
      cast_streaming::remoting::CreateMessageForVideoNaturalSizeChange(size);
  rpc->set_handle(remote_handle_);
  SendRpcMessageOnMainThread(std::move(rpc));

  // Notify the host.
  receiver_controller_->OnVideoNaturalSizeChange(size);
}

void Receiver::OnVideoOpacityChange(bool opaque) {
  auto rpc =
      cast_streaming::remoting::CreateMessageForVideoOpacityChange(opaque);
  rpc->set_handle(remote_handle_);
  SendRpcMessageOnMainThread(std::move(rpc));
}

void Receiver::OnVideoFrameRateChange(absl::optional<int>) {}

}  // namespace remoting
}  // namespace media
