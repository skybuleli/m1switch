#import "gpu/shader/MetalShaderCompiler.h"
#include "gpu/backend/MetalDevice.h"
#include "common/Log.h"

// ═══════════════════════════════════════════════════════════
// Metal Shader Compiler
// ═══════════════════════════════════════════════════════════
//
// Bridges SPIRV-Cross to produce MSL from SPIR-V binaries.
// Uses #ifdef M1SWITCH_USE_SPIRV to optionally use SPIRV-Cross.
//
// When SPIRV-Cross is not available, falls back to generating
// simple passthrough MSL shaders.

// ── Forward declarations for SPIRV-Cross types ──────────────
// We use void* to avoid requiring SPIRV-Cross headers in this file
// when the dependency isn't available.
struct SPIRVCrossContext;

// ── Internal context ───────────────────────────────────────
struct SPIRVCrossInternal {
    void* compiler = nullptr;  // spirv_cross::CompilerMSL*
    bool valid = false;
};

// ── SPIRV-Cross MSL compilation (if available) ──────────────
// This function converts SPIR-V binary to MSL source.
// If SPIRV-Cross is not linked, returns empty string.

#ifdef M1SWITCH_USE_SPIRV

// We include SPIRV-Cross headers when the define is set
#include "spirv_cross.hpp"
#include "spirv_glsl.hpp"
#include "spirv_msl.hpp"

static std::string SpirvCrossToMSL(
    const std::vector<u32>& spirv,
    ShaderStage stage,
    std::string& error_out) {

    try {
        spirv_cross::CompilerMSL msl_compiler(std::move(
            std::vector<uint32_t>(spirv.begin(), spirv.end())));

        spirv_cross::CompilerMSL::Options msl_opts;
        msl_opts.platform = spirv_cross::CompilerMSL::Options::macOS;
        msl_opts.set_msl_version(2, 3);
        msl_opts.pad_fragment_output_components = true;
        msl_compiler.set_msl_options(msl_opts);

        spirv_cross::CompilerGLSL::Options glsl_opts;
        glsl_opts.version = 100;
        glsl_opts.es = false;
        glsl_opts.enable_420pack_extension = false;
        glsl_opts.vertex.fixup_clipspace = false;
        glsl_opts.vertex.flip_vert_y = false;
        msl_compiler.set_common_options(glsl_opts);

        auto execution_model = (stage == ShaderStage::Fragment)
            ? spv::ExecutionModelFragment
            : spv::ExecutionModelVertex;

        auto entry_points = msl_compiler.get_entry_points_and_stages();
        for (auto& e : entry_points) {
            if (e.name == "main") {
                msl_compiler.rename_entry_point("main", "main0", e.execution_model);
                break;
            }
        }

        spirv_cross::MSLResourceBinding res_bind;
        res_bind.stage = execution_model;
        res_bind.desc_set = 0;
        res_bind.binding = 0;
        res_bind.msl_buffer = 0;
        msl_compiler.add_msl_resource_binding(res_bind);

        for (u32 i = 0; i < 16; i++) {
            spirv_cross::MSLResourceBinding va_bind;
            va_bind.stage = spv::ExecutionModelVertex;
            va_bind.desc_set = 0;
            va_bind.binding = i;
            va_bind.msl_buffer = i + 1;
            msl_compiler.add_msl_resource_binding(va_bind);
        }

        std::string msl_source = msl_compiler.compile();

        LOG_INFO("SPIRV-Cross: compiled %zu SPIR-V words → %zu bytes MSL",
                 spirv.size(), msl_source.size());

        return msl_source;

    } catch (const spirv_cross::CompilerError& e) {
        error_out = "SPIRV-Cross error: ";
        error_out += e.what();
        LOG_ERROR("SPIRV-Cross compilation failed: %s", e.what());
        return "";
    } catch (const std::exception& e) {
        error_out = "SPIRV-Cross exception: ";
        error_out += e.what();
        LOG_ERROR("SPIRV-Cross exception: %s", e.what());
        return "";
    }
}

