// ── TextureCache Invalidate/InvalidateRegion Unit Tests ──────
// Tests the invalidation API for the Metal texture cache.
// Uses a real MTLDevice with MTLStorageModeShared textures
// to avoid the MTLStorageModePrivate + replaceRegion: restriction.

#import <Metal/Metal.h>
#include "gpu/texture/TextureCache.h"

#include <cstring>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────

static TextureInfo MakeTextureInfo(u64 gpu_address, u32 w, u32 h,
                                    MaxwellPixelFormat fmt = MaxwellPixelFormat::RGBA8Unorm) {
    TextureInfo info = {};
    info.gpu_address = gpu_address;
    info.width = w;
    info.height = h;
    info.format = fmt;
    info.mip_levels = 1;
    return info;
}

// Insert a shared-storage test texture into the cache at the given address.
// The cache takes ownership; caller does NOT need to release the returned texture.
static id<MTLTexture> InsertTestTexture(TextureCache& cache, id<MTLDevice> device,
                                         u64 gpu_address, u32 w, u32 h) {
    id<MTLTexture> tex = CreateTestTexture(device, w, h);
    if (!tex) return nil;
    auto info = MakeTextureInfo(gpu_address, w, h);
    u64 key = 0;
    key ^= gpu_address;
    key ^= (u64)w << 40;
    key ^= (u64)h << 32;
    CachedTexture entry;
    entry.cache_key = key;
    entry.info = info;
    entry.texture = tex;
    entry.frame_used = 0;
    entry.size_bytes = (u64)w * h * 4;
    cache.SetEntryForTesting(key, entry);
    return tex;
}

// ── Test cases ────────────────────────────────────────────────

static bool Test_Invalidate_ExactAddress() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        bool ok = false;
        id<MTLTexture> tex = nil;
        {
            TextureCache cache(device);
            tex = InsertTestTexture(cache, device, 0x1000, 64, 64);
            if (tex && cache.Count() == 1) {
                cache.Invalidate(0x1000);
                ok = (cache.Count() == 0);
            }
        } // cache destroyed before device release
        [device release];
        return ok;
    }
}

static bool Test_Invalidate_NonExistent() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        bool ok = false;
        {
            TextureCache cache(device);
            id<MTLTexture> tex = InsertTestTexture(cache, device, 0x2000, 32, 32);
            if (tex && cache.Count() == 1) {
                // Invalidate a different address → no effect
                cache.Invalidate(0x9999);
                if (cache.Count() == 1) {
                    // Invalidate zero address → no-op
                    cache.Invalidate(0);
                    ok = (cache.Count() == 1);
                }
            }
        } // cache destroyed before device release
        [device release];
        return ok;
    }
}

static bool Test_Invalidate_MultipleEntries() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        bool ok = false;
        {
            TextureCache cache(device);
            id<MTLTexture> tex1 = InsertTestTexture(cache, device, 0x1000, 64, 64);
            id<MTLTexture> tex2 = InsertTestTexture(cache, device, 0x2000, 32, 32);
            if (tex1 && tex2 && cache.Count() == 2) {
                // Invalidate only the first address — second should remain
                cache.Invalidate(0x1000);
                ok = (cache.Count() == 1);
            }
        } // cache destroyed before device release
        [device release];
        return ok;
    }
}

static bool Test_InvalidateRegion_Overlap() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        bool ok = false;
        {
            TextureCache cache(device);
            id<MTLTexture> tex = InsertTestTexture(cache, device, 0x1000, 64, 64);
            if (tex && cache.Count() == 1) {
                // Region [0x1500, 0x1600) inside texture [0x1000, 0x5000) → overlap
                cache.InvalidateRegion(0x1500, 0x100);
                ok = (cache.Count() == 0);
            }
        } // cache destroyed before device release
        [device release];
        return ok;
    }
}

static bool Test_InvalidateRegion_NoOverlap() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        bool ok = false;
        {
            TextureCache cache(device);
            id<MTLTexture> tex = InsertTestTexture(cache, device, 0x1000, 64, 64);
            if (tex && cache.Count() == 1) {
                // Region entirely after texture → no overlap
                cache.InvalidateRegion(0x5000, 0x1000);
                if (cache.Count() == 1) {
                    // Region entirely before texture → no overlap
                    cache.InvalidateRegion(0, 0x1000);
                    ok = (cache.Count() == 1);
                }
            }
        } // cache destroyed before device release
        [device release];
        return ok;
    }
}

static bool Test_InvalidateRegion_ExactBoundary() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        bool ok = false;
        {
            TextureCache cache(device);
            id<MTLTexture> tex = InsertTestTexture(cache, device, 0x1000, 64, 64);
            if (tex && cache.Count() == 1) {
                // Region ends at exact start of texture → no overlap
                cache.InvalidateRegion(0x500, 0xB00);
                if (cache.Count() == 1) {
                    // Region starts at exact end of texture → no overlap
                    cache.InvalidateRegion(0x5000, 0x1000);
                    ok = (cache.Count() == 1);
                }
            }
        } // cache destroyed before device release
        [device release];
        return ok;
    }
}

