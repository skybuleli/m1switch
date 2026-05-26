// ── Debug Panel ────────────────────────────────────────────
// Technical debugging overlay: GPU state, memory layout,
// shader pipelines, thread info, IPC call log.

#import <Cocoa/Cocoa.h>

#include "common/Log.h"
#include "common/Types.h"

// ── C APIs for debug data ──────────────────────────────────
extern "C" {
double  Gpu_GetFps();
size_t  Gpu_GetShaderCount();
size_t  Gpu_GetTextureCacheCount();
size_t  Gpu_GetTextureCacheMemory();
u64     Cpu_GetSvcCallCount();
void    Gpu_DumpStats();
void    Memory_DumpPages();
}
extern "C" void ShaderCache_Invalidate();

@interface DebugPanelController : NSWindowController <NSWindowDelegate> {
    NSTextView* logView_;
    NSTimer*    updateTimer_;
    NSTextField* gpuDetailLabel_;
    NSTextField* memDetailLabel_;
    NSTextField* threadLabel_;
    NSTextField* svcLabel_;
    NSButton*   dumpGpuButton_;
    NSButton*   dumpMemButton_;
    NSButton*   reloadShadersButton_;
}

- (instancetype)init;
- (void)windowWillClose:(NSNotification*)notification;

@end

@implementation DebugPanelController

- (instancetype)init {
    NSRect frame = NSMakeRect(0, 0, 520, 500);
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskResizable |
                            NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered defer:NO];
    window.title = @"Debug";
    window.releasedWhenClosed = NO;

    self = [super initWithWindow:window];
    if (!self) return nil;

    window.delegate = self;
    [self buildUI:window.contentView];
    return self;
}

- (void)buildUI:(NSView*)contentView {
    CGFloat y = 470;

    // ── GPU state ──────────────────────────────────────
    NSTextField* gpuSec = [[NSTextField alloc] initWithFrame:NSMakeRect(15, y, 490, 18)];
    gpuSec.stringValue = @"GPU State";
    gpuSec.font = [NSFont boldSystemFontOfSize:12];
    gpuSec.bezeled = NO; gpuSec.editable = NO;
    gpuSec.backgroundColor = [NSColor clearColor];
    [contentView addSubview:gpuSec];
    y -= 22;

    gpuDetailLabel_ = [self makeLabel:15 y:y w:490];
    [contentView addSubview:gpuDetailLabel_];
    y -= 18;

    // ── Memory state ────────────────────────────────────
    NSTextField* memSec = [[NSTextField alloc] initWithFrame:NSMakeRect(15, y, 490, 18)];
    memSec.stringValue = @"Memory";
    memSec.font = [NSFont boldSystemFontOfSize:12];
    memSec.bezeled = NO; memSec.editable = NO;
    memSec.backgroundColor = [NSColor clearColor];
    [contentView addSubview:memSec];
    y -= 22;

    memDetailLabel_ = [self makeLabel:15 y:y w:490];
    [contentView addSubview:memDetailLabel_];
    y -= 18;

    // ── SVC / Thread info ───────────────────────────────
    svcLabel_ = [self makeLabel:15 y:y w:490];
    [contentView addSubview:svcLabel_];
    y -= 18;

    threadLabel_ = [self makeLabel:15 y:y w:490];
    [contentView addSubview:threadLabel_];
    y -= 22;

    // ── Action buttons ─────────────────────────────────
    dumpGpuButton_ = [[NSButton alloc] initWithFrame:NSMakeRect(15, y - 2, 140, 24)];
    dumpGpuButton_.title = @"Dump GPU Stats";
    dumpGpuButton_.bezelStyle = NSBezelStyleRounded;
    dumpGpuButton_.target = self;
    dumpGpuButton_.action = @selector(dumpGpu:);
    [contentView addSubview:dumpGpuButton_];

    dumpMemButton_ = [[NSButton alloc] initWithFrame:NSMakeRect(165, y - 2, 140, 24)];
    dumpMemButton_.title = @"Dump Memory Pages";
    dumpMemButton_.bezelStyle = NSBezelStyleRounded;
    dumpMemButton_.target = self;
    dumpMemButton_.action = @selector(dumpMem:);
    [contentView addSubview:dumpMemButton_];

    reloadShadersButton_ = [[NSButton alloc] initWithFrame:NSMakeRect(315, y - 2, 150, 24)];
    reloadShadersButton_.title = @"Reload Shaders";
    reloadShadersButton_.bezelStyle = NSBezelStyleRounded;
    reloadShadersButton_.target = self;
    reloadShadersButton_.action = @selector(reloadShaders:);
    [contentView addSubview:reloadShadersButton_];
    y -= 32;

    // ── Log output ─────────────────────────────────────
    NSTextField* logSec = [[NSTextField alloc] initWithFrame:NSMakeRect(15, y, 490, 18)];
    logSec.stringValue = @"Log";
    logSec.font = [NSFont boldSystemFontOfSize:12];
    logSec.bezeled = NO; logSec.editable = NO;
    logSec.backgroundColor = [NSColor clearColor];
    [contentView addSubview:logSec];
    y -= 20;

    logView_ = [[NSTextView alloc] initWithFrame:NSMakeRect(15, 10, 490, y - 10)];
    logView_.editable = NO;
    logView_.font = [NSFont fontWithName:@"Menlo" size:10];
    logView_.backgroundColor = [NSColor colorWithWhite:0.95 alpha:1.0];
    logView_.textColor = [NSColor blackColor];
    [contentView addSubview:logView_];

    // ── Update timer (4 Hz) ────────────────────────────
    updateTimer_ = [NSTimer scheduledTimerWithTimeInterval:0.25
                    target:self selector:@selector(updateStats:)
                    userInfo:nil repeats:YES];
}

