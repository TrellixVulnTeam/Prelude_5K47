// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/tiles/software_image_decode_cache.h"

#include "base/strings/stringprintf.h"
#include "cc/paint/draw_image.h"
#include "cc/paint/paint_image_builder.h"
#include "cc/test/fake_paint_image_generator.h"
#include "cc/test/skia_common.h"
#include "cc/test/test_tile_task_runner.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkRefCnt.h"

namespace cc {
namespace {

size_t kLockedMemoryLimitBytes = 128 * 1024 * 1024;
SkMatrix CreateMatrix(const SkSize& scale, bool is_decomposable) {
  SkMatrix matrix;
  matrix.setScale(scale.width(), scale.height());

  if (!is_decomposable) {
    // Perspective is not decomposable, add it.
    matrix[SkMatrix::kMPersp0] = 0.1f;
  }

  return matrix;
}

class BaseTest : public testing::Test {
 public:
  void SetUp() override {
    cache_ = CreateCache();
    paint_image_ = CreatePaintImage(gfx::Size(512, 512));
  }

  void TearDown() override {
    paint_image_ = PaintImage();
    cache_.reset();
  }

  DrawImage CreateDrawImageForScale(
      float scale,
      const SkIRect src_rect = SkIRect::MakeEmpty()) {
    return DrawImage(
        paint_image(),
        src_rect.isEmpty()
            ? SkIRect::MakeWH(paint_image().width(), paint_image().height())
            : src_rect,
        kMedium_SkFilterQuality, CreateMatrix(SkSize::Make(scale, scale), true),
        PaintImage::kDefaultFrameIndex, GetColorSpace());
  }

  SoftwareImageDecodeCache& cache() { return *cache_; }
  PaintImage& paint_image() { return paint_image_; }

 protected:
  struct CacheEntryResult {
    bool has_task = false;
    bool needs_unref = false;
  };

  virtual std::unique_ptr<SoftwareImageDecodeCache> CreateCache() = 0;
  virtual CacheEntryResult GenerateCacheEntry(const DrawImage& image) = 0;
  virtual PaintImage CreatePaintImage(const gfx::Size& size) = 0;
  virtual gfx::ColorSpace GetColorSpace() = 0;
  virtual void VerifyEntryExists(int line,
                                 const DrawImage& draw_image,
                                 const gfx::Size& expected_size) = 0;

 private:
  std::unique_ptr<SoftwareImageDecodeCache> cache_;
  PaintImage paint_image_;
};

class N32Cache : public virtual BaseTest {
 protected:
  std::unique_ptr<SoftwareImageDecodeCache> CreateCache() override {
    return std::make_unique<SoftwareImageDecodeCache>(
        kN32_SkColorType, kLockedMemoryLimitBytes,
        PaintImage::kDefaultGeneratorClientId);
  }
};

class RGBA4444Cache : public virtual BaseTest {
 protected:
  std::unique_ptr<SoftwareImageDecodeCache> CreateCache() override {
    return std::make_unique<SoftwareImageDecodeCache>(
        kARGB_4444_SkColorType, kLockedMemoryLimitBytes,
        PaintImage::kDefaultGeneratorClientId);
  }
};

class AtRaster : public virtual BaseTest {
 protected:
  CacheEntryResult GenerateCacheEntry(const DrawImage& image) override {
    // At raster doesn't generate cache entries.
    return {false, false};
  }
  void VerifyEntryExists(int line,
                         const DrawImage& draw_image,
                         const gfx::Size& expected_size) override {
    auto decoded = cache().GetDecodedImageForDraw(draw_image);
    SCOPED_TRACE(base::StringPrintf("Failure from line %d", line));
    EXPECT_EQ(decoded.image()->width(), expected_size.width());
    EXPECT_EQ(decoded.image()->height(), expected_size.height());
    cache().DrawWithImageFinished(draw_image, decoded);
  }
};

class Predecode : public virtual BaseTest {
 protected:
  CacheEntryResult GenerateCacheEntry(const DrawImage& image) override {
    auto task_result =
        cache().GetTaskForImageAndRef(image, ImageDecodeCache::TracingInfo());
    CacheEntryResult result = {!!task_result.task, task_result.need_unref};

    if (task_result.task)
      TestTileTaskRunner::ProcessTask(task_result.task.get());
    return result;
  }

