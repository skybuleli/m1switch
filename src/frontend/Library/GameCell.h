#pragma once

#include "frontend/Library/GameModel.h"
#import <Cocoa/Cocoa.h>

// ── Game Cell (single item in collection view) ─────────────
@interface GameCell : NSCollectionViewItem

@property (nonatomic, strong) NSImageView* coverView;
@property (nonatomic, strong) NSTextField* titleField;
@property (nonatomic, strong) NSTextField* subtitleField;

- (void)setGame:(const GameEntry*)game;

@end
