// Copyright (c) 2008-2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// TODO(scherkus): clean up PipelineImpl... too many crazy function names,
// potential deadlocks, etc...

#include "base/compiler_specific.h"
#include "base/condition_variable.h"
#include "base/stl_util-inl.h"
#include "media/base/media_format.h"
#include "media/base/pipeline_impl.h"

namespace media {

namespace {

// Small helper function to help us transition over to injected message loops.
//
// TODO(scherkus): have every filter support injected message loops.
template <class Filter>
bool SupportsSetMessageLoop() {
  switch (Filter::filter_type()) {
    case FILTER_DEMUXER:
    case FILTER_AUDIO_DECODER:
    case FILTER_VIDEO_DECODER:
      return true;

    case FILTER_DATA_SOURCE:
    case FILTER_AUDIO_RENDERER:
    case FILTER_VIDEO_RENDERER:
      return false;

    // Skipping default case so compiler will warn on a missed enumeration.
  }

  NOTREACHED() << "Unexpected filter type " << Filter::filter_type();
  return false;
}

// Small helper function to help us name filter threads for debugging.
//
// TODO(scherkus): figure out a cleaner way to derive the filter thread name.
template <class Filter>
const char* GetThreadName() {
  DCHECK(SupportsSetMessageLoop<Filter>());
  switch (Filter::filter_type()) {
    case FILTER_DEMUXER:
      return "DemuxerThread";
    case FILTER_AUDIO_DECODER:
      return "AudioDecoderThread";
    case FILTER_VIDEO_DECODER:
      return "VideoDecoderThread";
    default:
      return "FilterThread";
  }
}

// Helper function used with NewRunnableMethod to implement a (very) crude
// blocking counter.
//
// TODO(scherkus): remove this as soon as Stop() is made asynchronous.
void DecrementCounter(Lock* lock, ConditionVariable* cond_var, int* count) {
  AutoLock auto_lock(*lock);
  --(*count);
  CHECK(*count >= 0);
  if (*count == 0) {
    cond_var->Signal();
  }
}

}  // namespace

PipelineImpl::PipelineImpl(MessageLoop* message_loop)
    : message_loop_(message_loop) {
  ResetState();
}

PipelineImpl::~PipelineImpl() {
  DCHECK(!pipeline_internal_)
      << "Stop() must complete before destroying object";
}

// Creates the PipelineInternal and calls it's start method.
    bool PipelineImpl::Start(FilterFactory* factory,
                         const std::string& url,
                         PipelineCallback* start_callback) {
  DCHECK(!pipeline_internal_) << "PipelineInternal already exists";
  scoped_ptr<PipelineCallback> callback(start_callback);
  if (pipeline_internal_ || !factory) {
    return false;
  }

  // Create and start the PipelineInternal.
  pipeline_internal_ = new PipelineInternal(this, message_loop_);
  if (!pipeline_internal_) {
    NOTREACHED() << "Could not create PipelineInternal";
    return false;
  }
  pipeline_internal_->Start(factory, url, callback.release());
  return true;
}

// Stop the PipelineInternal who will NULL our reference to it and reset our
// state to a newly created PipelineImpl object.
void PipelineImpl::Stop(PipelineCallback* stop_callback) {
  scoped_ptr<PipelineCallback> callback(stop_callback);
  if (pipeline_internal_) {
    pipeline_internal_->Stop(callback.release());
  }
}

void PipelineImpl::Seek(base::TimeDelta time,
                        PipelineCallback* seek_callback) {
  scoped_ptr<PipelineCallback> callback(seek_callback);
  if (pipeline_internal_) {
    pipeline_internal_->Seek(time, callback.release());
  }
}

bool PipelineImpl::IsRunning() const {
  AutoLock auto_lock(lock_);
  return pipeline_internal_ != NULL;
}

bool PipelineImpl::IsInitialized() const {
  AutoLock auto_lock(lock_);
  return pipeline_internal_ && pipeline_internal_->IsInitialized();
}

bool PipelineImpl::IsRendered(const std::string& major_mime_type) const {
  AutoLock auto_lock(lock_);
  bool is_rendered = (rendered_mime_types_.find(major_mime_type) !=
                      rendered_mime_types_.end());
  return is_rendered;
}

float PipelineImpl::GetPlaybackRate() const {
  AutoLock auto_lock(lock_);
  return playback_rate_;
}

void PipelineImpl::SetPlaybackRate(float playback_rate) {
  if (playback_rate < 0.0f) {
    return;
  }

  AutoLock auto_lock(lock_);
  playback_rate_ = playback_rate;
  if (pipeline_internal_) {
    pipeline_internal_->PlaybackRateChanged(playback_rate);
  }
}

float PipelineImpl::GetVolume() const {
  AutoLock auto_lock(lock_);
  return volume_;
}

void PipelineImpl::SetVolume(float volume) {
  if (volume < 0.0f || volume > 1.0f) {
    return;
  }

  AutoLock auto_lock(lock_);
  volume_ = volume;
  if (pipeline_internal_) {
    pipeline_internal_->VolumeChanged(volume);
  }
}

base::TimeDelta PipelineImpl::GetCurrentTime() const {
  AutoLock auto_lock(lock_);
  return time_;
}

base::TimeDelta PipelineImpl::GetBufferedTime() const {
  AutoLock auto_lock(lock_);
  return buffered_time_;
}

base::TimeDelta PipelineImpl::GetDuration() const {
  AutoLock auto_lock(lock_);
  return duration_;
}

int64 PipelineImpl::GetBufferedBytes() const {
  AutoLock auto_lock(lock_);
  return buffered_bytes_;
}

int64 PipelineImpl::GetTotalBytes() const {
  AutoLock auto_lock(lock_);
  return total_bytes_;
}

void PipelineImpl::GetVideoSize(size_t* width_out, size_t* height_out) const {
  CHECK(width_out);
  CHECK(height_out);
  AutoLock auto_lock(lock_);
  *width_out = video_width_;
  *height_out = video_height_;
}

PipelineError PipelineImpl::GetError() const {
  AutoLock auto_lock(lock_);
  return error_;
}

void PipelineImpl::ResetState() {
  AutoLock auto_lock(lock_);
  pipeline_internal_  = NULL;
  duration_         = base::TimeDelta();
  buffered_time_    = base::TimeDelta();
  buffered_bytes_   = 0;
  total_bytes_      = 0;
  video_width_      = 0;
  video_height_     = 0;
  volume_           = 1.0f;
  playback_rate_    = 0.0f;
  error_            = PIPELINE_OK;
  time_             = base::TimeDelta();
  rendered_mime_types_.clear();
}

bool PipelineImpl::IsPipelineOk() const {
  return pipeline_internal_ && PIPELINE_OK == error_;
}

void PipelineImpl::SetError(PipelineError error) {
  AutoLock auto_lock(lock_);
  error_ = error;
}

base::TimeDelta PipelineImpl::GetTime() const {
  return GetCurrentTime();
}

void PipelineImpl::SetTime(base::TimeDelta time) {
  AutoLock auto_lock(lock_);
  time_ = time;
}

void PipelineImpl::SetDuration(base::TimeDelta duration) {
  AutoLock auto_lock(lock_);
  duration_ = duration;
}

void PipelineImpl::SetBufferedTime(base::TimeDelta buffered_time) {
  AutoLock auto_lock(lock_);
  buffered_time_ = buffered_time;
}

void PipelineImpl::SetTotalBytes(int64 total_bytes) {
  AutoLock auto_lock(lock_);
  total_bytes_ = total_bytes;
}

void PipelineImpl::SetBufferedBytes(int64 buffered_bytes) {
  AutoLock auto_lock(lock_);
  buffered_bytes_ = buffered_bytes;
}

void PipelineImpl::SetVideoSize(size_t width, size_t height) {
  AutoLock auto_lock(lock_);
  video_width_ = width;
  video_height_ = height;
}

void PipelineImpl::InsertRenderedMimeType(const std::string& major_mime_type) {
  AutoLock auto_lock(lock_);
  rendered_mime_types_.insert(major_mime_type);
}


//-----------------------------------------------------------------------------

PipelineInternal::PipelineInternal(PipelineImpl* pipeline,
                                   MessageLoop* message_loop)
    : pipeline_(pipeline),
      message_loop_(message_loop),
      state_(kCreated) {
}

PipelineInternal::~PipelineInternal() {
  DCHECK(state_ == kCreated || state_ == kStopped);
}

// Called on client's thread.
void PipelineInternal::Start(FilterFactory* filter_factory,
                             const std::string& url,
                             PipelineCallback* start_callback) {
  DCHECK(filter_factory);
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineInternal::StartTask, filter_factory, url,
                        start_callback));
}