  void VerifyEntryExists(int line,
                         const DrawImage& draw_image,
                         const gfx::Size& expected_size) override {
    auto decoded = cache().GetDecodedImageForDraw(draw_image);
    SCOPED_TRACE(base::StringPrintf("Failure from line %d", line));
    EXPECT_EQ(decoded.image()->width(), expected_size.width());
    EXPECT_EQ(decoded.image()->height(), expected_size.height());
    cache().DrawWithImageFinished(draw_image, decoded);
  }
};

class NoDecodeToScaleSupport : public virtual BaseTest {
 protected:
  PaintImage CreatePaintImage(const gfx::Size& size) override {
    return CreateDiscardablePaintImage(size, GetColorSpace().ToSkColorSpace());
  }
};

class DefaultColorSpace : public virtual BaseTest {
 protected:
  gfx::ColorSpace GetColorSpace() override {
    return gfx::ColorSpace::CreateSRGB();
  }
};

class ExoticColorSpace : public virtual BaseTest {
 protected:
  gfx::ColorSpace GetColorSpace() override {
    return gfx::ColorSpace(gfx::ColorSpace::PrimaryID::XYZ_D50,
                           gfx::ColorSpace::TransferID::IEC61966_2_1);
  }
};

class SoftwareImageDecodeCacheTest_Typical : public N32Cache,
                                             public Predecode,
                                             public NoDecodeToScaleSupport,
                                             public DefaultColorSpace {};

TEST_F(SoftwareImageDecodeCacheTest_Typical, UseClosestAvailableDecode) {
  auto draw_image_50 = CreateDrawImageForScale(0.5f);
  auto result = GenerateCacheEntry(draw_image_50);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);

  // Clear the cache to eliminate the transient 1.f scale from the cache.
  cache().ClearCache();
  VerifyEntryExists(__LINE__, draw_image_50, gfx::Size(256, 256));
  EXPECT_EQ(1u, cache().GetNumCacheEntriesForTesting());

  auto draw_image_125 = CreateDrawImageForScale(0.125f);
  result = GenerateCacheEntry(draw_image_125);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);
  VerifyEntryExists(__LINE__, draw_image_125, gfx::Size(64, 64));

  // We didn't clear the cache the second time, and should only expect to find
  // these entries: 0.5 scale and 0.125 scale.
  EXPECT_EQ(2u, cache().GetNumCacheEntriesForTesting());

  cache().UnrefImage(draw_image_50);
  cache().UnrefImage(draw_image_125);
}

TEST_F(SoftwareImageDecodeCacheTest_Typical,
       UseClosestAvailableDecodeNotSmaller) {
  auto draw_image_25 = CreateDrawImageForScale(0.25f);
  auto result = GenerateCacheEntry(draw_image_25);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);

  // Clear the cache to eliminate the transient 1.f scale from the cache.
  cache().ClearCache();
  VerifyEntryExists(__LINE__, draw_image_25, gfx::Size(128, 128));
  EXPECT_EQ(1u, cache().GetNumCacheEntriesForTesting());

  auto draw_image_50 = CreateDrawImageForScale(0.5f);
  result = GenerateCacheEntry(draw_image_50);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);
  VerifyEntryExists(__LINE__, draw_image_50, gfx::Size(256, 256));

  // We didn't clear the cache the second time, and should only expect to find
  // these entries: 1.0 scale, 0.5 scale and 0.25 scale.
  EXPECT_EQ(3u, cache().GetNumCacheEntriesForTesting());

  cache().UnrefImage(draw_image_50);
  cache().UnrefImage(draw_image_25);
}

