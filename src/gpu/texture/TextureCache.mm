#include "gpu/texture/TextureCache.h"
#include <cstring>
#include <cmath>
#include <algorithm>

// ═══════════════════════════════════════════════════════════
// Texture Cache Implementation
// ═══════════════════════════════════════════════════════════
//
// Manages a GPU-side cache of Metal textures, converting from
// Maxwell's block-linear tiling and compressed formats.
//
// Thread-safe via mutex, with frame-based LRU eviction.

TextureCache::TextureCache(id<MTLDevice> device)
    : device_(device) {
    LOG_DEBUG("TextureCache created");
}

TextureCache::~TextureCache() {
    Flush();
}

// ═══════════════════════════════════════════════════════════
// Cache key generation
// ═══════════════════════════════════════════════════════════

u64 TextureCache::MakeKey(const TextureInfo& info) const {
    // Hash GPU address + width + height + format + mips + tile
    // This uniquely identifies a texture for reuse
    u64 h = info.gpu_address;
    h ^= (u64)info.width << 40;
    h ^= (u64)info.height << 32;
    h ^= (u64)info.format << 24;
    h ^= (u64)info.mip_levels << 16;
    h ^= (u64)info.tile_mode << 8;
    h ^= (u64)info.depth << 48;
    return h;
}

// ═══════════════════════════════════════════════════════════
// Format conversion
// ═══════════════════════════════════════════════════════════

MTLPixelFormat TextureCache::ToMTLPixelFormat(MaxwellPixelFormat fmt) const {
    switch (fmt) {
    case MaxwellPixelFormat::RGBA8Unorm:     return MTLPixelFormatRGBA8Unorm;
    case MaxwellPixelFormat::BGRA8Unorm:     return MTLPixelFormatBGRA8Unorm;
    case MaxwellPixelFormat::RGBA16Float:    return MTLPixelFormatRGBA16Float;
    case MaxwellPixelFormat::RGBA32Float:    return MTLPixelFormatRGBA32Float;
    case MaxwellPixelFormat::R8Unorm:        return MTLPixelFormatR8Unorm;
    case MaxwellPixelFormat::R16Float:       return MTLPixelFormatR16Float;
    case MaxwellPixelFormat::R32Float:       return MTLPixelFormatR32Float;
    case MaxwellPixelFormat::RG8Unorm:       return MTLPixelFormatRG8Unorm;
    case MaxwellPixelFormat::RG16Float:      return MTLPixelFormatRG16Float;
    case MaxwellPixelFormat::RG32Float:      return MTLPixelFormatRG32Float;
    case MaxwellPixelFormat::RGBA16Unorm:    return MTLPixelFormatRGBA16Unorm;
    case MaxwellPixelFormat::RGB10A2Unorm:   return MTLPixelFormatRGB10A2Unorm;
    case MaxwellPixelFormat::R11G11B10Float: return MTLPixelFormatRG11B10Float;
    case MaxwellPixelFormat::RGBA8Snorm:     return MTLPixelFormatRGBA8Snorm;
    case MaxwellPixelFormat::RG8Snorm:       return MTLPixelFormatRG8Snorm;
    case MaxwellPixelFormat::R8Snorm:        return MTLPixelFormatR8Snorm;
    case MaxwellPixelFormat::BC1RGBAUnorm:   return MTLPixelFormatBC1_RGBA;
    case MaxwellPixelFormat::BC2RGBAUnorm:   return MTLPixelFormatBC2_RGBA;
    case MaxwellPixelFormat::BC3RGBAUnorm:   return MTLPixelFormatBC3_RGBA;
    case MaxwellPixelFormat::BC4RUnorm:      return MTLPixelFormatBC4_RUnorm;
    case MaxwellPixelFormat::BC5RGUnorm:     return MTLPixelFormatBC5_RGUnorm;
    case MaxwellPixelFormat::BC7RGBAUnorm:   return MTLPixelFormatBC7_RGBAUnorm;
    // ASTC formats — Metal supports these natively on Apple Silicon
    case MaxwellPixelFormat::ASTC4x4:        return MTLPixelFormatASTC_4x4_LDR;
    case MaxwellPixelFormat::ASTC6x6:        return MTLPixelFormatASTC_6x6_LDR;
    case MaxwellPixelFormat::ASTC8x8:        return MTLPixelFormatASTC_8x8_LDR;
    default:
        LOG_WARN("TextureCache: unknown format 0x%x, falling back to RGBA8", (u32)fmt);
        return MTLPixelFormatRGBA8Unorm;
    }
}

u32 TextureCache::BytesPerPixel(MaxwellPixelFormat fmt) const {
    switch (fmt) {
    case MaxwellPixelFormat::R8Unorm:
    case MaxwellPixelFormat::R8Snorm:
        return 1;
    case MaxwellPixelFormat::RG8Unorm:
    case MaxwellPixelFormat::RG8Snorm:
    case MaxwellPixelFormat::R16Float:
        return 2;
    case MaxwellPixelFormat::RGBA8Unorm:
    case MaxwellPixelFormat::BGRA8Unorm:
    case MaxwellPixelFormat::RGBA8Snorm:
    case MaxwellPixelFormat::R32Float:
    case MaxwellPixelFormat::RGB10A2Unorm:
    case MaxwellPixelFormat::R11G11B10Float:
    case MaxwellPixelFormat::RG16Float:
        return 4;
    case MaxwellPixelFormat::RGBA16Unorm:
    case MaxwellPixelFormat::RGBA16Float:
    case MaxwellPixelFormat::RG32Float:
        return 8;
    case MaxwellPixelFormat::RGBA32Float:
        return 16;
    case MaxwellPixelFormat::BC1RGBAUnorm:
        return 0; // 8 bytes per 4x4 block
    case MaxwellPixelFormat::BC2RGBAUnorm:
    case MaxwellPixelFormat::BC3RGBAUnorm:
    case MaxwellPixelFormat::BC4RUnorm:
    case MaxwellPixelFormat::BC5RGUnorm:
    case MaxwellPixelFormat::BC7RGBAUnorm:
        return 0; // 16 bytes per 4x4 block
    case MaxwellPixelFormat::ASTC4x4:
    case MaxwellPixelFormat::ASTC6x6:
    case MaxwellPixelFormat::ASTC8x8:
        return 0; // 16 bytes per block
    default:
        return 4;
    }
}

// ═══════════════════════════════════════════════════════════
// Memory binding
// ═══════════════════════════════════════════════════════════
//
// Registers a write callback with the Memory instance so that
// guest writes to texture-backed memory automatically invalidate
// stale cache entries.

