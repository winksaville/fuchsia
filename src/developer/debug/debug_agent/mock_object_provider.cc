// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_object_provider.h"

#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/logging.h"

namespace debug_agent {

std::vector<zx::job> MockObjectProvider::GetChildJobs(zx_handle_t job_handle) {
  zx_koid_t job_koid = static_cast<zx_koid_t>(job_handle);
  MockJobObject* job = reinterpret_cast<MockJobObject*>(object_map_[job_koid]);
  FXL_DCHECK(job) << "On koid: " << job_koid;
  FXL_DCHECK(job->type == MockObject::Type::kJob);

  std::vector<zx::job> child_jobs;
  for (auto& child_job : job->child_jobs) {
    child_jobs.push_back(zx::job(static_cast<zx_handle_t>(child_job->koid)));
  }

  return child_jobs;
}

std::vector<zx::process> MockObjectProvider::GetChildProcesses(zx_handle_t job_handle) {
  zx_koid_t job_koid = static_cast<zx_koid_t>(job_handle);
  MockJobObject* job = reinterpret_cast<MockJobObject*>(object_map_[job_koid]);
  FXL_DCHECK(job) << "On koid: " << job_koid;
  FXL_DCHECK(job->type == MockObject::Type::kJob);

  std::vector<zx::process> child_processes;
  for (auto& child_process : job->child_processes) {
    child_processes.push_back(zx::process(static_cast<zx_handle_t>(child_process->koid)));
  }

  return child_processes;
}

std::string MockObjectProvider::NameForObject(zx_handle_t object_handle) {
  zx_koid_t koid = static_cast<zx_koid_t>(object_handle);
  DEBUG_LOG(Test) << "Getting name for: " << object_handle;
  MockObject* object = object_map_[koid];
  FXL_DCHECK(object) << "No object for " << object_handle;

  DEBUG_LOG(Test) << "Getting name for " << object_handle << ", got " << object->name;

  return object->name;
}

zx_koid_t MockObjectProvider::KoidForObject(zx_handle_t object_handle) {
  zx_koid_t koid = static_cast<zx_koid_t>(object_handle);
  MockObject* object = object_map_[koid];
  FXL_DCHECK(object) << "No object for " << object_handle;

  DEBUG_LOG(Test) << "Getting koid for " << object_handle << ", got " << object->koid;

  return object->koid;
};

// Test Setup Implementation.

MockObjectProvider CreateDefaultMockObjectProvider() {
  MockObjectProvider provider;
  MockJobObject* root = provider.AppendJob(nullptr, "root");

  provider.AppendProcess(root, "root-p1");
  provider.AppendProcess(root, "root-p2");
  provider.AppendProcess(root, "root-p3");

  MockJobObject* job1 = provider.AppendJob(root, "job1");
  provider.AppendProcess(job1, "job1-p1");
  provider.AppendProcess(job1, "job1-p2");

  MockJobObject* job11 = provider.AppendJob(job1, "job11");
  provider.AppendProcess(job11, "job11-p1");   // process-6

  MockJobObject* job12= provider.AppendJob(job1, "job12");     // job-3
  MockJobObject* job121 = provider.AppendJob(job12, "job121");   // job-4
  provider.AppendProcess(job121, "job121-p1");  // process-7
  provider.AppendProcess(job121, "job121-p2");  // process-8

  return provider;
}

MockJobObject* MockObjectProvider::AppendJob(MockJobObject* job, std::string name) {
  if (job == nullptr) {
    root_ = CreateJob(name);
    return root_.get();
  }

  job->child_jobs.push_back(CreateJob(std::move(name)));
  return job->child_jobs.back().get();
}

MockProcessObject* MockObjectProvider::AppendProcess(MockJobObject* job, std::string name) {
  job->child_processes.push_back(CreateProcess(std::move(name)));
  return job->child_processes.back().get();
}

std::unique_ptr<MockJobObject> MockObjectProvider::CreateJob(std::string name) {
  int koid = next_koid_++;

  auto job = std::make_unique<MockJobObject>();
  job->koid = koid;
  job->name = std::move(name);
  job->type = MockObject::Type::kJob;

  object_map_[job->koid] = job.get();
  name_map_[job->name] = job.get();

  return job;
}

std::unique_ptr<MockProcessObject> MockObjectProvider::CreateProcess(std::string name) {
  int koid = next_koid_++;

  auto process = std::make_unique<MockProcessObject>();
  process->koid = koid;
  process->name = std::move(name);
  process->type = MockObject::Type::kProcess;

  object_map_[process->koid] = process.get();
  name_map_[process->name] = process.get();

  return process;
}

MockObject* MockObjectProvider::ObjectByKoid(zx_koid_t koid) const {
  auto it = object_map_.find(koid);
  if (it == object_map_.end())
    return nullptr;
  return it->second;
}

MockObject* MockObjectProvider::ObjectByName(const std::string& name) const {
  auto it = name_map_.find(name);
  if (it == name_map_.end())
    return nullptr;
  return it->second;
}

}  // namespace debug_agent