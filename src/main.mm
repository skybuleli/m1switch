#include "common/Log.h"
#include "common/Config.h"

#include "frontend/App/AppDelegate.h"

#include <AppKit/AppKit.h>
#include <exception>
#include <cstdio>
#include <cstdlib>

// ── 直接 stderr 输出 (abort 前必须 flush) ─────────────────
#define EMERGENCY_LOG(fmt, ...) \
    do { fprintf(stderr, "[M1CRASH] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

// ── 全局 ObjC 未捕获异常处理器 ───────────────────────────
// macOS 26+ 在后台线程更新 UI 等操作会抛出 ObjC 异常。
// 这个处理器阻止异常传播到 std::terminate。
static void M1UncaughtExceptionHandler(NSException* e) {
    EMERGENCY_LOG("=== Uncaught ObjC Exception ===");
    EMERGENCY_LOG("Name: %s", [[e name] UTF8String]);
    EMERGENCY_LOG("Reason: %s", [[e reason] UTF8String]);
    if (e.userInfo) {
        EMERGENCY_LOG("UserInfo: %s", [[[e userInfo] description] UTF8String]);
    }
    for (NSString* s in [e callStackSymbols]) {
        EMERGENCY_LOG("  %s", [s UTF8String]);
    }
    EMERGENCY_LOG("=== Continuing despite exception ===");
    fflush(stderr);
}

// ── C++ terminate handler ─────────────────────────────────
// 当 ObjC 异常通过 __cxa_rethrow 穿越 C++ 边界时，std::terminate 被触发。
// 此处理器捕获异常并干净退出，而非 abort()。
static void M1TerminateHandler() {
    NSLog(@"=== M1Switch terminate handler ===");
    fflush(stderr);

    if (std::current_exception()) {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (NSException* e) {
            EMERGENCY_LOG("Terminate caught NSException name: %s", [[e name] UTF8String]);
            EMERGENCY_LOG("Terminate caught NSException reason: %s", [[e reason] UTF8String]);
        } catch (std::exception& e) {
            EMERGENCY_LOG("Terminate caught std::exception: %s", e.what());
        } catch (...) {
            EMERGENCY_LOG("Terminate caught unknown exception");
        }
    } else {
        EMERGENCY_LOG("terminate called with no active exception");
    }

    // 不要 abort() — 干净的 exit 让 crash reporter 不弹窗
    _Exit(1);
}

int main(int argc, const char* argv[]) {
    // 安装 ObjC 未捕获异常处理器（必须最先设置）
    NSSetUncaughtExceptionHandler(&M1UncaughtExceptionHandler);
    std::set_terminate(M1TerminateHandler);

    Log::Init();
    LOG_INFO("M1Switch v%s starting...", PROJECT_VER);

    @autoreleasepool {
        @try {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

            AppDelegate* delegate = [[AppDelegate alloc] init];
            [NSApp setDelegate:delegate];
            [NSApp finishLaunching];

            LOG_INFO("Entering event loop");
            [NSApp run];
        } @catch (NSException* e) {
            EMERGENCY_LOG("Main @catch NSException: %s", [[e reason] UTF8String]);
        } @catch (...) {
            EMERGENCY_LOG("Main @catch unknown exception");
        }
    }

    Config::Instance().Save();
    Log::Shutdown();
    return 0;
}
