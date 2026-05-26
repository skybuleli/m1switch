// ── Settings Panel ──────────────────────────────────────────
// Modern tabbed preferences window with live statistics.

#import "frontend/App/AppDelegate.h"
#import <Cocoa/Cocoa.h>

#include "common/Log.h"
#include "common/Config.h"

#import <Metal/Metal.h>

extern "C" {
void Audio_SetVolume(float vol);
float Audio_GetVolume();
double Gpu_GetFps();
size_t Gpu_GetShaderCount();
size_t Gpu_GetTextureCacheCount();
size_t Gpu_GetTextureCacheMemory();
u64    Cpu_GetSvcCallCount();
}

@interface SettingsController : NSWindowController <NSWindowDelegate, NSTabViewDelegate> {
    NSTimer*    _updateTimer;
    
    // 音频页
    NSSlider*   _volumeSlider;
    NSTextField* _volumeLabel;
    
    // 日志页
    NSPopUpButton* _logLevelPopUp;
    
    // 统计页
    NSTextField* _fpsLabel;
    NSTextField* _shaderLabel;
    NSTextField* _texLabel;
    NSTextField* _svcLabel;
    NSTextField* _gpuInfoLabel;
}

- (instancetype)init;
@end

@implementation SettingsController

- (instancetype)init {
    NSRect frame = NSMakeRect(0, 0, 480, 380);
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskFullSizeContentView
                    backing:NSBackingStoreBuffered defer:NO];
    window.title = @"Settings";
    window.titlebarAppearsTransparent = YES;
    window.releasedWhenClosed = NO;

    self = [super initWithWindow:window];
    if (!self) return nil;

    window.delegate = self;
    [self buildUI:window.contentView];
    return self;
}

- (void)buildUI:(NSView*)contentView {
    NSTabView* tabView = [[NSTabView alloc] initWithFrame:NSMakeRect(0, 0, 480, 380)];
    tabView.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:tabView];

    // ── 分段控制器（替代标签）────────────────────────
    NSSegmentedControl* seg = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
    seg.segmentCount = 3;
    [seg setLabel:@"Audio" forSegment:0];
    [seg setLabel:@"Logging" forSegment:1];
    [seg setLabel:@"Stats" forSegment:2];
    seg.selectedSegment = 0;
    seg.target = self;
    seg.action = @selector(segmentChanged:);
    seg.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:seg];

    // ── 标签页 ──────────────────────────────────────
    NSTabViewItem* audioTab = [[NSTabViewItem alloc] initWithIdentifier:@"audio"];
    NSTabViewItem* logTab = [[NSTabViewItem alloc] initWithIdentifier:@"log"];
    NSTabViewItem* statsTab = [[NSTabViewItem alloc] initWithIdentifier:@"stats"];
    [tabView addTabViewItem:audioTab];
    [tabView addTabViewItem:logTab];
    [tabView addTabViewItem:statsTab];

    // ── 构建各页内容 ─────────────────────────────────
    [self buildAudioTab:audioTab.view];
    [self buildLogTab:logTab.view];
    [self buildStatsTab:statsTab.view];

    [NSLayoutConstraint activateConstraints:@[
        [seg.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:16],
        [seg.topAnchor constraintEqualToAnchor:contentView.topAnchor constant:12],
        [seg.widthAnchor constraintEqualToConstant:320],

        [tabView.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:16],
        [tabView.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-16],
        [tabView.topAnchor constraintEqualToAnchor:seg.bottomAnchor constant:16],
        [tabView.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor constant:-16],
    ]];
}

