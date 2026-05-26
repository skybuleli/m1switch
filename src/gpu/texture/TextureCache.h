#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include <Metal/Metal.h>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <vector>
#include <functional>
#include <atomic>

enum class MaxwellPixelFormat : u32 {
    RGBA8Unorm      = 0x37,
    BGRA8Unorm      = 0x38,
    RGBA16Float     = 0x3A,
    RGBA32Float     = 0x3B,
    R8Unorm         = 0x1A,
    R16Float        = 0x1D,
    R32Float        = 0x1F,
    RG8Unorm        = 0x27,
    RG16Float       = 0x2E,
    RG32Float       = 0x30,
    RGBA16Unorm     = 0x39,
    RGB10A2Unorm    = 0x3D,
    R11G11B10Float  = 0x3E,
    RGBA8Snorm      = 0x44,
    RG8Snorm        = 0x28,
    R8Snorm         = 0x1B,
    BC1RGBAUnorm    = 0x59,
    BC2RGBAUnorm    = 0x5A,
    BC3RGBAUnorm    = 0x5B,
    BC4RUnorm       = 0x5C,
    BC5RGUnorm      = 0x5D,
    BC7RGBAUnorm    = 0x5F,
    ASTC4x4         = 0x64,
    ASTC6x6         = 0x67,
    ASTC8x8         = 0x69,
    Unknown         = 0xFF,
};

struct TextureInfo {
    u64 gpu_address = 0;
    u32 width = 0;
    u32 height = 0;
    u32 depth = 1;
    u32 mip_levels = 1;
    MaxwellPixelFormat format = MaxwellPixelFormat::Unknown;
    u32 tile_mode = 0;
    u32 array_mode = 0;
    u32 pitch = 0;
    bool is_linear = false;
};

struct SamplerInfo {
    u32 min_filter = 0;
    u32 mag_filter = 0;
    u32 wrap_u = 0;
    u32 wrap_v = 0;
    u32 wrap_w = 0;
    f32 min_lod = 0.0f;
    f32 max_lod = 1000.0f;
    f32 lod_bias = 0.0f;
    s32 anisotropy = 0;
    f32 border_color[4] = {};
};

struct CachedTexture {
    u64 cache_key = 0;
    TextureInfo info;
    id<MTLTexture> texture = nil;
    u64 frame_used = 0;
    u64 size_bytes = 0;
};

class TextureCache {
public:
    TextureCache(id<MTLDevice> device);
    ~TextureCache();

    id<MTLTexture> GetOrCreate(const TextureInfo& info, const u8* guest_memory);
    id<MTLSamplerState> GetOrCreateSampler(const SamplerInfo& info);

    SamplerInfo ParseTSC(const u8* sampler_pool, u64 offset) const;
    TextureInfo ParseTIC(const u8* texture_pool, u64 offset) const;

    // ── Invalidation ───────────────────────────────────────
    // Remove a single cached texture whose gpu_address matches.
    // Removal is lazy: the Metal texture reference is released,
    // and GetOrCreate will re-upload on the next lookup.
    void Invalidate(u64 gpu_address);

    // Remove all cached textures whose gpu_address range overlaps
    // the given [address, address+size) region.
    void InvalidateRegion(u64 address, u64 size);

    void EndFrame();
    void Flush();

    // Set the Memory instance for write stamp tracking and register
    // a write callback that invalidates overlapping cache entries.
    void SetMemory(Memory* mem);

    size_t Count() const { std::lock_guard l(mutex_); return entries_.size(); }
    size_t MemoryUsed() const { return total_memory_; }

private:
    u64 MakeKey(const TextureInfo& info) const;
    MTLPixelFormat ToMTLPixelFormat(MaxwellPixelFormat fmt) const;
    u32 BytesPerPixel(MaxwellPixelFormat fmt) const;
    bool DecodeBC1(const u8* src, u8* dst, u32 width, u32 height, u32 src_pitch);
    bool DecodeBC3(const u8* src, u8* dst, u32 width, u32 height, u32 src_pitch);
    bool DecodeBC4(const u8* src, u8* dst, u32 width, u32 height, u32 src_pitch);
    bool DecodeBC5(const u8* src, u8* dst, u32 width, u32 height, u32 src_pitch);
    bool DecodeBC7(const u8* src, u8* dst, u32 width, u32 height, u32 src_pitch);
    bool DecodeASTC(const u8* src, u8* dst, u32 width, u32 height, u32 block_w, u32 block_h, u32 src_pitch);
    void Untile(const u8* src, u8* dst, u32 width, u32 height, u32 bpp, u32 tile_mode, u32 pitch);
    id<MTLTexture> CreateTexture(const TextureInfo& info, const u8* guest_memory);

    u64 MakeSamplerKey(const SamplerInfo& info) const;

    id<MTLDevice> device_;
    Memory* memory_ = nullptr;
    mutable std::mutex mutex_;
    std::unordered_map<u64, CachedTexture> entries_;
    std::unordered_map<u64, id<MTLSamplerState>> sampler_cache_;
    u64 frame_count_ = 0;
    size_t total_memory_ = 0;

    // Address→key index for fast invalidation by gpu_address
    std::unordered_multimap<u64, u64> addr_index_;
    static constexpr size_t MAX_CACHE_MEMORY = 512 * 1024 * 1024;
    static constexpr u64 EVICTION_THRESHOLD = 60;   // frames before an unused entry becomes evictable
    static constexpr u32 MAX_CACHE_ENTRIES = 4096;  // soft cap on entry count
    static constexpr u32 EVICT_BATCH_SIZE = 128;    // evict this many entries per pass

    void EvictLRU();
    u32 ComputeSurfaceSize(u32 width, u32 height, u32 bpp, u32 tile_mode) const;
    u64 ComputeMipOffset(const TextureInfo& info, u32 mip_level) const;

    // Helpers for address index management
    void IndexAdd(u64 gpu_address, u64 cache_key);
    void IndexRemove(u64 gpu_address, u64 cache_key);
};