// Called on client's thread.
void PipelineInternal::Stop(PipelineCallback* stop_callback) {
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineInternal::StopTask, stop_callback));
}

// Called on client's thread.
void PipelineInternal::Seek(base::TimeDelta time,
                          PipelineCallback* seek_callback) {
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineInternal::SeekTask, time,
                        seek_callback));
}

// Called on client's thread.
void PipelineInternal::PlaybackRateChanged(float playback_rate) {
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineInternal::PlaybackRateChangedTask,
                        playback_rate));
}

// Called on client's thread.
void PipelineInternal::VolumeChanged(float volume) {
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineInternal::VolumeChangedTask, volume));
}

// Called from any thread.
void PipelineInternal::SetError(PipelineError error) {
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineInternal::ErrorTask, error));
}

// Called from any thread.
base::TimeDelta PipelineInternal::GetTime() const {
  return pipeline_->GetCurrentTime();
}

// Called from any thread.
void PipelineInternal::SetTime(base::TimeDelta time) {
  pipeline_->SetTime(time);
}

// Called from any thread.
void PipelineInternal::SetDuration(base::TimeDelta duration) {
  pipeline_->SetDuration(duration);
}

// Called from any thread.
void PipelineInternal::SetBufferedTime(base::TimeDelta buffered_time) {
  pipeline_->SetBufferedTime(buffered_time);
}

