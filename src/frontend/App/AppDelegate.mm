#import "AppDelegate.h"
#import "frontend/GameView/EmuScreenView.h"
#import "frontend/GameView/LogPanelView.h"
#import "frontend/Library/GameModel.h"
#import "frontend/Library/GameScanner.h"
#import "frontend/Library/LibraryGrid.h"
#import "frontend/Library/LibrarySidebar.h"
#import "frontend/Settings/SettingsController.h"
#import "frontend/Debug/DebugPanelController.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "common/Log.h"
#include "common/Config.h"

@interface AppDelegate () {
    GameLibrary _gameLib;
    GameScanner _scanner;
    EmulatorCore _core;
    SettingsController* _settingsController;
    DebugPanelController* _debugController;
}
@property (nonatomic, strong) LibraryGrid* gridView;
@property (nonatomic, strong) LibrarySidebar* sideView;
@end

@implementation AppDelegate

- (EmulatorCore*)core { return &_core; }

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    LOG_INFO("Application did finish launching");

    // ── 核心初始化 ─────────────────────────────────
    [self initCore];

    // ── UI 构建 ─────────────────────────────────────
    [self createLibraryWindow];
    [self setupMainMenu];
}

- (void)initCore {
    @try {
        Config::Instance().Load();
        _core.Initialize();
        LOG_INFO("Core initialized successfully");
    } @catch (NSException* e) {
        LOG_ERROR("Init exception: %s reason: %s",
                  [[e name] UTF8String], [[e reason] UTF8String]);
    } @catch (...) {
        LOG_ERROR("Init: unknown C++ exception");
    }
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    _core.Stop();
    Config::Instance().Save();
    const char* home = getenv("HOME");
    if (home) {
        _gameLib.Save(std::string(home) + "/Library/Application Support/m1switch/library.json");
    }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}

// ═══════════════════════════════════════════════════════════
// Library Window
// ═══════════════════════════════════════════════════════════
- (void)createLibraryWindow {
    NSRect frame = NSMakeRect(0, 0, 960, 660);
    _libraryWindow = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                            NSWindowStyleMaskFullSizeContentView
                    backing:NSBackingStoreBuffered defer:NO];
    _libraryWindow.titlebarAppearsTransparent = YES;
    _libraryWindow.titleVisibility = NSWindowTitleVisible;
    _libraryWindow.title = @"M1Switch";
    _libraryWindow.minSize = NSMakeSize(640, 480);
    _libraryWindow.backgroundColor = [NSColor windowBackgroundColor];

    // ── 加载游戏库 ──────────────────────────────────
    [self loadGameLibrary];

    // ── 分割视图 ────────────────────────────────────
    NSSplitView* sv = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    sv.vertical = YES;
    sv.dividerStyle = NSSplitViewDividerStyleThin;
    sv.translatesAutoresizingMaskIntoConstraints = NO;

    _sideView = [[LibrarySidebar alloc] initWithFrame:NSMakeRect(0, 0, 200, 100) library:&_gameLib];
    _gridView = [[LibraryGrid alloc] initWithFrame:NSMakeRect(0, 0, 400, 100) library:&_gameLib];

    [sv addSubview:_sideView];
    [sv addSubview:_gridView];
    _libraryWindow.contentView = sv;

    [sv setHoldingPriority:NSLayoutPriorityDefaultLow+1 forSubviewAtIndex:0];

    AppDelegate* ws = self;
    _gridView.onGameSelected = ^(const GameEntry* g) {
        [ws openGameAtPath:[NSString stringWithUTF8String:g->path.c_str()]];
    };

    [_libraryWindow center];
    [_libraryWindow makeKeyAndOrderFront:nil];
    LOG_INFO("Library ready: %zu games", _gameLib.GetAll().size());
}

