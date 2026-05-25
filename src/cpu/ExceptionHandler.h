#pragma once

#include "common/Types.h"
#include "common/Log.h"

#include <mach/mach.h>
#include <functional>

using SvcHandlerFn = std::function<void(u32 brk_tag, u32 svc_num,
                                        arm_unified_thread_state* state)>;

class MachExceptionHandler {
public:
    MachExceptionHandler();
    ~MachExceptionHandler();

    MachExceptionHandler(const MachExceptionHandler&) = delete;
    MachExceptionHandler& operator=(const MachExceptionHandler&) = delete;

    void SetSvcDispatch(SvcHandlerFn fn);
    Result InstallOnCurrentThread();
    void Run();
    void Stop();

    // Expose the port for external use (e.g. task_set_exception_ports)
    mach_port_t Port() const { return exception_port_; }

private:
    void HandleOne();
    static u32 TagToSvc(u32 tag);

    mach_port_t exception_port_ = MACH_PORT_NULL;
    bool running_ = false;
    SvcHandlerFn dispatch_;
};