// Called from any thread.
void PipelineInternal::SetTotalBytes(int64 total_bytes) {
  pipeline_->SetTotalBytes(total_bytes);
}

// Called from any thread.
void PipelineInternal::SetBufferedBytes(int64 buffered_bytes) {
  pipeline_->SetBufferedBytes(buffered_bytes);
}

// Called from any thread.
void PipelineInternal::SetVideoSize(size_t width, size_t height) {
  pipeline_->SetVideoSize(width, height);
}

// Called from any thread.
void PipelineInternal::OnFilterInitialize() {
  // Continue the initialize task by proceeding to the next stage.
  message_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PipelineInternal::InitializeTask));
}

// Called from any thread.
void PipelineInternal::OnFilterSeek() {
  // TODO(scherkus): have PipelineInternal wait to receive replies from every
  // filter before calling the client's |seek_callback_|.
}

void PipelineInternal::StartTask(FilterFactory* filter_factory,
                                 const std::string& url,
                                 PipelineCallback* start_callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK_EQ(kCreated, state_);
  filter_factory_ = filter_factory;
  url_ = url;
  start_callback_.reset(start_callback);

  // Kick off initialization.
  InitializeTask();
}

// Main initialization method called on the pipeline thread.  This code attempts
// to use the specified filter factory to build a pipeline.
// Initialization step performed in this method depends on current state of this
// object, indicated by |state_|.  After each step of initialization, this
// object transits to the next stage.  It starts by creating a DataSource,
// connects it to a Demuxer, and then connects the Demuxer's audio stream to an
// AudioDecoder which is then connected to an AudioRenderer.  If the media has
// video, then it connects a VideoDecoder to the Demuxer's video stream, and
// then connects the VideoDecoder to a VideoRenderer.
//
// When all required filters have been created and have called their
// FilterHost's InitializationComplete() method, the pipeline will update its
// state to kStarted and |init_callback_|, will be executed.
//
// TODO(hclam): InitializeTask() is now starting the pipeline asynchronously. It
// works like a big state change table. If we no longer need to start filters
// in order, we need to get rid of all the state change.
void PipelineInternal::InitializeTask() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  // If we have received the stop or error signal, return immediately.
  if (state_ == kStopped || state_ == kError)
    return;

  DCHECK(state_ == kCreated || IsPipelineInitializing());

  // Just created, create data source.
  if (state_ == kCreated) {
    state_ = kInitDataSource;
    CreateDataSource();
    return;
  }

  // Data source created, create demuxer.
  if (state_ == kInitDataSource) {
    state_ = kInitDemuxer;
    CreateDemuxer();
    return;
  }

  // Demuxer created, create audio decoder.
  if (state_ == kInitDemuxer) {
    state_ = kInitAudioDecoder;
    // If this method returns false, then there's no audio stream.
    if (CreateDecoder<AudioDecoder>())
      return;
  }

  // Assuming audio decoder was created, create audio renderer.
  if (state_ == kInitAudioDecoder) {
    state_ = kInitAudioRenderer;
    // Returns false if there's no audio stream.
    if (CreateRenderer<AudioDecoder, AudioRenderer>()) {
      pipeline_->InsertRenderedMimeType(AudioDecoder::major_mime_type());
      return;
    }
  }

  // Assuming audio renderer was created, create video decoder.
  if (state_ == kInitAudioRenderer) {
    // Then perform the stage of initialization, i.e. initialize video decoder.
    state_ = kInitVideoDecoder;
    if (CreateDecoder<VideoDecoder>())
      return;
  }

  // Assuming video decoder was created, create video renderer.
  if (state_ == kInitVideoDecoder) {
    state_ = kInitVideoRenderer;
    if (CreateRenderer<VideoDecoder, VideoRenderer>()) {
      pipeline_->InsertRenderedMimeType(VideoDecoder::major_mime_type());
      return;
    }
  }

  if (state_ == kInitVideoRenderer) {
    if (!IsPipelineOk() || pipeline_->rendered_mime_types_.empty()) {
      SetError(PIPELINE_ERROR_COULD_NOT_RENDER);
      return;
    }

    // Initialization was successful, set the volume and playback rate.
    PlaybackRateChangedTask(pipeline_->GetPlaybackRate());
    VolumeChangedTask(pipeline_->GetVolume());

    state_ = kStarted;
    filter_factory_ = NULL;
    if (start_callback_.get()) {
      start_callback_->Run();
      start_callback_.reset();
    }
  }
}

