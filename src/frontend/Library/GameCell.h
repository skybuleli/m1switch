#pragma once

#include "frontend/Library/GameModel.h"
#import <Cocoa/Cocoa.h>

// ── Game Cell (single item in collection view) ─────────────
@interface GameCell : NSCollectionViewItem {
@private
    NSImageView* _iconView;
    NSTextField* _titleField;
    NSTextField* _subtitleField;
    NSView* _selectionRing;
    const GameEntry* _gameEntry;
}

@property (nonatomic, readonly) const GameEntry* gameEntry;

- (void)setGame:(const GameEntry*)game;
- (void)setSelected:(BOOL)selected;
- (BOOL)isSelected;

@end
