/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRasterPipeline_DEFINED
#define SkRasterPipeline_DEFINED

#include "SkArenaAlloc.h"
#include "SkImageInfo.h"
#include "SkNx.h"
#include "SkPM4f.h"
#include "SkTArray.h"
#include "SkTypes.h"
#include <functional>
#include <vector>
#include "../jumper/SkJumper.h"

/**
 * SkRasterPipeline provides a cheap way to chain together a pixel processing pipeline.
 *
 * It's particularly designed for situations where the potential pipeline is extremely
 * combinatoric: {N dst formats} x {M source formats} x {K mask formats} x {C transfer modes} ...
 * No one wants to write specialized routines for all those combinations, and if we did, we'd
 * end up bloating our code size dramatically.  SkRasterPipeline stages can be chained together
 * at runtime, so we can scale this problem linearly rather than combinatorically.
 *
 * Each stage is represented by a function conforming to a common interface and by an
 * arbitrary context pointer.  The stage funciton arguments and calling convention are
 * designed to maximize the amount of data we can pass along the pipeline cheaply, and
 * vary depending on CPU feature detection.
 *
 * If you'd like to see how this works internally, you want to start digging around src/jumper.
 */

#define SK_RASTER_PIPELINE_STAGES(M)                               \
    M(callback)                                                    \
    M(move_src_dst) M(move_dst_src)                                \
    M(clamp_0) M(clamp_1) M(clamp_a) M(clamp_a_dst) M(clamp_gamut) \
    M(unpremul) M(premul) M(premul_dst)                            \
    M(force_opaque) M(force_opaque_dst)                            \
    M(set_rgb) M(unbounded_set_rgb) M(swap_rb)                     \
    M(from_srgb) M(to_srgb)                                        \
    M(black_color) M(white_color) M(uniform_color) M(unbounded_uniform_color) \
    M(seed_shader) M(dither)                                       \
    M(load_a8)   M(load_a8_dst)   M(store_a8)   M(gather_a8)       \
    M(load_g8)   M(load_g8_dst)                 M(gather_g8)       \
    M(load_565)  M(load_565_dst)  M(store_565)  M(gather_565)      \
    M(load_4444) M(load_4444_dst) M(store_4444) M(gather_4444)     \
    M(load_f16)  M(load_f16_dst)  M(store_f16)  M(gather_f16)      \
    M(load_f32)  M(load_f32_dst)  M(store_f32)  M(gather_f32)      \
    M(load_8888) M(load_8888_dst) M(store_8888) M(gather_8888)     \
    M(load_bgra) M(load_bgra_dst) M(store_bgra) M(gather_bgra)     \
    M(load_1010102) M(load_1010102_dst) M(store_1010102) M(gather_1010102) \
    M(bilerp_clamp_8888)                                           \
    M(store_u16_be)                                                \
    M(load_rgba) M(store_rgba)                                     \
    M(scale_u8) M(scale_565) M(scale_1_float)                      \
    M( lerp_u8) M( lerp_565) M( lerp_1_float)                      \
    M(dstatop) M(dstin) M(dstout) M(dstover)                       \
    M(srcatop) M(srcin) M(srcout) M(srcover)                       \
    M(clear) M(modulate) M(multiply) M(plus_) M(screen) M(xor_)    \
    M(colorburn) M(colordodge) M(darken) M(difference)             \
    M(exclusion) M(hardlight) M(lighten) M(overlay) M(softlight)   \
    M(hue) M(saturation) M(color) M(luminosity)                    \
    M(srcover_rgba_8888) M(srcover_bgra_8888)                      \
    M(luminance_to_alpha)                                          \
    M(matrix_translate) M(matrix_scale_translate)                  \
    M(matrix_2x3) M(matrix_3x3) M(matrix_3x4) M(matrix_4x5) M(matrix_4x3) \
    M(matrix_perspective)                                          \
    M(parametric) M(gamma)                                         \
    M(mirror_x)   M(repeat_x)                                      \
    M(mirror_y)   M(repeat_y)                                      \
    M(decal_x)    M(decal_y)   M(decal_x_and_y)                    \
    M(check_decal_mask)                                            \
    M(negate_x)                                                    \
    M(bilinear_nx) M(bilinear_px) M(bilinear_ny) M(bilinear_py)    \
    M(bicubic_n3x) M(bicubic_n1x) M(bicubic_p1x) M(bicubic_p3x)    \
    M(bicubic_n3y) M(bicubic_n1y) M(bicubic_p1y) M(bicubic_p3y)    \
    M(save_xy) M(accumulate)                                       \
    M(clamp_x_1) M(mirror_x_1) M(repeat_x_1)                       \
    M(evenly_spaced_gradient)                                      \
    M(gradient)                                                    \
    M(evenly_spaced_2_stop_gradient)                               \
    M(xy_to_unit_angle)                                            \
    M(xy_to_radius)                                                \
    M(xy_to_2pt_conical_strip)                                     \
    M(xy_to_2pt_conical_focal_on_circle)                           \
    M(xy_to_2pt_conical_well_behaved)                              \
    M(xy_to_2pt_conical_smaller)                                   \
    M(xy_to_2pt_conical_greater)                                   \
    M(alter_2pt_conical_compensate_focal)                          \
    M(alter_2pt_conical_unswap)                                    \
    M(mask_2pt_conical_nan)                                        \
    M(mask_2pt_conical_degenerates) M(apply_vector_mask)           \
    M(byte_tables)                                                 \
    M(rgb_to_hsl) M(hsl_to_rgb)                                    \
    M(gauss_a_to_rgba)

