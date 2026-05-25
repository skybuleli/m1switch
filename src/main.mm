#include "common/Log.h"
#include "common/Config.h"

#include "frontend/App/AppDelegate.h"

#include <AppKit/AppKit.h>

// ── Entry point ─────────────────────────────────────────────
// M1Switch — Nintendo Switch emulator for Apple Silicon
//
// Phase 0: Initializes logging, configuration, and the AppKit
//          application event loop. Shows an empty window.
//
// Architecture:
//   main() → NSApplicationMain() → AppDelegate
//         → LibraryWindow (game browser, Phase 8)
//         → GameWindow (MTKView + emulator core)

int main(int argc, const char* argv[]) {
    // Initialize logging before anything else
    Log::Init();
    LOG_INFO("M1Switch v%s starting...", PROJECT_VER);

    // Log system info
    LOG_INFO("Platform: macOS %s (Apple Silicon)",
             [[[NSProcessInfo processInfo] operatingSystemVersionString] UTF8String]);

    size_t memSize = [[NSProcessInfo processInfo] physicalMemory];
    LOG_INFO("Physical memory: %.1f GB", (double)memSize / 1e9);

    if (memSize < 6ULL * 1024 * 1024 * 1024) {
        LOG_WARN("Less than 8 GB RAM detected — performance may be limited");
    }

    // Create application delegate
    AppDelegate* delegate = [[AppDelegate alloc] init];
    [NSApplication sharedApplication].delegate = delegate;

    // Run the AppKit event loop
    LOG_INFO("Entering NSApplicationMain");
    [NSApp run];

    // Cleanup (normally unreachable on macOS)
    Config::Instance().Save();
    Log::Shutdown();

    return 0;
}
