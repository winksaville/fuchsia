// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_DATA_HW_BREAKPOINTER_HELPERS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_DATA_HW_BREAKPOINTER_HELPERS_H_

#include <lib/fit/defer.h>
#include <lib/zx/event.h>
#include <lib/zx/exception.h>
#include <lib/zx/port.h>
#include <lib/zx/thread.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/threads.h>

#include <iostream>
#include <thread>
#include <mutex>

#include "src/lib/files/path.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"


    // auto base_name = files::GetBaseName(__FILE__);                                            \
    // std::cout << "[" << base_name << ":" << __LINE__ << "][t: " << std::this_thread::get_id()

#define PRINT(...)                                                                            \
  {                                                                                           \
    std::cout << "[t: " << std::this_thread::get_id() \
              << "] " << fxl::StringPrintf(__VA_ARGS__) << std::endl                          \
              << std::flush;                                                                  \
  }

#define DEFER_PRINT(...) auto __defer = fit::defer([=]() { PRINT(__VA_ARGS__); });

#define CHECK_OK(stmt)                                         \
  {                                                            \
    zx_status_t __res = (stmt);                                \
    FXL_DCHECK(__res == ZX_OK) << zx_status_get_string(__res); \
  }

#define ARRAY_SIZE(a) (sizeof((a)) / sizeof((a))[0])


constexpr char kBeacon[] = "Counter: Thread running.\n";
constexpr int kPortKey = 0x2312451;

constexpr uint32_t kHarnessToThread = ZX_USER_SIGNAL_0;
constexpr uint32_t kThreadToHarness = ZX_USER_SIGNAL_1;

// Control struct for each running test case.
struct ThreadSetup {
  using Function = int (*)(void*);

  ~ThreadSetup();

  zx::event event;
  zx::thread thread;
  thrd_t c_thread;

  std::atomic<bool> test_running = false;
  void* user = nullptr;
};

std::unique_ptr<ThreadSetup> CreateTestSetup(ThreadSetup::Function func, void* user = nullptr);

std::pair<zx::port, zx::channel> CreateExceptionChannel(const zx::thread&);

zx_thread_state_general_regs_t ReadGeneralRegs(const zx::thread& thread);

void WriteGeneralRegs(const zx::thread& thread, const zx_thread_state_debug_regs_t& regs);

std::optional<zx_port_packet_t> WaitOnPort(const zx::port& port, zx_signals_t signals,
                                           zx::time deadline = zx::time::infinite());

struct Exception {
  zx::process process;
  zx::thread thread;

  zx::exception handle;
  zx_exception_info_t info;
  zx_thread_state_general_regs_t regs;
  uint64_t pc = 0;
};

Exception GetException(const zx::channel& exception_channel);

std::optional<Exception> WaitForException(const zx::port& port,
                                          const zx::channel& exception_channel,
                                          zx::time deadline = zx::time::infinite());

void ResumeException(const zx::thread& thread, Exception&& exception, bool handled = true);

void WaitAsyncOnExceptionChannel(const zx::port& port, const zx::channel& exception_channel);

bool IsOnException(const zx::thread& thread);

// NOTE: This might return an invalid (empty) suspend_token.
//       If that happens, it means that |thread| is on an exception.
zx::suspend_token Suspend(const zx::thread& thread);

void InstallHWBreakpoint(const zx::thread& thread, uint64_t address);
void RemoveHWBreakpoint(const zx::thread& thread);

// |bytes_to_hit| is a bitfield that defines which bytes to hit from |address|.
//
// Bit 0 = Match |address|.
// ...
// Bit 7 = Match |address| + 7.
// Bit >= 8 = ignored.
void InstallWatchpoint(const zx::thread& thread, uint64_t address, uint32_t bytes_to_hit);
void RemoveWatchpoint(const zx::thread& thread);

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_DATA_HW_BREAKPOINTER_HELPERS_H_