static bool Test_RecreateAfterInvalidate() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        bool ok = false;
        {
            TextureCache cache(device);
            id<MTLTexture> tex = InsertTestTexture(cache, device, 0x1000, 32, 32);
            if (tex && cache.Count() == 1) {
                cache.Invalidate(0x1000);
                if (cache.Count() == 0) {
                    id<MTLTexture> tex2 = InsertTestTexture(cache, device, 0x1000, 32, 32);
                    ok = (tex2 != nil && cache.Count() == 1);
                }
            }
        } // cache destroyed before device release
        [device release];
        return ok;
    }
}

static bool Test_InvalidateRegion_PartialOverlap_Before() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        bool ok = false;
        {
            TextureCache cache(device);
            id<MTLTexture> tex = InsertTestTexture(cache, device, 0x1000, 64, 64);
            if (tex && cache.Count() == 1) {
                // Region [0x800, 0x1800) overlaps from the left
                cache.InvalidateRegion(0x800, 0x1000);
                ok = (cache.Count() == 0);
            }
        } // cache destroyed before device release
        [device release];
        return ok;
    }
}

static bool Test_InvalidateRegion_PartialOverlap_Inside() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        bool ok = false;
        {
            TextureCache cache(device);
            id<MTLTexture> tex = InsertTestTexture(cache, device, 0x1000, 64, 64);
            if (tex && cache.Count() == 1) {
                // Region [0x4000, 0x6000) overlaps from the right side
                cache.InvalidateRegion(0x4000, 0x2000);
                ok = (cache.Count() == 0);
            }
        } // cache destroyed before device release
        [device release];
        return ok;
    }
}

static bool Test_InvalidateRegion_MultipleEntries() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        bool ok = false;
        {
            TextureCache cache(device);
            // Texture A: [0x1000, 0x5000), B: [0x6000, 0x7000), C: [0x8000, 0x10000)
            id<MTLTexture> a = InsertTestTexture(cache, device, 0x1000, 64, 64);
            id<MTLTexture> b = InsertTestTexture(cache, device, 0x6000, 32, 32);
            id<MTLTexture> c = InsertTestTexture(cache, device, 0x8000, 128, 64);
            if (a && b && c && cache.Count() == 3) {
                // Region [0x5000, 0x7000) overlaps B only
                // (A ends at 0x5000, B: [0x6000, 0x7000), C starts at 0x8000)
                cache.InvalidateRegion(0x5000, 0x2000);
                ok = (cache.Count() == 2); // A and C should remain
            }
        } // cache destroyed before device release
        [device release];
        return ok;
    }
}

static bool Test_FlushAndRecreate() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        bool ok = false;
        {
            TextureCache cache(device);
            id<MTLTexture> tex = InsertTestTexture(cache, device, 0x1000, 32, 32);
            if (tex && cache.Count() == 1) {
                cache.Flush();
                if (cache.Count() == 0) {
                    id<MTLTexture> tex2 = InsertTestTexture(cache, device, 0x1000, 32, 32);
                    ok = (tex2 != nil && cache.Count() == 1);
                }
            }
        } // cache destroyed before device release
        [device release];
        return ok;
    }
}

// ── Main ──────────────────────────────────────────────────────

int main() {
    printf("=== TextureCache Invalidate Tests ===\n\n");

    struct { const char* name; bool (*fn)(); } tests[] = {
        {"Invalidate_ExactAddress",                    Test_Invalidate_ExactAddress},
        {"Invalidate_NonExistent",                     Test_Invalidate_NonExistent},
        {"Invalidate_MultipleEntries",                  Test_Invalidate_MultipleEntries},
        {"InvalidateRegion_Overlap",                   Test_InvalidateRegion_Overlap},
        {"InvalidateRegion_NoOverlap",                 Test_InvalidateRegion_NoOverlap},
        {"InvalidateRegion_ExactBoundary",              Test_InvalidateRegion_ExactBoundary},
        {"RecreateAfterInvalidate",                    Test_RecreateAfterInvalidate},
        {"InvalidateRegion_PartialOverlap_Before",      Test_InvalidateRegion_PartialOverlap_Before},
        {"InvalidateRegion_PartialOverlap_Inside",      Test_InvalidateRegion_PartialOverlap_Inside},
        {"InvalidateRegion_MultipleEntries",            Test_InvalidateRegion_MultipleEntries},
        {"FlushAndRecreate",                           Test_FlushAndRecreate},
    };

    int passed = 0, failed = 0;
    for (auto& t : tests) {
        printf("[ ] %s ... ", t.name);
        fflush(stdout);
        if (t.fn()) { printf("PASS\n"); passed++; }
        else { printf("FAIL\n"); failed++; }
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
