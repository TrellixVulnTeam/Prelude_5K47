// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_SIMPLE_SIMPLE_BACKEND_IMPL_H_
#define NET_DISK_CACHE_SIMPLE_SIMPLE_BACKEND_IMPL_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_split.h"
#include "base/task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "net/base/cache_type.h"
#include "net/base/net_export.h"
#include "net/disk_cache/disk_cache.h"
#include "net/disk_cache/simple/simple_entry_impl.h"
#include "net/disk_cache/simple/simple_index_delegate.h"

namespace base {
class SequencedTaskRunner;
class TaskRunner;
}  // namespace base

namespace net {
class PrioritizedTaskRunner;
}  // namespace net

namespace disk_cache {

// SimpleBackendImpl is a new cache backend that stores entries in individual
// files.
// See http://www.chromium.org/developers/design-documents/network-stack/disk-cache/very-simple-backend
//
// The SimpleBackendImpl provides safe iteration; mutating entries during
// iteration cannot cause a crash. It is undefined whether entries created or
// destroyed during the iteration will be included in any pre-existing
// iterations.
//
// The non-static functions below must be called on the IO thread unless
// otherwise stated.

class BackendCleanupTracker;
class SimpleEntryImpl;
class SimpleFileTracker;
class SimpleIndex;

class NET_EXPORT_PRIVATE SimpleBackendImpl : public Backend,
    public SimpleIndexDelegate,
    public base::SupportsWeakPtr<SimpleBackendImpl> {
 public:
  static const base::Feature kPrioritizedSimpleCacheTasks;

  // Note: only pass non-nullptr for |file_tracker| if you don't want the global
  // one (which things other than tests would want). |file_tracker| must outlive
  // the backend and all the entries, including their asynchronous close.
  SimpleBackendImpl(const base::FilePath& path,
                    scoped_refptr<BackendCleanupTracker> cleanup_tracker,
                    SimpleFileTracker* file_tracker,
                    int64_t max_bytes,
                    net::CacheType cache_type,
                    net::NetLog* net_log);

  ~SimpleBackendImpl() override;

  net::CacheType cache_type() const { return cache_type_; }
  SimpleIndex* index() { return index_.get(); }

  void SetWorkerPoolForTesting(scoped_refptr<base::TaskRunner> task_runner);

  int Init(CompletionOnceCallback completion_callback);

  // Sets the maximum size for the total amount of data stored by this instance.
  bool SetMaxSize(int64_t max_bytes);

  // Returns the maximum file size permitted in this backend.
  int GetMaxFileSize() const;

  // Flush our SequencedWorkerPool.
  static void FlushWorkerPoolForTesting();

  // The entry for |entry_hash| is being doomed; the backend will not attempt
  // run new operations for this |entry_hash| until the Doom is completed.
  void OnDoomStart(uint64_t entry_hash);

  // The entry for |entry_hash| has been successfully doomed, we can now allow
  // operations on this entry, and we can run any operations enqueued while the
  // doom completed.
  void OnDoomComplete(uint64_t entry_hash);

  // SimpleIndexDelegate:
  void DoomEntries(std::vector<uint64_t>* entry_hashes,
                   CompletionOnceCallback callback) override;

  // Backend:
  net::CacheType GetCacheType() const override;
  int32_t GetEntryCount() const override;
  int OpenEntry(const std::string& key,
                net::RequestPriority request_priority,
                Entry** entry,
                CompletionOnceCallback callback) override;
  int CreateEntry(const std::string& key,
                  net::RequestPriority request_priority,
                  Entry** entry,
                  CompletionOnceCallback callback) override;
  int DoomEntry(const std::string& key,
                net::RequestPriority priority,
                CompletionOnceCallback callback) override;
  int DoomAllEntries(CompletionOnceCallback callback) override;
  int DoomEntriesBetween(base::Time initial_time,
                         base::Time end_time,
                         CompletionOnceCallback callback) override;
  int DoomEntriesSince(base::Time initial_time,
                       CompletionOnceCallback callback) override;
  int64_t CalculateSizeOfAllEntries(
      Int64CompletionOnceCallback callback) override;
  int64_t CalculateSizeOfEntriesBetween(
      base::Time initial_time,
      base::Time end_time,
      Int64CompletionOnceCallback callback) override;
  std::unique_ptr<Iterator> CreateIterator() override;
  void GetStats(base::StringPairs* stats) override;
  void OnExternalCacheHit(const std::string& key) override;
  size_t DumpMemoryStats(
      base::trace_event::ProcessMemoryDump* pmd,
      const std::string& parent_absolute_name) const override;
  uint8_t GetEntryInMemoryData(const std::string& key) override;
  void SetEntryInMemoryData(const std::string& key, uint8_t data) override;

  net::PrioritizedTaskRunner* prioritized_task_runner() const {
    return prioritized_task_runner_.get();
  }

#if defined(OS_ANDROID)
  void set_app_status_listener(
      base::android::ApplicationStatusListener* app_status_listener) {
    app_status_listener_ = app_status_listener;
  }
#endif

 private:
  class SimpleIterator;
  friend class SimpleIterator;

  using EntryMap = std::unordered_map<uint64_t, SimpleEntryImpl*>;

  using InitializeIndexCallback =
      base::Callback<void(base::Time mtime, uint64_t max_size, int result)>;

  class ActiveEntryProxy;
  friend class ActiveEntryProxy;

  // Return value of InitCacheStructureOnDisk().
  struct DiskStatResult {
    base::Time cache_dir_mtime;
    uint64_t max_size;
    bool detected_magic_number_mismatch;
    int net_error;
  };

  struct PostDoomWaiter {
    PostDoomWaiter();
    // Also initializes |time_queued|.
    explicit PostDoomWaiter(base::OnceClosure to_run_post_doom);
    explicit PostDoomWaiter(PostDoomWaiter&& other);
    ~PostDoomWaiter();
    PostDoomWaiter& operator=(PostDoomWaiter&& other);

    base::TimeTicks time_queued;
    base::OnceClosure run_post_doom;
  };

  void InitializeIndex(CompletionOnceCallback callback,
                       const DiskStatResult& result);

  // Dooms all entries previously accessed between |initial_time| and
  // |end_time|. Invoked when the index is ready.
  void IndexReadyForDoom(base::Time initial_time,
                         base::Time end_time,
                         CompletionOnceCallback callback,
                         int result);

  // Calculates the size of the entire cache. Invoked when the index is ready.
  void IndexReadyForSizeCalculation(Int64CompletionOnceCallback callback,
                                    int result);

  // Calculates the size all cache entries between |initial_time| and
  // |end_time|. Invoked when the index is ready.
  void IndexReadyForSizeBetweenCalculation(base::Time initial_time,
                                           base::Time end_time,
                                           Int64CompletionOnceCallback callback,
                                           int result);

  // Try to create the directory if it doesn't exist. This must run on the IO
  // thread.
  static DiskStatResult InitCacheStructureOnDisk(const base::FilePath& path,
                                                 uint64_t suggested_max_size);

  // Looks at current state of |entries_pending_doom_| and |active_entries_|
  // relevant to |entry_hash|, and, as appropriate, either returns a valid entry
  // matching |entry_hash| and |key|, or returns nullptr and sets |*post_doom|
  // to point to a vector of closures which will be invoked when it's an
  // appropriate time to try again.  The caller is expected to append its retry
  // closure to that vector.
  scoped_refptr<SimpleEntryImpl> CreateOrFindActiveOrDoomedEntry(
      uint64_t entry_hash,
      const std::string& key,
      net::RequestPriority request_priority,
      std::vector<PostDoomWaiter>** post_doom);

  // Given a hash, will try to open the corresponding Entry. If we have an Entry
  // corresponding to |hash| in the map of active entries, opens it. Otherwise,
  // a new empty Entry will be created, opened and filled with information from
  // the disk.
  int OpenEntryFromHash(uint64_t entry_hash,
                        Entry** entry,
                        CompletionOnceCallback callback);

  // Doom the entry corresponding to |entry_hash|, if it's active or currently
  // pending doom. This function does not block if there is an active entry,
  // which is very important to prevent races in DoomEntries() above.
  int DoomEntryFromHash(uint64_t entry_hash, CompletionOnceCallback callback);

  // Called when we tried to open an entry with hash alone. When a blank entry
  // has been created and filled in with information from the disk - based on a
  // hash alone - this checks that a duplicate active entry was not created
  // using a key in the meantime.
  void OnEntryOpenedFromHash(uint64_t hash,
                             Entry** entry,
                             const scoped_refptr<SimpleEntryImpl>& simple_entry,
                             CompletionOnceCallback callback,
                             int error_code);

  // Called when we tried to open an entry from key. When the entry has been
  // opened, a check for key mismatch is performed.
  void OnEntryOpenedFromKey(const std::string key,
                            Entry** entry,
                            const scoped_refptr<SimpleEntryImpl>& simple_entry,
                            CompletionOnceCallback callback,
                            int error_code);

  // A callback thunk used by DoomEntries to clear the |entries_pending_doom_|
  // after a mass doom.
  void DoomEntriesComplete(std::unique_ptr<std::vector<uint64_t>> entry_hashes,
                           CompletionOnceCallback callback,
                           int result);

  // Calculates and returns a new entry's worker pool priority.
  uint32_t GetNewEntryPriority(net::RequestPriority request_priority);

  // We want this destroyed after every other field.
  scoped_refptr<BackendCleanupTracker> cleanup_tracker_;

  SimpleFileTracker* const file_tracker_;

  const base::FilePath path_;
  const net::CacheType cache_type_;
  std::unique_ptr<SimpleIndex> index_;

  // This is only used for initial open (including potential format upgrade)
  // and index load/save.
  const scoped_refptr<base::SequencedTaskRunner> cache_runner_;

  // This is used for all the entry I/O.
  scoped_refptr<net::PrioritizedTaskRunner> prioritized_task_runner_;

  int64_t orig_max_size_;
  const SimpleEntryImpl::OperationsMode entry_operations_mode_;

  EntryMap active_entries_;

  // The set of all entries which are currently being doomed. To avoid races,
  // these entries cannot have Doom/Create/Open operations run until the doom
  // is complete. The base::Closure |PostDoomWaiter::run_post_doom| field is
  // used to store deferred operations to be run at the completion of the Doom.
  std::unordered_map<uint64_t, std::vector<PostDoomWaiter>>
      entries_pending_doom_;

  net::NetLog* const net_log_;

  uint32_t entry_count_ = 0;

#if defined(OS_ANDROID)
  base::android::ApplicationStatusListener* app_status_listener_ = nullptr;
#endif
};

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_SIMPLE_SIMPLE_BACKEND_IMPL_H_
