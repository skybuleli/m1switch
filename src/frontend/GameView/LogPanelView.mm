#import "LogPanelView.h"

#include "common/Log.h"

// ── Log colour mapping ──────────────────────────────────────
static NSColor* ColorForLevel(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return [NSColor tertiaryLabelColor];
    case LogLevel::Debug: return [NSColor secondaryLabelColor];
    case LogLevel::Info:  return [NSColor labelColor];
    case LogLevel::Warn:  return [NSColor systemOrangeColor];
    case LogLevel::Error: return [NSColor systemRedColor];
    case LogLevel::Fatal: return [NSColor colorWithRed:0.8 green:0.0 blue:0.0 alpha:1.0];
    }
}

static NSString* LevelTag(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return @"TRC";
    case LogLevel::Debug: return @"DBG";
    case LogLevel::Info:  return @"INF";
    case LogLevel::Warn:  return @"WRN";
    case LogLevel::Error: return @"ERR";
    case LogLevel::Fatal: return @"FTL";
    }
}

// ── C callback trampoline ───────────────────────────────────
static void LogCallbackTrampoline(LogLevel level, const char* msg, int len, void* user) {
    LogPanelView* panel = (__bridge LogPanelView*)user;
    [panel appendLog:level message:msg length:len];
}

// ── Implementation ──────────────────────────────────────────
@implementation LogPanelView

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    autoScroll_ = YES;

    // ── Text view ────────────────────────────────────
    NSTextView* tv = [[NSTextView alloc] initWithFrame:NSZeroRect];
    tv.editable = NO;
    tv.selectable = YES;         // ← allows copy
    tv.backgroundColor = [NSColor colorWithWhite:0.08 alpha:1.0];
    tv.textColor = [NSColor labelColor];
    tv.font = [NSFont fontWithName:@"Menlo" size:10];
    tv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    tv.minSize = NSMakeSize(0, 0);
    tv.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    tv.verticallyResizable = YES;
    tv.horizontallyResizable = NO;

    // ── Scroll view ──────────────────────────────────
    NSScrollView* sv = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    sv.documentView = tv;
    sv.hasVerticalScroller = YES;
    sv.hasHorizontalScroller = NO;
    sv.autohidesScrollers = YES;
    sv.borderType = NSNoBorder;
    sv.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:sv];

    textView_ = tv;
    scrollView_ = sv;

    // ── Toolbar buttons ──────────────────────────────
    NSButton* clearBtn = [NSButton buttonWithTitle:@"Clear"
                                            target:self
                                            action:@selector(clearLog:)];
    clearBtn.bezelStyle = NSBezelStyleSmallSquare;
    clearBtn.font = [NSFont systemFontOfSize:10];
    clearBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:clearBtn];
    clearButton_ = clearBtn;

    NSButton* copyBtn = [NSButton buttonWithTitle:@"Copy"
                                           target:self
                                           action:@selector(copyLog:)];
    copyBtn.bezelStyle = NSBezelStyleSmallSquare;
    copyBtn.font = [NSFont systemFontOfSize:10];
    copyBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:copyBtn];
    copyButton_ = copyBtn;

    // ── Layout ───────────────────────────────────────
    [NSLayoutConstraint activateConstraints:@[
        // ScrollView fills the view below buttons
        [sv.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:4],
        [sv.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-4],
        [sv.topAnchor constraintEqualToAnchor:self.topAnchor constant:4],
        [sv.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-28],

        // Buttons at bottom
        [clearBtn.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8],
        [clearBtn.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-2],
        [clearBtn.widthAnchor constraintEqualToConstant:50],

        [copyBtn.leadingAnchor constraintEqualToAnchor:clearBtn.trailingAnchor constant:8],
        [copyBtn.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-2],
        [copyBtn.widthAnchor constraintEqualToConstant:50],
    ]];

    // ── Register log callback ────────────────────────
    Log::SetCallback(LogCallbackTrampoline, (__bridge void*)self);

    return self;
}

- (void)dealloc {
    Log::SetCallback(nullptr, nullptr);
}

// ── Append log (can be called from any thread) ──────────────
- (void)appendLog:(LogLevel)level message:(const char*)msg length:(int)len {
    // Dispatch to main thread for UI updates
    if ([NSThread isMainThread]) {
        [self appendLogOnMainThread:level message:msg length:len];
    } else {
        NSString* str = [NSString stringWithUTF8String:msg ?: ""];
        [self performSelectorOnMainThread:@selector(appendLogOnMainThreadStr:)
                               withObject:@[str, @((int)level)]
                            waitUntilDone:NO];
    }
}

- (void)appendLogOnMainThreadStr:(NSArray*)args {
    NSString* str = args[0];
    LogLevel level = (LogLevel)[args[1] intValue];
    [self appendLogOnMainThread:level message:[str UTF8String] length:(int)[str length]];
}

- (void)appendLogOnMainThread:(LogLevel)level message:(const char*)msg length:(int)len {
    // Format: "HH:MM:SS [LEVEL] message\n"
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);

    NSString* line = [NSString stringWithFormat:@"%02d:%02d:%02d [%@] %.*s\n",
                      tm.tm_hour, tm.tm_min, tm.tm_sec,
                      LevelTag(level), len, msg ?: ""];

    NSDictionary* attrs = @{NSForegroundColorAttributeName: ColorForLevel(level)};
    NSAttributedString* as = [[NSAttributedString alloc] initWithString:line
                                                             attributes:attrs];

    // Append to text view
    [[textView_ textStorage] appendAttributedString:as];

    // Auto-scroll
    if (autoScroll_) {
        NSRange range = NSMakeRange([[textView_ string] length], 0);
        [textView_ scrollRangeToVisible:range];
    }

    // Limit buffer size (keep last ~5000 lines)
    const NSInteger maxLen = 500000;
    if ([[textView_ string] length] > maxLen) {
        [textView_ setString:[[textView_ string] substringFromIndex:maxLen / 2]];
    }
}

// ── Actions ─────────────────────────────────────────────────
- (void)clearLog:(id)sender {
    [textView_ setString:@""];
}

- (void)copyLog:(id)sender {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:[textView_ string] forType:NSPasteboardTypeString];
}

@end
