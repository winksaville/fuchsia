// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_TEST_H_
#define SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_TEST_H_

#include <memory>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

namespace camera {

// |Imx227DeviceTester| is spawned by the driver in |imx227.cc|
class Imx227Device;

class Imx227DeviceTester : public zxtest::Test {
 public:
  static zx_status_t RunTests(Imx227Device* device);

 protected:
  void SetUp() override;
  void TearDown() override;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_TEST_H_
