#pragma once

#import <Cocoa/Cocoa.h>

#include "common/Log.h"

// ── Log Panel View ──────────────────────────────────────────
// Shows real-time log output in an NSTextView.
// Supports scrolling, copying, and auto-scroll.

@interface LogPanelView : NSView {
@private
    NSScrollView* scrollView_;
    NSTextView* textView_;
    NSButton* clearButton_;
    NSButton* copyButton_;
    BOOL autoScroll_;
}

- (instancetype)initWithFrame:(NSRect)frame;

// Append a log entry (thread-safe, dispatches to main thread)
- (void)appendLog:(LogLevel)level message:(const char*)msg length:(int)len;

@end