- (void)loadGameLibrary {
    const char* home = getenv("HOME");
    if (!home) return;

    std::string libPath = std::string(home) + "/Library/Application Support/m1switch/library.json";
    _gameLib.Load(libPath);

    if (_gameLib.GetAll().empty()) {
        std::vector<std::string> dirs;
        dirs.push_back(std::string(home) + "/Downloads");
        _scanner.ScanDirectories(dirs, _gameLib);
        _gameLib.Sort(GameLibrary::Title, true);
        _gameLib.Save(libPath);
    }
}

// ═══════════════════════════════════════════════════════════
// Game Window
// ═══════════════════════════════════════════════════════════
- (void)openGameAtPath:(NSString*)path {
    LOG_INFO("Open: %s", [path UTF8String]);

    // ── 关闭前一个游戏窗口 ──────────────────────────
    if (_gameWindow) {
        [_gameWindow close];
        _gameWindow = nil;
    }

    NSRect frame = NSMakeRect(0, 0, 1280, 760);
    _gameWindow = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                            NSWindowStyleMaskFullSizeContentView
                    backing:NSBackingStoreBuffered defer:NO];
    _gameWindow.titlebarAppearsTransparent = YES;
    _gameWindow.titleVisibility = NSWindowTitleHidden;
    _gameWindow.title = [path lastPathComponent];
    _gameWindow.minSize = NSMakeSize(480, 360);
    _gameWindow.backgroundColor = [NSColor blackColor];

    // ── 垂直分割: 显示 + 日志 ───────────────────────
    NSSplitView* split = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    split.vertical = NO;
    split.dividerStyle = NSSplitViewDividerStyleThin;
    split.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    EmuScreenView* screen = [[EmuScreenView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 580) core:&_core];
    LogPanelView* log = [[LogPanelView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 180)];

    // ── 安全检查：如果 EmuScreenView 初始化失败（无 Metal）─────
    if (![screen isValid]) {
        LOG_ERROR("EmuScreenView not available — Metal required");
        NSView* errView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 580)];
        errView.wantsLayer = YES;
        errView.layer.backgroundColor = [[NSColor colorWithSRGBRed:0.05 green:0.05 blue:0.08 alpha:1] CGColor];

        NSTextField* errLabel = [[NSTextField alloc] initWithFrame:NSZeroRect];
        errLabel.stringValue = @"Metal is not available on this device.\nM1Switch requires a Metal-capable Mac.";
        errLabel.textColor = [NSColor secondaryLabelColor];
        errLabel.font = [NSFont systemFontOfSize:14];
        errLabel.alignment = NSTextAlignmentCenter;
        errLabel.bezeled = NO;
        errLabel.editable = NO;
        errLabel.selectable = NO;
        errLabel.backgroundColor = [NSColor clearColor];
        [errLabel sizeToFit];
        errLabel.translatesAutoresizingMaskIntoConstraints = NO;
        [errView addSubview:errLabel];

        [NSLayoutConstraint activateConstraints:@[
            [errLabel.centerXAnchor constraintEqualToAnchor:errView.centerXAnchor],
            [errLabel.centerYAnchor constraintEqualToAnchor:errView.centerYAnchor],
        ]];
        [split addSubview:errView];
    } else {
        [split addSubview:screen];
    }

    [split addSubview:log];
    _gameWindow.contentView = split;
    _gameWindow.delegate = self;
    [_gameWindow center];
    [_gameWindow makeKeyAndOrderFront:nil];

    if ([screen isValid]) {
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            @try {
                [screen loadAndRunNRO:[path UTF8String]];
            } @catch (NSException* e) {
                LOG_ERROR("Game thread exception: %s reason: %s",
                          [[e name] UTF8String], [[e reason] UTF8String]);
            } @catch (...) {
                LOG_ERROR("Game thread unknown exception");
            }
        });
    }
}

// ═══════════════════════════════════════════════════════════
// Settings / Debug
// ═══════════════════════════════════════════════════════════
- (void)showSettings {
    if (!_settingsController) {
        _settingsController = [[SettingsController alloc] init];
    }
    [_settingsController showWindow:nil];
    [[_settingsController window] makeKeyAndOrderFront:nil];
}

