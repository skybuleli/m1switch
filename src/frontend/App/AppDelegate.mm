#import "AppDelegate.h"
#import "frontend/GameView/LogPanelView.h"
#import "frontend/GameView/EmuScreenView.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "common/Log.h"
#include "common/Config.h"

// ── Application Delegate ────────────────────────────────────
@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    LOG_INFO("Application did finish launching");

    // Load config
    Config::Instance().Load();

    // Create library window
    [self createLibraryWindow];

    // Setup application menu
    [self setupMainMenu];
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    LOG_INFO("Application will terminate");
    Config::Instance().Save();
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}

// ── Library Window ──────────────────────────────────────────

- (void)createLibraryWindow {
    NSRect frame = NSMakeRect(0, 0, 960, 640);

    _libraryWindow = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];

    _libraryWindow.title = @"M1Switch";
    _libraryWindow.minSize = NSMakeSize(640, 480);

    // Phase 0: placeholder content — will be replaced with
    // LibraryGrid (NSCollectionView) in Phase 8
    NSView* contentView = _libraryWindow.contentView;
    contentView.wantsLayer = YES;
    contentView.layer.backgroundColor = [[NSColor windowBackgroundColor] CGColor];

    // ── Title ───────────────────────────────────────────
    NSTextField* title = [[NSTextField alloc] initWithFrame:NSZeroRect];
    title.bezeled = NO;
    title.drawsBackground = NO;
    title.editable = NO;
    title.selectable = NO;
    title.stringValue = @"M1Switch";
    title.font = [NSFont systemFontOfSize:36 weight:NSFontWeightBold];
    title.textColor = [NSColor labelColor];
    title.alignment = NSTextAlignmentCenter;
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:title];

    // ── Subtitle ────────────────────────────────────────
    NSTextField* subtitle = [[NSTextField alloc] initWithFrame:NSZeroRect];
    subtitle.bezeled = NO;
    subtitle.drawsBackground = NO;
    subtitle.editable = NO;
    subtitle.selectable = NO;
    subtitle.stringValue = @"Nintendo Switch Emulator for Apple Silicon";
    subtitle.font = [NSFont systemFontOfSize:15 weight:NSFontWeightRegular];
    subtitle.textColor = [NSColor secondaryLabelColor];
    subtitle.alignment = NSTextAlignmentCenter;
    subtitle.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:subtitle];

    // ── Version ─────────────────────────────────────────
    NSTextField* version = [[NSTextField alloc] initWithFrame:NSZeroRect];
    version.bezeled = NO;
    version.drawsBackground = NO;
    version.editable = NO;
    version.selectable = NO;
    version.stringValue = @"Phase 0 — Engineering Foundation";
    version.font = [NSFont systemFontOfSize:12 weight:NSFontWeightRegular];
    version.textColor = [NSColor tertiaryLabelColor];
    version.alignment = NSTextAlignmentCenter;
    version.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:version];

    // ── Open Game Button ────────────────────────────────
    NSButton* openBtn = [NSButton buttonWithTitle:@"  Open Game  ⌘O  "
                                           target:self
                                           action:@selector(openGameDialog:)];
    openBtn.bezelStyle = NSBezelStyleRounded;
    openBtn.font = [NSFont systemFontOfSize:14 weight:NSFontWeightMedium];
    openBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:openBtn];

    // ── Settings Button ─────────────────────────────────
    NSButton* settingsBtn = [NSButton buttonWithTitle:@"  Preferences  ⌘,  "
                                               target:self
                                               action:@selector(showSettings)];
    settingsBtn.bezelStyle = NSBezelStyleRounded;
    settingsBtn.font = [NSFont systemFontOfSize:14 weight:NSFontWeightMedium];
    settingsBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:settingsBtn];

    // ── Quick help text ─────────────────────────────────
    NSTextField* help = [[NSTextField alloc] initWithFrame:NSZeroRect];
    help.bezeled = NO;
    help.drawsBackground = NO;
    help.editable = NO;
    help.selectable = NO;
    help.stringValue = @"Tip: Use File → Open Game or press ⌘O to select a game file";
    help.font = [NSFont systemFontOfSize:11 weight:NSFontWeightRegular];
    help.textColor = [NSColor tertiaryLabelColor];
    help.alignment = NSTextAlignmentCenter;
    help.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:help];

    // ── Layout ──────────────────────────────────────────
    [NSLayoutConstraint activateConstraints:@[
        // Title: center X, top region
        [title.centerXAnchor constraintEqualToAnchor:contentView.centerXAnchor],
        [title.centerYAnchor constraintEqualToAnchor:contentView.centerYAnchor constant:-80],

        // Subtitle: below title
        [subtitle.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:8],
        [subtitle.centerXAnchor constraintEqualToAnchor:contentView.centerXAnchor],

        // Version: below subtitle
        [version.topAnchor constraintEqualToAnchor:subtitle.bottomAnchor constant:4],
        [version.centerXAnchor constraintEqualToAnchor:contentView.centerXAnchor],

        // Open button: below version
        [openBtn.topAnchor constraintEqualToAnchor:version.bottomAnchor constant:40],
        [openBtn.centerXAnchor constraintEqualToAnchor:contentView.centerXAnchor constant:-80],
        [openBtn.widthAnchor constraintEqualToConstant:180],

        // Settings button: next to open button
        [settingsBtn.topAnchor constraintEqualToAnchor:version.bottomAnchor constant:40],
        [settingsBtn.centerXAnchor constraintEqualToAnchor:contentView.centerXAnchor constant:80],
        [settingsBtn.widthAnchor constraintEqualToConstant:180],

        // Help text: bottom of window
        [help.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor constant:-20],
        [help.centerXAnchor constraintEqualToAnchor:contentView.centerXAnchor],
    ]];

    // Center the window
    [_libraryWindow center];
    [_libraryWindow makeKeyAndOrderFront:nil];

    LOG_INFO("Library window created");
}