- (NSTextField*)makeLabel:(CGFloat)x y:(CGFloat)y w:(CGFloat)w {
    NSTextField* f = [[NSTextField alloc] initWithFrame:NSMakeRect(x, y, w, 16)];
    f.stringValue = @"";
    f.bezeled = NO; f.editable = NO;
    f.font = [NSFont systemFontOfSize:11];
    f.textColor = [NSColor grayColor];
    f.backgroundColor = [NSColor clearColor];
    return f;
}

- (void)updateStats:(NSTimer*)timer {
    double fps = Gpu_GetFps();
    size_t shaders = Gpu_GetShaderCount();
    size_t texCount = Gpu_GetTextureCacheCount();
    size_t texMem = Gpu_GetTextureCacheMemory();
    u64 svcCount = Cpu_GetSvcCallCount();

    gpuDetailLabel_.stringValue = [NSString stringWithFormat:
        @"FPS: %.1f  |  Shaders: %zu  |  TexCache: %zu (%.1f MB)",
        fps, shaders, texCount, (double)texMem / (1024.0 * 1024.0)];

    svcLabel_.stringValue = [NSString stringWithFormat:
        @"SVC calls: %llu", svcCount];
}

- (void)dumpGpu:(id)sender {
    Gpu_DumpStats();
    [self log:@"GPU stats dumped to console"];
}

- (void)dumpMem:(id)sender {
    Memory_DumpPages();
    [self log:@"Memory page dump sent to console"];
}

- (void)reloadShaders:(id)sender {
    ShaderCache_Invalidate();
    [self log:@"Shader cache invalidated"];
}

- (void)log:(NSString*)msg {
    NSTextStorage* ts = logView_.textStorage;
    NSString* line = [NSString stringWithFormat:@"[Debug] %@\n", msg];
    [ts appendAttributedString:[[NSAttributedString alloc]
        initWithString:line
            attributes:@{NSForegroundColorAttributeName: [NSColor darkGrayColor]}]];
    [logView_ scrollToEndOfDocument:nil];
}

- (void)windowWillClose:(NSNotification*)notification {
    [updateTimer_ invalidate];
}

@end
