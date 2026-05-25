#pragma once

#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "common/Types.h"
#include "gpu/backend/MetalDevice.h"
#include "gpu/backend/MetalRenderer.h"

// ── MTKView subclass for game rendering ─────────────────────
@interface EmuScreenView : MTKView <MTKViewDelegate> {
@private
    MetalDevice* _dev;
    MetalRenderer* _rnd;
}

@property (nonatomic, readonly) id<MTLCommandQueue> commandQueue;

- (instancetype)initWithFrame:(CGRect)frame;
- (void)toggleFullscreen;

@end