- (void)showDebug {
    if (!_debugController) {
        _debugController = [[DebugPanelController alloc] init];
    }
    [_debugController showWindow:nil];
    [[_debugController window] makeKeyAndOrderFront:nil];
}

// ═══════════════════════════════════════════════════════════
// Menu
// ═══════════════════════════════════════════════════════════
- (void)setupMainMenu {
    NSMenu* main = [[NSMenu alloc] init];

    // ── App 菜单 ────────────────────────────────────
    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [main addItem:appItem];
    NSMenu* appM = [[NSMenu alloc] init];
    [appM addItemWithTitle:@"About M1Switch" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appM addItem:[NSMenuItem separatorItem]];
    [appM addItemWithTitle:@"Settings…" action:@selector(showSettings) keyEquivalent:@","];
    [appM addItem:[NSMenuItem separatorItem]];
    [appM addItemWithTitle:@"Debug Panel" action:@selector(showDebug) keyEquivalent:@"d"];
    [appM addItem:[NSMenuItem separatorItem]];
    [appM addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appM;

    // ── File 菜单 ───────────────────────────────────
    NSMenuItem* fileItem = [[NSMenuItem alloc] init];
    fileItem.title = @"File";
    [main addItem:fileItem];
    NSMenu* fileM = [[NSMenu alloc] init];
    [fileM addItemWithTitle:@"Open Game…" action:@selector(openGameDialog:) keyEquivalent:@"o"];
    [fileM addItem:[NSMenuItem separatorItem]];
    [fileM addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
    fileItem.submenu = fileM;

    // ── View 菜单 ───────────────────────────────────
    NSMenuItem* viewItem = [[NSMenuItem alloc] init];
    viewItem.title = @"View";
    [main addItem:viewItem];
    NSMenu* viewM = [[NSMenu alloc] init];
    [viewM addItemWithTitle:@"Toggle Fullscreen" action:@selector(toggleFullscreen:) keyEquivalent:@"f"];
    [viewM addItemWithTitle:@"Toggle Pause" action:@selector(togglePause:) keyEquivalent:@" "];
    viewItem.submenu = viewM;

    [NSApp setMainMenu:main];
}

// ═══════════════════════════════════════════════════════════
// Actions
// ═══════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════
// Apple Event Handling
// ═══════════════════════════════════════════════════════════
- (BOOL)application:(NSApplication*)sender openFile:(NSString*)filename {
    [self openGameAtPath:filename];
    return YES;
}

- (void)application:(NSApplication*)sender openFiles:(NSArray<NSString*>*)filenames {
    for (NSString* path in filenames) {
        [self openGameAtPath:path];
    }
}

- (void)openGameDialog:(id)sender {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowedContentTypes = @[
        [UTType typeWithFilenameExtension:@"nro"],
        [UTType typeWithFilenameExtension:@"xci"],
        [UTType typeWithFilenameExtension:@"nsp"],
    ];
    [panel beginSheetModalForWindow:_libraryWindow
                  completionHandler:^(NSModalResponse r) {
        if (r == NSModalResponseOK) [self openGameAtPath:panel.URL.path];
    }];
}

- (void)toggleFullscreen:(id)sender {
    NSWindow* w = [NSApp keyWindow];
    if (w) [w toggleFullScreen:nil];
}

- (void)togglePause:(id)sender {
    NSWindow* keyW = [NSApp keyWindow];
    if (keyW && [keyW.contentView isKindOfClass:[NSSplitView class]]) {
        NSSplitView* sv = (NSSplitView*)keyW.contentView;
        for (NSView* sub in sv.subviews) {
            if ([sub isKindOfClass:[EmuScreenView class]]) {
                [(EmuScreenView*)sub togglePause];
                break;
            }
        }
    }
}

@end
