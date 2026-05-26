#pragma once

#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "common/Types.h"
#include "gpu/backend/MetalDevice.h"
#include "gpu/backend/MetalRenderer.h"
#include "gpu/StateTracker.h"
#include "gpu/texture/TextureCache.h"
#include "core/Core.h"

class ShaderManager;

@interface EmuScreenView : MTKView <MTKViewDelegate> {
@private
    MetalDevice* _dev;
    MetalRenderer* _rnd;
    ShaderManager* _shaderMgr;
    TextureCache* _texCache;
    EmulatorCore* _core;
}

@property (nonatomic, readonly) id<MTLCommandQueue> commandQueue;
@property (nonatomic, readonly) EmulatorCore* core;

- (instancetype)initWithFrame:(CGRect)frame core:(EmulatorCore*)core;
- (void)toggleFullscreen;

- (void)loadAndRunNRO:(const char*)path;

@end
