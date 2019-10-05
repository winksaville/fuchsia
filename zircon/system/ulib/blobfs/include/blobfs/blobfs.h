// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the global Blobfs structure used for constructing a Blobfs filesystem in
// memory.

#ifndef BLOBFS_BLOBFS_H_
#define BLOBFS_BLOBFS_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fuchsia/blobfs/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>

#include <atomic>
#include <cstring>
#include <utility>

#include <bitmap/raw-bitmap.h>
#include <bitmap/rle-bitmap.h>
#include <blobfs/allocator.h>
#include <blobfs/blob-cache.h>
#include <blobfs/blob.h>
#include <blobfs/common.h>
#include <blobfs/directory.h>
#include <blobfs/extent-reserver.h>
#include <blobfs/format.h>
#include <blobfs/iterator/allocated-extent-iterator.h>
#include <blobfs/iterator/extent-iterator.h>
#include <blobfs/metrics.h>
#include <blobfs/node-reserver.h>
#include <blobfs/transaction-manager.h>
#include <block-client/cpp/block-device.h>
#include <block-client/cpp/block-group-registry.h>
#include <block-client/cpp/client.h>
#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fs/journal/journal.h>
#include <fs/managed-vfs.h>
#include <fs/metrics/cobalt-metrics.h>
#include <fs/operation/unbuffered-operations-builder.h>
#include <fs/trace.h>
#include <fs/transaction/block-transaction.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <trace/event.h>

namespace blobfs {

using block_client::BlockDevice;
using digest::Digest;
using fs::OperationType;
using fs::UnbufferedOperationsBuilder;

enum class Writability {
  // Do not write to persistent storage under any circumstances whatsoever.
  ReadOnlyDisk,
  // Do not allow users of the filesystem to mutate filesystem state. This
  // state allows the journal to replay while initializing writeback.
  ReadOnlyFilesystem,
  // Permit all operations.
  Writable,
};

// Toggles that may be set on blobfs during initialization.
struct MountOptions {
  Writability writability = Writability::Writable;
  bool metrics = false;
  bool journal = false;
  CachePolicy cache_policy = CachePolicy::EvictImmediately;
};

class Blobfs : public fs::ManagedVfs, public fbl::RefCounted<Blobfs>, public TransactionManager {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Blobfs);

  ////////////////
  // fs::ManagedVfs interface.

  void Shutdown(fs::Vfs::ShutdownCallback closure) final;

  ////////////////
  // TransactionManager's fs::TransactionHandler interface.
  //
  // Allows transmitting read and write transactions directly to the underlying storage.

  uint32_t FsBlockSize() const final { return kBlobfsBlockSize; }

  uint32_t DeviceBlockSize() const final { return block_info_.block_size; }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final {
    return block_num * kBlobfsBlockSize / block_info_.block_size;
  }

  zx_status_t RunOperation(const fs::Operation& operation, fs::BlockBuffer* buffer) final;

  groupid_t BlockGroupID() final;

