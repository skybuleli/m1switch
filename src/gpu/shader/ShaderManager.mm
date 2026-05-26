#include "gpu/shader/ShaderManager.h"
#include "gpu/shader/MetalShaderCompiler.h"
#include "gpu/backend/MetalDevice.h"
#include "common/Log.h"

// ═══════════════════════════════════════════════════════════
// ShaderManager Implementation
// ═══════════════════════════════════════════════════════════

ShaderManager::ShaderManager(MetalDevice& device)
    : device_(device) {}

ShaderManager::~ShaderManager() {}

Result ShaderManager::Initialize() {
    initialized_ = true;
    LOG_INFO("ShaderManager initialized (cache ready)");
    return Result::Success;
}

// ═══════════════════════════════════════════════════════════
// Decode
// ═══════════════════════════════════════════════════════════

ShaderProgram ShaderManager::DecodeShader(const u8* data, u32 size,
                                           ShaderStage stage) {
    return decoder_.Decode(data, size, stage);
}

// ═══════════════════════════════════════════════════════════
// Cross-compile: SPIR-V → MSL via SPIRV-Cross
// ═══════════════════════════════════════════════════════════

CompiledShader ShaderManager::CrossCompile(const ShaderProgram& program) {
    CompiledShader result;
    result.hash = program.hash;
    result.stage = program.stage;

    // ── Step 1: Emit SPIR-V ────────────────────────────
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(program);

    if (emitter.HasError()) {
        LOG_ERROR("SPIR-V emission failed for shader 0x%016llx: %s",
                  program.hash, emitter.GetError().c_str());
        result.valid = false;
        return result;
    }

    result.spirv_binary = spirv;
    result.valid = true;

    // ── Step 2: SPIRV-Cross → MSL ──────────────────────
    std::string msl_error;
    std::string msl = MetalShaderCompile(spirv, program.stage, msl_error);

    if (!msl.empty()) {
        result.msl_source = msl;
        LOG_INFO("CrossCompile: hash=0x%016llx stage=%s spirv=%zu words, msl=%zu bytes",
                 program.hash, ShaderStageName(program.stage),
                 spirv.size(), msl.size());
    } else if (!msl_error.empty()) {
        LOG_ERROR("CrossCompile: MSL conversion failed for hash=0x%016llx: %s",
                  program.hash, msl_error.c_str());
    } else {
        LOG_INFO("CrossCompile: hash=0x%016llx stage=%s spirv=%zu words (no MSL yet)",
                 program.hash, ShaderStageName(program.stage), spirv.size());
    }

    return result;
}

// ═══════════════════════════════════════════════════════════
// Compile MSL source → MTLFunction
// ═══════════════════════════════════════════════════════════

id<MTLFunction> ShaderManager::CompileMSL(const std::string& msl_source,
                                           ShaderStage stage,
                                           std::string& error_out) {
    if (msl_source.empty()) {
        error_out = "Empty MSL source";
        return nil;
    }

    id<MTLLibrary> lib = device_.CompileLibrary(msl_source.c_str());
    if (!lib) {
        error_out = "Failed to compile MSL library";
        LOG_ERROR("ShaderManager: MSL compilation failed for stage=%s",
                  ShaderStageName(stage));
        return nil;
    }

    NSString* func_name = nil;
    switch (stage) {
    case ShaderStage::VertexA:
    case ShaderStage::VertexB:
        func_name = @"main0";  // SPIRV-Cross names the entry point "main0"
        break;
    case ShaderStage::Fragment:
        func_name = @"main0";
        break;
    default:
        func_name = @"main0";
        break;
    }

    id<MTLFunction> func = [lib newFunctionWithName:func_name];
    if (!func) {
        error_out = "Entry point 'main0' not found in MSL library";
        LOG_ERROR("ShaderManager: entry point not found for stage=%s",
                  ShaderStageName(stage));
        [lib release];
        return nil;
    }

    [lib release];
    return func;
}

// ═══════════════════════════════════════════════════════════
// Create render pipeline
// ═══════════════════════════════════════════════════════════