TEST_F(SoftwareImageDecodeCacheTest_Typical,
       UseClosestAvailableDecodeFirstImageSubrected) {
  auto draw_image_50 = CreateDrawImageForScale(0.5f, SkIRect::MakeWH(500, 500));
  auto result = GenerateCacheEntry(draw_image_50);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);

  // Clear the cache to eliminate the transient 1.f scale from the cache.
  cache().ClearCache();
  VerifyEntryExists(__LINE__, draw_image_50, gfx::Size(250, 250));
  EXPECT_EQ(1u, cache().GetNumCacheEntriesForTesting());

  auto draw_image_125 = CreateDrawImageForScale(0.125f);
  result = GenerateCacheEntry(draw_image_125);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);
  VerifyEntryExists(__LINE__, draw_image_125, gfx::Size(64, 64));

  // We didn't clear the cache the second time, and should only expect to find
  // these entries: 1.0 scale, 0.5 scale subrected and 0.125 scale.
  EXPECT_EQ(3u, cache().GetNumCacheEntriesForTesting());

  cache().UnrefImage(draw_image_50);
  cache().UnrefImage(draw_image_125);
}

TEST_F(SoftwareImageDecodeCacheTest_Typical,
       UseClosestAvailableDecodeSecondImageSubrected) {
  auto draw_image_50 = CreateDrawImageForScale(0.5f);
  auto result = GenerateCacheEntry(draw_image_50);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);

  // Clear the cache to eliminate the transient 1.f scale from the cache.
  cache().ClearCache();
  VerifyEntryExists(__LINE__, draw_image_50, gfx::Size(256, 256));
  EXPECT_EQ(1u, cache().GetNumCacheEntriesForTesting());

  auto draw_image_125 =
      CreateDrawImageForScale(0.125f, SkIRect::MakeWH(400, 400));
  result = GenerateCacheEntry(draw_image_125);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);
  VerifyEntryExists(__LINE__, draw_image_125, gfx::Size(50, 50));

  // We didn't clear the cache the second time, and should only expect to find
  // these entries: 1.0 scale, 0.5 scale and 0.125 scale subrected.
  EXPECT_EQ(3u, cache().GetNumCacheEntriesForTesting());

  cache().UnrefImage(draw_image_50);
  cache().UnrefImage(draw_image_125);
}

TEST_F(SoftwareImageDecodeCacheTest_Typical,
       UseClosestAvailableDecodeBothSubrected) {
  auto draw_image_50 = CreateDrawImageForScale(0.5f, SkIRect::MakeWH(400, 400));
  auto result = GenerateCacheEntry(draw_image_50);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);

  // Clear the cache to eliminate the transient 1.f scale from the cache.
  cache().ClearCache();
  VerifyEntryExists(__LINE__, draw_image_50, gfx::Size(200, 200));
  EXPECT_EQ(1u, cache().GetNumCacheEntriesForTesting());

  auto draw_image_125 =
      CreateDrawImageForScale(0.125f, SkIRect::MakeWH(400, 400));
  result = GenerateCacheEntry(draw_image_125);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);
  VerifyEntryExists(__LINE__, draw_image_125, gfx::Size(50, 50));

  // We didn't clear the cache the second time, and should only expect to find
  // these entries: 0.5 scale subrected and 0.125 scale subrected.
  EXPECT_EQ(2u, cache().GetNumCacheEntriesForTesting());

  cache().UnrefImage(draw_image_50);
  cache().UnrefImage(draw_image_125);
}

TEST_F(SoftwareImageDecodeCacheTest_Typical,
       UseClosestAvailableDecodeBothPastPostScaleSize) {
  auto draw_image_50 =
      CreateDrawImageForScale(0.5f, SkIRect::MakeXYWH(300, 300, 52, 52));
  auto result = GenerateCacheEntry(draw_image_50);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);

  // Clear the cache to eliminate the transient 1.f scale from the cache.
  cache().ClearCache();
  VerifyEntryExists(__LINE__, draw_image_50, gfx::Size(26, 26));
  EXPECT_EQ(1u, cache().GetNumCacheEntriesForTesting());

  auto draw_image_25 =
      CreateDrawImageForScale(0.25, SkIRect::MakeXYWH(300, 300, 52, 52));
  result = GenerateCacheEntry(draw_image_25);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);
  VerifyEntryExists(__LINE__, draw_image_25, gfx::Size(13, 13));

  // We didn't clear the cache the second time, and should only expect to find
  // these entries: 0.5 scale subrected and 0.25 scale subrected.
  EXPECT_EQ(2u, cache().GetNumCacheEntriesForTesting());

  cache().UnrefImage(draw_image_50);
  cache().UnrefImage(draw_image_25);
}

