// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../../task/task.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/mmio/mmio.h>
#include <lib/syslog/global.h>
#include <stdint.h>
#include <unistd.h>
#include <zircon/pixelformat.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <ddk/debug.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "../gdc-regs.h"
#include "../gdc.h"
#include "src/camera/drivers/test_utils/fake-buffer-collection.h"

namespace gdc {
namespace {

constexpr uint32_t kWidth = 1080;
constexpr uint32_t kHeight = 764;
constexpr uint32_t kNumberOfBuffers = 5;
constexpr uint32_t kNumberOfMmios = 50;
constexpr uint32_t kConfigSize = 1000;
constexpr uint32_t kMaxTasks = 10;
constexpr uint32_t kImageFormatTableSize = 8;

template <typename T>
ddk_mock::MockMmioReg& GetMockReg(ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get().addr()];
}

// Integration test for the driver defined in zircon/system/dev/camera/arm-isp.
class TaskTest : public zxtest::Test {
 public:
  void ProcessFrameCallback(uint32_t output_buffer_id) {
    fbl::AutoLock al(&lock_);
    callback_check_.push_back(output_buffer_id);
    frame_ready_ = true;
    event_.Signal();
  }

  void WaitAndReset() {
    fbl::AutoLock al(&lock_);
    while (frame_ready_ == false) {
      event_.Wait(&lock_);
    }
    frame_ready_ = false;
  }

  uint32_t GetCallbackSize() {
    fbl::AutoLock al(&lock_);
    return callback_check_.size();
  }

  uint32_t GetCallbackBack() {
    fbl::AutoLock al(&lock_);
    return callback_check_.back();
  }

 protected:
  void SetUpBufferCollections(uint32_t buffer_collection_count) {
    frame_ready_ = false;
    ASSERT_OK(fake_bti_create(bti_handle_.reset_and_get_address()));
    ASSERT_OK(camera::GetImageFormat2(fuchsia_sysmem_PixelFormatType_NV12, input_image_format_,
                                      kWidth, kHeight));
    ASSERT_OK(camera::GetImageFormat2(fuchsia_sysmem_PixelFormatType_NV12,
                                      output_image_format_table_[0], kWidth, kHeight));
    ASSERT_OK(camera::GetImageFormat2(fuchsia_sysmem_PixelFormatType_NV12,
                                      output_image_format_table_[1], kWidth, kHeight));
    ASSERT_OK(camera::GetImageFormat2(fuchsia_sysmem_PixelFormatType_NV12,
                                      output_image_format_table_[2], kWidth, kHeight));
    zx_status_t status = camera::CreateContiguousBufferCollectionInfo2(
        input_buffer_collection_, input_image_format_, bti_handle_.get(), kWidth, kHeight,
        buffer_collection_count);
    ASSERT_OK(status);

    status = camera::CreateContiguousBufferCollectionInfo2(
        output_buffer_collection_, output_image_format_table_[0], bti_handle_.get(), kWidth,
        kHeight, buffer_collection_count);
    ASSERT_OK(status);

    status = zx_vmo_create_contiguous(bti_handle_.get(), kConfigSize, 0,
                                      config_vmo_.reset_and_get_address());
    ASSERT_OK(status);
  }

  // Sets up GdcDevice, initialize a task.
  // Returns a task id.
  uint32_t SetupForFrameProcessing(ddk_mock::MockMmioRegRegion& fake_regs) {
    SetUpBufferCollections(kNumberOfBuffers);

    zx::port port;
    EXPECT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));

    callback_.frame_ready = [](void* ctx, const frame_available_info* info) {
      EXPECT_EQ(static_cast<TaskTest*>(ctx)->output_image_format_index_,
                info->metadata.image_format_index);
      return static_cast<TaskTest*>(ctx)->ProcessFrameCallback(info->buffer_id);
    };
    callback_.ctx = this;

