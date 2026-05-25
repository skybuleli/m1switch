#include "common/Log.h"
#include "common/Types.h"

#include <unordered_map>
#include <string>
#include <mutex>

// ── SM (Service Manager) ────────────────────────────────────
// Minimal stub for Phase 1.
// Full implementation in Phase 6.

struct ServiceEntry {
    std::string name;
    u64 handle;  // Session handle
};

static std::unordered_map<std::string, ServiceEntry> s_services;
static std::mutex s_mutex;

extern "C" void ServiceSmInitialize() {
    LOG_INFO("SM service initialized");
}

extern "C" u64 ServiceSmRegisterService(const char* name) {
    std::lock_guard<std::mutex> lock(s_mutex);
    static u64 next_handle = 0xCAFE0000;

    u64 handle = next_handle++;
    s_services[name] = {name, handle};
    LOG_DEBUG("SM register: '%s' → handle 0x%llx", name, handle);
    return handle;
}

extern "C" u64 ServiceSmLookupService(const char* name) {
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_services.find(name);
    if (it != s_services.end()) {
        return it->second.handle;
    }
    LOG_WARN("SM lookup: '%s' not found", name);
    return 0;
}
