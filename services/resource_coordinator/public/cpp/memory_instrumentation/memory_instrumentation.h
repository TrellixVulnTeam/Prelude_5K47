// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_INSTRUMENTATION_MEMORY_INSTRUMENTATION_H_
#define SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_INSTRUMENTATION_MEMORY_INSTRUMENTATION_H_

#include "base/callback_forward.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread_local_storage.h"
#include "base/trace_event/memory_dump_request_args.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/coordinator.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/global_memory_dump.h"
#include "services/resource_coordinator/public/cpp/resource_coordinator_export.h"
#include "services/resource_coordinator/public/mojom/memory_instrumentation/memory_instrumentation.mojom.h"
#include "services/service_manager/public/cpp/connector.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace memory_instrumentation {

// This a public API for the memory-infra service and allows any thread/process
// to request memory snapshots. This is a convenience wrapper around the
// memory_instrumentation service and hides away the complexity associated with
// having to deal with it (e.g., maintaining service connections, bindings,
// handling timeouts).
class SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_EXPORT MemoryInstrumentation {
 public:
  using MemoryDumpType = base::trace_event::MemoryDumpType;
  using MemoryDumpLevelOfDetail = base::trace_event::MemoryDumpLevelOfDetail;
  using RequestGlobalDumpCallback =
      base::OnceCallback<void(bool success,
                              std::unique_ptr<GlobalMemoryDump> dump)>;
  using RequestGlobalMemoryDumpAndAppendToTraceCallback =
      base::OnceCallback<void(bool success, uint64_t dump_id)>;

  static void CreateInstance(service_manager::Connector*,
                             const std::string& service_name);
  static MemoryInstrumentation* GetInstance();

  // Requests a global memory dump with |allocator_dump_names| indicating
  // the name of allocator dumps in which the consumer is interested. If
  // |allocator_dump_names| is empty, no dumps will be returned.
  // Returns asynchronously, via the callback argument:
  //  (true, global_dump) if succeeded;
  //  (false, global_dump) if failed, with global_dump being non-null
  //  but missing data.
  // The callback (if not null), will be posted on the same thread of the
  // RequestGlobalDump() call.
  // Note: Even if |allocator_dump_names| is empty, all MemoryDumpProviders will
  // still be queried.
  void RequestGlobalDump(const std::vector<std::string>& allocator_dump_names,
                         RequestGlobalDumpCallback);

  // Returns asynchronously, via the callback argument:
  //  (true, global_dump) if succeeded;
  //  (false, global_dump) if failed, with global_dump being non-null
  //  but missing data.
  // The callback (if not null), will be posted on the same thread of the
  // RequestPrivateMemoryFootprint() call.
  // Passing a null |pid| is the same as requesting the PrivateMemoryFootprint
  // for all processes.
  void RequestPrivateMemoryFootprint(base::ProcessId pid,
                                     RequestGlobalDumpCallback);

  // Requests a global memory dump with |allocator_dump_names| indicating
  // the name of allocator dumps in which the consumer is interested. If
  // |allocator_dump_names| is empty, all allocator dumps will be returned.
  // Returns asynchronously, via the callback argument, the global memory
  // dump with the process memory dump for the given pid:
  //  (true, global_dump) if succeeded;
  //  (false, global_dump) if failed, with global_dump being non-null
  //  but missing data.
  // The callback (if not null), will be posted on the same thread of the
  // RequestGlobalDump() call.
  void RequestGlobalDumpForPid(
      base::ProcessId pid,
      const std::vector<std::string>& allocator_dump_names,
      RequestGlobalDumpCallback);

  // Requests a global memory dump and serializes the result into the trace.
  // This requires that both tracing and the memory-infra category have been
  // previousy enabled. Will just gracefully fail otherwise.
  // Returns asynchronously, via the callback argument:
  //  (true, id of the object injected into the trace) if succeeded;
  //  (false, undefined) if failed.
  // The callback (if not null), will be posted on the same thread of the
  // RequestGlobalDumpAndAppendToTrace() call.
  void RequestGlobalDumpAndAppendToTrace(
      MemoryDumpType,
      MemoryDumpLevelOfDetail,
      RequestGlobalMemoryDumpAndAppendToTraceCallback);

 private:
  MemoryInstrumentation(service_manager::Connector* connector,
                        const std::string& service_name);
  ~MemoryInstrumentation();

  const mojom::CoordinatorPtr& GetCoordinatorBindingForCurrentThread();
  void BindCoordinatorRequestOnConnectorThread(mojom::CoordinatorRequest);

  service_manager::Connector* const connector_;
  scoped_refptr<base::SingleThreadTaskRunner> connector_task_runner_;
  base::ThreadLocalStorage::Slot tls_coordinator_;
  const std::string service_name_;

  DISALLOW_COPY_AND_ASSIGN(MemoryInstrumentation);
};

}  // namespace memory_instrumentation

#endif  // SERVICES_RESOURCE_COORDINATOR_PUBLIC_CPP_MEMORY_INSTRUMENTATION_MEMORY_INSTRUMENTATION_H_
