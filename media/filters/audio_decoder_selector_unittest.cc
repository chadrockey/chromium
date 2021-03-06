// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "media/base/gmock_callback_support.h"
#include "media/base/mock_filters.h"
#include "media/base/test_helpers.h"
#include "media/filters/audio_decoder_selector.h"
#include "media/filters/decrypting_demuxer_stream.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrictMock;

namespace media {

class AudioDecoderSelectorTest : public ::testing::Test {
 public:
  enum DecryptorCapability {
    kNoDecryptor,
    // Used to test Abort() during DecryptingAudioDecoder::Initialize() and
    // DecryptingDemuxerStream::Initialize(). We don't need this for normal
    // AudioDecoders since we use MockAudioDecoder.
    kHoldSetDecryptor,
    kDecryptOnly,
    kDecryptAndDecode
  };

  AudioDecoderSelectorTest()
      : demuxer_stream_(
            new StrictMock<MockDemuxerStream>(DemuxerStream::AUDIO)),
        decryptor_(new NiceMock<MockDecryptor>()),
        decoder_1_(new StrictMock<MockAudioDecoder>()),
        decoder_2_(new StrictMock<MockAudioDecoder>()) {
    all_decoders_.push_back(decoder_1_);
    all_decoders_.push_back(decoder_2_);

    EXPECT_CALL(*decoder_1_, Stop(_))
      .WillRepeatedly(RunClosure<0>());
    EXPECT_CALL(*decoder_2_, Stop(_))
      .WillRepeatedly(RunClosure<0>());
  }

  ~AudioDecoderSelectorTest() {
    if (selected_decoder_) {
      selected_decoder_->Stop(NewExpectedClosure());
    }

    message_loop_.RunUntilIdle();
  }

  MOCK_METHOD1(OnStatistics, void(const PipelineStatistics&));
  MOCK_METHOD1(SetDecryptorReadyCallback, void(const media::DecryptorReadyCB&));
  MOCK_METHOD2(OnDecoderSelected,
               void(AudioDecoder*, DecryptingDemuxerStream*));

  void MockOnDecoderSelected(
      scoped_ptr<AudioDecoder> decoder,
      scoped_ptr<DecryptingDemuxerStream> stream) {
    OnDecoderSelected(decoder.get(), stream.get());
    selected_decoder_ = decoder.Pass();
  }

  void UseClearStream() {
    AudioDecoderConfig clear_audio_config(
        kCodecVorbis, kSampleFormatPlanarF32, CHANNEL_LAYOUT_STEREO, 44100,
        NULL, 0, false);
    demuxer_stream_->set_audio_decoder_config(clear_audio_config);
  }

  void UseEncryptedStream() {
    AudioDecoderConfig encrypted_audio_config(
        kCodecVorbis, kSampleFormatPlanarF32, CHANNEL_LAYOUT_STEREO, 44100,
        NULL, 0, true);
    demuxer_stream_->set_audio_decoder_config(encrypted_audio_config);
  }

  void InitializeDecoderSelector(DecryptorCapability decryptor_capability,
                                 int num_decoders) {
    SetDecryptorReadyCB set_decryptor_ready_cb;
    if (decryptor_capability != kNoDecryptor) {
      set_decryptor_ready_cb =
          base::Bind(&AudioDecoderSelectorTest::SetDecryptorReadyCallback,
                     base::Unretained(this));
    }

    if (decryptor_capability == kDecryptOnly ||
        decryptor_capability == kDecryptAndDecode) {

      EXPECT_CALL(*this, SetDecryptorReadyCallback(_))
          .WillRepeatedly(RunCallback<0>(decryptor_.get()));

      if (decryptor_capability == kDecryptOnly) {
        EXPECT_CALL(*decryptor_, InitializeAudioDecoder(_, _))
            .WillRepeatedly(RunCallback<1>(false));
      } else {
        EXPECT_CALL(*decryptor_, InitializeAudioDecoder(_, _))
            .WillRepeatedly(RunCallback<1>(true));
      }
    } else if (decryptor_capability == kHoldSetDecryptor) {
      // Set and cancel DecryptorReadyCB but the callback is never fired.
      EXPECT_CALL(*this, SetDecryptorReadyCallback(_))
          .Times(2);
    }

    DCHECK_GE(all_decoders_.size(), static_cast<size_t>(num_decoders));
    all_decoders_.erase(
        all_decoders_.begin() + num_decoders, all_decoders_.end());

    decoder_selector_.reset(new AudioDecoderSelector(
        message_loop_.message_loop_proxy(),
        all_decoders_.Pass(),
        set_decryptor_ready_cb));
  }

