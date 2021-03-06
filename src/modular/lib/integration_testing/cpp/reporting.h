// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_INTEGRATION_TESTING_CPP_REPORTING_H_
#define SRC_MODULAR_LIB_INTEGRATION_TESTING_CPP_REPORTING_H_

#include <fuchsia/testing/runner/cpp/fidl.h>

#include <iostream>

#define TEST_PASS(label) std::cerr << "[TEST] PASS: " << (label) << std::endl
#define TEST_FAIL(label)                                  \
  {                                                       \
    std::cerr << "[TEST] FAIL: " << (label) << std::endl; \
    modular_testing::Fail(label);                         \
  }

namespace modular_testing {

// Helper class to record that a particular condition was reached sometime
// in the life span of an object. If the test point is not marked with Pass()
// by the time the destructor is called, the test point records failure.
class TestPoint {
 public:
  explicit TestPoint(std::string label);
  ~TestPoint();

  void Pass();

 private:
  std::string label_;
  bool value_{};
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_INTEGRATION_TESTING_CPP_REPORTING_H_
