// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include <future>

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "mpeg12_decoder.h"
#include "tests/test_support.h"
#include "vdec1.h"

class TestMpeg2 {
 public:
  static void Decode() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(std::make_unique<Mpeg12Decoder>(video.get()), false);
    }

    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/true, PAGE_SIZE,
                                                   /*is_secure=*/false));

    video->InitializeInterrupts();
    std::promise<void> wait_valid;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);

      uint32_t frame_count = 0;
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count, &wait_valid](std::shared_ptr<VideoFrame> frame) {
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame, "/tmp/bearmpeg2.yuv");
#endif
            ++frame_count;
            if (frame_count == 28)
              wait_valid.set_value();
            ReturnFrame(video.get(), frame);
          });
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }

    EXPECT_EQ(ZX_OK, video->InitializeEsParser());
    auto bear_mpeg2 = TestSupport::LoadFirmwareFile("video_test_data/bear.mpeg2");
    ASSERT_NE(nullptr, bear_mpeg2);
    EXPECT_EQ(ZX_OK, video->ParseVideo(bear_mpeg2->ptr, bear_mpeg2->size));
    EXPECT_EQ(ZX_OK, video->WaitForParsingCompleted(ZX_SEC(10)));

    EXPECT_EQ(std::future_status::ready, wait_valid.get_future().wait_for(std::chrono::seconds(1)));

    video.reset();
  }

  static void DecodeNoParser() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(std::make_unique<Mpeg12Decoder>(video.get()), false);
    }

    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/false, PAGE_SIZE * 1024,
                                                   /*is_secure=*/false));

    video->InitializeInterrupts();
    std::promise<void> wait_valid;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);

      uint32_t frame_count = 0;
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count, &wait_valid](std::shared_ptr<VideoFrame> frame) {
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame, "/tmp/bearmpeg2noparser.yuv");
#endif
            ++frame_count;
            if (frame_count == 28)
              wait_valid.set_value();
            ReturnFrame(video.get(), frame);
          });
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }
    video->core_->InitializeDirectInput();
    auto bear_mpeg2 = TestSupport::LoadFirmwareFile("video_test_data/bear.mpeg2");
    ASSERT_NE(nullptr, bear_mpeg2);
    EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(bear_mpeg2->ptr, bear_mpeg2->size));

    EXPECT_EQ(std::future_status::ready, wait_valid.get_future().wait_for(std::chrono::seconds(1)));

    video.reset();
  }

 private:
  // This is called from the interrupt handler, which already holds the lock.
  static void ReturnFrame(AmlogicVideo* video,
                          std::shared_ptr<VideoFrame> frame) __TA_NO_THREAD_SAFETY_ANALYSIS {
    video->video_decoder_->ReturnFrame(frame);
  }
};

TEST(MPEG2, Decode) { TestMpeg2::Decode(); }

TEST(MPEG2, DecodeNoParser) { TestMpeg2::DecodeNoParser(); }