- (void)buildAudioTab:(NSView*)view {
    // 音量标签
    NSTextField* desc = [[NSTextField alloc] initWithFrame:NSZeroRect];
    desc.stringValue = @"Master Volume";
    desc.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
    desc.bezeled = NO; desc.editable = NO; desc.selectable = NO;
    desc.backgroundColor = [NSColor clearColor];
    desc.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:desc];

    // 音量值显示
    _volumeLabel = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _volumeLabel.font = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightMedium];
    _volumeLabel.alignment = NSTextAlignmentRight;
    _volumeLabel.bezeled = NO; desc.editable = NO;
    _volumeLabel.backgroundColor = [NSColor clearColor];
    _volumeLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:_volumeLabel];

    // 滑条
    _volumeSlider = [[NSSlider alloc] initWithFrame:NSZeroRect];
    _volumeSlider.minValue = 0.0;
    _volumeSlider.maxValue = 1.0;
    _volumeSlider.doubleValue = Audio_GetVolume();
    _volumeSlider.target = self;
    _volumeSlider.action = @selector(volumeChanged:);
    _volumeSlider.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:_volumeSlider];

    _volumeLabel.stringValue = [NSString stringWithFormat:@"%d%%", (int)(_volumeSlider.doubleValue * 100)];

    // 提示
    NSTextField* hint = [[NSTextField alloc] initWithFrame:NSZeroRect];
    hint.stringValue = @"Audio output uses Core Audio for low-latency playback.";
    hint.font = [NSFont systemFontOfSize:11];
    hint.textColor = [NSColor secondaryLabelColor];
    hint.bezeled = NO; hint.editable = NO; hint.selectable = NO;
    hint.backgroundColor = [NSColor clearColor];
    hint.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:hint];

    [NSLayoutConstraint activateConstraints:@[
        [desc.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
        [desc.topAnchor constraintEqualToAnchor:view.topAnchor constant:20],

        [_volumeLabel.trailingAnchor constraintEqualToAnchor:view.trailingAnchor],
        [_volumeLabel.centerYAnchor constraintEqualToAnchor:desc.centerYAnchor],

        [_volumeSlider.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
        [_volumeSlider.trailingAnchor constraintEqualToAnchor:view.trailingAnchor],
        [_volumeSlider.topAnchor constraintEqualToAnchor:desc.bottomAnchor constant:12],

        [hint.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
        [hint.topAnchor constraintEqualToAnchor:_volumeSlider.bottomAnchor constant:20],
    ]];
}

- (void)buildLogTab:(NSView*)view {
    NSTextField* desc = [[NSTextField alloc] initWithFrame:NSZeroRect];
    desc.stringValue = @"Console Log Level";
    desc.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
    desc.bezeled = NO; desc.editable = NO; desc.selectable = NO;
    desc.backgroundColor = [NSColor clearColor];
    desc.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:desc];

    _logLevelPopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect];
    [_logLevelPopUp addItemsWithTitles:@[@"Trace", @"Debug", @"Info", @"Warn", @"Error"]];
    [_logLevelPopUp selectItemAtIndex:(NSInteger)Log::GetLevel()];
    _logLevelPopUp.target = self;
    _logLevelPopUp.action = @selector(logLevelChanged:);
    _logLevelPopUp.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:_logLevelPopUp];

    NSTextField* hint = [[NSTextField alloc] initWithFrame:NSZeroRect];
    hint.stringValue = @"Changes take effect immediately. Log output appears\nin the game window's log panel.";
    hint.font = [NSFont systemFontOfSize:11];
    hint.textColor = [NSColor secondaryLabelColor];
    hint.bezeled = NO; hint.editable = NO; hint.selectable = NO;
    hint.backgroundColor = [NSColor clearColor];
    hint.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:hint];

    [NSLayoutConstraint activateConstraints:@[
        [desc.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
        [desc.topAnchor constraintEqualToAnchor:view.topAnchor constant:20],

        [_logLevelPopUp.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
        [_logLevelPopUp.topAnchor constraintEqualToAnchor:desc.bottomAnchor constant:10],

        [hint.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
        [hint.topAnchor constraintEqualToAnchor:_logLevelPopUp.bottomAnchor constant:24],
    ]];
}

- (void)buildStatsTab:(NSView*)view {
    // 实时统计标签
    _fpsLabel = [self makeStatFieldAt:0 y:50 label:@"FPS" value:@"--" view:view];
    _shaderLabel = [self makeStatFieldAt:0 y:26 label:@"Shaders" value:@"--" view:view];
    _texLabel = [self makeStatFieldAt:0 y:2 label:@"Textures" value:@"--" view:view];
    _svcLabel = [self makeStatFieldAt:0 y:-22 label:@"SVC Calls" value:@"--" view:view];

    // GPU 信息
    _gpuInfoLabel = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _gpuInfoLabel.stringValue = [NSString stringWithFormat:@"GPU: %@",
                                  MTLCreateSystemDefaultDevice().name ?: @"Unknown"];
    _gpuInfoLabel.font = [NSFont systemFontOfSize:10];
    _gpuInfoLabel.textColor = [NSColor tertiaryLabelColor];
    _gpuInfoLabel.bezeled = NO; _gpuInfoLabel.editable = NO; _gpuInfoLabel.selectable = NO;
    _gpuInfoLabel.backgroundColor = [NSColor clearColor];
    _gpuInfoLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:_gpuInfoLabel];

    [NSLayoutConstraint activateConstraints:@[
        [_gpuInfoLabel.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
        [_gpuInfoLabel.bottomAnchor constraintEqualToAnchor:view.bottomAnchor constant:-8],
    ]];

    // 定时器
    if (!_updateTimer) {
        _updateTimer = [NSTimer scheduledTimerWithTimeInterval:0.5
                        target:self selector:@selector(updateStats:)
                        userInfo:nil repeats:YES];
    }
}

- (NSTextField*)makeStatFieldAt:(CGFloat)x y:(CGFloat)y label:(NSString*)label value:(NSString*)value view:(NSView*)parent {
    NSTextField* lbl = [[NSTextField alloc] initWithFrame:NSZeroRect];
    lbl.stringValue = [NSString stringWithFormat:@"%@  %@", label, value];
    lbl.font = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
    lbl.textColor = [NSColor labelColor];
    lbl.bezeled = NO; lbl.editable = NO; lbl.selectable = NO;
    lbl.backgroundColor = [NSColor clearColor];
    lbl.translatesAutoresizingMaskIntoConstraints = NO;
    [parent addSubview:lbl];

    [NSLayoutConstraint activateConstraints:@[
        [lbl.leadingAnchor constraintEqualToAnchor:parent.leadingAnchor],
        [lbl.topAnchor constraintEqualToAnchor:parent.topAnchor constant:20 + y],
    ]];
    return lbl;
}

// ═══════════════════════════════════════════════════════════
// Actions
// ═══════════════════════════════════════════════════════════
- (void)segmentChanged:(NSSegmentedControl*)sender {
    // 简单实现：切换 tab
    NSWindow* w = self.window;
    NSView* cv = w.contentView;
    for (NSView* sub in cv.subviews) {
        if ([sub isKindOfClass:[NSTabView class]]) {
            [(NSTabView*)sub selectTabViewItemAtIndex:sender.selectedSegment];
            break;
        }
    }
}

- (void)volumeChanged:(id)sender {
    float vol = (float)_volumeSlider.doubleValue;
    Audio_SetVolume(vol);
    Config::Instance().Audio().volume = vol;
    _volumeLabel.stringValue = [NSString stringWithFormat:@"%d%%", (int)(vol * 100)];
}

- (void)logLevelChanged:(id)sender {
    int level = (int)[_logLevelPopUp indexOfSelectedItem];
    Log::SetLevel(static_cast<LogLevel>(level));
}

- (void)updateStats:(NSTimer*)timer {
    double fps = Gpu_GetFps();
    _fpsLabel.stringValue = [NSString stringWithFormat:@"FPS  \t%.1f", fps];

    size_t shaders = Gpu_GetShaderCount();
    _shaderLabel.stringValue = [NSString stringWithFormat:@"Shaders  \t%zu", shaders];

    size_t texCount = Gpu_GetTextureCacheCount();
    size_t texMem = Gpu_GetTextureCacheMemory();
    _texLabel.stringValue = [NSString stringWithFormat:
        @"Textures  \t%zu (%.1f MB)", texCount, (double)texMem / (1024.0 * 1024.0)];

    u64 svcCount = Cpu_GetSvcCallCount();
    _svcLabel.stringValue = [NSString stringWithFormat:@"SVC Calls  \t%llu", svcCount];
}

- (void)windowWillClose:(NSNotification*)notification {
    [_updateTimer invalidate];
    _updateTimer = nil;
    Config::Instance().Save();
}

@end