// ── Game Window ─────────────────────────────────────────────

- (void)openGameAtPath:(NSString*)path {
    LOG_INFO("Opening game at: %s", [path UTF8String]);

    if (_gameWindow) {
        [_gameWindow close];
        _gameWindow = nil;
    }

    NSRect frame = NSMakeRect(0, 0, 1280, 720);
    _gameWindow = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];

    _gameWindow.title = [path lastPathComponent];
    _gameWindow.minSize = NSMakeSize(320, 240);

    // Create split view: EmuScreenView (top) + LogPanelView (bottom)
    NSSplitView* splitView = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    splitView.vertical = NO;
    splitView.dividerStyle = NSSplitViewDividerStyleThin;
    splitView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    EmuScreenView* screenView = [[EmuScreenView alloc]
        initWithFrame:NSMakeRect(0, 0, 1280, 540) memory:nil];
    LogPanelView* logPanel = [[LogPanelView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 180)];

    [splitView addSubview:screenView];
    [splitView addSubview:logPanel];
    [splitView adjustSubviews];

    _gameWindow.contentView = splitView;
    _gameWindow.delegate = self;

    [_gameWindow center];
    [_gameWindow makeKeyAndOrderFront:nil];

    LOG_INFO("Game window opened: %s", [[path lastPathComponent] UTF8String]);
}

// ── Settings ────────────────────────────────────────────────

- (void)showSettings {
    TODO("Settings window not yet implemented");
}

// ── Main Menu ───────────────────────────────────────────────

