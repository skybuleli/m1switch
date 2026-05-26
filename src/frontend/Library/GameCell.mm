#import "GameCell.h"
#include "common/Log.h"

@implementation GameCell

- (instancetype)initWithNibName:(NSNibName)nibNameOrNil
                         bundle:(NSBundle*)nibBundleOrNil {
    self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil];
    return self;
}

- (instancetype)initWithCoder:(NSCoder*)coder {
    self = [super initWithCoder:coder];
    return self;
}

// Code-only collection view item: create a plain view
- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 224)];
    [self setup];
}

- (void)setup {
    // Ensure view exists (NSCollectionViewItem might not have loaded it yet)
    if (!self.view) {
        LOG_WARN("GameCell: view is nil in setup, deferring to loadView");
        return;
    }

    // ── Cover image ────────────────────────────────────
    _coverView = [[NSImageView alloc] initWithFrame:NSZeroRect];
    _coverView.imageScaling = NSImageScaleProportionallyUpOrDown;
    _coverView.wantsLayer = YES;
    _coverView.layer.cornerRadius = 6;
    _coverView.layer.masksToBounds = YES;
    _coverView.layer.backgroundColor = [[NSColor colorWithWhite:0.15 alpha:1.0] CGColor];
    _coverView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_coverView];

    // ── Title ───────────────────────────────────────────
    _titleField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _titleField.bezeled = NO;
    _titleField.drawsBackground = NO;
    _titleField.editable = NO;
    _titleField.selectable = NO;
    _titleField.font = [NSFont systemFontOfSize:11 weight:NSFontWeightMedium];
    _titleField.textColor = [NSColor labelColor];
    _titleField.alignment = NSTextAlignmentCenter;
    _titleField.lineBreakMode = NSLineBreakByTruncatingTail;
    _titleField.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_titleField];

    // ── Layout ──────────────────────────────────────────
    [NSLayoutConstraint activateConstraints:@[
        [_coverView.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:4],
        [_coverView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:4],
        [_coverView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-4],
        [_coverView.heightAnchor constraintEqualToAnchor:_coverView.widthAnchor multiplier:0.56],

        [_titleField.topAnchor constraintEqualToAnchor:_coverView.bottomAnchor constant:4],
        [_titleField.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:4],
        [_titleField.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-4],
    ]];
}

- (void)setGame:(const GameEntry*)game {
    _titleField.stringValue = [NSString stringWithUTF8String:game->title.c_str()];

    // Load cover from cache
    NSString* cacheKey = [NSString stringWithUTF8String:game->path.c_str()];
    NSString* cachePath = [NSString stringWithFormat:@"~/Library/Caches/m1switch/covers/%08llu.png",
                          (unsigned long long)cacheKey.hash];
    cachePath = [cachePath stringByExpandingTildeInPath];

    NSImage* cover = [[NSImage alloc] initWithContentsOfFile:cachePath];
    if (cover) {
        _coverView.image = cover;
    } else {
        // Placeholder
        _coverView.image = nil;
    }
}

@end
