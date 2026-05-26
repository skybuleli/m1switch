#pragma once

#include "common/Types.h"
#include "gpu/StateTracker.h"
#include "gpu/backend/MetalDevice.h"
#include "gpu/shader/ShaderManager.h"
#include "gpu/texture/TextureCache.h"
#include <Metal/Metal.h>
#include <unordered_map>

class MetalRenderer {
public:
    MetalRenderer(MetalDevice& device);
    ~MetalRenderer();

    Result Initialize();
    void SetStateTracker(StateTracker* tracker) { tracker_ = tracker; }
    void SetShaderManager(ShaderManager* mgr) { shader_mgr_ = mgr; }
    void SetTextureCache(TextureCache* cache) { texture_cache_ = cache; }

    void SetGameTexture(u32 index, id<MTLTexture> texture);

    void RenderFrame(id<MTLCommandBuffer> cmdBuf,
                     MTLRenderPassDescriptor* passDesc);

    // ── Render target management ────────────────────────
    // Resolve render targets from GpuState3D and create Metal textures
    // that the 3D pipeline renders into. After rendering, the results
    // are copied back to guest memory for the game to read.
    void ResolveRenderTargets();
    void CopyRenderTargetsToGuest(id<MTLCommandBuffer> cmdBuf);

    // ── RT <-> guest memory sync ─────────────────────
    // Upload initial RT content from guest memory into newly created Metal textures
    void UploadInitialRTData(id<MTLCommandBuffer> cmdBuf);

    // Get bytes-per-pixel for a render target format (for stride)
    static u32 RtFormatBpp(RtFormat fmt);

    void SetTestTriangle();

    // ── VI framebuffer passthrough ─────────────────────
    // Set a framebuffer from guest memory to be rendered full-screen
    // when no 3D draws are active. The memory is referenced directly
    // (no copy) via MTLBuffer with no-copy storage mode.
    void SetFramebufferSource(const u8* guest_memory, u32 width, u32 height,
                               u32 stride, u32 pixel_format);

    // ── Stats for debug panels ────────────────────────
    size_t GetPipelineCacheCount() const { return pipeline_cache_.size(); }
    size_t GetTextureCacheCount() const { return tex_cache_count_; }
    size_t GetTextureCacheMemory() const { return tex_cache_mem_; }
    void SetTextureCacheStats(size_t count, size_t mem) {
        tex_cache_count_ = count; tex_cache_mem_ = mem;
    }

private:
    MTLPrimitiveType ToMetalPrimitive(PrimitiveType type);
    MTLCompareFunction ToMetalCompare(CompareOp op);
    MTLVertexFormat ToMetalVertexFormat(u32 size, u32 type);
    MTLPixelFormat RtFormatToMetal(RtFormat fmt);
    void ApplyViewport(id<MTLRenderCommandEncoder> enc);
    void ApplyScissor(id<MTLRenderCommandEncoder> enc);
    void ApplyBlend(id<MTLRenderCommandEncoder> enc);
    void ApplyDepthStencil(id<MTLRenderCommandEncoder> enc);
    void ApplyCulling(id<MTLRenderCommandEncoder> enc);
    void BindVertexBuffers(id<MTLRenderCommandEncoder> enc, u32 count);
    void BindFragmentTextures(id<MTLRenderCommandEncoder> enc);
    void BindUniformBuffers(id<MTLRenderCommandEncoder> enc);
    void IssueDraw(id<MTLRenderCommandEncoder> enc, const StateTracker::PendingDraw& draw);
    void IssueClear(id<MTLCommandBuffer> cmdBuf, MTLRenderPassDescriptor* passDesc);
    
    void BindGameTextures();  // reads TIC from StateTracker and binds to Metal

    // ── VBO/IBO cache ───────────────────────────────────
    id<MTLBuffer> GetOrCreateCachedBuffer(u64 address, u32 min_size);
    void InvalidateCachedBuffer(u64 address);

    bool CompilePipeline();
    id<MTLRenderPipelineState> GetOrCreatePipeline(u64 hash);

    MetalDevice& device_;
    StateTracker* tracker_ = nullptr;
    ShaderManager* shader_mgr_ = nullptr;
    TextureCache* texture_cache_ = nullptr;
    id<MTLRenderPipelineState> fallback_pipeline_ = nil;
    id<MTLRenderPipelineState> game_pipeline_ = nil;
    id<MTLDepthStencilState> depth_stencil_ = nil;
    id<MTLBuffer> vertex_buffer_ = nil;
    bool initialized_ = false;
    bool shaders_compiled_ = false;
    u64 last_program_region_ = 0;
    u64 last_shader_offsets_[2] = {};

    ShaderCompileResult vert_result_;
    ShaderCompileResult frag_result_;

    // ── VI framebuffer blit ─────────────────────────────
    id<MTLRenderPipelineState> fb_pipeline_ = nil;    // full-screen quad pipeline
    id<MTLTexture> fb_texture_ = nil;                  // framebuffer texture (updated per frame)
    id<MTLSamplerState> fb_sampler_ = nil;             // linear sampler
    u32 fb_width_ = 0;
    u32 fb_height_ = 0;
    u32 fb_stride_ = 0;
    bool fb_active_ = false;

    // ── Render target textures (from guest memory) ──────
    static constexpr u32 MAX_RT = 8;
    id<MTLTexture> rt_textures_[MAX_RT] = {};
    id<MTLTexture> depth_texture_ = nil;
    u32 rt_count_ = 0;
    bool rt_active_ = false;
    bool rt_textures_just_created_[MAX_RT] = {};  // true for newly created RT textures
    bool depth_just_created_ = false;              // true for newly created depth texture

    // Texture cache stats (updated by Gpu)
    size_t tex_cache_count_ = 0;
    size_t tex_cache_mem_ = 0;

    // Game textures bound from TextureCache (up to 32 texture units)
    static constexpr u32 MAX_BOUND_TEXTURES = 32;
    id<MTLTexture> game_textures_[MAX_BOUND_TEXTURES] = {};
    id<MTLSamplerState> game_samplers_[MAX_BOUND_TEXTURES] = {};
    bool has_game_textures_ = false;

    std::unordered_map<u64, id<MTLRenderPipelineState>> pipeline_cache_;

    // ── VBO/IBO cache (guest address → MTLBuffer no-copy wrapper) ──
    struct CachedBuffer {
        id<MTLBuffer> buffer = nil;
        u64 last_frame_used = 0;
        u32 size = 0;  // cached buffer length
    };
    std::unordered_map<u64, CachedBuffer> buffer_cache_;
    u64 frame_count_ = 0;

    static constexpr u32 BUFFER_CACHE_MAX_ENTRIES = 256;
    static constexpr u32 BUFFER_CACHE_EVICT_COUNT = 32;

    void EvictLRU();  // evict least-recently-used entries
};

