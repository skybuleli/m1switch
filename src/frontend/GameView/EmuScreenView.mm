#import "EmuScreenView.h"
#include "common/Log.h"
#include "services/Nv.h"
#include "gpu/shader/ShaderManager.h"
#include <cstring>

// VI 帧缓冲查询 API (来自 Vi.cpp)
extern "C" {
u64 Vi_GetCurrentFramebuffer();
u32 Vi_GetFramebufferWidth();
u32 Vi_GetFramebufferHeight();
u32 Vi_GetFramebufferStride();
bool Vi_HasNewFrame();
void Vi_ConsumeNewFrame();
}

extern "C" {
void Input_Poll(void);
void Input_WriteToHidSharedMemory(u8* mem, u64 size);
double Gpu_GetFps(void);
}

@implementation EmuScreenView

- (instancetype)initWithFrame:(CGRect)frame core:(EmulatorCore*)core {
    id<MTLDevice> mtlDev = MTLCreateSystemDefaultDevice();
    if (!mtlDev) {
        LOG_FATAL("Metal not supported on this device");
        // 返回一个标记为无效的实例
        self = [super initWithFrame:frame];
        _core = core;
        return self;
    }

    self = [super initWithFrame:frame device:mtlDev];
    if (!self) return nil;

    _commandQueue = [mtlDev newCommandQueue];
    _core = core;

    self.delegate = self;
    self.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    self.depthStencilPixelFormat = MTLPixelFormatInvalid;
    self.sampleCount = 1;
    self.clearColor = MTLClearColorMake(0.05, 0.05, 0.08, 1.0);
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.layer.backgroundColor = [[NSColor colorWithSRGBRed:0.05 green:0.05 blue:0.08 alpha:1] CGColor];

    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    metalLayer.maximumDrawableCount = 3;

    // ── Metal 后端初始化 ───────────────────────────────
    _dev = new MetalDevice();
    _rnd = new MetalRenderer(*_dev);
    _rnd->SetStateTracker(&core->GetTracker());
    
    Result r = _rnd->Initialize();
    if (Failed(r)) {
        LOG_ERROR("MetalRenderer init failed: %d", (int)r);
        // 标记不可用
        _rnd = nullptr;
    } else {
        _rnd->SetTestTriangle();

        _shaderMgr = new ShaderManager(*_dev);
        _shaderMgr->Initialize();
        _rnd->SetShaderManager(_shaderMgr);

        _texCache = new TextureCache(_dev->Device());
        _rnd->SetTextureCache(_texCache);
    }

    // ── HUD 覆盖层 ────────────────────────────────────
    [self setupHUD];

    LOG_INFO("EmuScreenView ready%@",
             _rnd ? @"" : @" (limited mode — no renderer)");
    return self;
}

- (BOOL)isValid {
    return _rnd != nullptr;
}

