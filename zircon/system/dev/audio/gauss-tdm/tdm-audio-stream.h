// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_GAUSS_TDM_TDM_AUDIO_STREAM_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_GAUSS_TDM_TDM_AUDIO_STREAM_H_

#include <fuchsia/hardware/audio/c/fidl.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmo.h>
#include <zircon/listnode.h>

#include <memory>
#include <optional>
#include <utility>

#include <audio-proto/audio-proto.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <dispatcher-pool/dispatcher-timer.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <soc/aml-a113/aml-tdm.h>

namespace audio {
namespace gauss {

class TdmOutputStream;
using TdmAudioStreamBase = ddk::Device<TdmOutputStream, ddk::Messageable, ddk::UnbindableNew>;

class TdmOutputStream : public TdmAudioStreamBase,
                        public ddk::EmptyProtocol<ZX_PROTOCOL_AUDIO_OUTPUT>,
                        public fbl::RefCounted<TdmOutputStream> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  // void PrintDebugPrefix() const;

  // DDK device implementation
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_audio_Device_dispatch(this, txn, msg, &AUDIO_FIDL_THUNKS);
  }

 private:
  static int IrqThread(void* arg);

  friend class fbl::RefPtr<TdmOutputStream>;

  // TODO(hollande) - the fifo bytes are adjustable on the audio fifos and should be scaled
  //                  with the desired sample rate.  Since this first pass has a fixed sample
  //                  sample rate we will set as constant for now.
  //                  We are using fifo C at this stage, which is max of 128 (64-bit wide)
  //                  Using 64 levels for now.
  static constexpr uint8_t kFifoDepth = 0x40;

  TdmOutputStream(zx_device_t* parent, fbl::RefPtr<dispatcher::ExecutionDomain>&& default_domain)
      : TdmAudioStreamBase(parent),
        default_domain_(std::move(default_domain)),
        create_time_(zx::clock::get_monotonic().get()) {}

  virtual ~TdmOutputStream();

  // Device FIDL implementation
  zx_status_t GetChannel(fidl_txn_t* txn);

  zx_status_t Bind(const char* devname);

  void ReleaseRingBufferLocked() __TA_REQUIRES(lock_);

  zx_status_t AddFormats(fbl::Vector<audio_stream_format_range_t>* supported_formats);

  // Thunks for dispatching stream channel events.
  zx_status_t ProcessStreamChannel(dispatcher::Channel* channel, bool privileged);
  void DeactivateStreamChannel(const dispatcher::Channel* channel);

  zx_status_t OnGetStreamFormatsLocked(dispatcher::Channel* channel,
                                       const audio_proto::StreamGetFmtsReq& req) const
      __TA_REQUIRES(lock_);
  zx_status_t OnSetStreamFormatLocked(dispatcher::Channel* channel,
                                      const audio_proto::StreamSetFmtReq& req, bool privileged)
      __TA_REQUIRES(lock_);
  zx_status_t OnGetGainLocked(dispatcher::Channel* channel,
                              const audio_proto::GetGainReq& req) const __TA_REQUIRES(lock_);
  zx_status_t OnSetGainLocked(dispatcher::Channel* channel, const audio_proto::SetGainReq& req)
      __TA_REQUIRES(lock_);
  zx_status_t OnPlugDetectLocked(dispatcher::Channel* channel,
                                 const audio_proto::PlugDetectReq& req) __TA_REQUIRES(lock_);
  zx_status_t OnGetUniqueIdLocked(dispatcher::Channel* channel,
                                  const audio_proto::GetUniqueIdReq& req) const
      __TA_REQUIRES(lock_);
  zx_status_t OnGetStringLocked(dispatcher::Channel* channel,
                                const audio_proto::GetStringReq& req) const __TA_REQUIRES(lock_);

  // Thunks for dispatching ring buffer channel events.
  zx_status_t ProcessRingBufferChannel(dispatcher::Channel* channel);

  void DeactivateRingBufferChannel(const dispatcher::Channel* channel);

  zx_status_t SetModuleClocks();

  zx_status_t ProcessRingNotification();

  // Stream command handlers
  // Ring buffer command handlers
  zx_status_t OnGetFifoDepthLocked(dispatcher::Channel* channel,
                                   const audio_proto::RingBufGetFifoDepthReq& req) const
      __TA_REQUIRES(lock_);
  zx_status_t OnGetBufferLocked(dispatcher::Channel* channel,
                                const audio_proto::RingBufGetBufferReq& req) __TA_REQUIRES(lock_);
  zx_status_t OnStartLocked(dispatcher::Channel* channel, const audio_proto::RingBufStartReq& req)
      __TA_REQUIRES(lock_);
  zx_status_t OnStopLocked(dispatcher::Channel* channel, const audio_proto::RingBufStopReq& req)
      __TA_REQUIRES(lock_);

  static fuchsia_hardware_audio_Device_ops_t AUDIO_FIDL_THUNKS;

  fbl::Mutex lock_;
  fbl::Mutex req_lock_ __TA_ACQUIRED_AFTER(lock_);

  // Dispatcher framework state
  fbl::RefPtr<dispatcher::Channel> stream_channel_ __TA_GUARDED(lock_);
  fbl::RefPtr<dispatcher::Channel> rb_channel_ __TA_GUARDED(lock_);
  fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

  // control registers for the tdm block
  std::optional<ddk::MmioBuffer> mmio_;

  fbl::RefPtr<dispatcher::Timer> notify_timer_;

  // TODO(johngro) : support parsing and selecting from all of the format
  // descriptors present for a stream, not just a single format (with multiple
  // sample rates).
  fbl::Vector<audio_stream_format_range_t> supported_formats_;

  pdev_protocol_t pdev_;
  i2c_protocol_t i2c_;

  std::unique_ptr<Tas57xx> left_sub_;
  std::unique_ptr<Tas57xx> right_sub_;
  std::unique_ptr<Tas57xx> tweeters_;

  float current_gain_ = -20.0;

  uint32_t frame_size_;
  uint32_t fifo_bytes_;

  const zx_time_t create_time_;
  uint32_t us_per_notification_ = 0;
  volatile bool running_;

  zx::bti bti_;
  io_buffer_t ring_buffer_;
  void* ring_buffer_virt_ = nullptr;
  uint32_t ring_buffer_phys_ = 0;
  uint32_t ring_buffer_size_ = 0;
};

}  // namespace gauss
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_GAUSS_TDM_TDM_AUDIO_STREAM_H_
