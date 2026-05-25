#import "EmuScreenView.h"
#include "common/Log.h"
#include "services/Nv.h"

@implementation EmuScreenView

- (instancetype)initWithFrame:(CGRect)frame memory:(Memory*)mem {
    id<MTLDevice> mtlDev = MTLCreateSystemDefaultDevice();
    if (!mtlDev) { LOG_FATAL("Metal not supported"); return nil; }

    self = [super initWithFrame:frame device:mtlDev];
    if (!self) return nil;

    _commandQueue = [mtlDev newCommandQueue];
    // Create Memory if not provided
    _memory = mem ? mem : new Memory();

    self.delegate = self;
    self.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    self.depthStencilPixelFormat = MTLPixelFormatInvalid;
    self.sampleCount = 1;
    self.clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    metalLayer.maximumDrawableCount = 3;

    // Create shared StateTracker and wire everything
    _tracker = new StateTracker();
    _tracker->SetMemory(mem);

    // Connect MetalRenderer to the shared tracker
    _dev = new MetalDevice();
    _rnd = new MetalRenderer(*_dev);
    _rnd->SetStateTracker(_tracker);
    _rnd->Initialize();
    _rnd->SetTestTriangle();  // fallback when no GPU state

    // Connect NV service GPFifo to the shared tracker
    ServiceNv_SetGpuFifo(&_tracker->GetGPFifo());
    ServiceNv_SetTracker(_tracker);
    ServiceNv_SetMemory(mem);

    LOG_INFO("EmuScreenView ready with wired StateTracker");
    return self;
}

- (void)dealloc {
    delete _rnd;
    delete _dev;
    delete _tracker;
    // Memory freed by whoever created it (or leaked if we created it)
    [super dealloc];
}

- (void)drawInMTKView:(MTKView*)view {
    id<MTLCommandBuffer> cmdBuf = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor* desc = view.currentRenderPassDescriptor;
    if (desc && _rnd) _rnd->RenderFrame(cmdBuf, desc);
    if (view.currentDrawable) [cmdBuf presentDrawable:view.currentDrawable];
    [cmdBuf commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    LOG_DEBUG("Drawable size: %.0fx%.0f", size.width, size.height);
}

- (void)toggleFullscreen {
    NSWindow* w = self.window;
    if (w) [w toggleFullScreen:nil];
}

- (BOOL)acceptsFirstResponder { return YES; }

@end
