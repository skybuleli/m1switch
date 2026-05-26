// ── Debug Panel ────────────────────────────────────────────
// Technical debugging overlay: GPU state, memory layout,
// shader pipelines, thread info, IPC call log,
// CPU registers, breakpoints, memory viewer.

#import <Cocoa/Cocoa.h>

#include "common/Log.h"
#include "common/Types.h"
#include "cpu/Debugger.h"
#include "memory/Memory.h"

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

// ── Tab types for segmented control ────────────────────────
enum DebugTab : NSInteger {
    TabGPU = 0,
    TabCPU = 1,
    TabMemory = 2,
    TabBreakpoints = 3,
    TabLog = 4,
};

@interface DebugPanelController : NSWindowController <NSWindowDelegate> {
    // 标签切换
    NSSegmentedControl* tabControl_;
    NSView*             tabContent_[5];

    // Tab 0: GPU
    NSTextField* gpuDetailLabel_;
    NSTextField* svcLabel_;
    NSTextField* threadLabel_;

    // Tab 1: CPU 寄存器
    NSTextField* regLabels_[35];  // x0-x30, sp, pc, pstate, tpidrro

    // Tab 2: 内存查看
    NSTextField* memAddrField_;
    NSTextField* memDataView_;
    NSButton*    memRefreshBtn_;

    // Tab 3: 断点
    NSTableView* bpTableView_;
    NSButton*    bpSetBtn_;
    NSButton*    bpClearBtn_;
    NSButton*    bpClearAllBtn_;
    NSTextField* bpAddrField_;

    // Tab 4: 日志
    NSTextView* logView_;

    // 通用
    NSTimer*    updateTimer_;
}

@end

@implementation DebugPanelController

- (instancetype)init {
    NSRect frame = NSMakeRect(0, 0, 620, 560);
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
    [self switchToTab:TabGPU];
    return self;
}

// ═══════════════════════════════════════════════════════════
//  UI 构建
// ═══════════════════════════════════════════════════════════

- (void)buildUI:(NSView*)contentView {
    CGFloat w = 600, h = 480;

    // ── 标签栏 ────────────────────────────────────────
    tabControl_ = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(10, h - 5, w - 20, 24)];
    tabControl_.segmentCount = 5;
    [tabControl_ setLabel:@"GPU"     forSegment:TabGPU];
    [tabControl_ setLabel:@"CPU"     forSegment:TabCPU];
    [tabControl_ setLabel:@"Memory"  forSegment:TabMemory];
    [tabControl_ setLabel:@"Breakpt" forSegment:TabBreakpoints];
    [tabControl_ setLabel:@"Log"     forSegment:TabLog];
    tabControl_.target = self;
    tabControl_.action = @selector(tabChanged:);
    tabControl_.segmentStyle = NSSegmentStyleTexturedRounded;
    [contentView addSubview:tabControl_];

    CGFloat tabY = 10;
    CGFloat tabH = h - 40;

    // ── Tab 0: GPU ─────────────────────────────────────
    tabContent_[TabGPU] = [self makeTabView:contentView];
    [self buildGPUTab:tabContent_[TabGPU] y:tabH];

    // ── Tab 1: CPU ─────────────────────────────────────
    tabContent_[TabCPU] = [self makeTabView:contentView];
    [self buildCPUTab:tabContent_[TabCPU] y:tabH];

    // ── Tab 2: Memory ──────────────────────────────────
    tabContent_[TabMemory] = [self makeTabView:contentView];
    [self buildMemoryTab:tabContent_[TabMemory] y:tabH];

    // ── Tab 3: Breakpoints ─────────────────────────────
    tabContent_[TabBreakpoints] = [self makeTabView:contentView];
    [self buildBreakpointTab:tabContent_[TabBreakpoints] y:tabH];

    // ── Tab 4: Log ─────────────────────────────────────
    tabContent_[TabLog] = [self makeTabView:contentView];
    [self buildLogTab:tabContent_[TabLog] y:tabH];

    // ── Update timer (4 Hz) ────────────────────────────
    updateTimer_ = [NSTimer scheduledTimerWithTimeInterval:0.25
                    target:self selector:@selector(updateStats:)
                    userInfo:nil repeats:YES];
}

