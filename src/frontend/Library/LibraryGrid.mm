#import "LibraryGrid.h"
#import "GameCell.h"
#include "common/Log.h"

@interface LibraryGrid ()

@property (nonatomic, strong) NSCollectionView* collectionView;
@property (nonatomic, strong) NSScrollView* scrollView;
@property (nonatomic, assign) GameLibrary* library;
@property (nonatomic, strong) NSArray<NSString*>* filteredPaths;  // nil = show all

@end

@implementation LibraryGrid

- (instancetype)initWithFrame:(NSRect)frame library:(GameLibrary*)lib {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    _library = lib;

    // ── Collection view layout ─────────────────────────
    NSCollectionViewFlowLayout* layout = [[NSCollectionViewFlowLayout alloc] init];
    layout.itemSize = NSMakeSize(160, 224);
    layout.minimumInteritemSpacing = 8;
    layout.minimumLineSpacing = 12;
    layout.sectionInset = NSEdgeInsetsMake(12, 12, 12, 12);

    // ── Collection view ────────────────────────────────
    _collectionView = [[NSCollectionView alloc] initWithFrame:NSZeroRect];
    _collectionView.collectionViewLayout = layout;
    _collectionView.delegate = self;
    _collectionView.dataSource = self;
    _collectionView.backgroundColors = @[[NSColor clearColor]];
    _collectionView.selectable = YES;
    _collectionView.allowsMultipleSelection = NO;

    // Register cell
    [_collectionView registerClass:[GameCell class]
            forItemWithIdentifier:@"GameCell"];

    // ── Scroll view ────────────────────────────────────
    _scrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    _scrollView.documentView = _collectionView;
    _scrollView.hasVerticalScroller = YES;
    _scrollView.autohidesScrollers = YES;
    _scrollView.borderType = NSNoBorder;
    _scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_scrollView];

    [NSLayoutConstraint activateConstraints:@[
        [_scrollView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [_scrollView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
        [_scrollView.topAnchor constraintEqualToAnchor:self.topAnchor],
        [_scrollView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
    ]];

    return self;
}

// ── Data source ────────────────────────────────────────────
- (NSInteger)collectionView:(NSCollectionView*)cv numberOfItemsInSection:(NSInteger)sec {
    if (!_library) return 0;

    if (_filteredPaths) {
        return _filteredPaths.count;
    }
    return (NSInteger)_library->GetAll().size();
}

- (NSCollectionViewItem*)collectionView:(NSCollectionView*)cv
                 itemForRepresentedObjectAtIndexPath:(NSIndexPath*)ip {
    GameCell* cell = [cv makeItemWithIdentifier:@"GameCell" forIndexPath:ip];

    const GameEntry* game = nullptr;
    if (_filteredPaths) {
        // Search by path
        NSString* pathStr = _filteredPaths[ip.item];
        std::string path = [pathStr UTF8String];
        auto& all = _library->GetAll();
        for (auto& g : all) {
            if (g.path == path) { game = &g; break; }
        }
    } else {
        auto& all = _library->GetAll();
        if (ip.item < (NSInteger)all.size())
            game = &all[ip.item];
    }

    if (game) [cell setGame:game];
    return cell;
}

// ── Selection ──────────────────────────────────────────────
- (void)collectionView:(NSCollectionView*)cv didSelectItemsAtIndexPaths:(NSSet<NSIndexPath*>*)paths {
    NSIndexPath* ip = [paths anyObject];
    if (!ip) return;

    const GameEntry* game = nullptr;
    if (_filteredPaths) {
        NSString* pathStr = _filteredPaths[ip.item];
        std::string path = [pathStr UTF8String];
        auto& all = _library->GetAll();
        for (auto& g : all) {
            if (g.path == path) { game = &g; break; }
        }
    } else {
        auto& all = _library->GetAll();
        if (ip.item < (NSInteger)all.size())
            game = &all[ip.item];
    }

    if (game && _onGameSelected) {
        _onGameSelected(game);
    }
}

- (void)reloadData {
    [_collectionView reloadData];
}

- (void)setFilter:(const std::vector<const GameEntry*>*)filtered {
    if (filtered) {
        NSMutableArray* arr = [NSMutableArray array];
        for (auto* g : *filtered)
            [arr addObject:[NSString stringWithUTF8String:g->path.c_str()]];
        _filteredPaths = arr;
    } else {
        _filteredPaths = nil;
    }
    [self reloadData];
}

- (void)clearFilter {
    _filteredPaths = nil;
    [self reloadData];
}

@end