- (void)setupHUD {
    // ── 半透明 HUD 背景 ─────────────────────────────
    _hudOverlay = [[NSView alloc] initWithFrame:NSZeroRect];
    _hudOverlay.wantsLayer = YES;
    _hudOverlay.layer.backgroundColor = [[NSColor colorWithWhite:0 alpha:0.35] CGColor];
    _hudOverlay.layer.cornerRadius = 8;
    _hudOverlay.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_hudOverlay];

    // ── FPS 标签 ────────────────────────────────────
    _fpsLabel = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _fpsLabel.stringValue = @"⏺ — FPS";
    _fpsLabel.textColor = [NSColor colorWithWhite:0.9 alpha:0.9];
    _fpsLabel.font = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightMedium];
    _fpsLabel.bezeled = NO;
    _fpsLabel.editable = NO;
    _fpsLabel.selectable = NO;
    _fpsLabel.backgroundColor = [NSColor clearColor];
    _fpsLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [_hudOverlay addSubview:_fpsLabel];

    // ── 状态标签 ────────────────────────────────────
    _statusLabel = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _statusLabel.stringValue = @"Idle";
    _statusLabel.textColor = [NSColor colorWithWhite:0.7 alpha:0.8];
    _statusLabel.font = [NSFont systemFontOfSize:11];
    _statusLabel.bezeled = NO;
    _statusLabel.editable = NO;
    _statusLabel.selectable = NO;
    _statusLabel.backgroundColor = [NSColor clearColor];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [_hudOverlay addSubview:_statusLabel];

    [NSLayoutConstraint activateConstraints:@[
        [_hudOverlay.topAnchor constraintEqualToAnchor:self.topAnchor constant:8],
        [_hudOverlay.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8],
        [_hudOverlay.widthAnchor constraintEqualToConstant:140],
        [_hudOverlay.heightAnchor constraintEqualToConstant:52],

        [_fpsLabel.topAnchor constraintEqualToAnchor:_hudOverlay.topAnchor constant:6],
        [_fpsLabel.leadingAnchor constraintEqualToAnchor:_hudOverlay.leadingAnchor constant:10],
        [_fpsLabel.trailingAnchor constraintEqualToAnchor:_hudOverlay.trailingAnchor constant:-6],

        [_statusLabel.topAnchor constraintEqualToAnchor:_fpsLabel.bottomAnchor constant:2],
        [_statusLabel.leadingAnchor constraintEqualToAnchor:_hudOverlay.leadingAnchor constant:10],
    ]];

    // ── HUD 定时器（每秒更新 2 次）───────────────────
    _hudTimer = [NSTimer scheduledTimerWithTimeInterval:0.5
                                                  target:self
                                                selector:@selector(updateHUD:)
                                                userInfo:nil
                                                 repeats:YES];
    _lastFpsUpdate = [[NSDate date] timeIntervalSince1970];
    _frameCount = 0;

    // ── 暂停覆盖层（默认隐藏）─────────────────────────
    _pauseOverlay = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
    _pauseOverlay.blendingMode = NSVisualEffectBlendingModeWithinWindow;
    _pauseOverlay.state = NSVisualEffectStateActive;
    _pauseOverlay.material = NSVisualEffectMaterialDark;
    _pauseOverlay.translatesAutoresizingMaskIntoConstraints = NO;
    _pauseOverlay.hidden = YES;
    [self addSubview:_pauseOverlay];

    NSTextField* pauseText = [[NSTextField alloc] initWithFrame:NSZeroRect];
    pauseText.stringValue = @"⏸ Paused";
    pauseText.textColor = [NSColor whiteColor];
    pauseText.font = [NSFont boldSystemFontOfSize:28];
    pauseText.alignment = NSTextAlignmentCenter;
    pauseText.bezeled = NO;
    pauseText.editable = NO;
    pauseText.selectable = NO;
    pauseText.backgroundColor = [NSColor clearColor];
    pauseText.translatesAutoresizingMaskIntoConstraints = NO;
    [_pauseOverlay addSubview:pauseText];

    [NSLayoutConstraint activateConstraints:@[
        [_pauseOverlay.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
        [_pauseOverlay.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [_pauseOverlay.widthAnchor constraintEqualToAnchor:self.widthAnchor],
        [_pauseOverlay.heightAnchor constraintEqualToAnchor:self.heightAnchor],

        [pauseText.centerXAnchor constraintEqualToAnchor:_pauseOverlay.centerXAnchor],
        [pauseText.centerYAnchor constraintEqualToAnchor:_pauseOverlay.centerYAnchor],
    ]];
}

- (void)updateHUD:(NSTimer*)timer {
    double now = [[NSDate date] timeIntervalSince1970];
    double dt = now - _lastFpsUpdate;
    if (dt > 0.01 && _rnd) {
        double fps = _frameCount / dt;
        _fpsLabel.stringValue = [NSString stringWithFormat:@"⏺ %.0f FPS", fps];
        _frameCount = 0;
        _lastFpsUpdate = now;
    }

    if (_core) {
        _statusLabel.stringValue = _core->IsRunning() ? @"▶ Running" : @"⏹ Stopped";
    }
}

- (void)dealloc {
    [_hudTimer invalidate];
    if (_rnd) {
        delete _texCache;
        delete _shaderMgr;
        delete _rnd;
    }
    delete _dev;
    [super dealloc];
}

- (void)drawInMTKView:(MTKView*)view {
    if (!_rnd) {
        // 无渲染器时快速填充黑色
        id<MTLCommandBuffer> cmdBuf = [_commandQueue commandBuffer];
        MTLRenderPassDescriptor* desc = view.currentRenderPassDescriptor;
        if (desc && cmdBuf) {
            id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:desc];
            [enc endEncoding];
            if (view.currentDrawable) [cmdBuf presentDrawable:view.currentDrawable];
            [cmdBuf commit];
        }
        return;
    }

    id<MTLCommandBuffer> cmdBuf = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor* desc = view.currentRenderPassDescriptor;
    if (desc) {
        // ── 从 VI BufferQueue 获取当前帧缓冲 ──────────
        if (_core && Vi_HasNewFrame()) {
            Memory& mem = _core->GetMemory();
            u64 fb_addr = Vi_GetCurrentFramebuffer();
            u32 fb_width = Vi_GetFramebufferWidth();
            u32 fb_height = Vi_GetFramebufferHeight();
            u32 fb_stride = Vi_GetFramebufferStride();

            u8* fb_ptr = mem.Pointer(fb_addr);
            if (fb_ptr) {
                _rnd->SetFramebufferSource(fb_ptr, fb_width, fb_height,
                                            fb_stride, 4);
            }
            Vi_ConsumeNewFrame();
        }

        _rnd->RenderFrame(cmdBuf, desc);
    }

    // ── 轮询输入 ──────────────────────────────────────
    if (_core) {
        Input_Poll();
        Memory& mem = _core->GetMemory();
        static constexpr u64 HID_SHARED_MEM = 0xE1000000;
        static constexpr u64 HID_SHARED_SIZE = 0x40000;
        u8* hid_ptr = mem.Pointer(HID_SHARED_MEM);
        if (hid_ptr) Input_WriteToHidSharedMemory(hid_ptr, HID_SHARED_SIZE);
    }

    if (view.currentDrawable) [cmdBuf presentDrawable:view.currentDrawable];
    [cmdBuf commit];

    _frameCount++;
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    LOG_DEBUG("Drawable size: %.0fx%.0f", size.width, size.height);
}

