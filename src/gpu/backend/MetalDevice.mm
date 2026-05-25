#import "gpu/backend/MetalDevice.h"
#include "common/Log.h"

MetalDevice::MetalDevice() {
    device_ = MTLCreateSystemDefaultDevice();
    if (!device_) {
        LOG_FATAL("Metal is not supported");
        return;
    }

    queue_ = [device_ newCommandQueue];
    if (!queue_) {
        LOG_FATAL("Failed to create Metal command queue");
        return;
    }

    LOG_INFO("Metal device: %s", [device_.name UTF8String]);
    LOG_INFO("Max buffer length: %llu bytes", [device_ maxBufferLength]);
    LOG_INFO("Has unified memory: %s", [device_ hasUnifiedMemory] ? "YES" : "NO");
}

MetalDevice::~MetalDevice() {
    [queue_ release];
    [device_ release];
}

id<MTLLibrary> MetalDevice::CompileLibrary(const char* source) const {
    NSError* error = nil;
    NSString* src = [NSString stringWithUTF8String:source];
    id<MTLLibrary> lib = [device_ newLibraryWithSource:src
                                                options:nil
                                                  error:&error];
    if (error) {
        LOG_ERROR("Shader compile error: %s", [[error localizedDescription] UTF8String]);
        return nil;
    }
    LOG_INFO("Shader library compiled successfully");
    return lib;
}

id<MTLRenderPipelineState> MetalDevice::CreateRenderPipeline(
    MTLRenderPipelineDescriptor* desc) const {
    NSError* error = nil;
    id<MTLRenderPipelineState> pso = [device_ newRenderPipelineStateWithDescriptor:desc
                                                                            error:&error];
    if (error) {
        LOG_ERROR("Pipeline state error: %s", [[error localizedDescription] UTF8String]);
        return nil;
    }
    return pso;
}