- (NSView*)makeTabView:(NSView*)parent {
    NSView* v = [[NSView alloc] initWithFrame:NSMakeRect(10, 10, 600, 440)];
    [parent addSubview:v];
    return v;
}

- (void)switchToTab:(DebugTab)tab {
    tabControl_.selectedSegment = tab;
    for (int i = 0; i < 5; i++) {
        tabContent_[i].hidden = (i != tab);
    }
}

- (void)tabChanged:(id)sender {
    [self switchToTab:(DebugTab)tabControl_.selectedSegment];
}

// ── Tab 0: GPU ────────────────────────────────────────────
- (void)buildGPUTab:(NSView*)view y:(CGFloat)h {
    CGFloat y = h - 10;

    // 执行控制
    NSButton* pauseBtn = [self makeButton:10 y:y-2 w:80 title:@"⏸ Pause"];
    pauseBtn.action = @selector(pauseClicked:);
    [view addSubview:pauseBtn];

    NSButton* contBtn = [self makeButton:100 y:y-2 w:80 title:@"▶ Continue"];
    contBtn.action = @selector(continueClicked:);
    [view addSubview:contBtn];

    NSButton* stepBtn = [self makeButton:190 y:y-2 w:80 title:@"⏭ Step"];
    stepBtn.action = @selector(stepClicked:);
    [view addSubview:stepBtn];
    y -= 30;

    // GPU 状态
    [self addSectionLabel:view title:@"GPU State" y:&y];
    gpuDetailLabel_ = [self makeLabel:10 y:y w:580];
    [view addSubview:gpuDetailLabel_];
    y -= 20;

    // SVC 计数
    svcLabel_ = [self makeLabel:10 y:y w:580];
    [view addSubview:svcLabel_];
    y -= 20;

    threadLabel_ = [self makeLabel:10 y:y w:580];
    [view addSubview:threadLabel_];
    y -= 24;

    // 操作按钮
    NSButton* dumpGpuBtn = [self makeButton:10 y:y-2 w:140 title:@"Dump GPU Stats"];
    dumpGpuBtn.action = @selector(dumpGpu:);
    [view addSubview:dumpGpuBtn];

    NSButton* dumpMemBtn = [self makeButton:160 y:y-2 w:150 title:@"Dump Memory Pages"];
    dumpMemBtn.action = @selector(dumpMem:);
    [view addSubview:dumpMemBtn];

    NSButton* reloadBtn = [self makeButton:320 y:y-2 w:150 title:@"Reload Shaders"];
    reloadBtn.action = @selector(reloadShaders:);
    [view addSubview:reloadBtn];
}

// ── Tab 1: CPU Registers ──────────────────────────────────
- (void)buildCPUTab:(NSView*)view y:(CGFloat)h {
    CGFloat y = h - 10;

    // 寄存器显示（5 列网格：3 个寄存器 + 标签/值）
    static const char* regNames[35] = {
        "x0","x1","x2","x3","x4","x5","x6","x7",
        "x8","x9","x10","x11","x12","x13","x14","x15",
        "x16","x17","x18","x19","x20","x21","x22","x23",
        "x24","x25","x26","x27","x28","x29(fp)","x30(lr)",
        "sp","pc","pstate","tpidrro"
    };

    [self addSectionLabel:view title:@"CPU Registers (last SVC/BP)" y:&y];
    y -= 6;

    int cols = 3;
    CGFloat regW = 180;
    CGFloat regH = 16;
    CGFloat gapX = 8;
    CGFloat gapY = 2;

    for (int i = 0; i < 35; i++) {
        int col = i % cols;
        int row = i / cols;
        CGFloat rx = 10 + col * (regW + gapX);
        CGFloat ry = y - row * (regH + gapY);

        regLabels_[i] = [[NSTextField alloc] initWithFrame:NSMakeRect(rx, ry, regW, regH)];
        regLabels_[i].stringValue = [NSString stringWithFormat:@"%s: 0x0000000000000000", regNames[i]];
        regLabels_[i].font = [NSFont fontWithName:@"Menlo" size:10];
        regLabels_[i].bezeled = NO;
        regLabels_[i].editable = NO;
        regLabels_[i].backgroundColor = [NSColor clearColor];
        regLabels_[i].textColor = [NSColor blackColor];
        [view addSubview:regLabels_[i]];
    }
}

