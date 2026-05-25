#pragma once

#include "frontend/Library/GameModel.h"
#import <Cocoa/Cocoa.h>

// ── Library Sidebar ─────────────────────────────────────────
// Search bar + filter categories.

@interface LibrarySidebar : NSView <NSSearchFieldDelegate>

- (instancetype)initWithFrame:(NSRect)frame library:(GameLibrary*)lib;

// Called when the search text or filter changes
@property (nonatomic, copy) void (^onFilterChanged)(const std::vector<const GameEntry*>* results);

// Trigger a search
- (void)setSearchText:(const std::string&)text;

@end