  block_client::BlockDevice* GetDevice() final { return block_device_.get(); }

  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
    TRACE_DURATION("blobfs", "Blobfs::Transaction", "count", count);
    return block_device_->FifoTransaction(requests, count);
  }

  ////////////////
  // TransactionManager's SpaceManager interface.
  //
  // Allows viewing and controlling the size of the underlying volume.

  const Superblock& Info() const final { return info_; }
  zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final;
  zx_status_t DetachVmo(vmoid_t vmoid) final;
  zx_status_t AddInodes(fzl::ResizeableVmoMapper* node_map) final;
  zx_status_t AddBlocks(size_t nblocks, RawBitmap* block_map) final;

  ////////////////
  // TransactionManager interface.
  //
  // Allows attaching VMOs, controlling the underlying volume, and sending transactions to the
  // underlying storage (optionally through the journal).

  BlobfsMetrics& Metrics() final { return metrics_; }
  size_t WritebackCapacity() const final;
  fs::Journal* journal() final;

  ////////////////
  // Other methods.

  uint64_t DataStart() const { return DataStartBlock(info_); }

  bool CheckBlocksAllocated(uint64_t start_block, uint64_t end_block,
                            uint64_t* first_unset = nullptr) const {
    return allocator_->CheckBlocksAllocated(start_block, end_block, first_unset);
  }
  AllocatedExtentIterator GetExtents(uint32_t node_index) {
    return AllocatedExtentIterator(allocator_.get(), node_index);
  }

  Allocator* GetAllocator() { return allocator_.get(); }

  Inode* GetNode(uint32_t node_index) { return allocator_->GetNode(node_index); }
  zx_status_t ReserveBlocks(size_t num_blocks, fbl::Vector<ReservedExtent>* out_extents) {
    return allocator_->ReserveBlocks(num_blocks, out_extents);
  }
  zx_status_t ReserveNodes(size_t num_nodes, fbl::Vector<ReservedNode>* out_node) {
    return allocator_->ReserveNodes(num_nodes, out_node);
  }

  static zx_status_t Create(std::unique_ptr<BlockDevice> device, MountOptions* options,
                            std::unique_ptr<Blobfs>* out);

  void CollectMetrics() { collecting_metrics_ = true; }
  bool CollectingMetrics() const { return collecting_metrics_; }
  void DisableMetrics() { collecting_metrics_ = false; }
  void DumpMetrics() const {
    if (collecting_metrics_) {
      metrics_.Dump();
    }
  }

  void SetUnmountCallback(fbl::Closure closure) { on_unmount_ = std::move(closure); }

  virtual ~Blobfs();

  // Invokes "open" on the root directory.
  // Acts as a special-case to bootstrap filesystem mounting.
  zx_status_t OpenRootNode(fbl::RefPtr<Directory>* out);

  BlobCache& Cache() { return blob_cache_; }

  zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual);

  BlockDevice* Device() const { return block_device_.get(); }

  // Returns an unique identifier for this instance.
  uint64_t GetFsId() const { return fs_id_; }

  using SyncCallback = fs::Vnode::SyncCallback;
  void Sync(SyncCallback closure);

  // Frees an inode, from both the reserved map and the inode table. If the
  // inode was allocated in the inode table, write the deleted inode out to
  // disk.
  void FreeInode(uint32_t node_index, fs::UnbufferedOperationsBuilder* operations);

  // Does a single pass of all blobs, creating uninitialized Vnode
  // objects for them all.
  //
  // By executing this function at mount, we can quickly assert
  // either the presence or absence of a blob on the system without
  // further scanning.
  zx_status_t InitializeVnodes();

  // Writes node data to the inode table and updates disk.
  void PersistNode(uint32_t node_index, fs::UnbufferedOperationsBuilder* operations);

  // Adds reserved blocks to allocated bitmap and writes the bitmap out to disk.
  void PersistBlocks(const ReservedExtent& extent, fs::UnbufferedOperationsBuilder* ops);

  // Record the location and size of all non-free block regions.
  fbl::Vector<BlockRegion> GetAllocatedRegions() const { return allocator_->GetAllocatedRegions(); }

  // Updates the flags field in superblock.
  void UpdateFlags(fs::UnbufferedOperationsBuilder* operations, uint32_t flags, bool set);

 private:
  friend class BlobfsChecker;

  Blobfs(std::unique_ptr<BlockDevice> device, const Superblock* info);

  // Reloads metadata from disk. Useful when metadata on disk
  // may have changed due to journal playback.
  zx_status_t Reload();

  // Frees blocks from the allocated map (if allocated) and updates disk if necessary.
  void FreeExtent(const Extent& extent, fs::UnbufferedOperationsBuilder* operations);

  // Free a single node. Doesn't attempt to parse the type / traverse nodes;
  // this function just deletes a single node.
  void FreeNode(uint32_t node_index, fs::UnbufferedOperationsBuilder* operations);

  // Given a contiguous number of blocks after a starting block,
  // write out the bitmap to disk for the corresponding blocks.
  // Should only be called by PersistBlocks and FreeExtent.
  void WriteBitmap(uint64_t nblocks, uint64_t start_block,
                   fs::UnbufferedOperationsBuilder* operations);

  // Given a node within the node map at an index, write it to disk.
  // Should only be called by AllocateNode and FreeNode.
  void WriteNode(uint32_t map_index, fs::UnbufferedOperationsBuilder* operations);

  // Enqueues an update for allocated inode/block counts.
  void WriteInfo(fs::UnbufferedOperationsBuilder* operations);

  // When will flush the metrics in the calling thread and will schedule itself
  // to flush again in the future.
  void ScheduleMetricFlush();

  // Creates an unique identifier for this instance. This is to be called only during
  // "construction".
  zx_status_t CreateFsId();

  // Verifies that the contents of a blob are valid.
  zx_status_t VerifyBlob(uint32_t node_index);

  // Check if filesystem is readonly.
  bool IsReadonly() FS_TA_EXCLUDES(vfs_lock_);

  fbl::unique_ptr<fs::Journal> journal_;
  Superblock info_;

  BlobCache blob_cache_;

  std::unique_ptr<BlockDevice> block_device_;
  fuchsia_hardware_block_BlockInfo block_info_ = {};
  block_client::BlockGroupRegistry group_registry_;

  fbl::unique_ptr<Allocator> allocator_;

  fzl::ResizeableVmoMapper info_mapping_;
  vmoid_t info_vmoid_ = {};

  uint64_t fs_id_ = 0;

  bool collecting_metrics_ = false;
  BlobfsMetrics metrics_ = {};

  fbl::Closure on_unmount_ = {};

  // Loop for flushing the collector periodically.
  async::Loop flush_loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
};

// Begins serving requests to the filesystem using |dispatch|, by parsing
// the on-disk format using |device|, using |root| as a filesystem server.
//
// This function does not block, and instead serves requests on the dispatcher
// asynchronously.
//
// Invokes |on_unmount| when the filesystem has been instructed to terminate.
// After |on_unmount| completes, |dispatcher| will no longer be accessed.
zx_status_t Mount(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device,
                  MountOptions* options, zx::channel root, fbl::Closure on_unmount);

// Formats the underlying device with an empty Blobfs partition.
zx_status_t FormatFilesystem(BlockDevice* device);

}  // namespace blobfs

#endif  // BLOBFS_BLOBFS_H_
