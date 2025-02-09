// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/decoder_selector.h"

#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "build/build_config.h"
#include "media/base/audio_decoder.h"
#include "media/base/cdm_context.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_log.h"
#include "media/base/video_decoder.h"
#include "media/filters/decoder_stream_traits.h"
#include "media/filters/decrypting_demuxer_stream.h"

namespace media {

template <DemuxerStream::Type StreamType>
DecoderSelector<StreamType>::DecoderSelector(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    CreateDecodersCB create_decoders_cb,
    MediaLog* media_log)
    : task_runner_(std::move(task_runner)),
      create_decoders_cb_(std::move(create_decoders_cb)),
      media_log_(media_log),
      weak_this_factory_(this) {}

template <DemuxerStream::Type StreamType>
DecoderSelector<StreamType>::~DecoderSelector() {
  DVLOG(2) << __func__;
  DCHECK(task_runner_->BelongsToCurrentThread());
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::Initialize(
    StreamTraits* traits,
    DemuxerStream* stream,
    CdmContext* cdm_context,
    base::RepeatingClosure waiting_for_decryption_key_cb) {
  DVLOG(2) << __func__;
  DCHECK(traits);
  DCHECK(stream);

  traits_ = traits;
  stream_ = stream;
  cdm_context_ = cdm_context;
  waiting_for_decryption_key_cb_ = std::move(waiting_for_decryption_key_cb);
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::SelectDecoder(
    SelectDecoderCB select_decoder_cb,
    typename Decoder::OutputCB output_cb) {
  DVLOG(2) << __func__;
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(select_decoder_cb_.is_null());

  select_decoder_cb_ = std::move(select_decoder_cb);
  output_cb_ = std::move(output_cb);
  config_ = traits_->GetDecoderConfig(stream_);

  if (!config_.IsValidConfig()) {
    DLOG(ERROR) << "Invalid stream config";
    ReturnNullDecoder();
    return;
  }

  // If this is the first selection (ever or since FinalizeDecoderSelection()),
  // start selection with the full list of potential decoders.
  if (!is_selecting_decoders_) {
    is_selecting_decoders_ = true;
    decoders_ = create_decoders_cb_.Run();
  }

  InitializeDecoder();
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::FinalizeDecoderSelection() {
  DVLOG(2) << __func__;
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(select_decoder_cb_.is_null());

  is_selecting_decoders_ = false;

  // Discard any remaining decoder instances, they won't be used.
  decoders_.clear();
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::InitializeDecoder() {
  DVLOG(2) << __func__;
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(!decoder_);

  if (decoders_.empty()) {
    // Decoder selection failed. If the stream is encrypted, try again using
    // DecryptingDemuxerStream.
    if (config_.is_encrypted() && cdm_context_) {
      InitializeDecryptingDemuxerStream();
      return;
    }

    ReturnNullDecoder();
    return;
  }

  // Initialize the first decoder on the list.
  decoder_ = std::move(decoders_.front());
  decoders_.erase(decoders_.begin());

  DVLOG(2) << __func__ << ": initializing " << decoder_->GetDisplayName();
  const bool is_live = stream_->liveness() == DemuxerStream::LIVENESS_LIVE;
  traits_->InitializeDecoder(
      decoder_.get(), config_, is_live, cdm_context_,
      base::BindRepeating(&DecoderSelector<StreamType>::OnDecoderInitializeDone,
                          weak_this_factory_.GetWeakPtr()),
      output_cb_, waiting_for_decryption_key_cb_);
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::OnDecoderInitializeDone(bool success) {
  DVLOG(2) << __func__ << ": " << decoder_->GetDisplayName()
           << " success=" << success;
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (!success) {
    // Try the next decoder on the list.
    decoder_.reset();
    InitializeDecoder();
    return;
  }

  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(select_decoder_cb_), std::move(decoder_),
                     std::move(decrypting_demuxer_stream_)));
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::ReturnNullDecoder() {
  DVLOG(1) << __func__ << ": No decoder selected";
  DCHECK(task_runner_->BelongsToCurrentThread());

  decoders_.clear();

  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(select_decoder_cb_), nullptr, nullptr));
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::InitializeDecryptingDemuxerStream() {
  DCHECK(decoders_.empty());
  DCHECK(config_.is_encrypted());
  DCHECK(cdm_context_);

  decrypting_demuxer_stream_ = std::make_unique<DecryptingDemuxerStream>(
      task_runner_, media_log_, waiting_for_decryption_key_cb_);

  decrypting_demuxer_stream_->Initialize(
      stream_, cdm_context_,
      base::BindRepeating(
          &DecoderSelector<StreamType>::OnDecryptingDemuxerStreamInitializeDone,
          weak_this_factory_.GetWeakPtr()));
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::OnDecryptingDemuxerStreamInitializeDone(
    PipelineStatus status) {
  DVLOG(2) << __func__
           << ": status=" << MediaLog::PipelineStatusToString(status);
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (status != PIPELINE_OK) {
    // Since we already tried every potential decoder without DDS, give up.
    decrypting_demuxer_stream_.reset();
    ReturnNullDecoder();
    return;
  }

  // Once DDS is enabled, there is no going back.
  // TODO(sandersd): Support transitions from encrypted to unencrypted.
  stream_ = decrypting_demuxer_stream_.get();
  cdm_context_ = nullptr;

  // We'll use the decrypted config from now on.
  config_ = traits_->GetDecoderConfig(stream_);
  DCHECK(!config_.is_encrypted());

  // Try decoder selection again now that DDS is being used.
  decoders_ = create_decoders_cb_.Run();
  InitializeDecoder();
}

// These forward declarations tell the compiler that we will use
// DecoderSelector with these arguments, allowing us to keep these definitions
// in our .cc without causing linker errors. This also means if anyone tries to
// instantiate a DecoderSelector with anything but these two specializations
// they'll most likely get linker errors.
template class DecoderSelector<DemuxerStream::AUDIO>;
template class DecoderSelector<DemuxerStream::VIDEO>;

}  // namespace media
