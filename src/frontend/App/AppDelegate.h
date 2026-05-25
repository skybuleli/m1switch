#pragma once

#import <Cocoa/Cocoa.h>

// ── Application Delegate ────────────────────────────────────
// Manages the main window, game library window, and app lifecycle.

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>

@property (nonatomic, strong) NSWindow* libraryWindow;
@property (nonatomic, strong) NSWindow* gameWindow;

- (void)openGameAtPath:(NSString*)path;
- (void)showSettings;

@end
