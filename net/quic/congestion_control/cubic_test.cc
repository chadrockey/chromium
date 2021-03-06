// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/logging.h"
#include "net/quic/congestion_control/cubic.h"
#include "net/quic/test_tools/mock_clock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {

class CubicTest : public ::testing::Test {
 protected:
  CubicTest()
      : one_ms_(QuicTime::Delta::FromMilliseconds(1)),
        hundred_ms_(QuicTime::Delta::FromMilliseconds(100)),
        cubic_(&clock_) {
  }
  const QuicTime::Delta one_ms_;
  const QuicTime::Delta hundred_ms_;
  MockClock clock_;
  Cubic cubic_;
};

TEST_F(CubicTest, AboveOrgin) {
  // Convex growth.
  const QuicTime::Delta rtt_min = hundred_ms_;
  uint32 current_cwnd = 10;
  uint32 expected_cwnd = current_cwnd + 1;
  // Initialize the state.
  clock_.AdvanceTime(one_ms_);
  EXPECT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterAck(current_cwnd, rtt_min));
  current_cwnd = expected_cwnd;
  // Normal TCP phase.
  for (int i = 0; i < 48; ++i) {
    for (uint32 n = 1; n < current_cwnd; ++n) {
      // Call once per ACK.
      EXPECT_EQ(current_cwnd,
                cubic_.CongestionWindowAfterAck(current_cwnd, rtt_min));
    }
    clock_.AdvanceTime(hundred_ms_);
    current_cwnd = cubic_.CongestionWindowAfterAck(current_cwnd, rtt_min);
    EXPECT_EQ(expected_cwnd, current_cwnd);
    expected_cwnd++;
  }
  // Cubic phase.
  for (int j = 48; j < 100; ++j) {
    for (uint32 n = 1; n < current_cwnd; ++n) {
      // Call once per ACK.
      EXPECT_EQ(current_cwnd,
                cubic_.CongestionWindowAfterAck(current_cwnd, rtt_min));
    }
    clock_.AdvanceTime(hundred_ms_);
    current_cwnd = cubic_.CongestionWindowAfterAck(current_cwnd, rtt_min);
  }
  float elapsed_time_s = 10.0f + 0.1f;  // We need to add the RTT here.
  expected_cwnd = 11 + (elapsed_time_s * elapsed_time_s * elapsed_time_s * 410)
      / 1024;
  EXPECT_EQ(expected_cwnd, current_cwnd);
}

TEST_F(CubicTest, LossEvents) {
  const QuicTime::Delta rtt_min = hundred_ms_;
  uint32 current_cwnd = 422;
  uint32 expected_cwnd = current_cwnd + 1;
  // Initialize the state.
  clock_.AdvanceTime(one_ms_);
  EXPECT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterAck(current_cwnd, rtt_min));
  expected_cwnd = current_cwnd * 939 / 1024;
  EXPECT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterPacketLoss(current_cwnd));
  expected_cwnd = current_cwnd * 939 / 1024;
  EXPECT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterPacketLoss(current_cwnd));
}

TEST_F(CubicTest, BelowOrgin) {
  // Concave growth.
  const QuicTime::Delta rtt_min = hundred_ms_;
  uint32 current_cwnd = 422;
  uint32 expected_cwnd = current_cwnd + 1;
  // Initialize the state.
  clock_.AdvanceTime(one_ms_);
  EXPECT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterAck(current_cwnd, rtt_min));
  expected_cwnd = current_cwnd * 939 / 1024;
  EXPECT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterPacketLoss(current_cwnd));
  current_cwnd = expected_cwnd;
  // First update after epoch.
  current_cwnd = cubic_.CongestionWindowAfterAck(current_cwnd, rtt_min);
  // Cubic phase.
  for (int i = 0; i < 54; ++i) {
    for (uint32 n = 1; n < current_cwnd; ++n) {
      // Call once per ACK.
      EXPECT_EQ(current_cwnd,
                cubic_.CongestionWindowAfterAck(current_cwnd, rtt_min));
    }
    clock_.AdvanceTime(hundred_ms_);
    current_cwnd = cubic_.CongestionWindowAfterAck(current_cwnd, rtt_min);
  }
  expected_cwnd = 440;
  EXPECT_EQ(expected_cwnd, current_cwnd);
}

}  // namespace test
}  // namespace net
