// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "base/rand_util.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "media/cast/logging/logging_impl.h"
#include "media/cast/test/fake_task_runner.h"

namespace media {
namespace cast {

// Insert frame duration- one second.
const int64 kIntervalTime1S = 1;
// Test frame rate goal - 30fps.
const int kFrameIntervalMs = 33;

static const int64 kStartMillisecond = GG_INT64_C(12345678900000);

class TestLogging : public ::testing::Test {
 protected:
  TestLogging() : config_(false) {
    // Enable all logging types.
    config_.enable_raw_data_collection = true;
    config_.enable_stats_data_collection = true;
    config_.enable_uma_stats = true;
    config_.enable_tracing = true;

    testing_clock_.Advance(
        base::TimeDelta::FromMilliseconds(kStartMillisecond));
    task_runner_ = new test::FakeTaskRunner(&testing_clock_);
    logging_.reset(new LoggingImpl(task_runner_, config_));
  }

  virtual ~TestLogging() {}

  CastLoggingConfig config_;
  scoped_refptr<test::FakeTaskRunner> task_runner_;
  scoped_ptr<LoggingImpl> logging_;
  base::SimpleTestTickClock testing_clock_;
};

TEST_F(TestLogging, BasicFrameLogging) {
  base::TimeTicks start_time = testing_clock_.NowTicks();
  base::TimeDelta time_interval = testing_clock_.NowTicks() - start_time;
  uint32 rtp_timestamp = 0;
  uint32 frame_id = 0;
  do {
    logging_->InsertFrameEvent(testing_clock_.NowTicks(),
                               kAudioFrameCaptured, rtp_timestamp, frame_id);
    testing_clock_.Advance(
        base::TimeDelta::FromMilliseconds(kFrameIntervalMs));
    rtp_timestamp += kFrameIntervalMs * 90;
    ++frame_id;
    time_interval = testing_clock_.NowTicks() - start_time;
  }  while (time_interval.InSeconds() < kIntervalTime1S);
  // Get logging data.
  FrameRawMap frame_map = logging_->GetFrameRawData();
  // Size of map should be equal to the number of frames logged.
  EXPECT_EQ(frame_id, frame_map.size());
  // Verify stats.
  const FrameStatsMap* frame_stats =
      logging_->GetFrameStatsData(testing_clock_.NowTicks());
  // Size of stats equals the number of events.
  EXPECT_EQ(1u, frame_stats->size());
  FrameStatsMap::const_iterator it = frame_stats->find(kAudioFrameCaptured);
  EXPECT_TRUE(it != frame_stats->end());
  EXPECT_NEAR(30.3, it->second->framerate_fps, 0.1);
  EXPECT_EQ(0, it->second->bitrate_kbps);
  EXPECT_EQ(0, it->second->max_delay_ms);
  EXPECT_EQ(0, it->second->min_delay_ms);
  EXPECT_EQ(0, it->second->avg_delay_ms);
}

TEST_F(TestLogging, FrameLoggingWithSize) {
  // Average packet size.
  const int kBaseFrameSizeBytes = 25000;
  const int kRandomSizeInterval = 100;
  base::TimeTicks start_time = testing_clock_.NowTicks();
  base::TimeDelta time_interval = testing_clock_.NowTicks() - start_time;
  uint32 rtp_timestamp = 0;
  uint32 frame_id = 0;
  do {
    int size = kBaseFrameSizeBytes +
        base::RandInt(-kRandomSizeInterval, kRandomSizeInterval);
    logging_->InsertFrameEventWithSize(testing_clock_.NowTicks(),
        kAudioFrameCaptured, rtp_timestamp, frame_id, size);
    testing_clock_.Advance(
        base::TimeDelta::FromMilliseconds(kFrameIntervalMs));
    rtp_timestamp += kFrameIntervalMs * 90;
    ++frame_id;
    time_interval = testing_clock_.NowTicks() - start_time;
  }  while (time_interval.InSeconds() < kIntervalTime1S);
  // Get logging data.
  FrameRawMap frame_map =  logging_->GetFrameRawData();
  // Size of map should be equal to the number of frames logged.
  EXPECT_EQ(frame_id, frame_map.size());
  // Verify stats.
  const FrameStatsMap* frame_stats =
      logging_->GetFrameStatsData(testing_clock_.NowTicks());
  // Size of stats equals the number of events.
  EXPECT_EQ(1u, frame_stats->size());
  FrameStatsMap::const_iterator it = frame_stats->find(kAudioFrameCaptured);
  EXPECT_TRUE(it != frame_stats->end());
  EXPECT_NEAR(30.3, it->second->framerate_fps, 0.1);
  EXPECT_NEAR(8 * kBaseFrameSizeBytes / (kFrameIntervalMs * 1000),
      it->second->bitrate_kbps, kRandomSizeInterval);
  EXPECT_EQ(0, it->second->max_delay_ms);
  EXPECT_EQ(0, it->second->min_delay_ms);
  EXPECT_EQ(0, it->second->avg_delay_ms);
}

TEST_F(TestLogging, FrameLoggingWithDelay) {
  // Average packet size.
  const int kPlayoutDelayMs = 50;
  const int kRandomSizeInterval = 20;
  base::TimeTicks start_time = testing_clock_.NowTicks();
  base::TimeDelta time_interval = testing_clock_.NowTicks() - start_time;
  uint32 rtp_timestamp = 0;
  uint32 frame_id = 0;
  do {
    int delay = kPlayoutDelayMs +
        base::RandInt(-kRandomSizeInterval, kRandomSizeInterval);
    logging_->InsertFrameEventWithDelay(testing_clock_.NowTicks(),
        kAudioFrameCaptured, rtp_timestamp, frame_id,
        base::TimeDelta::FromMilliseconds(delay));
    testing_clock_.Advance(
        base::TimeDelta::FromMilliseconds(kFrameIntervalMs));
    rtp_timestamp += kFrameIntervalMs * 90;
    ++frame_id;
    time_interval = testing_clock_.NowTicks() - start_time;
  }  while (time_interval.InSeconds() < kIntervalTime1S);
  // Get logging data.
  FrameRawMap frame_map =  logging_->GetFrameRawData();
  // Size of map should be equal to the number of frames logged.
  EXPECT_EQ(frame_id, frame_map.size());
  // Verify stats.
  const FrameStatsMap* frame_stats =
      logging_->GetFrameStatsData(testing_clock_.NowTicks());
  // Size of stats equals the number of events.
  EXPECT_EQ(1u, frame_stats->size());
  FrameStatsMap::const_iterator it = frame_stats->find(kAudioFrameCaptured);
  EXPECT_TRUE(it != frame_stats->end());
  EXPECT_NEAR(30.3, it->second->framerate_fps, 0.1);
  EXPECT_EQ(0, it->second->bitrate_kbps);
  EXPECT_GE(kPlayoutDelayMs + kRandomSizeInterval, it->second->max_delay_ms);
  EXPECT_LE(kPlayoutDelayMs - kRandomSizeInterval, it->second->min_delay_ms);
  EXPECT_NEAR(kPlayoutDelayMs, it->second->avg_delay_ms,
      0.2 * kRandomSizeInterval);
}

TEST_F(TestLogging, MultipleEventFrameLogging) {
  base::TimeTicks start_time = testing_clock_.NowTicks();
  base::TimeDelta time_interval = testing_clock_.NowTicks() - start_time;
  uint32 rtp_timestamp = 0;
  uint32 frame_id = 0;
  do {
    logging_->InsertFrameEvent(testing_clock_.NowTicks(), kAudioFrameCaptured,
                               rtp_timestamp, frame_id);
    if (frame_id % 2) {
      logging_->InsertFrameEventWithSize(testing_clock_.NowTicks(),
          kAudioFrameEncoded, rtp_timestamp, frame_id, 1500);
    } else if (frame_id % 3) {
      logging_->InsertFrameEvent(testing_clock_.NowTicks(), kVideoFrameDecoded,
                                 rtp_timestamp, frame_id);
    } else {
      logging_->InsertFrameEventWithDelay(testing_clock_.NowTicks(),
          kVideoRenderDelay, rtp_timestamp, frame_id,
          base::TimeDelta::FromMilliseconds(20));
    }
    testing_clock_.Advance(
        base::TimeDelta::FromMilliseconds(kFrameIntervalMs));
    rtp_timestamp += kFrameIntervalMs * 90;
    ++frame_id;
    time_interval = testing_clock_.NowTicks() - start_time;
  }  while (time_interval.InSeconds() < kIntervalTime1S);
  // Get logging data.
  FrameRawMap frame_map = logging_->GetFrameRawData();
  // Size of map should be equal to the number of frames logged.
  EXPECT_EQ(frame_id, frame_map.size());
  // Multiple events captured per frame.
}

TEST_F(TestLogging, PacketLogging) {
  const int kNumPacketsPerFrame = 10;
  const int kBaseSize = 2500;
  const int kSizeInterval = 100;
  base::TimeTicks start_time = testing_clock_.NowTicks();
  base::TimeDelta time_interval = testing_clock_.NowTicks() - start_time;
  uint32 rtp_timestamp = 0;
  uint32 frame_id = 0;
  do {
    for (int i = 0; i < kNumPacketsPerFrame; ++i) {
      int size = kBaseSize + base::RandInt(-kSizeInterval, kSizeInterval);
      logging_->InsertPacketEvent(testing_clock_.NowTicks(), kPacketSentToPacer,
          rtp_timestamp, frame_id, i, kNumPacketsPerFrame, size);
    }
    testing_clock_.Advance(
        base::TimeDelta::FromMilliseconds(kFrameIntervalMs));
    rtp_timestamp += kFrameIntervalMs * 90;
    ++frame_id;
    time_interval = testing_clock_.NowTicks() - start_time;
  }  while (time_interval.InSeconds() < kIntervalTime1S);
  // Get logging data.
  PacketRawMap raw_map = logging_->GetPacketRawData();
  // Size of map should be equal to the number of frames logged.
  EXPECT_EQ(frame_id, raw_map.size());
  // Verify stats.
  const PacketStatsMap* stats_map =
      logging_->GetPacketStatsData(testing_clock_.NowTicks());
  // Size of stats equals the number of events.
  EXPECT_EQ(1u, stats_map->size());
  PacketStatsMap::const_iterator it = stats_map->find(kPacketSentToPacer);
  EXPECT_TRUE(it != stats_map->end());
  // We only store the bitrate as a packet statistic.
  EXPECT_NEAR(8 * kNumPacketsPerFrame * kBaseSize / (kFrameIntervalMs * 1000),
      it->second, kSizeInterval);
}

TEST_F(TestLogging, GenericLogging) {
  // Insert multiple generic types.
  const size_t kNumRuns = 1000;
  const int kBaseValue = 20;
  for (size_t i = 0; i < kNumRuns; ++i) {
    int value = kBaseValue + base::RandInt(-5, 5);
    logging_->InsertGenericEvent(testing_clock_.NowTicks(), kRttMs, value);
    if (i % 2) {
      logging_->InsertGenericEvent(testing_clock_.NowTicks(), kPacketLoss,
                                   value);
    }
    if (!(i % 4)) {
      logging_->InsertGenericEvent(testing_clock_.NowTicks(), kJitterMs, value);
    }
  }
  GenericRawMap raw_map = logging_->GetGenericRawData();
  const GenericStatsMap* stats_map = logging_->GetGenericStatsData();
  // Size of generic map = number of different events.
  EXPECT_EQ(3u, raw_map.size());
  EXPECT_EQ(3u, stats_map->size());
  // Raw events - size of internal map = number of calls.
  GenericRawMap::iterator rit = raw_map.find(kRttMs);
  EXPECT_EQ(kNumRuns, rit->second.value.size());
  EXPECT_EQ(kNumRuns, rit->second.timestamp.size());
  rit = raw_map.find(kPacketLoss);
  EXPECT_EQ(kNumRuns / 2, rit->second.value.size());
  EXPECT_EQ(kNumRuns / 2, rit->second.timestamp.size());
  rit = raw_map.find(kJitterMs);
  EXPECT_EQ(kNumRuns / 4, rit->second.value.size());
  EXPECT_EQ(kNumRuns / 4, rit->second.timestamp.size());
  // Stats - one value per event.
  GenericStatsMap::const_iterator sit = stats_map->find(kRttMs);
  EXPECT_NEAR(kBaseValue, sit->second, 2.5);
  sit = stats_map->find(kPacketLoss);
  EXPECT_NEAR(kBaseValue, sit->second, 2.5);
  sit = stats_map->find(kJitterMs);
  EXPECT_NEAR(kBaseValue, sit->second, 2.5);
}

TEST_F(TestLogging, RtcpMultipleEventFrameLogging) {
  base::TimeTicks start_time = testing_clock_.NowTicks();
  base::TimeDelta time_interval = testing_clock_.NowTicks() - start_time;
  uint32 rtp_timestamp = 0;
  uint32 frame_id = 0;
  do {
    logging_->InsertFrameEvent(testing_clock_.NowTicks(), kAudioFrameCaptured,
                               rtp_timestamp, frame_id);
    if (frame_id % 2) {
      logging_->InsertFrameEventWithSize(testing_clock_.NowTicks(),
          kAudioFrameEncoded, rtp_timestamp, frame_id, 1500);
    } else if (frame_id % 3) {
      logging_->InsertFrameEvent(testing_clock_.NowTicks(), kVideoFrameDecoded,
                                 rtp_timestamp, frame_id);
    } else {
      logging_->InsertFrameEventWithDelay(testing_clock_.NowTicks(),
          kVideoRenderDelay, rtp_timestamp, frame_id,
          base::TimeDelta::FromMilliseconds(20));
    }
    testing_clock_.Advance(
        base::TimeDelta::FromMilliseconds(kFrameIntervalMs));
    rtp_timestamp += kFrameIntervalMs * 90;
    ++frame_id;
    time_interval = testing_clock_.NowTicks() - start_time;
  }  while (time_interval.InSeconds() < kIntervalTime1S);
  // Get logging data.
  FrameRawMap frame_map = logging_->GetFrameRawData();
  // Size of map should be equal to the number of frames logged.
  EXPECT_EQ(frame_id, frame_map.size());
  // Multiple events captured per frame.

  AudioRtcpRawMap audio_rtcp = logging_->GetAudioRtcpRawData();
  EXPECT_EQ(0u, audio_rtcp.size());

  VideoRtcpRawMap video_rtcp = logging_->GetVideoRtcpRawData();
  EXPECT_EQ((frame_id + 1) / 2, video_rtcp.size());
}

}  // namespace cast
}  // namespace media