class SkRasterPipeline {
public:
    explicit SkRasterPipeline(SkArenaAlloc*);

    SkRasterPipeline(const SkRasterPipeline&) = delete;
    SkRasterPipeline(SkRasterPipeline&&)      = default;

    SkRasterPipeline& operator=(const SkRasterPipeline&) = delete;
    SkRasterPipeline& operator=(SkRasterPipeline&&)      = default;

    void reset();

    enum StockStage {
    #define M(stage) stage,
        SK_RASTER_PIPELINE_STAGES(M)
    #undef M
    };
    void append(StockStage, void* = nullptr);
    void append(StockStage stage, const void* ctx) { this->append(stage, const_cast<void*>(ctx)); }
    // For raw functions (i.e. from a JIT).  Don't use this unless you know exactly what fn needs to
    // be. :)
    void append(void* fn, void* ctx);

    // Append all stages to this pipeline.
    void extend(const SkRasterPipeline&);

    // Runs the pipeline in 2d from (x,y) inclusive to (x+w,y+h) exclusive.
    void run(size_t x, size_t y, size_t w, size_t h) const;

    // Allocates a thunk which amortizes run() setup cost in alloc.
    std::function<void(size_t, size_t, size_t, size_t)> compile() const;

    void dump() const;

    // Appends a stage for the specified matrix.
    // Tries to optimize the stage by analyzing the type of matrix.
    void append_matrix(SkArenaAlloc*, const SkMatrix&);

    // Appends a stage for a constant uniform color.
    // Tries to optimize the stage based on the color.
    void append_constant_color(SkArenaAlloc*, const float rgba[4]);

    void append_constant_color(SkArenaAlloc* alloc, const SkPM4f& color) {
        this->append_constant_color(alloc, color.fVec);
    }
    void append_constant_color(SkArenaAlloc* alloc, const SkColor4f& color) {
        this->append_constant_color(alloc, color.vec());
    }

    // Like append_constant_color() but only affecting r,g,b, ignoring the alpha channel.
    void append_set_rgb(SkArenaAlloc*, const float rgb[3]);

    void append_set_rgb(SkArenaAlloc* alloc, const SkColor4f& color) {
        this->append_set_rgb(alloc, color.vec());
    }

    void append_load    (SkColorType, const SkJumper_MemoryCtx*);
    void append_load_dst(SkColorType, const SkJumper_MemoryCtx*);
    void append_store   (SkColorType, const SkJumper_MemoryCtx*);

    bool empty() const { return fStages == nullptr; }


private:
    struct StageList {
        StageList* prev;
        uint64_t   stage;
        void*      ctx;
        bool       rawFunction;
    };

    using StartPipelineFn = void(*)(size_t,size_t,size_t,size_t, void** program);
    StartPipelineFn build_pipeline(void**) const;

    void unchecked_append(StockStage, void*);

    SkArenaAlloc* fAlloc;
    StageList*    fStages;
    int           fNumStages;
    int           fSlotsNeeded;
};

template <size_t bytes>
class SkRasterPipeline_ : public SkRasterPipeline {
public:
    SkRasterPipeline_()
        : SkRasterPipeline(&fBuiltinAlloc) {}

private:
    SkSTArenaAlloc<bytes> fBuiltinAlloc;
};


#endif//SkRasterPipeline_DEFINED
