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

    // Wrap initialization in ObjC exception handler to prevent
    // C++/ObjC exception cross-contamination in the event loop.
    @try {
        Config::Instance().Load();
        _core.Initialize();
    } @catch (NSException* e) {
        LOG_ERROR("Init exception: %s reason: %s",
                  [[e name] UTF8String], [[e reason] UTF8String]);
    } @catch (...) {
        LOG_ERROR("Init: unknown C++ exception");
    }

    @try {
        [self createLibraryWindow];
        [self setupMainMenu];
    } @catch (NSException* e) {
        LOG_ERROR("Window/menu exception: %s reason: %s",
                  [[e name] UTF8String], [[e reason] UTF8String]);
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

- (void)createLibraryWindow {
    NSRect frame = NSMakeRect(0, 0, 960, 640);
    _libraryWindow = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered defer:NO];
    _libraryWindow.title = @"M1Switch";
    _libraryWindow.minSize = NSMakeSize(640, 480);

    NSView* cv = _libraryWindow.contentView;

    const char* home = getenv("HOME");
    std::string libPath;
    if (home) {
        libPath = std::string(home) + "/Library/Application Support/m1switch/library.json";
        _gameLib.Load(libPath);
    }

    if (_gameLib.GetAll().empty()) {
        std::vector<std::string> dirs;
        if (home) dirs.push_back(std::string(home) + "/Downloads");
        _scanner.ScanDirectories(dirs, _gameLib);
        _gameLib.Sort(GameLibrary::Title, true);
        if (!libPath.empty()) _gameLib.Save(libPath);
    }

    NSSplitView* sv = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    sv.vertical = YES;
    sv.dividerStyle = NSSplitViewDividerStyleThin;
    sv.translatesAutoresizingMaskIntoConstraints = NO;

    _sideView = [[LibrarySidebar alloc] initWithFrame:NSMakeRect(0, 0, 180, 100) library:&_gameLib];
    _gridView = [[LibraryGrid alloc] initWithFrame:NSMakeRect(0, 0, 400, 100) library:&_gameLib];

    [sv addSubview:_sideView];
    [sv addSubview:_gridView];
    [cv addSubview:sv];

    [NSLayoutConstraint activateConstraints:@[
        [sv.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor],
        [sv.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor],
        [sv.topAnchor constraintEqualToAnchor:cv.topAnchor],
        [sv.bottomAnchor constraintEqualToAnchor:cv.bottomAnchor],
    ]];

    AppDelegate* ws = self;
    _gridView.onGameSelected = ^(const GameEntry* g) {
        [ws openGameAtPath:[NSString stringWithUTF8String:g->path.c_str()]];
    };

    [_libraryWindow center];
    [_libraryWindow makeKeyAndOrderFront:nil];
    LOG_INFO("Library ready: %zu games", _gameLib.GetAll().size());
}

- (void)openGameAtPath:(NSString*)path {
    LOG_INFO("Open: %s", [path UTF8String]);
    if (_gameWindow) { [_gameWindow close]; _gameWindow = nil; }

    NSRect frame = NSMakeRect(0, 0, 1280, 720);
    _gameWindow = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered defer:NO];
    _gameWindow.title = [path lastPathComponent];
    _gameWindow.minSize = NSMakeSize(480, 360);

    NSSplitView* split = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    split.vertical = NO;
    split.dividerStyle = NSSplitViewDividerStyleThin;
    split.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    EmuScreenView* screen = [[EmuScreenView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 540) core:&_core];
    LogPanelView* log = [[LogPanelView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 180)];

    [split addSubview:screen];
    [split addSubview:log];
    _gameWindow.contentView = split;
    _gameWindow.delegate = self;
    [_gameWindow center];
    [_gameWindow makeKeyAndOrderFront:nil];

    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [screen loadAndRunNRO:[path UTF8String]];
    });
}

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

- (void)setupMainMenu {
    NSMenu* main = [[NSMenu alloc] init];

    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [main addItem:appItem];
    NSMenu* appM = [[NSMenu alloc] init];
    [appM addItemWithTitle:@"About" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appM addItem:[NSMenuItem separatorItem]];
    [appM addItemWithTitle:@"Preferences..." action:@selector(showSettings) keyEquivalent:@","];
    [appM addItem:[NSMenuItem separatorItem]];
    [appM addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appM;

    NSMenuItem* fileItem = [[NSMenuItem alloc] init];
    fileItem.title = @"File";
    [main addItem:fileItem];
    NSMenu* fileM = [[NSMenu alloc] init];
    [fileM addItemWithTitle:@"Open Game..." action:@selector(openGameDialog:) keyEquivalent:@"o"];
    [fileM addItem:[NSMenuItem separatorItem]];
    [fileM addItemWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];
    fileItem.submenu = fileM;

    NSMenuItem* debugItem = [[NSMenuItem alloc] init];
    debugItem.title = @"Debug";
    [main addItem:debugItem];
    NSMenu* debugM = [[NSMenu alloc] init];
    [debugM addItemWithTitle:@"Debug Panel" action:@selector(showDebug) keyEquivalent:@"d"];
    [debugM addItemWithTitle:@"Settings" action:@selector(showSettings) keyEquivalent:@","];
    debugItem.submenu = debugM;

    [NSApp setMainMenu:main];
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

@end
