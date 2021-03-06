// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <utility>

#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <fs-test-utils/fixture.h>
#include <minfs/format.h>
#include <minfs/fsck.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

namespace {

void RepairCorruptedSuperblock(fbl::unique_fd fd, const char* mount_path,
                               const char* ramdisk_path, uint32_t backup_location) {
  minfs::Superblock info;
  // Try reading the superblock.
  ASSERT_GE(pread(fd.get(), &info, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to read superblock.");
  ASSERT_EQ(minfs::kMinfsMagic0, info.magic0);

  // Try reading the backup superblock.
  minfs::Superblock backup_info;
  ASSERT_GE(pread(fd.get(), &backup_info, minfs::kMinfsBlockSize,
            backup_location * minfs::kMinfsBlockSize), 0, "Unable to read backup superblock.");
  ASSERT_EQ(minfs::kMinfsMagic0, backup_info.magic0);

  // Corrupt superblock, by erasing it completely from disk.
  memset(&info, 0, sizeof(info));
  ASSERT_GE(pwrite(fd.get(), &info, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to corrupt superblock.");

  // Running mount to repair the filesystem.
  ASSERT_EQ(ZX_OK, mount(fd.get(), mount_path, DISK_FORMAT_MINFS, &default_mount_options,
      launch_stdio_async));
  ASSERT_EQ(ZX_OK, umount(mount_path));

  // Mount consumes the fd, hence ramdisk needs to be opened again.
  int fd_mount = open(ramdisk_path, O_RDWR);
  ASSERT_GE(fd_mount, 0, "Could not open ramdisk device.");

  // Try reading the superblock.
  ASSERT_GE(pread(fd_mount, &info, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to read superblock.");

  // Confirm that the corrupted superblock is repaired by backup superblock.
  ASSERT_EQ(minfs::kMinfsMagic0, info.magic0);
}

enum class Comparison {
  kSame,
  kDifferent,
};

void CompareSuperblockAndBackupAllocCounts(const fbl::unique_fd& fd, uint32_t backup_location,
                                           Comparison value) {
  minfs::Superblock info;

  // Try reading the superblock.
  ASSERT_GE(pread(fd.get(), &info, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to read superblock.");
  ASSERT_EQ(minfs::kMinfsMagic0, info.magic0);

  // Try reading the backup superblock.
  minfs::Superblock backup_info;
  ASSERT_GE(pread(fd.get(), &backup_info, minfs::kMinfsBlockSize,
            backup_location * minfs::kMinfsBlockSize), 0, "Unable to read backup superblock.");
  ASSERT_EQ(minfs::kMinfsMagic0, backup_info.magic0);

  if (value == Comparison::kSame) {
    EXPECT_EQ(info.alloc_block_count, backup_info.alloc_block_count);
    EXPECT_EQ(info.alloc_inode_count, backup_info.alloc_inode_count);
  } else {
    EXPECT_NE(info.alloc_block_count, backup_info.alloc_block_count);
    EXPECT_NE(info.alloc_inode_count, backup_info.alloc_inode_count);
  }
}

void TestAllocCountWriteFrequency(fbl::unique_fd fd, const char* mount_path,
                                  const char* ramdisk_path, uint32_t backup_location) {
  // Check that superblock and backup alloc counts are same, before mounting the filesystem.
  ASSERT_NO_FAILURES(CompareSuperblockAndBackupAllocCounts(fd, backup_location,
                     Comparison::kSame));

  // Mount the filesystem.
  ASSERT_EQ(ZX_OK, mount(fd.release(), mount_path, DISK_FORMAT_MINFS, &default_mount_options,
            launch_stdio_async));

  // Mount consumes the device fd, hence ramdisk needs to be opened again.
  fbl::unique_fd fd_device(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd_device);

  // Check that superblock and backup alloc counts are same.
  ASSERT_NO_FAILURES(CompareSuperblockAndBackupAllocCounts(fd_device, backup_location,
                     Comparison::kSame));

  // Create a directory to force allocating inodes as well as data blocks.
  char path[PATH_MAX] = {0};
  const char test_dir[] = "test_dir";
  snprintf(path, sizeof(path), "%s/%s", mount_path, test_dir);
  ASSERT_EQ(0, mkdir(path, 0777));

  // Create a file in the test directory to force allocating inodes as well as data blocks
  // and write to it.
  char file_path[PATH_MAX] = {0};
  const char file_name[] = "test_dir/file1";
  snprintf(file_path, sizeof(file_path), "%s/%s", mount_path, file_name);
  fbl::unique_fd fd_file(open(file_path, O_RDWR | O_CREAT));
  ASSERT_TRUE(fd_file);

  // Write something to the file.
  char data[minfs::kMinfsBlockSize];
  memset(data, 0xb0b, sizeof(data));
  ASSERT_EQ(write(fd_file.get(), data, sizeof(data)), sizeof(data));
  ASSERT_EQ(fsync(fd_file.get()), 0);
  ASSERT_EQ(close(fd_file.release()), 0);

  // Open the root directory to fsync the filesystem.
  fbl::unique_fd fd_mount(open(mount_path, O_RDONLY));
  ASSERT_TRUE(fd_mount);
  ASSERT_EQ(fsync(fd_mount.get()), 0);
  ASSERT_EQ(close(fd_mount.release()), 0);

  // Check that superblock and backup alloc counts are now different.
  ASSERT_NO_FAILURES(CompareSuperblockAndBackupAllocCounts(fd_device, backup_location,
                     Comparison::kDifferent));

  // Unmount the filesystem.
  ASSERT_EQ(ZX_OK, umount(mount_path));

  // Check that superblock and backup alloc counts are same.
  ASSERT_NO_FAILURES(CompareSuperblockAndBackupAllocCounts(fd_device, backup_location,
                     Comparison::kSame));
}

// Tests backup superblock functionality on minfs backed with non-FVM block device.
TEST(NonFvmBackupSuperblockTest, NonFvmMountCorruptedSuperblock)  {
  const char* mount_path = "/tmp/mount_backup";
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
  const char* ramdisk_path = ramdisk_get_path(ramdisk);
  ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options), ZX_OK);
  ASSERT_EQ(mkdir(mount_path, 0666), 0);
  fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd);

  ASSERT_NO_FAILURES(RepairCorruptedSuperblock(std::move(fd), mount_path, ramdisk_path,
                     minfs::kNonFvmSuperblockBackup));

  ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
  ASSERT_EQ(unlink(mount_path), 0);
}

// Tests alloc_*_counts write frequency difference for backup superblock on minfs backed with
// non-FVM block device.
TEST(NonFvmBackupSuperblockTest, NonFvmAllocCountWriteFrequency)  {
  const char* mount_path = "/tmp/mount_backup";
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
  const char* ramdisk_path = ramdisk_get_path(ramdisk);
  ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options), ZX_OK);
  ASSERT_EQ(mkdir(mount_path, 0666), 0);
  fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd);

  ASSERT_NO_FAILURES(TestAllocCountWriteFrequency(std::move(fd), mount_path, ramdisk_path,
                     minfs::kNonFvmSuperblockBackup));

  ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
  ASSERT_EQ(unlink(mount_path), 0);
}

class FvmBackupSuperblockTest : public zxtest::Test {
 public:
  void SetUp() final {
    fs_test_utils::FixtureOptions options =
        fs_test_utils::FixtureOptions::Default(DISK_FORMAT_MINFS);
    options.isolated_devmgr = true;
    options.use_fvm = true;
    options.fs_mount = false;

    fixture_ = std::make_unique<fs_test_utils::Fixture>(options);
    ASSERT_EQ(ZX_OK, fixture_->SetUpTestCase());
    ASSERT_EQ(ZX_OK, fixture_->SetUp());
    ASSERT_EQ(mkdir(mount_path_, 0666), 0);
  }

  void TearDown() final {
    ASSERT_EQ(unlink(mount_path_), 0);
    ASSERT_EQ(ZX_OK, fixture_->TearDown());
    ASSERT_EQ(ZX_OK, fixture_->TearDownTestCase());
  }

  const char* partition_path() const { return fixture_->partition_path().c_str(); }
  const char* block_device_path() const { return fixture_->block_device_path().c_str(); }
  disk_format_t fs_type() const { return fixture_->options().fs_type; }
  const char* mount_path() const { return mount_path_; }

 private:
  const char* mount_path_ = "/tmp/mount_fvm_backup";
  std::unique_ptr<fs_test_utils::Fixture> fixture_;
};

// Tests backup superblock functionality on minfs backed with FVM block device.
TEST_F(FvmBackupSuperblockTest, FvmMountCorruptedSuperblock)  {
  fbl::unique_fd block_fd(open(block_device_path(), O_RDWR));
  ASSERT_TRUE(block_fd);
  fbl::unique_fd fs_fd(open(partition_path(), O_RDWR));
  ASSERT_TRUE(fs_fd);
  ASSERT_NO_FAILURES(RepairCorruptedSuperblock(std::move(fs_fd), mount_path(), partition_path(),
      minfs::kFvmSuperblockBackup));
}

}  // namespace
