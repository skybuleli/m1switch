#pragma once

#include "frontend/Library/GameModel.h"
#import <Cocoa/Cocoa.h>

// ── Library Grid ────────────────────────────────────────────
// Displays games in a collection view grid.

@interface LibraryGrid : NSView <NSCollectionViewDelegate, NSCollectionViewDataSource>

- (instancetype)initWithFrame:(NSRect)frame library:(GameLibrary*)lib;

- (void)reloadData;
- (void)setFilter:(const std::vector<const GameEntry*>*)filteredGames;
- (void)clearFilter;

@property (nonatomic, copy) void (^onGameSelected)(const GameEntry* game);

@end
