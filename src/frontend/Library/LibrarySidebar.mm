#import "LibrarySidebar.h"
#include "common/Log.h"
#include <algorithm>

@interface LibrarySidebar ()

@property (nonatomic, strong) NSSearchField* searchField;
@property (nonatomic, assign) GameLibrary* library;
@property (nonatomic, strong) NSButton* allBtn;
@property (nonatomic, strong) NSButton* favoritesBtn;

@end

@implementation LibrarySidebar

- (instancetype)initWithFrame:(NSRect)frame library:(GameLibrary*)lib {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    _library = lib;

    // ── Search field ──────────────────────────────────
    _searchField = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    _searchField.placeholderString = @"Search games...";
    _searchField.font = [NSFont systemFontOfSize:13];
    _searchField.delegate = self;
    _searchField.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_searchField];

    // ── Filter buttons ────────────────────────────────
    _allBtn = [NSButton buttonWithTitle:@"All Games"
                                 target:self
                                 action:@selector(filterAll:)];
    _allBtn.bezelStyle = NSBezelStyleInline;
    _allBtn.font = [NSFont systemFontOfSize:12];
    _allBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_allBtn];

    _favoritesBtn = [NSButton buttonWithTitle:@"Favorites"
                                       target:self
                                       action:@selector(filterFavorites:)];
    _favoritesBtn.bezelStyle = NSBezelStyleInline;
    _favoritesBtn.font = [NSFont systemFontOfSize:12];
    _favoritesBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_favoritesBtn];

    // ── Layout ───────────────────────────────────────
    [NSLayoutConstraint activateConstraints:@[
        [_searchField.topAnchor constraintEqualToAnchor:self.topAnchor constant:12],
        [_searchField.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8],
        [_searchField.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8],

        [_allBtn.topAnchor constraintEqualToAnchor:_searchField.bottomAnchor constant:12],
        [_allBtn.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8],
        [_allBtn.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8],

        [_favoritesBtn.topAnchor constraintEqualToAnchor:_allBtn.bottomAnchor constant:4],
        [_favoritesBtn.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8],
        [_favoritesBtn.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8],
    ]];

    return self;
}

// ── Search ─────────────────────────────────────────────────
- (void)searchFieldDidChange:(NSSearchField*)sender {
    [self triggerFilter];
}

- (void)controlTextDidChange:(NSNotification*)note {
    [self triggerFilter];
}

- (void)setSearchText:(const std::string&)text {
    _searchField.stringValue = [NSString stringWithUTF8String:text.c_str()];
    [self triggerFilter];
}

- (void)triggerFilter {
    std::string query = [_searchField.stringValue UTF8String];
    std::vector<const GameEntry*> results;

    if (query.empty()) {
        // No filter — show all
        if (_onFilterChanged) _onFilterChanged(nullptr);
        return;
    }

    // Case-insensitive search
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    auto& all = _library->GetAll();
    for (auto& g : all) {
        std::string title = g.title;
        std::transform(title.begin(), title.end(), title.begin(), ::tolower);
        if (title.find(q) != std::string::npos) {
            results.push_back(&g);
        }
    }

    if (_onFilterChanged) {
        if (results.empty())
            _onFilterChanged(nullptr);
        else
            _onFilterChanged(&results);
    }
}

- (void)filterAll:(id)sender {
    _searchField.stringValue = @"";
    if (_onFilterChanged) _onFilterChanged(nullptr);
}

- (void)filterFavorites:(id)sender {
    std::vector<const GameEntry*> favs;
    auto& all = _library->GetAll();
    for (auto& g : all) {
        if (g.is_favorite) favs.push_back(&g);
    }
    if (_onFilterChanged) _onFilterChanged(favs.empty() ? nullptr : &favs);
}

@end