class SoftwareImageDecodeCacheTest_AtRaster : public N32Cache,
                                              public AtRaster,
                                              public NoDecodeToScaleSupport,
                                              public DefaultColorSpace {};

TEST_F(SoftwareImageDecodeCacheTest_AtRaster, UseClosestAvailableDecode) {
  auto draw_image_50 = CreateDrawImageForScale(0.5f);
  auto decoded = cache().GetDecodedImageForDraw(draw_image_50);
  {
    VerifyEntryExists(__LINE__, draw_image_50, gfx::Size(256, 256));
    cache().ClearCache();
    EXPECT_EQ(1u, cache().GetNumCacheEntriesForTesting());

    auto draw_image_125 = CreateDrawImageForScale(0.125f);
    VerifyEntryExists(__LINE__, draw_image_125, gfx::Size(64, 64));
  }
  cache().DrawWithImageFinished(draw_image_50, decoded);

  // We should only expect to find these entries: 0.5 scale and 0.125 scale.
  EXPECT_EQ(2u, cache().GetNumCacheEntriesForTesting());
}

TEST_F(SoftwareImageDecodeCacheTest_AtRaster,
       UseClosestAvailableDecodeSubrected) {
  auto draw_image_50 = CreateDrawImageForScale(0.5f, SkIRect::MakeWH(500, 500));
  auto decoded = cache().GetDecodedImageForDraw(draw_image_50);
  {
    VerifyEntryExists(__LINE__, draw_image_50, gfx::Size(250, 250));
    cache().ClearCache();
    EXPECT_EQ(1u, cache().GetNumCacheEntriesForTesting());

    auto draw_image_125 = CreateDrawImageForScale(0.125f);
    VerifyEntryExists(__LINE__, draw_image_125, gfx::Size(64, 64));
  }
  cache().DrawWithImageFinished(draw_image_50, decoded);

  // We should only expect to find these entries: 1.0 scale, 0.5 scale subrected
  // and 0.125 scale.
  EXPECT_EQ(3u, cache().GetNumCacheEntriesForTesting());
}

class SoftwareImageDecodeCacheTest_ExoticColorSpace
    : public N32Cache,
      public Predecode,
      public NoDecodeToScaleSupport,
      public ExoticColorSpace {};

TEST_F(SoftwareImageDecodeCacheTest_ExoticColorSpace,
       UseClosestAvailableDecode) {
  auto draw_image_50 = CreateDrawImageForScale(0.5f);
  auto result = GenerateCacheEntry(draw_image_50);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);

  // Clear the cache to eliminate the transient 1.f scale from the cache.
  cache().ClearCache();
  VerifyEntryExists(__LINE__, draw_image_50, gfx::Size(256, 256));
  EXPECT_EQ(1u, cache().GetNumCacheEntriesForTesting());

  auto draw_image_125 = CreateDrawImageForScale(0.125f);
  result = GenerateCacheEntry(draw_image_125);
  EXPECT_TRUE(result.has_task);
  EXPECT_TRUE(result.needs_unref);
  VerifyEntryExists(__LINE__, draw_image_125, gfx::Size(64, 64));

  // We didn't clear the cache the second time, and should only expect to find
  // these entries: 0.5 scale and 0.125 scale.
  EXPECT_EQ(2u, cache().GetNumCacheEntriesForTesting());

  cache().UnrefImage(draw_image_50);
  cache().UnrefImage(draw_image_125);
}

}  // namespace
}  // namespace cc