    EXPECT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq_));
    EXPECT_OK(port.duplicate(ZX_RIGHT_SAME_RIGHTS, &port_));

    zx::interrupt irq;
    EXPECT_OK(irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, &irq));

    gdc_device_ =
        std::make_unique<GdcDevice>(nullptr, ddk::MmioBuffer(fake_regs.GetMmioBuffer()),
                                    ddk::MmioBuffer(fake_regs.GetMmioBuffer()), std::move(irq),
                                    std::move(bti_handle_), std::move(port));

    zx::vmo config_vmo;
    EXPECT_OK(config_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &config_vmo));
    uint32_t task_id;

    zx_status_t status = gdc_device_->GdcInitTask(&input_buffer_collection_,
                                                  &output_buffer_collection_, &input_image_format_,
                                                  output_image_format_table_, kImageFormatTableSize,
                                                  0, std::move(config_vmo), &callback_, &task_id);
    EXPECT_OK(status);
    output_image_format_index_ = 0;

    // Start the thread.
    EXPECT_OK(gdc_device_->StartThread());

    return task_id;
  }

  void SetExpectations(ddk_mock::MockMmioRegRegion& fake_regs) {
    GetMockReg<Config>(fake_regs).ExpectWrite(0).ExpectWrite(0).ExpectWrite(1);

    GetMockReg<ConfigAddr>(fake_regs).ExpectWrite();
    GetMockReg<ConfigSize>(fake_regs).ExpectWrite();

    GetMockReg<DataInWidth>(fake_regs).ExpectWrite(input_image_format_.display_width);
    GetMockReg<DataInHeight>(fake_regs).ExpectWrite(input_image_format_.display_height);
    GetMockReg<DataOutWidth>(fake_regs).ExpectWrite(output_image_format_table_[0].display_width);
    GetMockReg<DataOutHeight>(fake_regs).ExpectWrite(output_image_format_table_[0].display_height);

    GetMockReg<Data1InAddr>(fake_regs).ExpectWrite();
    GetMockReg<Data1InOffset>(fake_regs).ExpectWrite(input_image_format_.bytes_per_row);

    GetMockReg<Data2InAddr>(fake_regs).ExpectWrite();
    GetMockReg<Data2InOffset>(fake_regs).ExpectWrite(input_image_format_.bytes_per_row);

    GetMockReg<Data1OutAddr>(fake_regs).ExpectWrite();
    GetMockReg<Data1OutOffset>(fake_regs).ExpectWrite(output_image_format_table_[0].bytes_per_row);
    GetMockReg<Data2OutAddr>(fake_regs).ExpectWrite();
    GetMockReg<Data2OutOffset>(fake_regs).ExpectWrite(output_image_format_table_[0].bytes_per_row);
  }

  void TearDown() override {
    if (bti_handle_ != ZX_HANDLE_INVALID) {
      fake_bti_destroy(bti_handle_.get());
    }
  }

  zx::vmo config_vmo_;
  zx::bti bti_handle_;
  zx::port port_;
  zx::interrupt irq_;
  hw_accel_callback_t callback_;
  buffer_collection_info_2_t input_buffer_collection_;
  buffer_collection_info_2_t output_buffer_collection_;
  image_format_2_t input_image_format_;
  // Array of output Image formats.
  image_format_2_t output_image_format_table_[kImageFormatTableSize];
  std::unique_ptr<GdcDevice> gdc_device_;
  uint32_t output_image_format_index_;

 private:
  std::vector<uint32_t> callback_check_;
  bool frame_ready_;
  fbl::Mutex lock_;
  fbl::ConditionVariable event_;
};

TEST_F(TaskTest, BasicCreationTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  fbl::AllocChecker ac;
  auto task = std::unique_ptr<GdcTask>(new (&ac) GdcTask());
  EXPECT_TRUE(ac.check());
  zx_status_t status;
  status = task->Init(&input_buffer_collection_, &output_buffer_collection_, &input_image_format_,
                      output_image_format_table_, kImageFormatTableSize, 0, config_vmo_, &callback_,
                      bti_handle_);
  EXPECT_OK(status);
}

TEST_F(TaskTest, InvalidFormatTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  image_format_2_t format;
  EXPECT_OK(camera::GetImageFormat2(fuchsia_sysmem_PixelFormatType_NV12, format, kWidth, kHeight));
  format.pixel_format.type = ZX_PIXEL_FORMAT_MONO_8;
  fbl::AllocChecker ac;
  auto task = std::unique_ptr<GdcTask>(new (&ac) GdcTask());
  EXPECT_TRUE(ac.check());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            task->Init(&input_buffer_collection_, &output_buffer_collection_, &format,
                       output_image_format_table_, kImageFormatTableSize, 0, config_vmo_,
                       &callback_, bti_handle_));
}

