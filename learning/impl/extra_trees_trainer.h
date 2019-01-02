// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_LEARNING_IMPL_EXTRA_TREES_TRAINER_H_
#define MEDIA_LEARNING_IMPL_EXTRA_TREES_TRAINER_H_

#include <memory>
#include <vector>

#include "base/component_export.h"
#include "base/macros.h"
#include "media/learning/common/learning_task.h"
#include "media/learning/impl/random_number_generator.h"
#include "media/learning/impl/training_algorithm.h"

namespace media {
namespace learning {

// Bagged forest of extremely randomized trees.
//
// These are an ensemble of trees.  Each tree is constructed from the full
// training set.  The trees are constructed by selecting a random subset of
// features at each node.  For each feature, a uniformly random split point is
// chosen.  The feature with the best randomly chosen split point is used.
//
// These will automatically convert nominal values to one-hot vectors.
class COMPONENT_EXPORT(LEARNING_IMPL) ExtraTreesTrainer
    : public HasRandomNumberGenerator {
 public:
  ExtraTreesTrainer();
  ~ExtraTreesTrainer();

  std::unique_ptr<Model> Train(const LearningTask& task,
                               const TrainingData& training_data);

 private:
  DISALLOW_COPY_AND_ASSIGN(ExtraTreesTrainer);
};

}  // namespace learning
}  // namespace media

#endif  // MEDIA_LEARNING_IMPL_EXTRA_TREES_TRAINER_H_
