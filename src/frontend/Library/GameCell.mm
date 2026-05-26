#import "GameCell.h"
#include "common/Log.h"
#include <unordered_map>

// 封面缓存（内存 + 磁盘）
static std::unordered_map<std::string, NSImage*> g_cover_cache;

@implementation GameCell {
    NSBox* _separator;
}

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 224)];
    self.view.wantsLayer = YES;
    self.view.layer.cornerRadius = 10;
    self.view.layer.backgroundColor = [[NSColor colorWithWhite:0.12 alpha:1] CGColor];
    self.view.layer.shadowColor = [[NSColor blackColor] CGColor];
    self.view.layer.shadowOpacity = 0.3;
    self.view.layer.shadowRadius = 4;
    self.view.layer.shadowOffset = CGSizeMake(0, 2);
}

- (void)setup {
    // ── 封面图 ──────────────────────────────────────
    _iconView = [[NSImageView alloc] initWithFrame:NSMakeRect(0, 64, 160, 160)];
    _iconView.imageScaling = NSImageScaleProportionallyUpOrDown;
    _iconView.imageAlignment = NSImageAlignCenter;
    _iconView.wantsLayer = YES;
    _iconView.layer.cornerRadius = 8;
    _iconView.layer.masksToBounds = YES;
    _iconView.layer.backgroundColor = [[NSColor colorWithWhite:0.15 alpha:1] CGColor];
    [self.view addSubview:_iconView];

    // ── 分隔线 ──────────────────────────────────────
    _separator = [[NSBox alloc] initWithFrame:NSMakeRect(8, 56, 144, 1)];
    _separator.boxType = NSBoxSeparator;
    _separator.alphaValue = 0.3;
    [self.view addSubview:_separator];

    // ── 标题 ────────────────────────────────────────
    _titleField = [[NSTextField alloc] initWithFrame:NSMakeRect(8, 8, 144, 40)];
    _titleField.stringValue = @"";
    _titleField.textColor = [NSColor labelColor];
    _titleField.font = [NSFont systemFontOfSize:12 weight:NSFontWeightMedium];
    _titleField.alignment = NSTextAlignmentCenter;
    _titleField.bezeled = NO;
    _titleField.editable = NO;
    _titleField.selectable = NO;
    _titleField.backgroundColor = [NSColor clearColor];
    _titleField.lineBreakMode = NSLineBreakByTruncatingTail;
    _titleField.maximumNumberOfLines = 2;
    [self.view addSubview:_titleField];

    // ── 副标题 ──────────────────────────────────────
    _subtitleField = [[NSTextField alloc] initWithFrame:NSMakeRect(8, -2, 144, 16)];
    _subtitleField.stringValue = @"";
    _subtitleField.textColor = [NSColor secondaryLabelColor];
    _subtitleField.font = [NSFont systemFontOfSize:9 weight:NSFontWeightRegular];
    _subtitleField.alignment = NSTextAlignmentCenter;
    _subtitleField.bezeled = NO;
    _subtitleField.editable = NO;
    _subtitleField.selectable = NO;
    _subtitleField.backgroundColor = [NSColor clearColor];
    [self.view addSubview:_subtitleField];

    // ── 选中环 ─────────────────────────────────────
    _selectionRing = [[NSView alloc] initWithFrame:self.view.bounds];
    _selectionRing.wantsLayer = YES;
    _selectionRing.layer.borderColor = [[NSColor keyboardFocusIndicatorColor] CGColor];
    _selectionRing.layer.borderWidth = 2.5;
    _selectionRing.layer.cornerRadius = 10;
    _selectionRing.hidden = YES;
    [self.view addSubview:_selectionRing];
}

- (void)setGame:(const GameEntry*)game {
    _gameEntry = game;

    // 标题
    NSString* title = [NSString stringWithUTF8String:game->title.c_str()];
    _titleField.stringValue = title;

    // 副标题：显示文件大小
    if (game->file_size > 0) {
        double mb = game->file_size / (1024.0 * 1024.0);
        _subtitleField.stringValue = [NSString stringWithFormat:@"%.0f MB", mb];
    }

    // 封面
    [self loadCoverFor:game];
}

- (void)loadCoverFor:(const GameEntry*)game {
    // 尝试从缓存加载
    auto it = g_cover_cache.find(game->path);
    if (it != g_cover_cache.end()) {
        _iconView.image = it->second;
        return;
    }

    // 从磁盘缓存加载
    NSString* home = NSHomeDirectory();
    NSString* cacheDir = [home stringByAppendingPathComponent:@"Library/Caches/m1switch/covers"];
    // 使用文件路径的 hash 作为缓存键
    NSString* hashStr = [NSString stringWithFormat:@"%lu",
                          (unsigned long)[game->path.c_str() hash]];
    NSString* coverPath = [cacheDir stringByAppendingPathComponent:
                            [hashStr stringByAppendingPathExtension:@"png"]];

    NSImage* img = [[NSImage alloc] initWithContentsOfFile:coverPath];
    if (img) {
        _iconView.image = img;
        g_cover_cache[game->path] = img;
        return;
    }

    // 无封面：显示占位符
    _iconView.image = nil;
}

// ── 选中/高亮 ──────────────────────────────────────────────
- (void)setSelected:(BOOL)selected {
    _selectionRing.hidden = !selected;
    if (selected) {
        self.view.layer.backgroundColor = [[NSColor colorWithWhite:0.2 alpha:1] CGColor];
    } else {
        self.view.layer.backgroundColor = [[NSColor colorWithWhite:0.12 alpha:1] CGColor];
    }
}

- (BOOL)isSelected {
    return !_selectionRing.hidden;
}

@end