  void SelectDecoder() {
    decoder_selector_->SelectAudioDecoder(
        demuxer_stream_.get(),
        base::Bind(&AudioDecoderSelectorTest::OnStatistics,
                   base::Unretained(this)),
        base::Bind(&AudioDecoderSelectorTest::MockOnDecoderSelected,
                   base::Unretained(this)));
    message_loop_.RunUntilIdle();
  }

  void SelectDecoderAndAbort() {
    SelectDecoder();

    EXPECT_CALL(*this, OnDecoderSelected(IsNull(), IsNull()));
    decoder_selector_->Abort();
    message_loop_.RunUntilIdle();
  }

  // Fixture members.
  scoped_ptr<AudioDecoderSelector> decoder_selector_;
  scoped_ptr<StrictMock<MockDemuxerStream> > demuxer_stream_;
  // Use NiceMock since we don't care about most of calls on the decryptor, e.g.
  // RegisterNewKeyCB().
  scoped_ptr<NiceMock<MockDecryptor> > decryptor_;
  StrictMock<MockAudioDecoder>* decoder_1_;
  StrictMock<MockAudioDecoder>* decoder_2_;
  ScopedVector<AudioDecoder> all_decoders_;
  scoped_ptr<AudioDecoder> selected_decoder_;
  base::MessageLoop message_loop_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AudioDecoderSelectorTest);
};

// The stream is not encrypted but we have no clear decoder. No decoder can be
// selected.
TEST_F(AudioDecoderSelectorTest, ClearStream_NoDecryptor_NoClearDecoder) {
  UseClearStream();
  InitializeDecoderSelector(kNoDecryptor, 0);

  EXPECT_CALL(*this, OnDecoderSelected(IsNull(), IsNull()));

  SelectDecoder();
}

// The stream is not encrypted and we have one clear decoder. The decoder
// will be selected.
TEST_F(AudioDecoderSelectorTest, ClearStream_NoDecryptor_OneClearDecoder) {
  UseClearStream();
  InitializeDecoderSelector(kNoDecryptor, 1);

  EXPECT_CALL(*decoder_1_, Initialize(_, _, _))
      .WillOnce(RunCallback<1>(PIPELINE_OK));
  EXPECT_CALL(*this, OnDecoderSelected(decoder_1_, IsNull()));

  SelectDecoder();
}

TEST_F(AudioDecoderSelectorTest,
       Abort_ClearStream_NoDecryptor_OneClearDecoder) {
  UseClearStream();
  InitializeDecoderSelector(kNoDecryptor, 1);

  EXPECT_CALL(*decoder_1_, Initialize(_, _, _));

  SelectDecoderAndAbort();
}

// The stream is not encrypted and we have multiple clear decoders. The first
// decoder that can decode the input stream will be selected.
TEST_F(AudioDecoderSelectorTest, ClearStream_NoDecryptor_MultipleClearDecoder) {
  UseClearStream();
  InitializeDecoderSelector(kNoDecryptor, 2);

  EXPECT_CALL(*decoder_1_, Initialize(_, _, _))
      .WillOnce(RunCallback<1>(DECODER_ERROR_NOT_SUPPORTED));
  EXPECT_CALL(*decoder_2_, Initialize(_, _, _))
      .WillOnce(RunCallback<1>(PIPELINE_OK));
  EXPECT_CALL(*this, OnDecoderSelected(decoder_2_, IsNull()));

  SelectDecoder();
}

TEST_F(AudioDecoderSelectorTest,
       Abort_ClearStream_NoDecryptor_MultipleClearDecoder) {
  UseClearStream();
  InitializeDecoderSelector(kNoDecryptor, 2);

  EXPECT_CALL(*decoder_1_, Initialize(_, _, _))
      .WillOnce(RunCallback<1>(DECODER_ERROR_NOT_SUPPORTED));
  EXPECT_CALL(*decoder_2_, Initialize(_, _, _));

  SelectDecoderAndAbort();
}

// There is a decryptor but the stream is not encrypted. The decoder will be
// selected.
TEST_F(AudioDecoderSelectorTest, ClearStream_HasDecryptor) {
  UseClearStream();
  InitializeDecoderSelector(kDecryptOnly, 1);

  EXPECT_CALL(*decoder_1_, Initialize(_, _, _))
      .WillOnce(RunCallback<1>(PIPELINE_OK));
  EXPECT_CALL(*this, OnDecoderSelected(decoder_1_, IsNull()));

  SelectDecoder();
}