id<MTLRenderPipelineState> ShaderManager::CreatePipeline(
    id<MTLFunction> vert_fn,
    id<MTLFunction> frag_fn,
    std::string& error_out) {

    if (!vert_fn || !frag_fn) {
        error_out = "Missing vertex or fragment function";
        return nil;
    }

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vert_fn;
    desc.fragmentFunction = frag_fn;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    desc.depthAttachmentPixelFormat = MTLPixelFormatInvalid;

    // Default vertex descriptor (will be refined as needed)
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

    id<MTLRenderPipelineState> pipeline = device_.CreateRenderPipeline(desc);

    [desc release];
    [vd release];

    if (!pipeline) {
        error_out = "Failed to create render pipeline state";
    }

    return pipeline;
}

// ═══════════════════════════════════════════════════════════
// Compile a single shader from guest memory
// ═══════════════════════════════════════════════════════════

ShaderCompileResult ShaderManager::CompileShader(
    const u8* program_region,
    u64 region_size,
    const ShaderState& shader_state) {

    ShaderCompileResult result;
    result.stage = static_cast<ShaderStage>(shader_state.stage_id);

    if (!shader_state.enabled || shader_state.offset == 0) {
        result.success = false;
        result.error_msg = "Shader not enabled or zero offset";
        return result;
    }

    u64 hash_key = (u64)shader_state.offset ^ ((u64)shader_state.stage_id << 32);

    // ── Check cache ───────────────────────────────────
    CompiledShader* cached = cache_.FindCompiled(hash_key);
    if (cached && cached->valid) {
        LOG_DEBUG("ShaderManager: cache hit for shader 0x%016llx", hash_key);
        result.hash = hash_key;
        result.success = true;
        // Re-compile MSL from cached source
        std::string err;
        id<MTLFunction> func = CompileMSL(cached->msl_source, result.stage, err);
        if (result.stage == ShaderStage::Fragment || result.stage == ShaderStage::VertexA || result.stage == ShaderStage::VertexB) {
            // Determine if it's fragment or vertex
            if (result.stage == ShaderStage::Fragment) {
                result.fragment_function = func;
            } else {
                result.vertex_function = func;
            }
        }
        result.msl_source = cached->msl_source;
        result.spirv_word_count = cached->spirv_binary.size();
        return result;
    }

    // ── Check decoded cache ───────────────────────────
    ShaderProgram* prog = cache_.FindDecoded(hash_key);

    if (!prog) {
        // ── Extract from guest memory ─────────────────
        auto raw = ShaderExtract::ExtractShader(
            program_region, region_size, shader_state.offset);

        if (raw.empty()) {
            result.success = false;
            result.error_msg = "Failed to extract shader from guest memory";
            return result;
        }

        // ── Decode ───────────────────────────────────
        ShaderProgram decoded = DecodeShader(
            raw.data(), (u32)raw.size(), result.stage);

        if (decoder_.HasErrors()) {
            result.success = false;
            for (const auto& err : decoder_.GetErrors()) {
                result.error_msg += err + "; ";
            }
            return result;
        }

        decoded.hash = hash_key;
        decoded.program_offset = (u32)shader_state.offset;
        cache_.StoreDecoded(decoded);
        prog = cache_.FindDecoded(hash_key);
    }

    if (!prog) {
        result.success = false;
        result.error_msg = "Failed to decode shader";
        return result;
    }

    result.hash = hash_key;
    result.ir_instruction_count = prog->instructions.size();

    // ── Cross-compile: SPIR-V → MSL ───────────────────
    CompiledShader compiled = CrossCompile(*prog);
    if (!compiled.valid) {
        result.success = false;
        result.error_msg = "Cross-compilation failed";
        return result;
    }

    // ── Compile MSL → Metal function ─────────────────
    std::string msl_error;
    id<MTLFunction> func = CompileMSL(compiled.msl_source, result.stage, msl_error);

    if (!func && compiled.msl_source.empty()) {
        // For now, if SPIRV-Cross hasn't produced MSL yet,
        // still mark as success — the pipeline will use fallback shaders
        LOG_WARN("ShaderManager: MSL source not available for hash=0x%016llx, "
                 "will use fallback", hash_key);
        result.success = true;
        result.msl_source = "";
        cache_.StoreCompiled(hash_key, compiled);
        return result;
    }

    if (!func) {
        result.success = false;
        result.error_msg = "MSL compilation failed: " + msl_error;
        return result;
    }

    // Store function
    if (result.stage == ShaderStage::Fragment) {
        result.fragment_function = func;
    } else {
        result.vertex_function = func;
    }

    // Cache the compiled result
    compiled.msl_source = compiled.msl_source.empty() ? "" : compiled.msl_source;
    cache_.StoreCompiled(hash_key, compiled);

    result.msl_source = compiled.msl_source;
    result.spirv_word_count = compiled.spirv_binary.size();
    result.success = true;

    LOG_INFO("ShaderManager: compiled shader hash=0x%016llx stage=%s "
             "%zu insts, %zu SPIR-V words",
             hash_key, ShaderStageName(result.stage),
             result.ir_instruction_count, result.spirv_word_count);

    return result;
}

