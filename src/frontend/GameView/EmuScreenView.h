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
    
    // ── HUD 覆盖层 ──────────────────────────────────
    NSTextField* _fpsLabel;
    NSTextField* _statusLabel;
    NSView* _hudOverlay;
    NSTimer* _hudTimer;
    double _lastFpsUpdate;
    int _frameCount;
    
    // ── 暂停状态 ─────────────────────────────────────
    BOOL _paused;
    NSButton* _pauseBtn;
    NSVisualEffectView* _pauseOverlay;
}

@property (nonatomic, readonly) id<MTLCommandQueue> commandQueue;
@property (nonatomic, readonly) EmulatorCore* core;

- (instancetype)initWithFrame:(CGRect)frame core:(EmulatorCore*)core;
- (BOOL)isValid;  // 检查初始化是否成功
- (void)toggleFullscreen;
- (void)togglePause;

- (void)loadAndRunNRO:(const char*)path;

@end
