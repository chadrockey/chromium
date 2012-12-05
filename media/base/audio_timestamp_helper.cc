// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/audio_timestamp_helper.h"

#include "base/logging.h"
#include "media/base/buffers.h"

namespace media {

AudioTimestampHelper::AudioTimestampHelper(int bytes_per_frame,
                                           int samples_per_second)
    : bytes_per_frame_(bytes_per_frame),
      base_timestamp_(kNoTimestamp()),
      frame_count_(0) {
  DCHECK_GT(bytes_per_frame, 0);
  DCHECK_GT(samples_per_second, 0);
  double fps = samples_per_second;
  microseconds_per_frame_ = base::Time::kMicrosecondsPerSecond / fps;
}

void AudioTimestampHelper::SetBaseTimestamp(base::TimeDelta base_timestamp) {
  base_timestamp_ = base_timestamp;
  frame_count_ = 0;
}

base::TimeDelta AudioTimestampHelper::base_timestamp() const {
  return base_timestamp_;
}

void AudioTimestampHelper::AddBytes(int byte_count) {
  DCHECK_GE(byte_count, 0);
  DCHECK(base_timestamp_ != kNoTimestamp());
  DCHECK_EQ(byte_count % bytes_per_frame_, 0);
  frame_count_ += byte_count / bytes_per_frame_;
}

base::TimeDelta AudioTimestampHelper::GetTimestamp() const {
  return ComputeTimestamp(frame_count_);
}

base::TimeDelta AudioTimestampHelper::GetDuration(int byte_count) const {
  DCHECK_GE(byte_count, 0);
  DCHECK_EQ(byte_count % bytes_per_frame_, 0);
  int frames = byte_count / bytes_per_frame_;
  base::TimeDelta end_timestamp = ComputeTimestamp(frame_count_ + frames);
  return end_timestamp - GetTimestamp();
}

int64 AudioTimestampHelper::GetBytesToTarget(
    base::TimeDelta target) const {
  DCHECK(base_timestamp_ != kNoTimestamp());
  DCHECK(target >= base_timestamp_);

  int64 delta_in_us = (target - GetTimestamp()).InMicroseconds();
  if (delta_in_us == 0)
    return 0;

  // Compute a timestamp relative to |base_timestamp_| since timestamps
  // created from |frame_count_| are computed relative to this base.
  // This ensures that the time to frame computation here is the proper inverse
  // of the frame to time computation in ComputeTimestamp().
  base::TimeDelta delta_from_base = target - base_timestamp_;

  // Compute frame count for the time delta. This computation rounds to
  // the nearest whole number of frames.
  double threshold = microseconds_per_frame_ / 2;
  int64 target_frame_count =
      (delta_from_base.InMicroseconds() + threshold) / microseconds_per_frame_;
  return bytes_per_frame_ * (target_frame_count - frame_count_);
}

base::TimeDelta AudioTimestampHelper::ComputeTimestamp(
    int64 frame_count) const {
  DCHECK_GE(frame_count, 0);
  DCHECK(base_timestamp_ != kNoTimestamp());
  double frames_us = microseconds_per_frame_ * frame_count;
  return base_timestamp_ + base::TimeDelta::FromMicroseconds(frames_us);
}

}  // namespace media
