// ── Settings Panel ──────────────────────────────────────────
// Provides UI to view and modify emulator configuration.
// Connected to the Config system for persistence.

#import "frontend/App/AppDelegate.h"
#import <Cocoa/Cocoa.h>

#include "common/Log.h"
#include "common/Config.h"

// ── C API for audio volume ─────────────────────────────────
extern "C" {
void Audio_SetVolume(float vol);
float Audio_GetVolume();
}

@interface SettingsController : NSWindowController <NSWindowDelegate> {
    NSSlider*   volumeSlider_;
    NSPopUpButton* logLevelPopUp_;
    NSTextField* fpsLabel_;
    NSTextField* gpuLabel_;
    NSTextField* memLabel_;
    NSTimer*    updateTimer_;
    NSTextField* shaderLabel_;
    NSTextField* texCacheLabel_;
}

- (instancetype)init;
- (void)windowWillClose:(NSNotification*)notification;

@end

// Forward declarations for runtime stats
extern "C" {
size_t Gpu_GetShaderCount();
size_t Gpu_GetTextureCacheCount();
size_t Gpu_GetTextureCacheMemory();
u64    Cpu_GetSvcCallCount();
double Gpu_GetFps();
}

@implementation SettingsController

- (instancetype)init {
    NSRect frame = NSMakeRect(0, 0, 420, 360);
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered defer:NO];
    window.title = @"Settings";
    window.releasedWhenClosed = NO;

    self = [super initWithWindow:window];
    if (!self) return nil;

    window.delegate = self;
    [self buildUI:window.contentView];
    return self;
}

- (void)buildUI:(NSView*)contentView {
    CGFloat y = 330;

    // ── Audio section ──────────────────────────────────
    NSTextField* audioLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, y, 380, 18)];
    audioLabel.stringValue = @"Audio";
    audioLabel.font = [NSFont boldSystemFontOfSize:13];
    audioLabel.bezeled = NO;
    audioLabel.editable = NO;
    audioLabel.backgroundColor = [NSColor clearColor];
    [contentView addSubview:audioLabel];
    y -= 28;

    NSTextField* volLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(30, y, 60, 18)];
    volLabel.stringValue = @"Volume:";
    volLabel.bezeled = NO;
    volLabel.editable = NO;
    volLabel.backgroundColor = [NSColor clearColor];
    [contentView addSubview:volLabel];

    volumeSlider_ = [[NSSlider alloc] initWithFrame:NSMakeRect(100, y - 2, 200, 22)];
    volumeSlider_.minValue = 0.0;
    volumeSlider_.maxValue = 1.0;
    volumeSlider_.doubleValue = Audio_GetVolume();
    volumeSlider_.target = self;
    volumeSlider_.action = @selector(volumeChanged:);
    [contentView addSubview:volumeSlider_];
    y -= 30;

    // ── Log section ────────────────────────────────────
    NSTextField* logLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, y, 380, 18)];
    logLabel.stringValue = @"Logging";
    logLabel.font = [NSFont boldSystemFontOfSize:13];
    logLabel.bezeled = NO;
    logLabel.editable = NO;
    logLabel.backgroundColor = [NSColor clearColor];
    [contentView addSubview:logLabel];
    y -= 28;

    NSTextField* llLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(30, y, 100, 18)];
    llLabel.stringValue = @"Log Level:";
    llLabel.bezeled = NO;
    llLabel.editable = NO;
    llLabel.backgroundColor = [NSColor clearColor];
    [contentView addSubview:llLabel];

    logLevelPopUp_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(140, y - 4, 120, 22)];
    [logLevelPopUp_ addItemsWithTitles:@[@"Trace", @"Debug", @"Info", @"Warn", @"Error"]];
    [logLevelPopUp_ selectItemAtIndex:(NSInteger)Log::GetLevel()];
    logLevelPopUp_.target = self;
    logLevelPopUp_.action = @selector(logLevelChanged:);
    [contentView addSubview:logLevelPopUp_];
    y -= 34;

    // ── Separator ──────────────────────────────────────
    NSBox* sep = [[NSBox alloc] initWithFrame:NSMakeRect(20, y, 380, 1)];
    sep.boxType = NSBoxSeparator;
    [contentView addSubview:sep];
    y -= 20;

    // ── Statistics section ──────────────────────────────
    NSTextField* statsLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, y, 380, 18)];
    statsLabel.stringValue = @"Runtime Statistics";
    statsLabel.font = [NSFont boldSystemFontOfSize:13];
    statsLabel.bezeled = NO;
    statsLabel.editable = NO;
    statsLabel.backgroundColor = [NSColor clearColor];
    [contentView addSubview:statsLabel];
    y -= 28;

    fpsLabel_ = [self makeStatFieldAt:30 y:y text:@"FPS: --"];
    [contentView addSubview:fpsLabel_];
    y -= 20;

    gpuLabel_ = [self makeStatFieldAt:30 y:y text:@"Shaders compiled: --"];
    [contentView addSubview:gpuLabel_];
    y -= 20;

    shaderLabel_ = [self makeStatFieldAt:30 y:y text:@"Pipeline cache: --"];
    [contentView addSubview:shaderLabel_];
    y -= 20;

    texCacheLabel_ = [self makeStatFieldAt:30 y:y text:@"Texture cache: --"];
    [contentView addSubview:texCacheLabel_];
    y -= 20;

    memLabel_ = [self makeStatFieldAt:30 y:y text:@"Memory: --"];
    [contentView addSubview:memLabel_];

    // ── Update timer (2 Hz) ────────────────────────────
    updateTimer_ = [NSTimer scheduledTimerWithTimeInterval:0.5
                    target:self selector:@selector(updateStats:)
                    userInfo:nil repeats:YES];
}

- (NSTextField*)makeStatFieldAt:(CGFloat)x y:(CGFloat)y text:(NSString*)text {
    NSTextField* f = [[NSTextField alloc] initWithFrame:NSMakeRect(x, y, 370, 16)];
    f.stringValue = text;
    f.bezeled = NO;
    f.editable = NO;
    f.font = [NSFont systemFontOfSize:11];
    f.textColor = [NSColor darkGrayColor];
    f.backgroundColor = [NSColor clearColor];
    return f;
}

- (void)volumeChanged:(id)sender {
    float vol = (float)volumeSlider_.doubleValue;
    Audio_SetVolume(vol);
    Config::Instance().Audio().volume = vol;
}

- (void)logLevelChanged:(id)sender {
    int level = (int)[logLevelPopUp_ indexOfSelectedItem];
    Log::SetLevel(static_cast<LogLevel>(level));
}

- (void)updateStats:(NSTimer*)timer {
    double fps = Gpu_GetFps();
    fpsLabel_.stringValue = [NSString stringWithFormat:@"FPS: %.1f", fps];

    size_t shaders = Gpu_GetShaderCount();
    gpuLabel_.stringValue = [NSString stringWithFormat:@"Shaders compiled: %zu", shaders];

    size_t texCount = Gpu_GetTextureCacheCount();
    size_t texMem = Gpu_GetTextureCacheMemory();
    texCacheLabel_.stringValue = [NSString stringWithFormat:
        @"Texture cache: %zu entries (%.1f MB)", texCount, (double)texMem / (1024.0 * 1024.0)];

    u64 svcCount = Cpu_GetSvcCallCount();
    shaderLabel_.stringValue = [NSString stringWithFormat:@"SVC calls: %llu", svcCount];
}

- (void)windowWillClose:(NSNotification*)notification {
    [updateTimer_ invalidate];
    Config::Instance().Save();
}

@end