TEST_F(AudioDecoderSelectorTest, Abort_ClearStream_HasDecryptor) {
  UseClearStream();
  InitializeDecoderSelector(kDecryptOnly, 1);

  EXPECT_CALL(*decoder_1_, Initialize(_, _, _));

  SelectDecoderAndAbort();
}

// The stream is encrypted and there's no decryptor. No decoder can be selected.
TEST_F(AudioDecoderSelectorTest, EncryptedStream_NoDecryptor) {
  UseEncryptedStream();
  InitializeDecoderSelector(kNoDecryptor, 1);

  EXPECT_CALL(*this, OnDecoderSelected(IsNull(), IsNull()));

  SelectDecoder();
}

// Decryptor can only do decryption and there's no decoder available. No decoder
// can be selected.
TEST_F(AudioDecoderSelectorTest, EncryptedStream_DecryptOnly_NoClearDecoder) {
  UseEncryptedStream();
  InitializeDecoderSelector(kDecryptOnly, 0);

  EXPECT_CALL(*this, OnDecoderSelected(IsNull(), IsNull()));

  SelectDecoder();
}

TEST_F(AudioDecoderSelectorTest,
       Abort_EncryptedStream_DecryptOnly_NoClearDecoder) {
  UseEncryptedStream();
  InitializeDecoderSelector(kHoldSetDecryptor, 0);

  SelectDecoderAndAbort();
}

// Decryptor can do decryption-only and there's a decoder available. The decoder
// will be selected and a DecryptingDemuxerStream will be created.
TEST_F(AudioDecoderSelectorTest, EncryptedStream_DecryptOnly_OneClearDecoder) {
  UseEncryptedStream();
  InitializeDecoderSelector(kDecryptOnly, 1);

  EXPECT_CALL(*decoder_1_, Initialize(_, _, _))
      .WillOnce(RunCallback<1>(PIPELINE_OK));
  EXPECT_CALL(*this, OnDecoderSelected(decoder_1_, NotNull()));

  SelectDecoder();
}

TEST_F(AudioDecoderSelectorTest,
       Abort_EncryptedStream_DecryptOnly_OneClearDecoder) {
  UseEncryptedStream();
  InitializeDecoderSelector(kDecryptOnly, 1);

  EXPECT_CALL(*decoder_1_, Initialize(_, _, _));

  SelectDecoderAndAbort();
}

// Decryptor can only do decryption and there are multiple decoders available.
// The first decoder that can decode the input stream will be selected and
// a DecryptingDemuxerStream will be created.
TEST_F(AudioDecoderSelectorTest,
       EncryptedStream_DecryptOnly_MultipleClearDecoder) {
  UseEncryptedStream();
  InitializeDecoderSelector(kDecryptOnly, 2);

  EXPECT_CALL(*decoder_1_, Initialize(_, _, _))
      .WillOnce(RunCallback<1>(DECODER_ERROR_NOT_SUPPORTED));
  EXPECT_CALL(*decoder_2_, Initialize(_, _, _))
      .WillOnce(RunCallback<1>(PIPELINE_OK));
  EXPECT_CALL(*this, OnDecoderSelected(decoder_2_, NotNull()));

  SelectDecoder();
}

TEST_F(AudioDecoderSelectorTest,
       Abort_EncryptedStream_DecryptOnly_MultipleClearDecoder) {
  UseEncryptedStream();
  InitializeDecoderSelector(kDecryptOnly, 2);

  EXPECT_CALL(*decoder_1_, Initialize(_, _, _))
      .WillOnce(RunCallback<1>(DECODER_ERROR_NOT_SUPPORTED));
  EXPECT_CALL(*decoder_2_, Initialize(_, _, _));

  SelectDecoderAndAbort();
}

// Decryptor can do decryption and decoding. A DecryptingAudioDecoder will be
// created and selected. The clear decoders should not be touched at all.
// No DecryptingDemuxerStream should to be created.
TEST_F(AudioDecoderSelectorTest, EncryptedStream_DecryptAndDecode) {
  UseEncryptedStream();
  InitializeDecoderSelector(kDecryptAndDecode, 1);

  EXPECT_CALL(*this, OnDecoderSelected(NotNull(), IsNull()));

  SelectDecoder();
}

TEST_F(AudioDecoderSelectorTest, Abort_EncryptedStream_DecryptAndDecode) {
  UseEncryptedStream();
  InitializeDecoderSelector(kHoldSetDecryptor, 1);

  SelectDecoderAndAbort();
}

}  // namespace media
