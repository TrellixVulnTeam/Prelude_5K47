// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAPABILITIES_VIDEO_DECODE_STATS_DB_H_
#define MEDIA_CAPABILITIES_VIDEO_DECODE_STATS_DB_H_

#include <memory>
#include <string>

#include "base/callback_forward.h"
#include "base/logging.h"
#include "base/macros.h"
#include "media/base/media_export.h"
#include "media/base/video_codecs.h"
#include "ui/gfx/geometry/size.h"

namespace media {

// This defines the interface to be used by various media capabilities services
// to store/retrieve video decoding performance statistics.
class MEDIA_EXPORT VideoDecodeStatsDB {
 public:
  // Simple description of video decode complexity, serving as a key to look up
  // associated DecodeStatsEntries in the database.
  struct MEDIA_EXPORT VideoDescKey {
    static VideoDescKey MakeBucketedKey(VideoCodecProfile codec_profile,
                                        const gfx::Size& size,
                                        int frame_rate);

    // Returns a concise string representation of the key for storing in DB.
    std::string Serialize() const;

    // For debug logging. NOT interchangeable with Serialize().
    std::string ToLogString() const;

    // Note: operator == and != are defined outside this class.
    const VideoCodecProfile codec_profile;
    const gfx::Size size;
    const int frame_rate;

   private:
    // All key's should be "bucketed" using MakeBucketedKey(...).
    VideoDescKey(VideoCodecProfile codec_profile,
                 const gfx::Size& size,
                 int frame_rate);
  };

  // DecodeStatsEntry saved to identify the capabilities related to a given
  // |VideoDescKey|.
  struct MEDIA_EXPORT DecodeStatsEntry {
    DecodeStatsEntry(uint64_t frames_decoded,
                     uint64_t frames_dropped,
                     uint64_t frames_decoded_power_efficient);
    DecodeStatsEntry(const DecodeStatsEntry& entry);

    // Add stats from |right| to |this| entry.
    DecodeStatsEntry& operator+=(const DecodeStatsEntry& right);

    // For debug logging.
    std::string ToLogString() const;

    // Note: operator == and != are defined outside this class.
    uint64_t frames_decoded;
    uint64_t frames_dropped;
    uint64_t frames_decoded_power_efficient;
  };

  virtual ~VideoDecodeStatsDB();

  // Run asynchronous initialization of database. Initialization must complete
  // before calling other APIs. Initialization must be RE-RUN after calling
  // DestroyStats() and receiving its completion callback. |init_cb| must not be
  // a null callback.
  using InitializeCB = base::OnceCallback<void(bool)>;
  virtual void Initialize(InitializeCB init_cb) = 0;

  // Appends `stats` to existing entry associated with `key`. Will create a new
  // entry if none exists. The operation is asynchronous. The caller should be
  // aware of potential race conditions when calling this method for the same
  // `key` very close to other calls. `append_done_cb` will be run with a bool
  // to indicate whether the save succeeded.
  using AppendDecodeStatsCB = base::OnceCallback<void(bool)>;
  virtual void AppendDecodeStats(const VideoDescKey& key,
                                 const DecodeStatsEntry& entry,
                                 AppendDecodeStatsCB append_done_cb) = 0;

  // Returns the stats  associated with `key`. The `get_stats_cb` will receive
  // the stats in addition to a boolean signaling if the call was successful.
  // DecodeStatsEntry can be nullptr if there was no data associated with `key`.
  using GetDecodeStatsCB =
      base::OnceCallback<void(bool, std::unique_ptr<DecodeStatsEntry>)>;
  virtual void GetDecodeStats(const VideoDescKey& key,
                              GetDecodeStatsCB get_stats_cb) = 0;

  // Clear all statistics by DESTROYING the underlying the database.
  // DO NOT use the database until |callback| is run. When finished, users must
  // RE-RUN Initialize() before performing further I/O.
  virtual void DestroyStats(base::OnceClosure destroy_done_cb) = 0;

  // Tracking down root cause of crash probable UAF (https://crbug/865321).
  // We will CHECK if a |dependent_db_| is found to be set during destruction.
  // Dependent DB should always be destroyed and unhooked before |this|.
  void set_dependent_db(VideoDecodeStatsDB* dependent) {
    // One of these should be non-null.
    CHECK(!dependent_db_ || !dependent);
    // They shouldn't already match.
    CHECK(dependent_db_ != dependent);

    dependent_db_ = dependent;
  }

 private:
  // See set_dependent_db().
  VideoDecodeStatsDB* dependent_db_ = nullptr;
};

MEDIA_EXPORT bool operator==(const VideoDecodeStatsDB::VideoDescKey& x,
                             const VideoDecodeStatsDB::VideoDescKey& y);
MEDIA_EXPORT bool operator!=(const VideoDecodeStatsDB::VideoDescKey& x,
                             const VideoDecodeStatsDB::VideoDescKey& y);
MEDIA_EXPORT bool operator==(const VideoDecodeStatsDB::DecodeStatsEntry& x,
                             const VideoDecodeStatsDB::DecodeStatsEntry& y);
MEDIA_EXPORT bool operator!=(const VideoDecodeStatsDB::DecodeStatsEntry& x,
                             const VideoDecodeStatsDB::DecodeStatsEntry& y);

// Factory interface to create a DB instance.
class MEDIA_EXPORT VideoDecodeStatsDBFactory {
 public:
  virtual ~VideoDecodeStatsDBFactory() {}
  virtual std::unique_ptr<VideoDecodeStatsDB> CreateDB() = 0;
};

}  // namespace media

#endif  // MEDIA_CAPABILITIES_VIDEO_DECODE_STATS_DB_H_
