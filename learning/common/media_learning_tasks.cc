// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/learning/common/media_learning_tasks.h"

namespace media {
namespace learning {

static const LearningTask& GetWillPlayTask() {
  static LearningTask task_;
  if (!task_.feature_descriptions.size()) {
    task_.name = "MediaLearningWillPlay";
    // TODO(liberato): fill in the rest here, once we have the features picked.
  }

  return task_;
}

static const LearningTask& GetConsecutiveBadWindowsTask() {
  static LearningTask task_;
  if (!task_.feature_descriptions.size()) {
    task_.name = "MediaLearningConsecutiveBadWindows";
    task_.model = LearningTask::Model::kExtraTrees;

    // Target is max number of consecutive bad windows.
    task_.target_description = {"max_bad_windows",
                                LearningTask::Ordering::kNumeric};

    // TODO: Be sure to update webmediaplayer_impl if you change these, since it
    // memorizes them.
    task_.feature_descriptions.push_back(
        {"codec", LearningTask::Ordering::kUnordered});
    task_.feature_descriptions.push_back(
        {"profile", LearningTask::Ordering::kUnordered});
    task_.feature_descriptions.push_back(
        {"width", LearningTask::Ordering::kNumeric});
    task_.feature_descriptions.push_back(
        {"fps", LearningTask::Ordering::kNumeric});

    // Report via UKM, but allow up to 100 bad windows, since it'll auto-scale
    // to two digits of precision.  Might as well use all of it, even if 100
    // consecutive bad windows is unlikely.
    task_.report_via_ukm = true;
    task_.ukm_min_input_value = 0.0;
    task_.ukm_max_input_value = 100.0;
  }

  return task_;
}

// static
const LearningTask& MediaLearningTasks::Get(Id id) {
  switch (id) {
    case Id::kWillPlay:
      return GetWillPlayTask();
    case Id::kConsecutiveBadWindows:
      return GetConsecutiveBadWindowsTask();
  }
}

// static
void MediaLearningTasks::Register(
    base::RepeatingCallback<void(const LearningTask&)> cb) {
  cb.Run(Get(Id::kWillPlay));
  cb.Run(Get(Id::kConsecutiveBadWindows));
}

}  // namespace learning
}  // namespace media