// ── Tab 2: Memory Viewer ──────────────────────────────────
- (void)buildMemoryTab:(NSView*)view y:(CGFloat)h {
    CGFloat y = h - 10;

    [self addSectionLabel:view title:@"Memory Viewer" y:&y];
    y -= 6;

    // 地址输入
    NSTextField* addrLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(10, y-1, 50, 18)];
    addrLabel.stringValue = @"Addr: 0x";
    addrLabel.font = [NSFont systemFontOfSize:12];
    addrLabel.bezeled = NO; addrLabel.editable = NO;
    addrLabel.backgroundColor = [NSColor clearColor];
    [view addSubview:addrLabel];

    memAddrField_ = [[NSTextField alloc] initWithFrame:NSMakeRect(65, y-2, 160, 22)];
    memAddrField_.stringValue = @"0";
    memAddrField_.font = [NSFont fontWithName:@"Menlo" size:12];
    [view addSubview:memAddrField_];

    memRefreshBtn_ = [[NSButton alloc] initWithFrame:NSMakeRect(235, y-2, 80, 24)];
    memRefreshBtn_.title = @"Refresh";
    memRefreshBtn_.bezelStyle = NSBezelStyleRounded;
    memRefreshBtn_.target = self;
    memRefreshBtn_.action = @selector(refreshMemory:);
    [view addSubview:memRefreshBtn_];

    y -= 28;

    // 十六进制数据显示
    memDataView_ = [[NSTextField alloc] initWithFrame:NSMakeRect(10, 10, 580, y - 10)];
    memDataView_.font = [NSFont fontWithName:@"Menlo" size:10];
    memDataView_.bezeled = YES;
    memDataView_.editable = NO;
    memDataView_.drawsBackground = YES;
    memDataView_.backgroundColor = [NSColor colorWithWhite:0.95 alpha:1.0];
    memDataView_.textColor = [NSColor blackColor];
    [view addSubview:memDataView_];
}

// ── Tab 3: Breakpoints ────────────────────────────────────
- (void)buildBreakpointTab:(NSView*)view y:(CGFloat)h {
    CGFloat y = h - 10;

    [self addSectionLabel:view title:@"Breakpoints" y:&y];
    y -= 6;

    // 地址输入 + 按钮
    NSTextField* bpLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(10, y-1, 60, 18)];
    bpLabel.stringValue = @"Address: 0x";
    bpLabel.font = [NSFont systemFontOfSize:12];
    bpLabel.bezeled = NO; bpLabel.editable = NO;
    bpLabel.backgroundColor = [NSColor clearColor];
    [view addSubview:bpLabel];

    bpAddrField_ = [[NSTextField alloc] initWithFrame:NSMakeRect(75, y-2, 160, 22)];
    bpAddrField_.stringValue = @"0";
    bpAddrField_.font = [NSFont fontWithName:@"Menlo" size:12];
    [view addSubview:bpAddrField_];

    bpSetBtn_ = [[NSButton alloc] initWithFrame:NSMakeRect(245, y-2, 80, 24)];
    bpSetBtn_.title = @"Set BP";
    bpSetBtn_.bezelStyle = NSBezelStyleRounded;
    bpSetBtn_.target = self;
    bpSetBtn_.action = @selector(setBreakpoint:);
    [view addSubview:bpSetBtn_];

    bpClearBtn_ = [[NSButton alloc] initWithFrame:NSMakeRect(330, y-2, 80, 24)];
    bpClearBtn_.title = @"Clear BP";
    bpClearBtn_.bezelStyle = NSBezelStyleRounded;
    bpClearBtn_.target = self;
    bpClearBtn_.action = @selector(clearBreakpoint:);
    [view addSubview:bpClearBtn_];

    bpClearAllBtn_ = [[NSButton alloc] initWithFrame:NSMakeRect(415, y-2, 100, 24)];
    bpClearAllBtn_.title = @"Clear All";
    bpClearAllBtn_.bezelStyle = NSBezelStyleRounded;
    bpClearAllBtn_.target = self;
    bpClearAllBtn_.action = @selector(clearAllBreakpoints:);
    [view addSubview:bpClearAllBtn_];

    y -= 30;

    // 断点列表
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(10, 10, 580, y - 10)];
    bpTableView_ = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 560, y - 30)];

    NSTableColumn* addrCol = [[NSTableColumn alloc] initWithIdentifier:@"addr"];
    addrCol.title = @"Address";
    addrCol.width = 160;
    [bpTableView_ addTableColumn:addrCol];

    NSTableColumn* hitsCol = [[NSTableColumn alloc] initWithIdentifier:@"hits"];
    hitsCol.title = @"Hits";
    hitsCol.width = 60;
    [bpTableView_ addTableColumn:hitsCol];

    NSTableColumn* stateCol = [[NSTableColumn alloc] initWithIdentifier:@"state"];
    stateCol.title = @"State";
    stateCol.width = 80;
    [bpTableView_ addTableColumn:stateCol];

    bpTableView_.headerView = [[NSTableHeaderView alloc] init];
    scroll.documentView = bpTableView_;
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    [view addSubview:scroll];
}

