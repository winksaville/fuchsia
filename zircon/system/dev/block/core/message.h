// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BLOCK_CORE_MESSAGE_H_
#define ZIRCON_SYSTEM_DEV_BLOCK_CORE_MESSAGE_H_

#include <zircon/device/block.h>

#include <memory>

#include <ddk/protocol/block.h>
#include <fbl/intrusive_double_list.h>

class IoBuffer;
class Server;

// A single unit of work transmitted to the underlying block layer.
// Message contains a block_op_t, which is dynamically sized. Therefore, it implements its
// own allocator that takes block_op_size.
class Message final : public fbl::DoublyLinkedListable<Message*> {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Message);
  Message() = default;

  // Overloaded new operator allows variable-sized allocation to match block op size.
  void* operator new(size_t size) = delete;
  void* operator new(size_t size, size_t block_op_size) {
    return calloc(1, size + block_op_size - sizeof(block_op_t));
  }
  void operator delete(void* msg) { free(msg); }

  // Allocate a new, uninitialized Message whose block_op begins in a memory region that
  // is block_op_size bytes long.
  static zx_status_t Create(size_t block_op_size, std::unique_ptr<Message>* out);

  // Initialize the contents of this from the supplied args. block_op op_ is cleared.
  void Init(fbl::RefPtr<IoBuffer> iobuf, Server* server, block_fifo_request_t* req);

  // End the transaction specified by reqid and group, and release iobuf.
  // Message can be reused with another call to Init().
  void Complete(zx_status_t status);

  block_op_t* Op() { return &op_; }

 private:
  fbl::RefPtr<IoBuffer> iobuf_;
  Server* server_;
  reqid_t reqid_;
  groupid_t group_;
  size_t op_size_;
  // Must be at the end of structure.
  union {
    block_op_t op_;
    uint8_t _op_raw_[1];  // Extra space for underlying block_op.
  };
};

using MessageQueue = fbl::DoublyLinkedList<Message*>;

#endif  // ZIRCON_SYSTEM_DEV_BLOCK_CORE_MESSAGE_H_
