// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/tracer.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include <utility>

#include <trace-engine/fields.h>
#include <trace-reader/reader.h>

#include "src/lib/fxl/logging.h"

namespace tracing {

Tracer::Tracer(controller::Controller* controller)
    : controller_(controller), dispatcher_(nullptr), wait_(this) {
  FXL_DCHECK(controller_);
  wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED);
}

Tracer::~Tracer() { CloseSocket(); }

void Tracer::Start(TraceConfig config, bool binary, BytesConsumer bytes_consumer,
                   RecordConsumer record_consumer, ErrorHandler error_handler,
                   StartCallback start_callback, FailCallback fail_callback,
                   DoneCallback done_callback) {
  FXL_DCHECK(state_ == State::kStopped);

  state_ = State::kStarted;
  start_callback_ = std::move(start_callback);
  fail_callback_ = std::move(fail_callback);
  done_callback_ = std::move(done_callback);

  zx::socket outgoing_socket;
  zx_status_t status = zx::socket::create(0u, &socket_, &outgoing_socket);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket: status=" << status;
    Fail();
    return;
  }

  controller_->StartTracing(std::move(config), std::move(outgoing_socket),
                            [this]() { start_callback_(); });

  binary_ = binary;
  bytes_consumer_ = std::move(bytes_consumer);
  reader_.reset(new trace::TraceReader(std::move(record_consumer), std::move(error_handler)));

  dispatcher_ = async_get_default_dispatcher();
  wait_.set_object(socket_.get());
  status = wait_.Begin(dispatcher_);
  FXL_CHECK(status == ZX_OK) << "Failed to add handler: status=" << status;
}

void Tracer::Stop() {
  // Note: The controller will close the socket when finished.
  if (state_ == State::kStarted) {
    state_ = State::kStopping;
    controller_->StopTracing();
  }
}

void Tracer::OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                           zx_status_t status, const zx_packet_signal_t* signal) {
  FXL_DCHECK(state_ == State::kStarted || state_ == State::kStopping);

  if (status != ZX_OK) {
    OnHandleError(status);
    return;
  }

  if (signal->observed & ZX_SOCKET_READABLE) {
    DrainSocket(dispatcher);
  } else if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
    Done();
  } else {
    FXL_CHECK(false);
  }
}

void Tracer::DrainSocket(async_dispatcher_t* dispatcher) {
  for (;;) {
    size_t actual;
    zx_status_t status =
        socket_.read(0u, buffer_.data() + buffer_end_, buffer_.size() - buffer_end_, &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      status = wait_.Begin(dispatcher);
      if (status != ZX_OK) {
        OnHandleError(status);
      }
      return;
    }

    if (status || actual == 0) {
      if (status == ZX_ERR_PEER_CLOSED) {
        Done();
      } else {
        FXL_LOG(ERROR) << "Failed to read data from socket: status=" << status;
        Fail();
      }
      return;
    }

    buffer_end_ += actual;
    size_t bytes_available = buffer_end_;
    FXL_DCHECK(bytes_available > 0);

    size_t bytes_consumed;
    if (binary_) {
      bytes_consumer_(buffer_.data(), bytes_available);
      bytes_consumed = bytes_available;
    } else {
      trace::Chunk chunk(reinterpret_cast<const uint64_t*>(buffer_.data()),
                         trace::BytesToWords(bytes_available));
      if (!reader_->ReadRecords(chunk)) {
        FXL_LOG(ERROR) << "Trace stream is corrupted";
        Fail();
        return;
      }
      bytes_consumed = bytes_available - trace::WordsToBytes(chunk.remaining_words());
    }

    bytes_available -= bytes_consumed;
    memmove(buffer_.data(), buffer_.data() + bytes_consumed, bytes_available);
    buffer_end_ = bytes_available;
  }
}

void Tracer::OnHandleError(zx_status_t status) {
  FXL_LOG(ERROR) << "Failed to wait on socket: status=" << status;
  Fail();
}

void Tracer::CloseSocket() {
  if (socket_) {
    wait_.Cancel();
    wait_.set_object(ZX_HANDLE_INVALID);
    dispatcher_ = nullptr;
    socket_.reset();
  }
}

void Tracer::Fail() {
  fail_callback_();
}

void Tracer::Done() {
  FXL_DCHECK(state_ == State::kStarted || state_ == State::kStopping);

  state_ = State::kStopped;
  reader_.reset();

  CloseSocket();

  // TODO(dje): Watch for errors finishing writing of the trace file.

  if (done_callback_) {
    async::PostTask(async_get_default_dispatcher(), std::move(done_callback_));
  }
}

}  // namespace tracing