// This method is called as a result of the client calling Pipeline::Stop() or
// as the result of an error condition.  If there is no error, then set the
// pipeline's |error_| member to PIPELINE_STOPPING.  We stop the filters in the
// reverse order.
//
// TODO(scherkus): beware!  this can get posted multiple times since we post
// Stop() tasks even if we've already stopped.  Perhaps this should no-op for
// additional calls, however most of this logic will be changing.
void PipelineInternal::StopTask(PipelineCallback* stop_callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  stop_callback_.reset(stop_callback);

  // If we've already stopped, return immediately.
  if (state_ == kStopped) {
    return;
  }

  // Carry out setting the error, notifying the client and destroying filters.
  ErrorTask(PIPELINE_STOPPING);

  // We no longer need to examine our previous state, set it to stopped.
  state_ = kStopped;

  // Reset the pipeline and set our reference to NULL so we don't accidentally
  // modify the pipeline.  Once remaining tasks execute we will be destroyed.
  pipeline_->ResetState();
  pipeline_ = NULL;

  // Notify the client that stopping has finished.
  if (stop_callback_.get()) {
    stop_callback_->Run();
    stop_callback_.reset();
  }
}

void PipelineInternal::ErrorTask(PipelineError error) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK_NE(PIPELINE_OK, error) << "PIPELINE_OK isn't an error!";

  // Suppress executing additional error logic.
  if (state_ == kError) {
    return;
  }

  // Update our error code first in case we execute the start callback.
  pipeline_->SetError(error);

  // Notify the client that starting did not complete, if necessary.
  if (IsPipelineInitializing() && start_callback_.get()) {
    start_callback_->Run();
  }
  start_callback_.reset();
  filter_factory_ = NULL;

  // We no longer need to examine our previous state, set it to stopped.
  state_ = kError;

  // Destroy every filter and reset the pipeline as well.
  DestroyFilters();
}

void PipelineInternal::PlaybackRateChangedTask(float playback_rate) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  for (FilterVector::iterator iter = filters_.begin();
       iter != filters_.end();
       ++iter) {
    (*iter)->SetPlaybackRate(playback_rate);
  }
}

void PipelineInternal::VolumeChangedTask(float volume) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  scoped_refptr<AudioRenderer> audio_renderer;
  GetFilter(&audio_renderer);
  if (audio_renderer) {
    audio_renderer->SetVolume(volume);
  }
}

void PipelineInternal::SeekTask(base::TimeDelta time,
                                PipelineCallback* seek_callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  seek_callback_.reset(seek_callback);

  // Supress seeking if we haven't fully started.
  if (state_ != kStarted) {
    return;
  }

  for (FilterVector::iterator iter = filters_.begin();
       iter != filters_.end();
       ++iter) {
    (*iter)->Seek(time, NewCallback(this, &PipelineInternal::OnFilterSeek));
  }

  // TODO(hclam): we should set the time when the above seek operations were all
  // successful and first frame/packet at the desired time is decoded. I'm
  // setting the time here because once we do the callback the user can ask for
  // current time immediately, which is the old time. In order to get rid this
  // little glitch, we either assume the seek was successful and time is updated
  // immediately here or we set time and do callback when we have new
  // frames/packets.
  SetTime(time);
  if (seek_callback_.get()) {
    seek_callback_->Run();
    seek_callback_.reset();
  }
}

