// ── Debug Panel Controller ─────────────────────────────────

#import <Cocoa/Cocoa.h>

@interface DebugPanelController : NSWindowController

- (instancetype)init;

// 向日志区域追加文本
- (void)log:(NSString*)msg;

@end