// ═══════════════════════════════════════════════════════════
// Compile all shaders from GPU state
// ═══════════════════════════════════════════════════════════

u32 ShaderManager::CompileAllShaders(
    const u8* program_region,
    u64 region_size,
    const ShaderState shaders[MAX_SHADER_STAGES]) {

    u32 compiled = 0;
    for (u32 i = 0; i < MAX_SHADER_STAGES; i++) {
        if (shaders[i].enabled && shaders[i].offset > 0) {
            auto result = CompileShader(program_region, region_size, shaders[i]);
            if (result.success) compiled++;
        }
    }

    LOG_INFO("ShaderManager: compiled %u/%u shaders from GPU state",
             compiled, MAX_SHADER_STAGES);
    return compiled;
}

// ═══════════════════════════════════════════════════════════
// Create render pipeline from compiled vertex+fragment shaders
// ═══════════════════════════════════════════════════════════

ShaderCompileResult ShaderManager::CreateRenderPipeline(
    const ShaderCompileResult& vertex,
    const ShaderCompileResult& fragment) {

    ShaderCompileResult result;
    result.success = false;

    if (!vertex.success || !fragment.success) {
        result.error_msg = "Both vertex and fragment shaders required";
        return result;
    }

    id<MTLFunction> vert_fn = vertex.vertex_function;
    id<MTLFunction> frag_fn = fragment.fragment_function;

    // If MSL source was provided but functions not compiled yet, compile now
    if (!vert_fn && !vertex.msl_source.empty()) {
        std::string err;
        vert_fn = CompileMSL(vertex.msl_source, vertex.stage, err);
    }
    if (!frag_fn && !fragment.msl_source.empty()) {
        std::string err;
        frag_fn = CompileMSL(fragment.msl_source, fragment.stage, err);
    }

    if (!vert_fn || !frag_fn) {
        result.error_msg = "Missing vertex or fragment Metal function";
        return result;
    }

    // Create combined pipeline state
    std::string pipeline_error;
    id<MTLRenderPipelineState> pipeline = CreatePipeline(
        vert_fn, frag_fn, pipeline_error);

    if (!pipeline) {
        result.error_msg = "Pipeline creation failed: " + pipeline_error;
        return result;
    }

    // Cache pipeline
    u64 pipeline_hash = vertex.hash ^ (fragment.hash << 1);
    cache_.StorePipeline(pipeline_hash, (__bridge void*)pipeline);

    result.success = true;
    result.pipeline_state = pipeline;
    result.hash = pipeline_hash;
    result.msl_source = vertex.msl_source + "\n" + fragment.msl_source;

    LOG_INFO("ShaderManager: created render pipeline hash=0x%016llx", pipeline_hash);

    return result;
}

// ═══════════════════════════════════════════════════════════
// Stats / Cache management
// ═══════════════════════════════════════════════════════════

void ShaderManager::DumpStats() const {
    LOG_INFO("=== ShaderManager Stats ===");
    LOG_INFO("  Decoded programs: %zu", cache_.DecodedCount());
    LOG_INFO("  Compiled shaders: %zu", cache_.CompiledCount());
    LOG_INFO("  Pipeline states:  %zu", cache_.PipelineCount());
    LOG_INFO("===========================");
}

void ShaderManager::InvalidateCache() {
    cache_.Clear();
    LOG_INFO("ShaderManager: cache invalidated");
}
