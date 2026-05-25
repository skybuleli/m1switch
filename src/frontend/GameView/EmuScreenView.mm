#import "EmuScreenView.h"

#include "common/Log.h"

// ── Private interface ───────────────────────────────────────
@interface EmuScreenView ()

@property (nonatomic, strong) id<MTLDevice> metalDevice;
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;

// HUD labels (Phase 0: simple NSTextField placeholders)
@property (nonatomic, strong) NSTextField* fpsLabel;
@property (nonatomic, strong) NSTextField* memLabel;

@end

// ── Implementation ──────────────────────────────────────────
@implementation EmuScreenView

- (instancetype)initWithFrame:(CGRect)frame {
    // Create Metal device
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        LOG_FATAL("Metal is not supported on this device");
        return nil;
    }

    LOG_INFO("Metal device: %s (registryID: %llu)",
             [device.name UTF8String],
             (unsigned long long)device.registryID);

    self = [super initWithFrame:frame device:device];
    if (!self) return nil;

    _metalDevice = device;
    _commandQueue = [device newCommandQueue];

    // Configure MTKView
    self.delegate = self;
    self.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    self.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    self.sampleCount = 1;
    self.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.wantsLayer = YES;

    // Setup Metal layer
    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    metalLayer.framebufferOnly = YES;
    metalLayer.maximumDrawableCount = 3; // triple buffer

    // HUD labels (simple, Phase 0)
    _fpsLabel = [self createLabel];
    _memLabel = [self createLabel];

    LOG_INFO("EmuScreenView initialized (%dx%d)",
             (int)frame.size.width, (int)frame.size.height);

    return self;
}

- (NSTextField*)createLabel {
    NSTextField* label = [[NSTextField alloc] initWithFrame:NSZeroRect];
    label.bezeled = NO;
    label.drawsBackground = NO;
    label.editable = NO;
    label.selectable = NO;
    label.textColor = [NSColor colorWithWhite:1.0 alpha:0.85];
    label.font = [NSFont monospacedDigitSystemFontOfSize:12.0 weight:NSFontWeightRegular];
    [self addSubview:label];
    return label;
}

- (void)resizeSubviewsWithOldSize:(NSSize)oldSize {
    [super resizeSubviewsWithOldSize:oldSize];
    // Position HUD labels
    _fpsLabel.frame = NSMakeRect(8, self.bounds.size.height - 22, 200, 18);
    _memLabel.frame = NSMakeRect(8, self.bounds.size.height - 40, 300, 18);
}

// ── MTKViewDelegate ─────────────────────────────────────────

- (void)drawInMTKView:(MTKView*)view {
    // Phase 0: No emulator rendering yet.
    // Just clear to black. This will be replaced with actual
    // emulator frame presentation.
    id<MTLCommandBuffer> cmdBuf = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor* desc = view.currentRenderPassDescriptor;
    if (desc) {
        id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:desc];
        [enc endEncoding];
        [cmdBuf presentDrawable:view.currentDrawable];
    }
    [cmdBuf commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    LOG_DEBUG("Drawable size changed to %.0fx%.0f", size.width, size.height);
}

// ── Public API ──────────────────────────────────────────────

- (void)presentFrame:(id<MTLTexture>)frame {
    // Phase 0: stub — will blit the frame to the drawable
    TODO("Frame presentation not yet implemented");
}

- (void)updateHudWithFps:(float)fps
                 gpuUtil:(float)gpuUtil
              memoryUsed:(uint64_t)memUsed
              memoryTotal:(uint64_t)memTotal {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.fpsLabel.stringValue = [NSString stringWithFormat:@"%.1f FPS", fps];
        self.memLabel.stringValue = [NSString stringWithFormat:@"%.1f/%.1f GB  GPU: %.0f%%",
                                       (double)memUsed / 1e9,
                                       (double)memTotal / 1e9,
                                       gpuUtil * 100.0];
    });
}

- (void)toggleFullscreen {
    NSWindow* window = self.window;
    if (!window) return;

    if (window.styleMask & NSWindowStyleMaskFullScreen) {
        [window toggleFullScreen:nil];
    } else {
        [window toggleFullScreen:nil];
    }
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)keyDown:(NSEvent*)event {
    // Pass keyboard events to input system
    LOG_TRACE("Key down: 0x%x", event.keyCode);
}

@end

// ── C++ Bridge ──────────────────────────────────────────────

EmuScreenViewBridge::EmuScreenViewBridge() {
    NSRect frame = NSMakeRect(0, 0, 1280, 720);
    view_ = [[EmuScreenView alloc] initWithFrame:frame];
}

EmuScreenViewBridge::~EmuScreenViewBridge() {
    if (view_) {
        [view_ removeFromSuperview];
        view_ = nil;
    }
}

void EmuScreenViewBridge::PresentFrame(id<MTLTexture> frame) {
    [view_ presentFrame:frame];
}

void EmuScreenViewBridge::UpdateHud(float fps, float gpuUtil,
                                     uint64_t memUsed, uint64_t memTotal) {
    [view_ updateHudWithFps:fps gpuUtil:gpuUtil memoryUsed:memUsed memoryTotal:memTotal];
}
