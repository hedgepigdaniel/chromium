// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/feature_engagement_tracker/internal/condition_validator.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace feature_engagement_tracker {

TEST(ConditionValidatorResultTest, TestAllOK) {
  EXPECT_TRUE(ConditionValidator::Result(true).NoErrors());
}

TEST(ConditionValidatorResultTest, TestAllErrors) {
  EXPECT_FALSE(ConditionValidator::Result(false).NoErrors());
}

TEST(ConditionValidatorResultTest, TestModelNotReady) {
  ConditionValidator::Result result(true);
  result.model_ready_ok = false;
  EXPECT_FALSE(result.NoErrors());
}

TEST(ConditionValidatorResultTest, TestCurrentlyShowing) {
  ConditionValidator::Result result(true);
  result.currently_showing_ok = false;
  EXPECT_FALSE(result.NoErrors());
}

TEST(ConditionValidatorResultTest, TestFeatureEnabled) {
  ConditionValidator::Result result(true);
  result.feature_enabled_ok = false;
  EXPECT_FALSE(result.NoErrors());
}

TEST(ConditionValidatorResultTest, TestInvalidConfig) {
  ConditionValidator::Result result(true);
  result.config_ok = false;
  EXPECT_FALSE(result.NoErrors());
}

TEST(ConditionValidatorResultTest, TestUsedFailed) {
  ConditionValidator::Result result(true);
  result.used_ok = false;
  EXPECT_FALSE(result.NoErrors());
}

TEST(ConditionValidatorResultTest, TestTriggerFailed) {
  ConditionValidator::Result result(true);
  result.trigger_ok = false;
  EXPECT_FALSE(result.NoErrors());
}

TEST(ConditionValidatorResultTest, TestPreconditionsFailed) {
  ConditionValidator::Result result(true);
  result.preconditions_ok = false;
  EXPECT_FALSE(result.NoErrors());
}

TEST(ConditionValidatorResultTest, TestSessionRateFailed) {
  ConditionValidator::Result result(true);
  result.session_rate_ok = false;
  EXPECT_FALSE(result.NoErrors());
}

TEST(ConditionValidatorResultTest, TestMultipleErrors) {
  ConditionValidator::Result result(true);
  result.preconditions_ok = false;
  result.session_rate_ok = false;
  EXPECT_FALSE(result.NoErrors());
}

}  // namespace feature_engagement_tracker