- (void)loadAndRunNRO:(const char*)path {
    LOG_INFO("EmuScreenView: loading %s", path);

    if (!_core) { LOG_ERROR("No core to load game"); return; }

    _statusLabel.stringValue = [NSString stringWithFormat:@"📂 Loading %s…",
                                                         path ? strrchr(path, '/') ? strrchr(path, '/') + 1 : path : ""];

    Result r = _core->LoadGame(path);
    if (Failed(r)) {
        LOG_ERROR("Failed to load game: %d", (int)r);
        _statusLabel.stringValue = @"❌ Load failed";
        return;
    }

    _statusLabel.stringValue = @"▶ Running";
    _core->Run();
}

- (void)togglePause {
    if (!_core) return;
    _paused = !_paused;
    _pauseOverlay.hidden = !_paused;
    if (_paused) {
        _core->Pause();
        _statusLabel.stringValue = @"⏸ Paused";
    } else {
        _core->Resume();
        _statusLabel.stringValue = @"▶ Running";
    }
}

- (void)keyDown:(NSEvent*)event {
    if (event.keyCode == 49) { // Space
        [self togglePause];
    } else if (event.keyCode == 35) { // 'P'
        [self togglePause];
    } else {
        [super keyDown:event];
    }
}

- (void)mouseDown:(NSEvent*)event {
    // 单击切换暂停
    if (event.clickCount == 1) {
        [self togglePause];
    } else {
        [super mouseDown:event];
    }
}

- (void)toggleFullscreen {
    NSWindow* w = self.window;
    if (w) [w toggleFullScreen:nil];
}

- (BOOL)acceptsFirstResponder { return YES; }

@end