TEST_F(TaskTest, InputBufferTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  fbl::AllocChecker ac;
  auto task = std::unique_ptr<GdcTask>(new (&ac) GdcTask());
  EXPECT_TRUE(ac.check());
  zx_status_t status;
  status = task->Init(&input_buffer_collection_, &output_buffer_collection_, &input_image_format_,
                      output_image_format_table_, kImageFormatTableSize, 0, config_vmo_, &callback_,
                      bti_handle_);
  EXPECT_OK(status);

  // Get the input buffers physical addresses
  // Request for physical addresses of the buffers.
  // Expecting to get error when requesting for invalid index.
  zx_paddr_t addr;
  for (uint32_t i = 0; i < kNumberOfBuffers; i++) {
    EXPECT_OK(task->GetInputBufferPhysAddr(0, &addr));
  }

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, task->GetInputBufferPhysAddr(kNumberOfBuffers, &addr));
}

TEST_F(TaskTest, InvalidVmoTest) {
  SetUpBufferCollections(0);
  fbl::AllocChecker ac;
  auto task = std::unique_ptr<GdcTask>(new (&ac) GdcTask());
  EXPECT_TRUE(ac.check());
  zx_status_t status;
  status = task->Init(&input_buffer_collection_, &output_buffer_collection_, &input_image_format_,
                      output_image_format_table_, kImageFormatTableSize, 0, config_vmo_, &callback_,
                      bti_handle_);
  // Expecting Task setup to be returning an error when there are
  // no VMOs in the buffer collection. At the moment VmoPool library
  // doesn't return an error.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
}

TEST_F(TaskTest, InitTaskTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  __UNUSED auto task_id = SetupForFrameProcessing(fake_regs);

  std::vector<uint32_t> received_ids;
  for (uint32_t i = 0; i < kMaxTasks; i++) {
    zx::vmo config_vmo;
    ASSERT_OK(config_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &config_vmo));

    uint32_t task_id;
    zx_status_t status = gdc_device_->GdcInitTask(&input_buffer_collection_,
                                                  &output_buffer_collection_, &input_image_format_,
                                                  output_image_format_table_, kImageFormatTableSize,
                                                  0, std::move(config_vmo), &callback_, &task_id);
    EXPECT_OK(status);
    // Checking to see if we are getting unique task ids.
    auto entry = find(received_ids.begin(), received_ids.end(), task_id);
    EXPECT_EQ(received_ids.end(), entry);
    received_ids.push_back(task_id);
  }
  ASSERT_OK(gdc_device_->StopThread());
}

TEST_F(TaskTest, RemoveTaskTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  auto task_id = SetupForFrameProcessing(fake_regs);

  // Valid id.
  ASSERT_NO_DEATH(([this, task_id]() { gdc_device_->GdcRemoveTask(task_id); }));

  // Invalid id.
  ASSERT_DEATH(([this, task_id]() { gdc_device_->GdcRemoveTask(task_id + 1); }));

  ASSERT_OK(gdc_device_->StopThread());
}

TEST_F(TaskTest, ProcessInvalidFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  __UNUSED auto task_id = SetupForFrameProcessing(fake_regs);

  // Invalid task id.
  zx_status_t status = gdc_device_->GdcProcessFrame(0xFF, 0);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);

  ASSERT_OK(gdc_device_->StopThread());
}

TEST_F(TaskTest, InvalidBufferProcessFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  __UNUSED auto task_id = SetupForFrameProcessing(fake_regs);

  // Invalid buffer id.
  zx_status_t status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);

  ASSERT_OK(gdc_device_->StopThread());
}

TEST_F(TaskTest, ProcessFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  auto task_id = SetupForFrameProcessing(fake_regs);

  SetExpectations(fake_regs);

  // Valid buffer & task id.
  zx_status_t status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  WaitAndReset();
  EXPECT_EQ(1, GetCallbackSize());

  ASSERT_OK(gdc_device_->StopThread());

  fake_regs.VerifyAll();
}

TEST_F(TaskTest, SetOutputResTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  auto task_id = SetupForFrameProcessing(fake_regs);

  SetExpectations(fake_regs);

  zx_status_t status = gdc_device_->GdcSetOutputResolution(task_id, 2);
  EXPECT_OK(status);
  output_image_format_index_ = 2;
  // Valid buffer & task id.
  status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  // The callback checks that the index of the output format returned matches
  // what we set it to above.
  WaitAndReset();
  EXPECT_EQ(1, GetCallbackSize());

  ASSERT_OK(gdc_device_->StopThread());

  fake_regs.VerifyAll();
}

TEST_F(TaskTest, ReleaseValidFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  auto task_id = SetupForFrameProcessing(fake_regs);

  // Valid buffer & task id.
  zx_status_t status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  WaitAndReset();
  EXPECT_EQ(1, GetCallbackSize());

  // Release the output buffer index provided as callback.
  ASSERT_NO_DEATH(
      ([this, task_id]() { gdc_device_->GdcReleaseFrame(task_id, GetCallbackBack()); }));

  ASSERT_OK(gdc_device_->StopThread());
}

