// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cast/receiver/cast_streaming_renderer_factory.h"

#include "media/cast/receiver/cast_streaming_renderer.h"

namespace media {
namespace cast {

CastStreamingRendererFactory::CastStreamingRendererFactory(
    std::unique_ptr<RendererFactory> renderer_factory,
    mojo::PendingReceiver<media::mojom::Renderer> pending_renderer_controls)
    : pending_renderer_controls_(std::move(pending_renderer_controls)),
      real_renderer_factory_(std::move(renderer_factory)) {
  DCHECK(pending_renderer_controls_);
  DCHECK(real_renderer_factory_);
}

CastStreamingRendererFactory::~CastStreamingRendererFactory() = default;

std::unique_ptr<Renderer> CastStreamingRendererFactory::CreateRenderer(
    const scoped_refptr<base::SingleThreadTaskRunner>& media_task_runner,
    const scoped_refptr<base::TaskRunner>& worker_task_runner,
    AudioRendererSink* audio_renderer_sink,
    VideoRendererSink* video_renderer_sink,
    RequestOverlayInfoCB request_overlay_info_cb,
    const gfx::ColorSpace& target_color_space) {
  DCHECK(pending_renderer_controls_);
  return std::make_unique<CastStreamingRenderer>(
      real_renderer_factory_->CreateRenderer(
          media_task_runner, worker_task_runner, audio_renderer_sink,
          video_renderer_sink, request_overlay_info_cb, target_color_space),
      media_task_runner, std::move(pending_renderer_controls_));
}

}  // namespace cast
}  // namespace media