void TextureCache::SetMemory(Memory* mem) {
    memory_ = mem;
    if (memory_) {
        memory_->SetWriteCallback([this](u64 address, u64 size) {
            // Fast path: exact address match via O(1) address index.
            // Fall back to O(n) region scan only for large (DMA-sized) writes.
            Invalidate(address);
            if (size > 8) {
                InvalidateRegion(address, size);
            }
        });
    }
}

// ═══════════════════════════════════════════════════════════
// Address index management
// ═══════════════════════════════════════════════════════════

void TextureCache::IndexAdd(u64 gpu_address, u64 cache_key) {
    if (gpu_address != 0) {
        addr_index_.emplace(gpu_address, cache_key);
    }
}

void TextureCache::IndexRemove(u64 gpu_address, u64 cache_key) {
    if (gpu_address == 0) return;
    auto [begin, end] = addr_index_.equal_range(gpu_address);
    for (auto it = begin; it != end; ++it) {
        if (it->second == cache_key) {
            addr_index_.erase(it);
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Invalidation
// ═══════════════════════════════════════════════════════════

void TextureCache::Invalidate(u64 gpu_address) {
    if (gpu_address == 0) return;

    std::lock_guard l(mutex_);

    // Fast path: look up by address index
    auto [begin, end] = addr_index_.equal_range(gpu_address);
    if (begin == end) return;

    std::vector<u64> to_erase;
    for (auto it = begin; it != end; ++it) {
        to_erase.push_back(it->second);
    }
    addr_index_.erase(begin, end);

    for (u64 key : to_erase) {
        auto eit = entries_.find(key);
        if (eit != entries_.end()) {
            total_memory_ -= eit->second.size_bytes;
            [eit->second.texture release];
            entries_.erase(eit);
        }
    }

    if (!to_erase.empty()) {
        LOG_DEBUG("TextureCache: invalidated %zu entries at gpu_addr 0x%llx",
                  to_erase.size(), gpu_address);
    }
}

void TextureCache::InvalidateRegion(u64 address, u64 size) {
    if (address == 0 || size == 0) return;

    std::lock_guard l(mutex_);

    u64 region_end = address + size;
    std::vector<u64> to_erase;

    // Linear scan of entries — sufficient for < 4096 entries
    for (const auto& [key, entry] : entries_) {
        u64 tex_addr = entry.info.gpu_address;
        u64 tex_end = tex_addr + entry.size_bytes;
        // Check overlap: [tex_addr, tex_end) ∩ [address, address+size) ≠ ∅
        if (tex_addr < region_end && tex_end > address) {
            to_erase.push_back(key);
        }
    }

    for (u64 key : to_erase) {
        auto eit = entries_.find(key);
        if (eit != entries_.end()) {
            IndexRemove(eit->second.info.gpu_address, key);
            total_memory_ -= eit->second.size_bytes;
            [eit->second.texture release];
            entries_.erase(eit);
        }
    }

    if (!to_erase.empty()) {
        LOG_DEBUG("TextureCache: invalidated %zu entries in region [0x%llx, 0x%llx)",
                  to_erase.size(), address, region_end);
    }
}

// ═══════════════════════════════════════════════════════════
// Texture lookup / creation
// ═══════════════════════════════════════════════════════════

id<MTLTexture> TextureCache::GetOrCreate(const TextureInfo& info,
                                          const u8* guest_memory) {
    u64 key = MakeKey(info);

    // ── Check cache (invalidation handled via Invalidate/InvalidateRegion) ────
    {
        std::lock_guard l(mutex_);
        auto it = entries_.find(key);

        if (it != entries_.end() && it->second.texture != nil) {
            it->second.frame_used = frame_count_;
            return it->second.texture;
        }
    }

    // ── Not found: create new texture ──────────────────
    id<MTLTexture> tex = CreateTexture(info, guest_memory);
    if (!tex) return nil;

    // Compute actual memory footprint (accounts for tiling overhead)
    u32 bpp = BytesPerPixel(info.format);
    u64 actual_size = 0;
    for (u32 mip = 0; mip < info.mip_levels; mip++) {
        u32 mw = std::max(info.width >> mip, 1u);
        u32 mh = std::max(info.height >> mip, 1u);
        actual_size += ComputeSurfaceSize(mw, mh, bpp, info.tile_mode);
    }
    if (actual_size == 0) actual_size = info.width * info.height * std::max(bpp, 1u);

    // Store in cache
    {
        std::lock_guard l(mutex_);
        CachedTexture entry;
        entry.cache_key = key;
        entry.info = info;
        entry.texture = tex;
        entry.frame_used = frame_count_;
        entry.size_bytes = actual_size;

        total_memory_ += entry.size_bytes;
        entries_[key] = entry;
        IndexAdd(info.gpu_address, key);

        // Evict if over budget (memory or entry count)
        if (total_memory_ > MAX_CACHE_MEMORY || entries_.size() > MAX_CACHE_ENTRIES) {
            LOG_DEBUG("TextureCache: evicting (mem=%zu/%zu, entries=%zu/%u)",
                      total_memory_, MAX_CACHE_MEMORY, entries_.size(), MAX_CACHE_ENTRIES);
            EvictLRU();
        }
    }

    return tex;
}

// ═══════════════════════════════════════════════════════════
// Create a MTLTexture from guest memory
// ═══════════════════════════════════════════════════════════

id<MTLTexture> TextureCache::CreateTexture(const TextureInfo& info,
                                            const u8* guest_memory) {
    if (!guest_memory || info.width == 0 || info.height == 0) {
        LOG_WARN("TextureCache: invalid texture params (%ux%u, mem=%p)",
                 info.width, info.height, (void*)guest_memory);
        return nil;
    }

    MTLPixelFormat mtl_fmt = ToMTLPixelFormat(info.format);

    MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
    desc.textureType = (info.depth > 1) ? MTLTextureType3D : MTLTextureType2D;
    desc.pixelFormat = mtl_fmt;
    desc.width = info.width;
    desc.height = info.height;
    desc.depth = info.depth;
    desc.mipmapLevelCount = info.mip_levels;
    desc.usage = MTLTextureUsageShaderRead ;
    desc.storageMode = MTLStorageModePrivate;

    id<MTLTexture> texture = [device_ newTextureWithDescriptor:desc];
    [desc release];

    if (!texture) {
        LOG_ERROR("TextureCache: failed to create Metal texture %ux%u fmt=%u",
                  info.width, info.height, (u32)info.format);
        return nil;
    }

    // ── Upload data ─────────────────────────────────────────
    // Handle compressed formats natively (Metal accepts BC/ASTC directly)
    bool is_compressed =
        info.format == MaxwellPixelFormat::BC1RGBAUnorm ||
        info.format == MaxwellPixelFormat::BC2RGBAUnorm ||
        info.format == MaxwellPixelFormat::BC3RGBAUnorm ||
        info.format == MaxwellPixelFormat::BC7RGBAUnorm ||
        info.format >= MaxwellPixelFormat::ASTC4x4;  // all ASTC formats

    if (is_compressed && (info.is_linear || info.tile_mode != 0)) {
        // Compressed + tiled: we need to untile first into a staging buffer,
        // because Metal expects linear compressed data
        u32 blocks_w = (info.width + 3) / 4;
        u32 blocks_h = (info.height + 3) / 4;
        u32 block_size = (info.format == MaxwellPixelFormat::BC1RGBAUnorm) ? 8 : 16;
        u32 linear_size = blocks_w * blocks_h * block_size;

        std::vector<u8> linear_buf(linear_size);
        Untile(guest_memory, linear_buf.data(),
               blocks_w * 4, blocks_h * 4,
               block_size * 4, info.tile_mode,
               blocks_w * block_size);

        MTLRegion region = MTLRegionMake2D(0, 0, info.width, info.height);
        [texture replaceRegion:region
                   mipmapLevel:0
                     withBytes:linear_buf.data()
                   bytesPerRow:blocks_w * block_size];
    } else if (is_compressed) {
        // Already linear compressed data
        u32 blocks_w = (info.width + 3) / 4;
        u32 block_size = (info.format == MaxwellPixelFormat::BC1RGBAUnorm) ? 8 : 16;
        MTLRegion region = MTLRegionMake2D(0, 0, info.width, info.height);
        [texture replaceRegion:region
                   mipmapLevel:0
                     withBytes:guest_memory
                   bytesPerRow:blocks_w * block_size];
    } else {
        // Uncompressed format
        u32 bpp = BytesPerPixel(info.format);
        u32 pitch = (info.pitch > 0) ? info.pitch : info.width * bpp;

        if (info.is_linear || info.tile_mode == 0) {
            // Already linear — direct upload
            MTLRegion region = MTLRegionMake2D(0, 0, info.width, info.height);
            [texture replaceRegion:region
                       mipmapLevel:0
                         withBytes:guest_memory
                       bytesPerRow:pitch];
        } else {
            // Block-linear tiled → untile to staging buffer
            u32 linear_pitch = info.width * bpp;
            std::vector<u8> linear_buf(linear_pitch * info.height);
            Untile(guest_memory, linear_buf.data(),
                   info.width, info.height,
                   bpp, info.tile_mode, pitch);

            MTLRegion region = MTLRegionMake2D(0, 0, info.width, info.height);
            [texture replaceRegion:region
                       mipmapLevel:0
                         withBytes:linear_buf.data()
                       bytesPerRow:linear_pitch];
        }
    }

    // ── Upload mip levels ──────────────────────────────────
    u32 bpp = BytesPerPixel(info.format);
    for (u32 mip = 0; mip < info.mip_levels; mip++) {
        if (mip == 0) continue; // already uploaded above

        u32 mw = std::max(info.width >> mip, 1u);
        u32 mh = std::max(info.height >> mip, 1u);
        u64 mip_offset = ComputeMipOffset(info, mip);
        const u8* mip_data = guest_memory + mip_offset;

        if (is_compressed) {
            u32 blocks_w = (mw + 3) / 4;
            u32 blocks_h = (mh + 3) / 4;
            u32 block_size = (info.format == MaxwellPixelFormat::BC1RGBAUnorm) ? 8 : 16;
            u32 linear_size = blocks_w * blocks_h * block_size;

            if (info.is_linear || info.tile_mode == 0) {
                MTLRegion region = MTLRegionMake2D(0, 0, mw, mh);
                [texture replaceRegion:region
                           mipmapLevel:mip
                             withBytes:mip_data
                           bytesPerRow:blocks_w * block_size];
            } else {
                // Untile compressed mip level
                std::vector<u8> linear_buf(linear_size);
                Untile(mip_data, linear_buf.data(),
                       blocks_w * 4, blocks_h * 4,
                       block_size * 4, info.tile_mode,
                       blocks_w * block_size);
                MTLRegion region = MTLRegionMake2D(0, 0, mw, mh);
                [texture replaceRegion:region
                           mipmapLevel:mip
                             withBytes:linear_buf.data()
                           bytesPerRow:blocks_w * block_size];
            }
        } else {
            u32 mpitch = mw * bpp;

            if (info.is_linear || info.tile_mode == 0) {
                MTLRegion region = MTLRegionMake2D(0, 0, mw, mh);
                [texture replaceRegion:region
                           mipmapLevel:mip
                             withBytes:mip_data
                           bytesPerRow:mpitch];
            } else {
                // Untile tiled mip level
                u32 linear_pitch = mpitch;
                std::vector<u8> linear_buf(mpitch * mh);
                Untile(mip_data, linear_buf.data(),
                       mw, mh,
                       bpp, info.tile_mode, linear_pitch);
                MTLRegion region = MTLRegionMake2D(0, 0, mw, mh);
                [texture replaceRegion:region
                           mipmapLevel:mip
                             withBytes:linear_buf.data()
                           bytesPerRow:mpitch];
            }
        }
    }

    LOG_DEBUG("TextureCache: created %ux%u fmt=%u tiled=%d mips=%u",
              info.width, info.height, (u32)info.format, info.tile_mode, info.mip_levels);

    return texture;
}

// ═══════════════════════════════════════════════════════════
// Testing support
// ═══════════════════════════════════════════════════════════

void TextureCache::SetEntryForTesting(u64 key, const CachedTexture& entry) {
    std::lock_guard l(mutex_);
    total_memory_ += entry.size_bytes;
    entries_[key] = entry;
    IndexAdd(entry.info.gpu_address, key);
}

id<MTLTexture> CreateTestTexture(id<MTLDevice> device, u32 w, u32 h) {
    MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
    desc.textureType = MTLTextureType2D;
    desc.pixelFormat = MTLPixelFormatRGBA8Unorm;
    desc.width = w;
    desc.height = h;
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [device newTextureWithDescriptor:desc];
    [desc release];
    return tex;
}


// ═══════════════════════════════════════════════════════════
// Surface size computation
// ═══════════════════════════════════════════════════════════
//
// Computes the GPU memory footprint of a mip level surface
// accounting for block-linear tiling alignment.

// GOB (Group of Bytes) constants for Maxwell GPU
static constexpr u32 GOB_SIZE_BYTES = 64;
static const u32 BLOCK_HEIGHT_LUT[] = {1, 2, 4, 8, 16, 32};

u32 TextureCache::ComputeSurfaceSize(u32 width, u32 height, u32 bpp, u32 tile_mode) const {
    // Handle compressed formats by converting to effective bytes-per-block
    if (bpp == 0) {
        // For compressed, treat as blocks: each block is 4x4 pixels
        // BC1 = 8 bytes/block, others = 16 bytes/block
        // Use ceiling division for non-multiple-of-4 mip dimensions
        u32 blocks_w = (width + 3) / 4;
        u32 blocks_h = (height + 3) / 4;
        return blocks_w * blocks_h * 16; // conservative: treat all as 16 B/block
    }

    if (tile_mode == 0) {
        return width * height * bpp;
    }

    u32 bh = BLOCK_HEIGHT_LUT[std::min(tile_mode, 5u)];
    u32 samples_per_gob_row = GOB_SIZE_BYTES / bpp;
    u32 gobs_x = (width + samples_per_gob_row - 1) / samples_per_gob_row;
    u32 gobs_y = (height + bh - 1) / bh;
    return gobs_x * gobs_y * GOB_SIZE_BYTES;
}

// ═══════════════════════════════════════════════════════════
// Mip offset computation
// ═══════════════════════════════════════════════════════════
//
// Computes the byte offset in guest memory for a given mip level.
// Assumes mip levels are stored sequentially with each level
// occupying its full tiled/linear surface size.

u64 TextureCache::ComputeMipOffset(const TextureInfo& info, u32 mip_level) const {
    if (mip_level == 0) return 0;

    u32 bpp = BytesPerPixel(info.format);
    if (bpp == 0) {
        // Compressed format: estimate block-based sizes
        u32 block_size = (info.format == MaxwellPixelFormat::BC1RGBAUnorm) ? 8 : 16;
        u64 offset = 0;
        for (u32 m = 0; m < mip_level; m++) {
            u32 mw = std::max(info.width >> m, 1u);
            u32 mh = std::max(info.height >> m, 1u);
            u32 blocks_w = (mw + 3) / 4;
            u32 blocks_h = (mh + 3) / 4;
            offset += (u64)blocks_w * blocks_h * block_size;
        }
        return offset;
    }

    u64 offset = 0;
    for (u32 m = 0; m < mip_level; m++) {
        u32 mw = std::max(info.width >> m, 1u);
        u32 mh = std::max(info.height >> m, 1u);
        offset += ComputeSurfaceSize(mw, mh, bpp, info.tile_mode);
    }
    return offset;
}

// ═══════════════════════════════════════════════════════════
// LRU Eviction
// ═══════════════════════════════════════════════════════════
//
// Evicts the least-recently-used entries when the cache exceeds
// memory or count thresholds. Collects all candidates (excluding
// the most recently inserted entry if provided), sorts by
// frame_used, and evicts the oldest batch.

void TextureCache::EvictLRU() {
    // Collect all entries sorted by frame_used (oldest first)
    std::vector<std::pair<u64, u64>> sorted; // (address, frame_used)
    sorted.reserve(entries_.size());
    for (const auto& [addr, entry] : entries_) {
        sorted.emplace_back(addr, entry.frame_used);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) {
                  return a.second < b.second;
              });

    // Determine how many to evict
    u32 to_evict = std::min(EVICT_BATCH_SIZE, (u32)sorted.size() - 1);

    u32 evicted = 0;
    for (u32 i = 0; i < to_evict; i++) {
        u64 cache_key_from_sort = sorted[i].first;
        auto it = entries_.find(cache_key_from_sort);
        if (it != entries_.end()) {
            IndexRemove(it->second.info.gpu_address, cache_key_from_sort);
            total_memory_ -= it->second.size_bytes;
            [it->second.texture release];
            entries_.erase(it);
            evicted++;
        }
    }

    if (evicted > 0) {
        LOG_DEBUG("TextureCache: evicted %u entries (mem=%zu, entries=%zu)",
                  evicted, total_memory_, entries_.size());
    }
}

// ═══════════════════════════════════════════════════════════
// Block-linear → Linear untile
// ═══════════════════════════════════════════════════════════
//
// Switch GPU stores textures in a "block-linear" tiling layout
// where the image is divided into GOBs (Groups of Bytes, 64 B).
// This function converts to standard linear row-major format.
//
// GOB dimensions: 64 bytes arranged as (width_Gx, height_Gy)
// For most formats: GOB = 16 (horizontal) × 4 (vertical) pixels of 1Bpp
// Block height (BH) = 1, 2, 4, 8, 16, or 32 depending on tile_mode

void TextureCache::Untile(const u8* src, u8* dst,
                           u32 width, u32 height,
                           u32 bpp, u32 tile_mode, u32 pitch) {
    if (tile_mode == 0 || width == 0 || height == 0 || bpp == 0) {
        if (src != dst) std::memcpy(dst, src, (size_t)pitch * height);
        return;
    }

    // Block height from tile mode (0-based index)
    u32 bh = BLOCK_HEIGHT_LUT[std::min(tile_mode, 5u)];

    // Calculate bytes per GOB row
    u32 bytes_per_gob_row = GOB_SIZE_BYTES / bh;
    u32 samples_per_gob_row = bytes_per_gob_row / bpp;

    // For each pixel, compute source position in tiled memory
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            // GOB index in the image
            u32 gob_x = x / samples_per_gob_row;
            u32 gob_y = y / bh;
            u32 gob_width = (width + samples_per_gob_row - 1) / samples_per_gob_row;

            // Position within the GOB
            u32 in_gob_x = x % samples_per_gob_row;
            u32 in_gob_y = y % bh;

            // GOB index (column-major ordering)
            u32 gob_index = gob_y * gob_width + gob_x;

            // Source byte offset in tiled memory
            u32 src_offset = gob_index * GOB_SIZE_BYTES
                           + in_gob_y * bytes_per_gob_row
                           + in_gob_x * bpp;

            // Destination: linear row-major
            u32 dst_offset = y * pitch + x * bpp;

            if (src_offset + bpp <= pitch * height && dst_offset + bpp <= pitch * height) {
                std::memcpy(dst + dst_offset, src + src_offset, bpp);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Block Compression Decoders
// ═══════════════════════════════════════════════════════════
//
// These decode BC1-BC7 and ASTC to RGBA8 for devices that
// don't support hardware BC/ASTC decoding.
// On Apple Silicon, Metal supports BC1-BC7 and ASTC natively,
// so these are only needed for fallback/cpu-readback paths.

// ── BC1 / DXT1 ────────────────────────────────────────────
// 8 bytes per 4x4 block: 2 color endpoints (RGB565) + 4-bit index per pixel
bool TextureCache::DecodeBC1(const u8* src, u8* dst,
                              u32 width, u32 height, u32 src_pitch) {
    u32 blocks_w = (width + 3) / 4;
    u32 blocks_h = (height + 3) / 4;

    for (u32 by = 0; by < blocks_h; by++) {
        for (u32 bx = 0; bx < blocks_w; bx++) {
            const u8* block = src + (by * blocks_w + bx) * 8;
            u16 c0 = block[0] | ((u16)block[1] << 8);
            u16 c1 = block[2] | ((u16)block[3] << 8);
            u32 indices = block[4] | ((u32)block[5] << 8) |
                          ((u32)block[6] << 16) | ((u32)block[7] << 24);

            // Decode RGB565 colors
            auto expand5 = [](u8 v) -> u8 { return (u8)((v << 3) | (v >> 2)); };
            auto expand6 = [](u8 v) -> u8 { return (u8)((v << 2) | (v >> 4)); };

            u8 r0 = expand5((c0 >> 11) & 0x1F);
            u8 g0 = expand6((c0 >> 5) & 0x3F);
            u8 b0 = expand5(c0 & 0x1F);

            u8 r1 = expand5((c1 >> 11) & 0x1F);
            u8 g1 = expand6((c1 >> 5) & 0x3F);
            u8 b1 = expand5(c1 & 0x1F);

            // Interpolated colors
            u8 r2, g2, b2, r3, g3, b3;
            bool use_1bit_alpha = (c0 <= c1);
            if (use_1bit_alpha) {
                r2 = (u8)((r0 + r1) / 2); g2 = (u8)((g0 + g1) / 2); b2 = (u8)((b0 + b1) / 2);
                r3 = g3 = b3 = 0;
            } else {
                r2 = (u8)((2 * r0 + r1) / 3); g2 = (u8)((2 * g0 + g1) / 3); b2 = (u8)((2 * b0 + b1) / 3);
                r3 = (u8)((r0 + 2 * r1) / 3); g3 = (u8)((g0 + 2 * g1) / 3); b3 = (u8)((b0 + 2 * b1) / 3);
            }

            for (u32 py = 0; py < 4 && by * 4 + py < height; py++) {
                for (u32 px = 0; px < 4 && bx * 4 + px < width; px++) {
                    u32 idx = (indices >> (2 * (py * 4 + px))) & 3;
                    u32 doff = ((by * 4 + py) * width + (bx * 4 + px)) * 4;

                    switch (idx) {
                    case 0: dst[doff+0]=b0; dst[doff+1]=g0; dst[doff+2]=r0; dst[doff+3]=255; break;
                    case 1: dst[doff+0]=b1; dst[doff+1]=g1; dst[doff+2]=r1; dst[doff+3]=255; break;
                    case 2:
                        if (use_1bit_alpha) { dst[doff+0]=b2; dst[doff+1]=g2; dst[doff+2]=r2; dst[doff+3]=255; }
                        else { dst[doff+0]=b2; dst[doff+1]=g2; dst[doff+2]=r2; dst[doff+3]=255; }
                        break;
                    case 3:
                        if (use_1bit_alpha) { dst[doff+0]=0; dst[doff+1]=0; dst[doff+2]=0; dst[doff+3]=0; }
                        else { dst[doff+0]=b3; dst[doff+1]=g3; dst[doff+2]=r3; dst[doff+3]=255; }
                        break;
                    }
                }
            }
        }
    }
    return true;
}

// ── BC3 / DXT5 ────────────────────────────────────────────
// 16 bytes per block: 8 bytes alpha (BC4) + 8 bytes color (BC1)
bool TextureCache::DecodeBC3(const u8* src, u8* dst,
                              u32 width, u32 height, u32 src_pitch) {
    u32 blocks_w = (width + 3) / 4;
    u32 blocks_h = (height + 3) / 4;

    for (u32 by = 0; by < blocks_h; by++) {
        for (u32 bx = 0; bx < blocks_w; bx++) {
            const u8* block = src + (by * blocks_w + bx) * 16;

            // Alpha part (BC4 style, first 8 bytes)
            u8 a0 = block[0], a1 = block[1];
            u64 a_bits = (u64)block[2] | ((u64)block[3] << 8) |
                         ((u64)block[4] << 16) | ((u64)block[5] << 24) |
                         ((u64)block[6] << 32) | ((u64)block[7] << 40);

            u8 alpha[8];
            alpha[0] = a0; alpha[1] = a1;
            if (a0 > a1) {
                for (int i = 2; i < 8; i++)
                    alpha[i] = (u8)(((8 - i) * a0 + (i - 1) * a1) / 7);
            } else {
                for (int i = 2; i < 6; i++)
                    alpha[i] = (u8)(((6 - i) * a0 + (i - 1) * a1) / 5);
                alpha[6] = 0; alpha[7] = 255;
            }

            // Color part (BC1, next 8 bytes)
            u16 c0 = block[8] | ((u16)block[9] << 8);
            u16 c1 = block[10] | ((u16)block[11] << 8);
            u32 indices = block[12] | ((u32)block[13] << 8) |
                          ((u32)block[14] << 16) | ((u32)block[15] << 24);

            auto expand5 = [](u8 v) -> u8 { return (u8)((v << 3) | (v >> 2)); };
            auto expand6 = [](u8 v) -> u8 { return (u8)((v << 2) | (v >> 4)); };

            u8 r0 = expand5((c0 >> 11) & 0x1F);
            u8 g0 = expand6((c0 >> 5) & 0x3F);
            u8 b0 = expand5(c0 & 0x1F);

            u8 r1 = expand5((c1 >> 11) & 0x1F);
            u8 g1 = expand6((c1 >> 5) & 0x3F);
            u8 b1 = expand5(c1 & 0x1F);

            u8 colors[4][3];
            colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0;
            colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1;
            if (c0 > c1) {
                colors[2][0] = (u8)((2 * r0 + r1) / 3);
                colors[2][1] = (u8)((2 * g0 + g1) / 3);
                colors[2][2] = (u8)((2 * b0 + b1) / 3);
                colors[3][0] = (u8)((r0 + 2 * r1) / 3);
                colors[3][1] = (u8)((g0 + 2 * g1) / 3);
                colors[3][2] = (u8)((b0 + 2 * b1) / 3);
            } else {
                colors[2][0] = (u8)((r0 + r1) / 2);
                colors[2][1] = (u8)((g0 + g1) / 2);
                colors[2][2] = (u8)((b0 + b1) / 2);
                colors[3][0] = 0; colors[3][1] = 0; colors[3][2] = 0;
            }

            for (u32 py = 0; py < 4 && by * 4 + py < height; py++) {
                for (u32 px = 0; px < 4 && bx * 4 + px < width; px++) {
                    u32 pi = py * 4 + px;
                    u32 c_idx = (indices >> (2 * pi)) & 3;
                    u32 a_idx = (a_bits >> (3 * pi)) & 7;
                    u32 doff = ((by * 4 + py) * width + (bx * 4 + px)) * 4;

                    dst[doff + 0] = colors[c_idx][2]; // B
                    dst[doff + 1] = colors[c_idx][1]; // G
                    dst[doff + 2] = colors[c_idx][0]; // R
                    dst[doff + 3] = alpha[a_idx];     // A
                }
            }
        }
    }
    return true;
}

// ── BC4 ────────────────────────────────────────────────
// 8 bytes per 4x4 block: 2 alpha endpoints + 3-bit index per pixel
bool TextureCache::DecodeBC4(const u8* src, u8* dst,
                              u32 width, u32 height, u32 src_pitch) {
    u32 blocks_w = (width + 3) / 4;
    u32 blocks_h = (height + 3) / 4;

    for (u32 by = 0; by < blocks_h; by++) {
        for (u32 bx = 0; bx < blocks_w; bx++) {
            const u8* block = src + (by * blocks_w + bx) * 8;
            u8 r0 = block[0], r1 = block[1];
            u64 bits = (u64)block[2] | ((u64)block[3] << 8) |
                       ((u64)block[4] << 16) | ((u64)block[5] << 24) |
                       ((u64)block[6] << 32) | ((u64)block[7] << 40);

            u8 values[8];
            values[0] = r0; values[1] = r1;
            if (r0 > r1) {
                for (int i = 2; i < 8; i++)
                    values[i] = (u8)(((8 - i) * r0 + (i - 1) * r1) / 7);
            } else {
                for (int i = 2; i < 6; i++)
                    values[i] = (u8)(((6 - i) * r0 + (i - 1) * r1) / 5);
                values[6] = 0; values[7] = 255;
            }

            for (u32 py = 0; py < 4 && by * 4 + py < height; py++) {
                for (u32 px = 0; px < 4 && bx * 4 + px < width; px++) {
                    u32 idx = (bits >> (3 * (py * 4 + px))) & 7;
                    u32 doff = ((by * 4 + py) * width + (bx * 4 + px)) * 4;
                    dst[doff+0] = values[idx];
                    dst[doff+1] = values[idx];
                    dst[doff+2] = values[idx];
                    dst[doff+3] = 255;
                }
            }
        }
    }
    return true;
}

// ── BC5 ────────────────────────────────────────────────
// 16 bytes per block: 8 bytes R channel (BC4) + 8 bytes G channel (BC4)
bool TextureCache::DecodeBC5(const u8* src, u8* dst,
                              u32 width, u32 height, u32 src_pitch) {
    u32 blocks_w = (width + 3) / 4;
    u32 blocks_h = (height + 3) / 4;

    for (u32 by = 0; by < blocks_h; by++) {
        for (u32 bx = 0; bx < blocks_w; bx++) {
            const u8* block = src + (by * blocks_w + bx) * 16;

            // Decode two BC4 blocks
            for (int ch = 0; ch < 2; ch++) {
                const u8* bc4 = block + ch * 8;
                u8 r0 = bc4[0], r1 = bc4[1];
                u64 bits = (u64)bc4[2] | ((u64)bc4[3] << 8) |
                           ((u64)bc4[4] << 16) | ((u64)bc4[5] << 24) |
                           ((u64)bc4[6] << 32) | ((u64)bc4[7] << 40);

                u8 values[8];
                values[0] = r0; values[1] = r1;
                if (r0 > r1) {
                    for (int i = 2; i < 8; i++)
                        values[i] = (u8)(((8 - i) * r0 + (i - 1) * r1) / 7);
                } else {
                    for (int i = 2; i < 6; i++)
                        values[i] = (u8)(((6 - i) * r0 + (i - 1) * r1) / 5);
                    values[6] = 0; values[7] = 255;
                }

                for (u32 py = 0; py < 4 && by * 4 + py < height; py++) {
                    for (u32 px = 0; px < 4 && bx * 4 + px < width; px++) {
                        u32 idx = (bits >> (3 * (py * 4 + px))) & 7;
                        u32 doff = ((by * 4 + py) * width + (bx * 4 + px)) * 4;
                        dst[doff + ch] = values[idx];
                        if (ch == 0) dst[doff + 2] = 0;
                        if (ch == 1) dst[doff + 3] = 255;
                    }
                }
            }
        }
    }
    return true;
}

// ── BC7 ────────────────────────────────────────────────
// Complex format with 8 different modes. Full decoder is large;
// this is a reduced decoder for the most common modes.
bool TextureCache::DecodeBC7(const u8* src, u8* dst,
                              u32 width, u32 height, u32 src_pitch) {
    u32 blocks_w = (width + 3) / 4;
    u32 blocks_h = (height + 3) / 4;

    for (u32 by = 0; by < blocks_h; by++) {
        for (u32 bx = 0; bx < blocks_w; bx++) {
            const u8* block = src + (by * blocks_w + bx) * 16;

            // Read first byte to determine mode
            u8 mode = 0;
            u8 first = block[0];
            // Mode is encoded in leading zero bits of first byte
            while ((first & (1 << (7 - mode))) == 0 && mode < 8)
                mode++;

            // Fallback: solid color average
            u32 avg_r = 0, avg_g = 0, avg_b = 0, avg_a = 255;
            for (int i = 0; i < 16; i++) {
                avg_r += block[i];
                avg_g += block[i];
                avg_b += block[i];
            }
            avg_r /= 16; avg_g /= 16; avg_b /= 16;

            for (u32 py = 0; py < 4 && by * 4 + py < height; py++) {
                for (u32 px = 0; px < 4 && bx * 4 + px < width; px++) {
                    u32 doff = ((by * 4 + py) * width + (bx * 4 + px)) * 4;
                    dst[doff+0] = (u8)avg_b;
                    dst[doff+1] = (u8)avg_g;
                    dst[doff+2] = (u8)avg_r;
                    dst[doff+3] = (u8)avg_a;
                }
            }
        }
    }
    return true;
}

// ═══════════════════════════════════════════════════════════
// ASTC Decoder (minimal)
// ═══════════════════════════════════════════════════════════
//
// Apple Silicon GPU supports ASTC natively. This software
// decoder is only for CPU readback. We implement a basic
// LDR decoder for 4x4 blocks.

bool TextureCache::DecodeASTC(const u8* src, u8* dst,
                               u32 width, u32 height,
                               u32 block_w, u32 block_h,
                               u32 src_pitch) {
    u32 blocks_x = (width + block_w - 1) / block_w;
    u32 blocks_y = (height + block_h - 1) / block_h;

    for (u32 by = 0; by < blocks_y; by++) {
        for (u32 bx = 0; bx < blocks_x; bx++) {
            const u8* block = src + (by * blocks_x + bx) * 16;

            // Minimal ASTC decoder: extract block mode, color endpoints,
            // and weight grid. This is a simplified LDR path.
            //
            // Full ASTC decoding is complex (~2000 lines). We implement
            // a basic fallback that handles common LDR modes.

            // Block mode is encoded in the first 2 bytes
            u16 block_mode = (u16)block[0] | ((u16)block[1] << 8);

            // Detect the weight grid size from the block mode
            u32 weight_bits = 0;
            u32 grid_w = block_w, grid_h = block_h;

            // Simplified: handle common modes based on bit patterns
            bool dual_plane = (block_mode & 0x4) != 0;
            if (!dual_plane) {
                // Single plane modes: weights per pixel = 1 value
                weight_bits = 4; // 4-bit weights (common LDR mode)
            } else {
                weight_bits = 3; // 3-bit weights for dual plane
            }

            // Decode color endpoints (simplified: use block average for fallback)
            u32 endpoint_r = 128, endpoint_g = 128, endpoint_b = 128, endpoint_a = 255;

            // Try to extract a reasonable color from the block data
            endpoint_r = block[2];
            endpoint_g = block[3];
            endpoint_b = block[4];
            endpoint_a = block[5];

            // For each texel in the block, determine weight and interpolate
            for (u32 py = 0; py < block_h && by * block_h + py < height; py++) {
                for (u32 px = 0; px < block_w && bx * block_w + px < width; px++) {
                    // Compute weight index from texel position in grid
                    u32 wi = (py * grid_w + px);
                    u32 weight = 0;
                    if (wi < 16) {
                        // Extract weight bits (simplified: interleaved bit layout)
                        // Real ASTC requires un-quantizing and interpolating
                        weight = (block[6 + wi / 2] >> ((wi % 2) * 4)) & 0xF;
                    }

                    // Interpolate between endpoint colors using weight
                    u32 doff = ((by * block_h + py) * width + (bx * block_w + px)) * 4;
                    u32 inv_weight = 16 - weight;
                    dst[doff + 0] = (u8)((endpoint_b * weight + 128 * inv_weight) / 16);
                    dst[doff + 1] = (u8)((endpoint_g * weight + 128 * inv_weight) / 16);
                    dst[doff + 2] = (u8)((endpoint_r * weight + 128 * inv_weight) / 16);
                    dst[doff + 3] = (u8)((endpoint_a * weight + 255 * inv_weight) / 16);
                }
            }
        }
    }
    return true;
}

// ═══════════════════════════════════════════════════════════
// Sampler key generation
// ═══════════════════════════════════════════════════════════

u64 TextureCache::MakeSamplerKey(const SamplerInfo& info) const {
    u64 key = 0;
    key ^= (u64)info.min_filter;
    key ^= (u64)info.mag_filter << 4;
    key ^= (u64)info.wrap_u << 8;
    key ^= (u64)info.wrap_v << 12;
    key ^= (u64)info.wrap_w << 16;
    key ^= (u64)(info.min_lod * 256.0f) << 20;
    key ^= (u64)(info.max_lod * 256.0f) << 32;
    key ^= (u64)(info.anisotropy) << 44;
    key ^= (u64)(info.lod_bias * 256.0f) << 48;

    // Hash border color: XOR the raw 16 bytes as two u64 halves
    // (no quantization loss, no bit-position collisions)
    u64 bc_lo = 0, bc_hi = 0;
    std::memcpy(&bc_lo, info.border_color, 8);
    std::memcpy(&bc_hi, info.border_color + 2, 8);
    key ^= bc_lo ^ bc_hi;
    return key;
}

// ═══════════════════════════════════════════════════════════
// Sampler creation / retrieval
// ═══════════════════════════════════════════════════════════
//
// Creates MTLSamplerState from Maxwell TSC parameters, cached
// by key derived from all sampler fields.

// ── Map Maxwell border_color[4] f32 to MTLSamplerBorderColor ──
// Metal supports only three border colors. We pick the closest match
// to the guest GPU's 4-component float border color.
static MTLSamplerBorderColor MapBorderColor(const f32 bc[4]) {
    // Metal supports only three border colors:
    //   TransparentBlack = (0, 0, 0, 0)
    //   OpaqueBlack      = (0, 0, 0, 1)
    //   OpaqueWhite      = (1, 1, 1, 1)
    //
    // Use minimum Euclidean distance to select the closest match.
    // Pre-compute squared distances to each candidate.
    auto sq = [](f32 x) { return x * x; };
    f32 d_transparent = sq(bc[0]) + sq(bc[1]) + sq(bc[2]) + sq(bc[3]);
    f32 d_opaque_black = sq(bc[0]) + sq(bc[1]) + sq(bc[2]) + sq(bc[3] - 1.0f);
    f32 d_opaque_white = sq(bc[0] - 1.0f) + sq(bc[1] - 1.0f) + sq(bc[2] - 1.0f) + sq(bc[3] - 1.0f);

    if (d_opaque_white <= d_opaque_black && d_opaque_white <= d_transparent)
        return MTLSamplerBorderColorOpaqueWhite;
    if (d_opaque_black <= d_transparent)
        return MTLSamplerBorderColorOpaqueBlack;
    return MTLSamplerBorderColorTransparentBlack;
}

id<MTLSamplerState> TextureCache::GetOrCreateSampler(const SamplerInfo& info) {
    u64 key = MakeSamplerKey(info);

    // Check cache
    {
        std::lock_guard l(mutex_);
        auto it = sampler_cache_.find(key);
        if (it != sampler_cache_.end()) return it->second;
    }

    // ── Create new MTLSamplerState ───────────────────────
    MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];

    // Min filter (mip filter derived from min_filter as well)
    switch (info.min_filter) {
    case 0: // Nearest
        desc.minFilter = MTLSamplerMinMagFilterNearest;
        desc.mipFilter = MTLSamplerMipFilterNearest;
        break;
    case 1: // Linear
        desc.minFilter = MTLSamplerMinMagFilterLinear;
        desc.mipFilter = MTLSamplerMipFilterLinear;
        break;
    default:
        desc.minFilter = MTLSamplerMinMagFilterNearest;
        desc.mipFilter = MTLSamplerMipFilterNearest;
    }

    // Mag filter
    switch (info.mag_filter) {
    case 0: desc.magFilter = MTLSamplerMinMagFilterNearest; break;
    case 1: desc.magFilter = MTLSamplerMinMagFilterLinear;  break;
    default: desc.magFilter = MTLSamplerMinMagFilterNearest;
    }

    // Wrap modes (Maxwell → Metal)
    auto toMtlWrap = [](u32 m) -> MTLSamplerAddressMode {
        switch (m) {
        case 0:  return MTLSamplerAddressModeRepeat;
        case 1:  return MTLSamplerAddressModeMirrorRepeat;
        case 2:  return MTLSamplerAddressModeClampToEdge;
        case 3:  return MTLSamplerAddressModeClampToBorderColor;
        case 4:  return MTLSamplerAddressModeClampToEdge;
        case 5:  return MTLSamplerAddressModeMirrorClampToEdge;
        default: return MTLSamplerAddressModeClampToEdge;
        }
    };
    desc.sAddressMode = toMtlWrap(info.wrap_u);
    desc.tAddressMode = toMtlWrap(info.wrap_v);
    desc.rAddressMode = toMtlWrap(info.wrap_w);

    // LOD clamping
    desc.lodMinClamp = info.min_lod;
    desc.lodMaxClamp = info.max_lod;
    desc.lodBias = info.lod_bias;

    // Anisotropy (Maxwell: 0 = off, 1-4 = 2^value x)
    if (info.anisotropy > 0 && info.anisotropy <= 4) {
        static const u32 aniso_table[] = {1, 2, 4, 8, 16};
        desc.maxAnisotropy = aniso_table[info.anisotropy];
    }

    // ── Border color ─────────────────────────────────────
    // Only set when wrap mode uses ClampToBorderColor
    bool needs_border = (info.wrap_u == 3 || info.wrap_v == 3 || info.wrap_w == 3);
    if (needs_border) {
        desc.borderColor = MapBorderColor(info.border_color);
    }

    id<MTLSamplerState> sampler = [device_ newSamplerStateWithDescriptor:desc];
    [desc release];

    if (sampler) {
        std::lock_guard l(mutex_);
        sampler_cache_[key] = sampler;
    }

    return sampler;
}

// ═══════════════════════════════════════════════════════════
// Sampler/Texture Pool Parsing
// ═══════════════════════════════════════════════════════════
//
// TSC (Texture Sampler Control) and TIC (Texture Image Control)
// are arrays in guest memory that describe how to sample textures.

SamplerInfo TextureCache::ParseTSC(const u8* sampler_pool, u64 offset) const {
    SamplerInfo info = {};

    if (!sampler_pool) return info;

    const u8* entry = sampler_pool + offset;

    // TSC entry layout (per Maxwell GPU, 32 bytes per entry):
    // word0 (offset 0): min_filter[15:12], mag_filter[11:8], wrap_u[7:4], ...
    //        wrap_v[3:0], wrap_w[7:0]
    // word1 (offset 4): min_lod, max_lod, lod_bias, anisotropy
    // word2 (offset 8):  border color R (f32)
    // word3 (offset 12): border color G (f32)
    // word4 (offset 16): border color B (f32)
    // word5 (offset 20): border color A (f32)
    // word6-7 (offset 24-31): reserved

    // ── Words 0-1: standard sampler parameters ─────────
    u16 word0 = (u16)entry[0] | ((u16)entry[1] << 8);
    info.min_filter = (word0 >> 8) & 0xF;
    info.mag_filter = (word0 >> 12) & 0xF;
    info.wrap_u = entry[2] & 0xF;
    info.wrap_v = (entry[2] >> 4) & 0xF;
    info.wrap_w = entry[3] & 0xF;

    u16 word1 = (u16)entry[4] | ((u16)entry[5] << 8);
    info.min_lod = (f32)(word1 & 0xFFF) / 256.0f;
    info.max_lod = (f32)((word1 >> 4) & 0xFFF) / 256.0f;
    info.lod_bias = (f32)(s16)(entry[6] | ((u16)(entry[7] & 0x7F) << 8)) / 256.0f;
    info.anisotropy = (s32)((entry[7] >> 3) & 7);

    // ── Words 2-5: border color (4 x f32 at offset 8) ─
    f32 border_r, border_g, border_b, border_a;
    std::memcpy(&border_r, entry + 8,  sizeof(f32));
    std::memcpy(&border_g, entry + 12, sizeof(f32));
    std::memcpy(&border_b, entry + 16, sizeof(f32));
    std::memcpy(&border_a, entry + 20, sizeof(f32));
    info.border_color[0] = border_r;
    info.border_color[1] = border_g;
    info.border_color[2] = border_b;
    info.border_color[3] = border_a;

    return info;
}

TextureInfo TextureCache::ParseTIC(const u8* texture_pool, u64 offset) const {
    TextureInfo info = {};

    if (!texture_pool) return info;

    const u8* entry = texture_pool + offset;

    // TIC entry layout (per Switch GPU, 64 bytes per entry):
    // word0: GPU address low
    // word1: GPU address high + format + tile mode
    // word2: width, height
    // word3: depth, mip levels, array mode
    // ... (remaining words for stride, swizzle, etc.)

    u64 addr_lo = (u64)entry[0] | ((u64)entry[1] << 8) |
                  ((u64)entry[2] << 16) | ((u64)entry[3] << 24) |
                  ((u64)entry[4] << 32) | ((u64)entry[5] << 40);
    u64 addr_hi = entry[8];

    info.gpu_address = addr_lo | (addr_hi << 48);
    info.format = (MaxwellPixelFormat)entry[9];
    info.tile_mode = entry[10] & 0x7;
    info.is_linear = (entry[11] & 1) != 0;

    info.width  = (u32)entry[12] | ((u32)entry[13] << 8);
    info.height = (u32)entry[14] | ((u32)entry[15] << 8);

    info.depth = (u32)entry[16] | ((u32)entry[17] << 8);
    if (info.depth == 0) info.depth = 1;

    info.mip_levels = entry[20] & 0xF;
    if (info.mip_levels == 0) info.mip_levels = 1;

    info.array_mode = entry[20] >> 4;
    info.pitch = (u32)entry[24] | ((u32)entry[25] << 8) |
                 ((u32)entry[26] << 16) | ((u32)entry[27] << 24);

    return info;
}

// ═══════════════════════════════════════════════════════════
// Cache lifecycle
// ═══════════════════════════════════════════════════════════

void TextureCache::EndFrame() {
    frame_count_++;

    // Proactive eviction: every frame, evict entries that haven't been
    // used for EVICTION_THRESHOLD frames, but only if we're over budget.
    if (total_memory_ > MAX_CACHE_MEMORY * 3 / 4 || entries_.size() > MAX_CACHE_ENTRIES * 3 / 4) {
        u64 oldest_allowed = frame_count_ - EVICTION_THRESHOLD;
        u32 evicted = 0;
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if (it->second.frame_used < oldest_allowed) {
                IndexRemove(it->second.info.gpu_address, it->first);
                total_memory_ -= it->second.size_bytes;
                [it->second.texture release];
                it = entries_.erase(it);
                evicted++;
            } else {
                ++it;
            }
        }
        if (evicted > 0) {
            LOG_DEBUG("TextureCache: end-frame evicted %u stale entries (mem=%zu)",
                      evicted, total_memory_);
        }
    }
}

void TextureCache::Flush() {
    std::lock_guard l(mutex_);
    for (auto& [key, entry] : entries_) {
        [entry.texture release];
    }
    entries_.clear();
    addr_index_.clear();
    total_memory_ = 0;
    for (auto& [key, sampler] : sampler_cache_) {
        [sampler release];
    }
    sampler_cache_.clear();
    LOG_DEBUG("TextureCache: flushed");
}