template <class Filter, class Source>
void PipelineInternal::CreateFilter(FilterFactory* filter_factory,
                                    Source source,
                                    const MediaFormat& media_format) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  // Create the filter.
  scoped_refptr<Filter> filter = filter_factory->Create<Filter>(media_format);
  if (!filter) {
    SetError(PIPELINE_ERROR_REQUIRED_FILTER_MISSING);
    return;
  }

  // Create a dedicated thread for this filter if applicable.
  if (SupportsSetMessageLoop<Filter>()) {
    scoped_ptr<base::Thread> thread(new base::Thread(GetThreadName<Filter>()));
    if (!thread.get() || !thread->Start()) {
      NOTREACHED() << "Could not start filter thread";
      SetError(PIPELINE_ERROR_INITIALIZATION_FAILED);
      return;
    }

    filter->set_message_loop(thread->message_loop());
    filter_threads_.push_back(thread.release());
  }

  // Register ourselves as the filter's host.
  DCHECK(IsPipelineOk());
  DCHECK(filter_types_.find(Filter::filter_type()) == filter_types_.end())
      << "Filter type " << Filter::filter_type() << " already exists";
  filter->set_host(this);
  filters_.push_back(filter.get());
  filter_types_[Filter::filter_type()] = filter.get();

  // Now initialize the filter.
  filter->Initialize(source,
      NewCallback(this, &PipelineInternal::OnFilterInitialize));
}

void PipelineInternal::CreateDataSource() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  MediaFormat url_format;
  url_format.SetAsString(MediaFormat::kMimeType, mime_type::kURL);
  url_format.SetAsString(MediaFormat::kURL, url_);
  CreateFilter<DataSource>(filter_factory_, url_, url_format);
}

void PipelineInternal::CreateDemuxer() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  scoped_refptr<DataSource> data_source;
  GetFilter(&data_source);
  DCHECK(data_source);
  CreateFilter<Demuxer, DataSource>(filter_factory_, data_source);
}

template <class Decoder>
bool PipelineInternal::CreateDecoder() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  scoped_refptr<Demuxer> demuxer;
  GetFilter(&demuxer);
  DCHECK(demuxer);

  const std::string major_mime_type = Decoder::major_mime_type();
  const int num_outputs = demuxer->GetNumberOfStreams();
  for (int i = 0; i < num_outputs; ++i) {
    scoped_refptr<DemuxerStream> stream = demuxer->GetStream(i);
    std::string value;
    if (stream->media_format().GetAsString(MediaFormat::kMimeType, &value) &&
        0 == value.compare(0, major_mime_type.length(), major_mime_type)) {
      CreateFilter<Decoder, DemuxerStream>(filter_factory_, stream);
      return true;
    }
  }
  return false;
}

template <class Decoder, class Renderer>
bool PipelineInternal::CreateRenderer() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(IsPipelineOk());

  scoped_refptr<Decoder> decoder;
  GetFilter(&decoder);

  if (decoder) {
    // If the decoder was created.
    const std::string major_mime_type = Decoder::major_mime_type();
    CreateFilter<Renderer, Decoder>(filter_factory_, decoder);
    return true;
  }
  return false;
}

template <class Filter>
void PipelineInternal::GetFilter(scoped_refptr<Filter>* filter_out) const {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  FilterTypeMap::const_iterator ft = filter_types_.find(Filter::filter_type());
  if (ft == filter_types_.end()) {
    *filter_out = NULL;
  } else {
    *filter_out = reinterpret_cast<Filter*>(ft->second.get());
  }
}

void PipelineInternal::DestroyFilters() {
  // Stop every filter.
  for (FilterVector::iterator iter = filters_.begin();
       iter != filters_.end();
       ++iter) {
    (*iter)->Stop();
  }

  // Crude blocking counter implementation.
  Lock lock;
  ConditionVariable wait_for_zero(&lock);
  int count = filter_threads_.size();

  // Post a task to every filter's thread to ensure that they've completed their
  // stopping logic before stopping the threads themselves.
  //
  // TODO(scherkus): again, Stop() should either be synchronous or we should
  // receive a signal from filters that they have indeed stopped.
  for (FilterThreadVector::iterator iter = filter_threads_.begin();
       iter != filter_threads_.end();
       ++iter) {
    (*iter)->message_loop()->PostTask(FROM_HERE,
        NewRunnableFunction(&DecrementCounter, &lock, &wait_for_zero, &count));
  }

  // Wait on our "blocking counter".
  {
    AutoLock auto_lock(lock);
    while (count > 0) {
      wait_for_zero.Wait();
    }
  }

  // Stop every running filter thread.
  //
  // TODO(scherkus): can we watchdog this section to detect wedged threads?
  for (FilterThreadVector::iterator iter = filter_threads_.begin();
       iter != filter_threads_.end();
       ++iter) {
    (*iter)->Stop();
  }

  // Reset the pipeline, which will decrement a reference to this object.
  // We will get destroyed as soon as the remaining tasks finish executing.
  // To be safe, we'll set our pipeline reference to NULL.
  filters_.clear();
  filter_types_.clear();
  STLDeleteElements(&filter_threads_);
}

}  // namespace media
