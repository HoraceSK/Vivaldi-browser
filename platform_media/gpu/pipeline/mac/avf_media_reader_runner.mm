// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (c) 2018 Vivaldi Technologies AS. All rights reserved.
// Copyright (C) 2014 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA

#include "platform_media/gpu/pipeline/mac/avf_media_reader_runner.h"

#include "base/mac/mac_util.h"
#include "platform_media/gpu/decoders/mac/avf_media_reader.h"
#include "platform_media/gpu/data_source/ipc_data_source.h"
#include "media/base/bind_to_current_loop.h"
#include "platform_media/common/platform_logging_util.h"
#include "platform_media/common/platform_media_pipeline_types.h"
#include "platform_media/common/mac/platform_media_pipeline_types_mac.h"

namespace media {

namespace {

void InitializeReader(AVFMediaReader* reader,
                      IPCDataSource* data_source,
                      const std::string& mime_type,
                      const AVFMediaReaderRunner::InitializeCB& initialize_cb) {
  const bool success = reader->Initialize(data_source, mime_type);

  int bitrate = -1;
  media::PlatformMediaTimeInfo time_info;
  media::PlatformAudioConfig audio_config;
  media::PlatformVideoConfig video_config;

  if (success) {
    bitrate = reader->bitrate();
    time_info.duration = reader->duration();
    time_info.start_time = reader->start_time();

    if (reader->has_audio_track()) {
      audio_config.format = SampleFormat::kSampleFormatF32;
      audio_config.channel_count =
          reader->audio_stream_format().mChannelsPerFrame;
      audio_config.samples_per_second =
          reader->audio_stream_format().mSampleRate;
      VLOG(1) << " PROPMEDIA(GPU) : " << __FUNCTION__
              << " Using PlatformAudioConfig : "
              << Loggable(audio_config);
    } else {
      VLOG(1) << " PROPMEDIA(GPU) : " << __FUNCTION__
              << " No Audio Track ";
    }

    if (reader->has_video_track()) {
      video_config = GetPlatformVideoConfig(
          reader->video_stream_format(), reader->video_transform());
      VLOG(1) << " PROPMEDIA(GPU) : " << __FUNCTION__
              << " Using PlatformVideoConfig : "
              << Loggable(video_config);
    } else {
      VLOG(1) << " PROPMEDIA(GPU) : " << __FUNCTION__
              << " No Video Track ";
    }
  }

  initialize_cb.Run(success, bitrate, time_info, audio_config, video_config);
}

}  // namespace

AVFMediaReaderRunner::AVFMediaReaderRunner(IPCDataSource* data_source)
    : data_source_(data_source),
      reader_queue_(nullptr),
      will_seek_(false),
      weak_ptr_factory_(this) {
  VLOG(1) << " PROPMEDIA(GPU) : " << __FUNCTION__;
}

AVFMediaReaderRunner::~AVFMediaReaderRunner() {
  VLOG(1) << " PROPMEDIA(GPU) : " << __FUNCTION__;
  DCHECK(thread_checker_.CalledOnValidThread());

  // Unblock any ongoing data read operations.
  data_source_->Stop();

  if (reader_queue_) {
    // Because |reader_queue_| is a FIFO queue, this effectively "joins" with
    // the queue: Any |reader_| tasks currently in flight will run on
    // |reader_queue_|, and then the following block will run.
    dispatch_sync(reader_queue_, ^{
      reader_.reset();
    });

    dispatch_release(reader_queue_);
  }
}

// static
bool AVFMediaReaderRunner::IsAvailable() {
  return true;
}

void AVFMediaReaderRunner::Initialize(const std::string& mime_type,
                                      const InitializeCB& initialize_cb) {
  DCHECK(thread_checker_.CalledOnValidThread());

  VLOG(1) << " PROPMEDIA(GPU) : " << __FUNCTION__
          << " Mime type : " << mime_type;

  reader_queue_ = dispatch_queue_create("com.operasoftware.MediaReader",
                                        DISPATCH_QUEUE_SERIAL);
  if (!reader_queue_) {
    LOG(ERROR) << " PROPMEDIA(GPU) : " << __FUNCTION__
               << ": Failed, could not create dispatch queue";
    initialize_cb.Run(false,
                      -1,
                      media::PlatformMediaTimeInfo(),
                      media::PlatformAudioConfig(),
                      media::PlatformVideoConfig());
    return;
  }

  reader_.reset(new AVFMediaReader(reader_queue_));

  const auto cb = BindToCurrentLoop(initialize_cb);
  const std::string mt = mime_type;
  dispatch_async(reader_queue_, ^{
    InitializeReader(reader_.get(), data_source_, mt, cb);
  });
}

void AVFMediaReaderRunner::ReadAudioData(const ReadDataCB& read_audio_data_cb) {
  VLOG(5) << " PROPMEDIA(GPU) : " << __FUNCTION__
          << " Renderer asking for audio data";
  DCHECK(thread_checker_.CalledOnValidThread());

  const auto cb = BindToCurrentLoop(base::Bind(
      &AVFMediaReaderRunner::DataReady, weak_ptr_factory_.GetWeakPtr(),
      PlatformMediaDataType::PLATFORM_MEDIA_AUDIO, read_audio_data_cb));
  dispatch_async(reader_queue_, ^{
    cb.Run(reader_->GetNextAudioSample());
  });
}

void AVFMediaReaderRunner::ReadVideoData(const ReadDataCB& read_video_data_cb) {
  VLOG(5) << " PROPMEDIA(GPU) : " << __FUNCTION__
          << " Renderer asking for video data";
  DCHECK(thread_checker_.CalledOnValidThread());

  const auto cb = BindToCurrentLoop(base::Bind(
      &AVFMediaReaderRunner::DataReady, weak_ptr_factory_.GetWeakPtr(),
      PlatformMediaDataType::PLATFORM_MEDIA_VIDEO, read_video_data_cb));
  dispatch_async(reader_queue_, ^{
    cb.Run(reader_->GetNextVideoSample());
  });
}

void AVFMediaReaderRunner::WillSeek() {
  VLOG(1) << " PROPMEDIA(GPU) : " << __FUNCTION__;
  DCHECK(thread_checker_.CalledOnValidThread());

  will_seek_ = true;
  data_source_->Suspend();
}

void AVFMediaReaderRunner::Seek(base::TimeDelta time, const SeekCB& seek_cb) {
  VLOG(1) << " PROPMEDIA(GPU) : " << __FUNCTION__;
  DCHECK(thread_checker_.CalledOnValidThread());

  will_seek_ = false;

  const auto cb = BindToCurrentLoop(seek_cb);
  dispatch_async(reader_queue_, ^{
    data_source_->Resume();
    cb.Run(reader_->Seek(time));
  });
}

void AVFMediaReaderRunner::DataReady(
    PlatformMediaDataType type,
    const ReadDataCB& read_data_cb,
    const scoped_refptr<DataBuffer>& data) {
  VLOG(5) << " PROPMEDIA(GPU) : " << __FUNCTION__
          << " (type = " << type
          << ", will seek = " << will_seek_ << ")";
  DCHECK(thread_checker_.CalledOnValidThread());

  if (will_seek_ && last_data_buffer_[type]) {
    // In |WillSeek()| we interrupt the IPCDataSource, which can easily cause
    // decode errors.  But we do that because of the approaching seek request,
    // so it's fine to ignore any output buffers that have arrived between the
    // |WillSeek()| and |Seek()| calls.
    read_data_cb.Run(last_data_buffer_[type]);
    return;
  }

  last_data_buffer_[type] = data;
  read_data_cb.Run(data);
}

}  // namespace media
