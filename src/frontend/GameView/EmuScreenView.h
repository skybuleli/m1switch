#pragma once

#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "common/Types.h"
#include "gpu/backend/MetalDevice.h"
#include "gpu/backend/MetalRenderer.h"
#include "gpu/StateTracker.h"
#include "memory/Memory.h"

// ── MTKView subclass for game rendering ─────────────────────
@interface EmuScreenView : MTKView <MTKViewDelegate> {
@private
    MetalDevice* _dev;
    MetalRenderer* _rnd;
    StateTracker* _tracker;
    Memory* _memory;
}

@property (nonatomic, readonly) id<MTLCommandQueue> commandQueue;
@property (nonatomic, readonly) StateTracker* tracker;
@property (nonatomic, readonly) Memory* memory;

- (instancetype)initWithFrame:(CGRect)frame memory:(Memory*)mem;
- (void)toggleFullscreen;

@end