TEST_F(TaskTest, ReleaseInValidFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  auto task_id = SetupForFrameProcessing(fake_regs);

  // Valid buffer & task id.
  zx_status_t status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  WaitAndReset();
  EXPECT_EQ(1, GetCallbackSize());

  // Release the output buffer index provided as callback.
  ASSERT_DEATH(
      ([this, task_id]() { gdc_device_->GdcReleaseFrame(task_id + 1, GetCallbackBack()); }));
  ASSERT_OK(gdc_device_->StopThread());
}

TEST_F(TaskTest, MultipleProcessFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  auto task_id = SetupForFrameProcessing(fake_regs);

  // Process few frames, putting them in a queue
  zx_status_t status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);
  status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 2);
  EXPECT_OK(status);
  status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 3);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called once.
  WaitAndReset();
  EXPECT_EQ(1, GetCallbackSize());

  // Trigger the interrupt manually.
  packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called one more time.
  WaitAndReset();
  EXPECT_EQ(2, GetCallbackSize());

  // This time adding another frame to process while its
  // waiting for an interrupt.
  status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 4);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called one more time.
  WaitAndReset();
  EXPECT_EQ(3, GetCallbackSize());

  // Trigger the interrupt manually.
  packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called one more time.
  WaitAndReset();
  EXPECT_EQ(4, GetCallbackSize());

  ASSERT_OK(gdc_device_->StopThread());
}

TEST(TaskTest, NonContigVmoTest) {
  zx_handle_t bti_handle = ZX_HANDLE_INVALID;
  hw_accel_callback_t callback;
  zx_handle_t config_vmo;
  buffer_collection_info_2_t input_buffer_collection;
  buffer_collection_info_2_t output_buffer_collection;
  ASSERT_OK(fake_bti_create(&bti_handle));
  image_format_2_t format;
  EXPECT_OK(camera::GetImageFormat2(fuchsia_sysmem_PixelFormatType_NV12, format, kWidth, kHeight));
  zx_status_t status = camera::CreateContiguousBufferCollectionInfo2(
      input_buffer_collection, format, bti_handle, kWidth, kHeight, 0);
  ASSERT_OK(status);

  status = camera::CreateContiguousBufferCollectionInfo2(output_buffer_collection, format,
                                                         bti_handle, kWidth, kHeight, 0);
  ASSERT_OK(status);

  status = zx_vmo_create(kConfigSize, 0, &config_vmo);
  ASSERT_OK(status);
  fbl::AllocChecker ac;
  auto task = std::unique_ptr<GdcTask>(new (&ac) GdcTask());
  EXPECT_TRUE(ac.check());
  image_format_2_t image_format_table[kImageFormatTableSize];
  EXPECT_OK(camera::GetImageFormat2(fuchsia_sysmem_PixelFormatType_NV12, image_format_table[0],
                                    kWidth, kHeight));
  status =
      task->Init(&input_buffer_collection, &output_buffer_collection, &format, image_format_table,
                 kImageFormatTableSize, 0, zx::vmo(config_vmo), &callback, zx::bti(bti_handle));
  // Expecting Task setup to be returning an error when config vmo is not
  // contig.
  EXPECT_NE(ZX_OK, status);
}

TEST(TaskTest, InvalidBufferCollectionTest) {
  zx_handle_t bti_handle = ZX_HANDLE_INVALID;
  hw_accel_callback_t callback;
  zx_handle_t config_vmo;
  ASSERT_OK(fake_bti_create(&bti_handle));

  zx_status_t status = zx_vmo_create_contiguous(bti_handle, kConfigSize, 0, &config_vmo);
  ASSERT_OK(status);
  fbl::AllocChecker ac;
  auto task = std::unique_ptr<GdcTask>(new (&ac) GdcTask());
  EXPECT_TRUE(ac.check());
  image_format_2_t image_format_table[kImageFormatTableSize];
  EXPECT_OK(camera::GetImageFormat2(fuchsia_sysmem_PixelFormatType_NV12, image_format_table[0],
                                    kWidth, kHeight));
  status = task->Init(nullptr, nullptr, nullptr, image_format_table, kImageFormatTableSize, 0,
                      zx::vmo(config_vmo), &callback, zx::bti(bti_handle));
  EXPECT_NE(ZX_OK, status);
}

}  // namespace
}  // namespace gdc
