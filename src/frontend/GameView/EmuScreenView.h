#pragma once

#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "common/Types.h"
#include "common/Log.h"

// ── Forward declarations ───────────────────────────────────
class EmulatorCore;

// ── MTKView subclass for game rendering ─────────────────────
// This view owns the Metal device, command queue, and display
// pipeline. It's the bridge between the emulator core (C++)
// and the Metal rendering (ObjC++).

@interface EmuScreenView : MTKView <MTKViewDelegate>

@property (nonatomic, readonly) id<MTLDevice> metalDevice;
@property (nonatomic, readonly) id<MTLCommandQueue> commandQueue;

- (instancetype)initWithFrame:(CGRect)frame;

// Called from emulator thread to present a completed frame
- (void)presentFrame:(id<MTLTexture>)frame;

// Called from emulator thread to update HUD data
- (void)updateHudWithFps:(float)fps
                gpuUtil:(float)gpuUtil
             memoryUsed:(uint64_t)memUsed
             memoryTotal:(uint64_t)memTotal;

// Fullscreen toggle
- (void)toggleFullscreen;

@end

// ── C++ wrapper for non-ObjC code ───────────────────────────
class EmuScreenViewBridge {
public:
    EmuScreenViewBridge();
    ~EmuScreenViewBridge();

    NSView* NativeView() const { return (__bridge NSView*)view_; }

    void PresentFrame(id<MTLTexture> frame);
    void UpdateHud(float fps, float gpuUtil, uint64_t memUsed, uint64_t memTotal);

private:
    EmuScreenView* view_ = nil;
};
