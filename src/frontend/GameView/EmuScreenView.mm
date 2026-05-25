#import "EmuScreenView.h"
#include "common/Log.h"

@implementation EmuScreenView

- (instancetype)initWithFrame:(CGRect)frame {
    id<MTLDevice> mtlDev = MTLCreateSystemDefaultDevice();
    if (!mtlDev) { LOG_FATAL("Metal not supported"); return nil; }

    self = [super initWithFrame:frame device:mtlDev];
    if (!self) return nil;

    _commandQueue = [mtlDev newCommandQueue];

    // Configure view
    self.delegate = self;
    self.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    self.depthStencilPixelFormat = MTLPixelFormatInvalid;
    self.sampleCount = 1;
    self.clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    metalLayer.maximumDrawableCount = 3;

    // Initialize C++ Metal device + renderer
    _dev = new MetalDevice();
    _rnd = new MetalRenderer(*_dev);
    _rnd->Initialize();
    _rnd->SetTestTriangle();

    LOG_INFO("EmuScreenView ready (%dx%d)", (int)frame.size.width, (int)frame.size.height);
    return self;
}

- (void)dealloc {
    delete _rnd;
    delete _dev;
    [super dealloc];
}

// ── MTKViewDelegate ─────────────────────────────────────────
- (void)drawInMTKView:(MTKView*)view {
    id<MTLCommandBuffer> cmdBuf = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor* desc = view.currentRenderPassDescriptor;

    if (desc && _rnd) {
        _rnd->RenderFrame(cmdBuf, desc);
    }

    if (view.currentDrawable) {
        [cmdBuf presentDrawable:view.currentDrawable];
    }

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
