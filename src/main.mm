#include "common/Log.h"
#include "common/Config.h"

#include "frontend/App/AppDelegate.h"

#include <AppKit/AppKit.h>
#include <exception>
#include <cstdio>

// ── 直接 stderr 输出 (abort 前必须 flush) ─────────────────
#define EMERGENCY_LOG(fmt, ...) \
    do { fprintf(stderr, "[M1CRASH] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

static void M1TerminateHandler() {
    if (std::current_exception()) {
        try { std::rethrow_exception(std::current_exception()); }
        catch (NSException* e) {
            EMERGENCY_LOG("NSException name: %s", [[e name] UTF8String]);
            EMERGENCY_LOG("NSException reason: %s", [[e reason] UTF8String]);
            EMERGENCY_LOG("NSException userInfo: %s",
                         [[[e userInfo] description] UTF8String]);
            for (NSString* s in [e callStackSymbols]) {
                EMERGENCY_LOG("  %s", [s UTF8String]);
            }
        }
        catch (std::exception& e) {
            EMERGENCY_LOG("std::exception: %s", e.what());
        }
        catch (...) {
            EMERGENCY_LOG("unknown C++ exception");
        }
    } else {
        EMERGENCY_LOG("terminate called with no active exception");
    }
    abort();
}

int main(int argc, const char* argv[]) {
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
