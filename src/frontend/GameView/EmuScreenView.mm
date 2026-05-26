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
}

@implementation EmuScreenView

- (instancetype)initWithFrame:(CGRect)frame core:(EmulatorCore*)core {
    id<MTLDevice> mtlDev = MTLCreateSystemDefaultDevice();
    if (!mtlDev) { LOG_FATAL("Metal not supported"); return nil; }

    self = [super initWithFrame:frame device:mtlDev];
    if (!self) return nil;

    _commandQueue = [mtlDev newCommandQueue];
    _core = core;

    self.delegate = self;
    self.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    self.depthStencilPixelFormat = MTLPixelFormatInvalid;
    self.sampleCount = 1;
    self.clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    metalLayer.maximumDrawableCount = 3;

    _dev = new MetalDevice();
    _rnd = new MetalRenderer(*_dev);
    _rnd->SetStateTracker(&core->GetTracker());
    _rnd->Initialize();
    _rnd->SetTestTriangle();

    _shaderMgr = new ShaderManager(*_dev);
    _shaderMgr->Initialize();
    _rnd->SetShaderManager(_shaderMgr);

    _texCache = new TextureCache(_dev->Device());
    _rnd->SetTextureCache(_texCache);

    LOG_INFO("EmuScreenView ready with EmulatorCore + ShaderManager + TextureCache");
    return self;
}

- (void)dealloc {
    delete _texCache;
    delete _shaderMgr;
    delete _rnd;
    delete _dev;
    [super dealloc];
}

- (void)drawInMTKView:(MTKView*)view {
    id<MTLCommandBuffer> cmdBuf = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor* desc = view.currentRenderPassDescriptor;
    if (desc && _rnd) {
        // ── 从 VI BufferQueue 获取当前帧缓冲 ─────
        // 使用 Vi_HasNewFrame 判断是否有新帧提交（避免每帧探测 marker）
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
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    LOG_DEBUG("Drawable size: %.0fx%.0f", size.width, size.height);
}


- (void)loadAndRunNRO:(const char*)path {
    LOG_INFO("EmuScreenView: loading %s", path);

    Result r = _core->LoadGame(path);
    if (Failed(r)) { LOG_ERROR("Failed to load game"); return; }

    _core->Run();
}

- (void)toggleFullscreen {
    NSWindow* w = self.window;
    if (w) [w toggleFullScreen:nil];
}

- (void)captureScreenshot:(id)sender {
    id<MTLTexture> texture = self.currentDrawable.texture;
    if (!texture) return;

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:texture.pixelFormat
                                                                                    width:texture.width
                                                                                   height:texture.height
                                                                                mipmapped:NO];
    id<MTLTexture> copy = [self.device newTextureWithDescriptor:desc];
    if (!copy) return;

    id<MTLCommandBuffer> cb = [_commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:texture toTexture:copy];
    [blit endEncoding];

    __block id<MTLTexture> blockCopy = copy;
    [cb addCompletedHandler:^(id<MTLCommandBuffer> buf) {
        NSInteger width = blockCopy.width;
        NSInteger height = blockCopy.height;
        NSMutableData* data = [NSMutableData dataWithLength:width * height * 4];
        
        [blockCopy getBytes:data.mutableBytes
               bytesPerRow:width * 4
                fromRegion:MTLRegionMake2D(0, 0, width, height)
               mipmapLevel:0];

        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL
                          pixelsWide:width
                          pixelsHigh:height
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSCalibratedRGBColorSpace
                         bytesPerRow:width * 4
                        bitsPerPixel:32];
        memcpy([rep bitmapData], data.bytes, width * height * 4);

        NSData* png = [rep representationUsingType:NSPNGFileType properties:@{}];
        NSString* path = [NSString stringWithFormat:@"%%@/Desktop/m1switch_%%@.png",
                         NSHomeDirectory(),
                         [NSDateFormatter localizedStringFromDate:[NSDate date]
                                                        dateStyle:NSDateFormatterNoStyle
                                                        timeStyle:NSDateFormatterMediumStyle]];
        [png writeToFile:path atomically:YES];
        LOG_INFO("Screenshot saved: %%s", [path UTF8String]);
        [blockCopy release];
    }];
    [cb commit];
    [copy release];
}

- (BOOL)acceptsFirstResponder { return YES; }

@end
