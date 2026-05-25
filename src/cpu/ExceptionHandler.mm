#import "cpu/ExceptionHandler.h"
#import <mach/mach_error.h>
#import <mach/mach_port.h>
#import <mach/mach_traps.h>
#import <mach/mach_init.h>

constexpr u32 BRK_PATTERN  = 0xD4200000;
constexpr u32 BRK_MASK     = 0xFF00001F;
constexpr u32 BRK_TAG_BASE = 0x1000;

MachExceptionHandler::MachExceptionHandler() {
    kern_return_t kr = mach_port_allocate(
        mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &exception_port_);
    if (kr != KERN_SUCCESS) { LOG_ERROR("mach_port_allocate: %d", kr); return; }
    kr = mach_port_insert_right(mach_task_self(), exception_port_,
                                exception_port_, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        LOG_ERROR("mach_port_insert_right: %d", kr);
        mach_port_deallocate(mach_task_self(), exception_port_);
        exception_port_ = MACH_PORT_NULL; return;
    }
    LOG_DEBUG("Exception port: 0x%x", exception_port_);
}

MachExceptionHandler::~MachExceptionHandler() {
    Stop();
    if (exception_port_ != MACH_PORT_NULL)
        mach_port_deallocate(mach_task_self(), exception_port_);
}

void MachExceptionHandler::SetSvcDispatch(SvcHandlerFn fn) {
    dispatch_ = std::move(fn);
}

Result MachExceptionHandler::InstallOnCurrentThread() {
    if (exception_port_ == MACH_PORT_NULL) return Result::InvalidHandle;
    kern_return_t kr = thread_set_exception_ports(
        mach_thread_self(), EXC_MASK_BREAKPOINT, exception_port_,
        EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, ARM_EXCEPTION_STATE64);
    if (kr != KERN_SUCCESS) {
        LOG_WARN("thread_set_exception_ports: %d, trying task-level", kr);
        kr = task_set_exception_ports(
            mach_task_self(), EXC_MASK_BREAKPOINT, exception_port_,
            EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, ARM_EXCEPTION_STATE64);
        if (kr != KERN_SUCCESS) {
            LOG_ERROR("task_set_exception_ports: %d", kr);
            return Result::PermissionDenied;
        }
    }
    return Result::Success;
}

// ── Send a simple KERN_SUCCESS reply ────────────────────────
static void SendReply(const mach_msg_header_t* req) {
    mach_msg_header_t reply = {};
    reply.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(req->msgh_bits), 0);
    reply.msgh_size = sizeof(reply);
    reply.msgh_remote_port = req->msgh_local_port;
    reply.msgh_local_port = MACH_PORT_NULL;
    reply.msgh_id = req->msgh_id + 100;
    mach_msg(&reply, MACH_SEND_MSG, sizeof(reply), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
}

void MachExceptionHandler::HandleOne() {
    union { mach_msg_header_t hdr; u8 buf[2048]; } msg;

    kern_return_t kr = mach_msg(
        &msg.hdr, MACH_RCV_MSG | MACH_RCV_LARGE,
        0, sizeof(msg), exception_port_,
        MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) {
        if (kr != MACH_RCV_TOO_LARGE) LOG_ERROR("mach_msg: %d", kr);
        return;
    }

    // Parse exception message for faulting thread port
    mach_msg_body_t* body = (mach_msg_body_t*)(&msg.hdr + 1);
    thread_act_t faulting_thread = MACH_PORT_NULL;

    if (body->msgh_descriptor_count >= 2) {
        auto* desc = (mach_msg_port_descriptor_t*)(body + 1);
        faulting_thread = desc[0].name;
    }

    if (faulting_thread == MACH_PORT_NULL) {
        LOG_ERROR("No faulting thread in exception message");
        SendReply(&msg.hdr);
        return;
    }

    // Read faulting thread state
    arm_unified_thread_state state;
    mach_msg_type_number_t sc = ARM_UNIFIED_THREAD_STATE_COUNT;
    kr = thread_get_state(faulting_thread, ARM_UNIFIED_THREAD_STATE,
                          (thread_state_t)&state, &sc);
    if (kr != KERN_SUCCESS) {
        LOG_ERROR("thread_get_state(0x%x): %d", faulting_thread, kr);
        SendReply(&msg.hdr);
        return;
    }

    // Decode BRK at PC
    u64 pc = state.ts_64.__pc;
    u32 inst;
    vm_size_t rsz;
    kr = vm_read_overwrite(mach_task_self(), pc, sizeof(inst),
                           (vm_address_t)&inst, &rsz);
    if (kr == KERN_SUCCESS && (inst & BRK_MASK) == BRK_PATTERN) {
        u32 tag = (inst >> 5) & 0xFFFF;
        u32 svc = tag - BRK_TAG_BASE;
        LOG_TRACE("SVC #%u @ 0x%llx [thread 0x%x]", svc, pc, faulting_thread);
        if (dispatch_) dispatch_(tag, svc, &state);
    }

    // Advance PC and write back
    state.ts_64.__pc = pc + 4;
    thread_set_state(faulting_thread, ARM_UNIFIED_THREAD_STATE,
                     (thread_state_t)&state, sc);

    SendReply(&msg.hdr);
}

void MachExceptionHandler::Run() {
    if (exception_port_ == MACH_PORT_NULL) return;
    running_ = true;
    LOG_DEBUG("Handler running on port 0x%x", exception_port_);
    while (running_) HandleOne();
    running_ = false;
}

void MachExceptionHandler::Stop() {
    running_ = false;
    if (exception_port_ != MACH_PORT_NULL) {
        mach_msg_header_t dummy = {};
        dummy.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
        dummy.msgh_size = sizeof(dummy);
        dummy.msgh_local_port = exception_port_;
        mach_msg(&dummy, MACH_SEND_MSG, sizeof(dummy),
                 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
    }
}

u32 MachExceptionHandler::TagToSvc(u32 tag) { return tag - BRK_TAG_BASE; }