#else // !M1SWITCH_USE_SPIRV

// ── Fallback: generate passthrough MSL shaders ──────────────
// This allows the system to work without SPIRV-Cross linked.

static std::string GeneratePassthroughMSL(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::VertexA:
    case ShaderStage::VertexB:
        return R"(#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float4 color    [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

vertex VertexOut main0(VertexIn in [[stage_in]]) {
    VertexOut out;
    out.position = float4(in.position, 0.0, 1.0);
    out.color = in.color;
    return out;
}
)";

    case ShaderStage::Fragment:
        return R"(#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

fragment float4 main0(VertexOut in [[stage_in]]) {
    return in.color;
}
)";

    default:
        return "";
    }
}

static std::string SpirvCrossToMSL(
    const std::vector<u32>& spirv,
    ShaderStage stage,
    std::string& error_out) {

    LOG_WARN("SPIRV-Cross not available, generating passthrough MSL for stage=%s",
             ShaderStageName(stage));
    return GeneratePassthroughMSL(stage);
}

#endif // M1SWITCH_USE_SPIRV

// ═══════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════

// ── Convert SPIR-V + stage → MSL source ────────────────────
// Called from ShaderManager after SPIR-V emission.
std::string MetalShaderCompile(const std::vector<u32>& spirv,
                                ShaderStage stage,
                                std::string& error_out) {
    return SpirvCrossToMSL(spirv, stage, error_out);
}

// ── Compile MSL source directly → MTLFunction ──────────────
// Internal helper (used by MetalCreateRenderPipeline)
static id<MTLFunction> MetalCompileMSL(id<MTLDevice> device,
                                        const std::string& msl_source,
                                        const char* entry_point,
                                        std::string& error_out) {
    if (!device) {
        error_out = "Metal device is nil";
        return nil;
    }

    NSError* error = nil;
    NSString* src = [NSString stringWithUTF8String:msl_source.c_str()];
    id<MTLLibrary> lib = [device newLibraryWithSource:src
                                              options:nil
                                                error:&error];

    if (!lib) {
        if (error) {
            error_out = [[error localizedDescription] UTF8String];
        } else {
            error_out = "Unknown library compilation error";
        }
        LOG_ERROR("Metal MSL compilation failed: %s", error_out.c_str());
        return nil;
    }

    NSString* entry = [NSString stringWithUTF8String:entry_point];
    id<MTLFunction> func = [lib newFunctionWithName:entry];
    [lib release];

    if (!func) {
        error_out = "Entry point '";
        error_out += entry_point;
        error_out += "' not found";
        LOG_ERROR("Metal MSL: entry point not found: %s", entry_point);
    }

    return func;
}

// ── Compile a MTLRenderPipelineState from vertex+fragment ──
static id<MTLRenderPipelineState> MetalCreateRenderPipeline(
    id<MTLDevice> device,
    id<MTLFunction> vert_fn,
    id<MTLFunction> frag_fn,
    std::string& error_out) {

    if (!device || !vert_fn || !frag_fn) {
        error_out = "Invalid pipeline inputs";
        return nil;
    }

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vert_fn;
    desc.fragmentFunction = frag_fn;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    desc.depthAttachmentPixelFormat = MTLPixelFormatInvalid;

    // 最小化顶点描述符 — 由 MetalRenderer 动态构建，此处为回退
    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
    vd.attributes[0].format = MTLVertexFormatFloat2;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat4;
    vd.attributes[1].offset = 8;
    vd.attributes[1].bufferIndex = 0;
    vd.layouts[0].stride = 24;
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    desc.vertexDescriptor = vd;

    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline =
        [device newRenderPipelineStateWithDescriptor:desc
                                               error:&error];

    [desc release];
    [vd release];

    if (!pipeline) {
        if (error) {
            error_out = [[error localizedDescription] UTF8String];
        } else {
            error_out = "Unknown pipeline creation error";
        }
        LOG_ERROR("Metal pipeline creation failed: %s", error_out.c_str());
    }

    return pipeline;
}
