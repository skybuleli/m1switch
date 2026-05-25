#import "EmuScreenView.h"
#include "common/Log.h"
#include "services/Nv.h"
#include "loader/NroLoader.h"
#include "cpu/NativeExec.h"
#include "cpu/ExceptionHandler.h"
#include "kernel/SvcTable.h"


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


// ── Load NRO and start emulation ────────────────────────────
- (void)loadAndRunNRO:(const char*)path {
    LOG_INFO("EmuScreenView: loading %s", path);

    NroLoader loader(*_memory);
    NroLoadInfo info;
    Result r = loader.LoadFromFile(path, info);
    if (Failed(r)) { LOG_ERROR("Failed to load NRO"); return; }

    if (info.segments.empty()) { LOG_ERROR("NRO has no segments"); return; }

    auto& seg = info.segments[0];
    u8* text = _memory->Pointer(seg.guest_address);
    if (!text) { LOG_ERROR("Bad text pointer"); return; }

    std::vector<std::pair<u32, u32>> svc_map;
    NativeExec::PatchSVCs(text, seg.size, svc_map);
    LOG_INFO("Patched %zu SVCs, entry=0x%llx", svc_map.size(), info.entry_point);

    _memory->SetupStack(0x100000);
    SvcTable_Init();

    // Set up NV service wiring
    ServiceNv_SetMemory(_memory);
    ServiceNv_SetGpuFifo(&_tracker->GetGPFifo());
    ServiceNv_SetTracker(_tracker);
    _tracker->SetMemory(_memory);

    // Install SIGTRAP handler for SVC dispatch
    SigHandler sig_handler;
    sig_handler.SetSvcDispatch([](u32 svc, GuestThreadState* st) {
        SvcHandler_Dispatch(svc, st);
    });
    sig_handler.Install();

    LOG_INFO("Starting guest: PC=0x%llx, SP=0x%llx",
             _memory->BaseAddress() + info.entry_point,
             _memory->BaseAddress() + _memory->GetStackTop());

    NativeExec::RunGuest(
        _memory->BaseAddress() + info.entry_point,
        _memory->BaseAddress() + _memory->GetStackTop(), 0);
}

- (void)toggleFullscreen {
    NSWindow* w = self.window;
    if (w) [w toggleFullScreen:nil];
}

- (void)captureScreenshot:(id)sender {
    // Capture current Metal drawable and save to desktop
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
        // Create bitmap
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