- (void)setupMainMenu {
    NSMenu* mainMenu = [[NSMenu alloc] init];

    // App menu
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:appMenuItem];

    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"About M1Switch"
                       action:@selector(orderFrontStandardAboutPanel:)
                keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Preferences..."
                       action:@selector(showSettings)
                keyEquivalent:@","];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit"
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
    appMenuItem.submenu = appMenu;

    // File menu
    NSMenuItem* fileMenuItem = [[NSMenuItem alloc] init];
    fileMenuItem.title = @"File";
    [mainMenu addItem:fileMenuItem];

    NSMenu* fileMenu = [[NSMenu alloc] init];
    [fileMenu addItemWithTitle:@"Open Game..."
                        action:@selector(openGameDialog:)
                 keyEquivalent:@"o"];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [fileMenu addItemWithTitle:@"Close Window"
                        action:@selector(performClose:)
                 keyEquivalent:@"w"];
    fileMenuItem.submenu = fileMenu;

    // Emulation menu
    NSMenuItem* emuMenuItem = [[NSMenuItem alloc] init];
    emuMenuItem.title = @"Emulation";
    [mainMenu addItem:emuMenuItem];

    NSMenu* emuMenu = [[NSMenu alloc] init];
    [emuMenu addItemWithTitle:@"Pause/Resume"
                       action:@selector(togglePause:)
                keyEquivalent:@"p"];
    [emuMenu addItemWithTitle:@"Reset"
                       action:@selector(resetEmulation:)
                keyEquivalent:@"r"];
    [emuMenu addItem:[NSMenuItem separatorItem]];
    [emuMenu addItemWithTitle:@"Toggle Fullscreen"
                       action:@selector(toggleFullscreen:)
                keyEquivalent:@"f"];
    emuMenuItem.submenu = emuMenu;

    // View menu
    NSMenuItem* viewMenuItem = [[NSMenuItem alloc] init];
    viewMenuItem.title = @"View";
    [mainMenu addItem:viewMenuItem];

    NSMenu* viewMenu = [[NSMenu alloc] init];
    [viewMenu addItemWithTitle:@"Show HUD"
                        action:@selector(toggleHud:)
                 keyEquivalent:@"h"];
    viewMenuItem.submenu = viewMenu;

    // Debug menu (Release builds may hide this)
    NSMenuItem* debugMenuItem = [[NSMenuItem alloc] init];
    debugMenuItem.title = @"Debug";
    [mainMenu addItem:debugMenuItem];

    NSMenu* debugMenu = [[NSMenu alloc] init];
    [debugMenu addItemWithTitle:@"Capture GPU Frame"
                         action:@selector(captureGpuFrame:)
                  keyEquivalent:@"g"];
    [debugMenu addItemWithTitle:@"Open Log Directory"
                         action:@selector(openLogDir:)
                  keyEquivalent:@""];
    debugMenuItem.submenu = debugMenu;

    [NSApp setMainMenu:mainMenu];
}

// ── Menu Actions ────────────────────────────────────────────

- (void)openGameDialog:(id)sender {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowedContentTypes = @[
        [UTType typeWithFilenameExtension:@"nsp"],
        [UTType typeWithFilenameExtension:@"xci"],
        [UTType typeWithFilenameExtension:@"nro"],
    ];
    panel.allowsMultipleSelection = NO;

    [panel beginSheetModalForWindow:_libraryWindow
                  completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK) {
            NSString* path = panel.URL.path;
            [self openGameAtPath:path];
        }
    }];
}

- (void)togglePause:(id)sender {
    TODO("Pause/resume emulation");
}

- (void)resetEmulation:(id)sender {
    TODO("Reset emulation");
}

- (void)toggleFullscreen:(id)sender {
    NSWindow* keyWindow = NSApp.keyWindow;
    if (keyWindow && (keyWindow.styleMask & NSWindowStyleMaskFullScreen)) {
        [keyWindow toggleFullScreen:nil];
    } else if (keyWindow) {
        [keyWindow toggleFullScreen:nil];
    }
}

- (void)toggleHud:(id)sender {
    TODO("Toggle HUD overlay");
}

- (void)captureGpuFrame:(id)sender {
    // Use Metal Capture Manager
    TODO("GPU frame capture");
}

- (void)openLogDir:(id)sender {
    // Phase 0: logs go to stderr
    LOG_INFO("Log directory: (stderr only in Phase 0)");
}

@end
