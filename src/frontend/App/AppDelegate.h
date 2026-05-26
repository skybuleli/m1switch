#pragma once

#import <Cocoa/Cocoa.h>

#include "core/Core.h"

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>

@property (nonatomic, strong) NSWindow* libraryWindow;
@property (nonatomic, strong) NSWindow* gameWindow;
@property (nonatomic, readonly) EmulatorCore* core;

- (void)openGameAtPath:(NSString*)path;
- (void)showSettings;

@end
