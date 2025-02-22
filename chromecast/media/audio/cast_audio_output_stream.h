// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_MEDIA_AUDIO_CAST_AUDIO_OUTPUT_STREAM_H_
#define CHROMECAST_MEDIA_AUDIO_CAST_AUDIO_OUTPUT_STREAM_H_

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "media/audio/audio_io.h"
#include "media/audio/audio_parameters.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace chromecast {
namespace media {

class CastAudioManager;
class DecoderBufferBase;

class CastAudioOutputStream : public ::media::AudioOutputStream {
 public:
  CastAudioOutputStream(const ::media::AudioParameters& audio_params,
                        CastAudioManager* audio_manager);
  ~CastAudioOutputStream() override;

  // ::media::AudioOutputStream implementation.
  bool Open() override;
  void Close() override;
  void Start(AudioSourceCallback* source_callback) override;
  void Stop() override;
  void SetVolume(double volume) override;
  void GetVolume(double* volume) override;

 private:
  class Backend;

  void OnClosed();
  void PushBuffer();
  void OnPushBufferComplete(bool success);

  const ::media::AudioParameters audio_params_;
  CastAudioManager* const audio_manager_;

  double volume_;
  AudioSourceCallback* source_callback_;
  scoped_ptr<::media::AudioBus> audio_bus_;
  scoped_refptr<media::DecoderBufferBase> decoder_buffer_;
  scoped_ptr<Backend> backend_;
  const base::TimeDelta buffer_duration_;
  bool push_in_progress_;
  base::TimeTicks next_push_time_;

  scoped_refptr<base::SingleThreadTaskRunner> audio_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> backend_task_runner_;

  base::WeakPtrFactory<CastAudioOutputStream> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(CastAudioOutputStream);
};

}  // namespace media
}  // namespace chromecast

#endif  // CHROMECAST_MEDIA_AUDIO_CAST_AUDIO_OUTPUT_STREAM_H_