// ── Tab 4: Log ────────────────────────────────────────────
- (void)buildLogTab:(NSView*)view y:(CGFloat)h {
    logView_ = [[NSTextView alloc] initWithFrame:NSMakeRect(10, 10, 580, h - 20)];
    logView_.editable = NO;
    logView_.font = [NSFont fontWithName:@"Menlo" size:10];
    logView_.backgroundColor = [NSColor colorWithWhite:0.95 alpha:1.0];
    logView_.textColor = [NSColor blackColor];
    [view addSubview:logView_];
}

// ═══════════════════════════════════════════════════════════
//  辅助方法
// ═══════════════════════════════════════════════════════════

- (void)addSectionLabel:(NSView*)parent title:(NSString*)title y:(CGFloat*)y {
    NSTextField* label = [[NSTextField alloc] initWithFrame:NSMakeRect(10, *y - 18, 580, 18)];
    label.stringValue = title;
    label.font = [NSFont boldSystemFontOfSize:12];
    label.bezeled = NO; label.editable = NO;
    label.backgroundColor = [NSColor clearColor];
    [parent addSubview:label];
    *y -= 22;
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

- (NSButton*)makeButton:(CGFloat)x y:(CGFloat)y w:(CGFloat)w title:(NSString*)title {
    NSButton* btn = [[NSButton alloc] initWithFrame:NSMakeRect(x, y, w, 24)];
    btn.title = title;
    btn.bezelStyle = NSBezelStyleRounded;
    btn.target = self;
    return btn;
}

// ═══════════════════════════════════════════════════════════
//  更新 & 动作
// ═══════════════════════════════════════════════════════════

- (void)updateStats:(NSTimer*)timer {
    // ── GPU tab stats ─────────────────────────────────
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

    auto& dbg = GlobalDebugger();
    threadLabel_.stringValue = [NSString stringWithFormat:
        @"Debugger: %@ | BPs: %zu",
        dbg.IsPaused() ? @"PAUSED" : @"RUNNING",
        dbg.GetBreakpointCount()];

    // ── CPU register tab ──────────────────────────────
    CpuRegisterSnapshot regs = dbg.GetLastRegisters();
    static const char* regNames[35] = {
        "x0","x1","x2","x3","x4","x5","x6","x7",
        "x8","x9","x10","x11","x12","x13","x14","x15",
        "x16","x17","x18","x19","x20","x21","x22","x23",
        "x24","x25","x26","x27","x28","x29(fp)","x30(lr)",
        "sp","pc","pstate","tpidrro"
    };
    u64 regValues[35] = {};
    for (int i = 0; i < 31; i++) regValues[i] = regs.x[i];
    regValues[31] = regs.sp;
    regValues[32] = regs.pc;
    regValues[33] = regs.pstate;
    regValues[34] = regs.tpidrro_el0;

    for (int i = 0; i < 35; i++) {
        regLabels_[i].stringValue = [NSString stringWithFormat:
            @"%s: 0x%016llx", regNames[i], regValues[i]];
    }

    // ── Memory tab ────────────────────────────────────
    // (只在点击 Refresh 时更新)

    // ── Breakpoint tab ────────────────────────────────
    // (只在修改时更新)
}

// ── GPU actions ───────────────────────────────────────────

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

// ── Execution control ─────────────────────────────────────

- (void)pauseClicked:(id)sender {
    GlobalDebugger().Pause();
    [self log:@"Execution paused"];
}

- (void)continueClicked:(id)sender {
    GlobalDebugger().Continue();
    [self log:@"Execution continued"];
}

- (void)stepClicked:(id)sender {
    GlobalDebugger().StepOver();
    [self log:@"Step over"];
}

// ── Memory viewer ─────────────────────────────────────────

- (void)refreshMemory:(id)sender {
    NSString* addrStr = memAddrField_.stringValue;
    if (addrStr.length == 0) return;

    // 解析地址（支持 0x 前缀和纯十进制）
    unsigned long long addr = 0;
    NSScanner* scanner = [NSScanner scannerWithString:addrStr];
    [scanner scanHexLongLong:&addr];

    auto& dbg = GlobalDebugger();
    auto data = dbg.ReadMemory(addr, 256);
    if (data.empty()) {
        memDataView_.stringValue = @"(no memory data)";
        return;
    }

    NSMutableString* hex = [NSMutableString string];
    for (size_t i = 0; i < data.size(); i += 16) {
        [hex appendFormat:@"%08llx: ", (unsigned long long)(addr + i)];
        for (size_t j = 0; j < 16 && (i + j) < data.size(); j++) {
            [hex appendFormat:@"%02x ", data[i + j]];
        }
        [hex appendString:@" "];
        for (size_t j = 0; j < 16 && (i + j) < data.size(); j++) {
            char c = data[i + j];
            [hex appendFormat:@"%c", (c >= 32 && c < 127) ? c : '.'];
        }
        [hex appendString:@"\n"];
    }
    memDataView_.stringValue = hex;
}

// ── Breakpoints ───────────────────────────────────────────

- (void)setBreakpoint:(id)sender {
    NSString* addrStr = bpAddrField_.stringValue;
    if (addrStr.length == 0) return;
    unsigned long long addr = 0;
    NSScanner* scanner = [NSScanner scannerWithString:addrStr];
    [scanner scanHexLongLong:&addr];

    GlobalDebugger().SetBreakpoint(addr);
    [self log:[NSString stringWithFormat:@"Breakpoint set at 0x%llx", addr]];
    [self refreshBreakpointList];
}

- (void)clearBreakpoint:(id)sender {
    NSString* addrStr = bpAddrField_.stringValue;
    if (addrStr.length == 0) return;
    unsigned long long addr = 0;
    NSScanner* scanner = [NSScanner scannerWithString:addrStr];
    [scanner scanHexLongLong:&addr];

    GlobalDebugger().RemoveBreakpoint(addr);
    [self log:[NSString stringWithFormat:@"Breakpoint cleared at 0x%llx", addr]];
    [self refreshBreakpointList];
}

- (void)clearAllBreakpoints:(id)sender {
    GlobalDebugger().ClearAllBreakpoints();
    [self log:@"All breakpoints cleared"];
    [self refreshBreakpointList];
}

- (void)refreshBreakpointList {
    auto bps = GlobalDebugger().GetBreakpoints();
    // 简化为在日志中显示
    [self log:[NSString stringWithFormat:@"Active BPs: %zu", bps.size()]];
    for (auto& bp : bps) {
        [self log:[NSString stringWithFormat:@"  0x%llx (hits: %u, %s)",
                   bp.guest_address, bp.hit_count,
                   bp.enabled ? "enabled" : "disabled"]];
    }
}

// ── Log ───────────────────────────────────────────────────

- (void)log:(NSString*)msg {
    if (!logView_) return;
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
