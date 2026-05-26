// ═══════════════════════════════════════════════════════════
// Input Subsystem — Full HID shared memory + GCController
// ═══════════════════════════════════════════════════════════
// HID shared memory layout based on Switchbrew / libnx.
// The game reads directly from HID_SHARED_MEM (0xE1000000).
// We write controller state there in the format libnx expects.

#import <GameController/GameController.h>
#import <Carbon/Carbon.h>
#include "common/Log.h"
#include "common/Types.h"
#include <cstring>
#include <atomic>
#include <mutex>
#include <chrono>
#include <mach/mach_time.h>

// ── HID Descriptor / Section IDs ─────────────────────────
// These match the indices used in the official HID shared memory
enum HidDeviceType : u32 {
    HID_Type_DebugPad        = 0,
    HID_Type_TouchScreen     = 1,
    HID_Type_Mouse           = 2,
    HID_Type_Keyboard        = 3,
    HID_Type_BasicXpad       = 4,
    HID_Type_Digitizer       = 5,
    HID_Type_HomeButton      = 6,
    HID_Type_SleepButton     = 7,
    HID_Type_CaptureButton   = 8,
    HID_Type_Npad            = 9,
    HID_Type_InputDetector   = 10,
    HID_Type_UniquePad       = 11,
    HID_Type_Gesture         = 12,
    HID_Type_ConsoleSixAxis  = 13,
    HID_Type_DebugMouse      = 14,
    HID_Type_Palma           = 15,
    HID_Type_System          = 16,
    HID_NUM_TYPES            = 17,
};

// ── Npad Button (matches libnx HidNpadButton exactly) ────
// BITL(n) = 1ULL << n
enum NpadButton : u64 {
    Npad_A            = 1ULL << 0,
    Npad_B            = 1ULL << 1,
    Npad_X            = 1ULL << 2,
    Npad_Y            = 1ULL << 3,
    Npad_StickL       = 1ULL << 4,
    Npad_StickR       = 1ULL << 5,
    Npad_L            = 1ULL << 6,
    Npad_R            = 1ULL << 7,
    Npad_ZL           = 1ULL << 8,
    Npad_ZR           = 1ULL << 9,
    Npad_Plus         = 1ULL << 10,
    Npad_Minus        = 1ULL << 11,
    Npad_Left         = 1ULL << 12,
    Npad_Up           = 1ULL << 13,
    Npad_Right        = 1ULL << 14,
    Npad_Down         = 1ULL << 15,
    Npad_StickLLeft   = 1ULL << 16,
    Npad_StickLUp     = 1ULL << 17,
    Npad_StickLRight  = 1ULL << 18,
    Npad_StickLDown   = 1ULL << 19,
    Npad_StickRLeft   = 1ULL << 20,
    Npad_StickRUp     = 1ULL << 21,
    Npad_StickRRight  = 1ULL << 22,
    Npad_StickRDown   = 1ULL << 23,
};

// ── NpadStyleTag ─────────────────────────────────────────
enum NpadStyleTag : u32 {
    Style_FullKey    = 1 << 0,
    Style_Handheld   = 1 << 1,
    Style_JoyDual    = 1 << 2,
    Style_JoyLeft    = 1 << 3,
    Style_JoyRight   = 1 << 4,
    Style_Gc         = 1 << 5,
    Style_Palma      = 1 << 6,
    Style_Lark       = 1 << 7,
    Style_Lucia      = 1 << 9,
    Style_Lagon      = 1 << 10,
    Style_Lager      = 1 << 11,
    Style_SystemExt  = 1 << 29,
    Style_System     = 1 << 30,
};

static constexpr u32 NPAD_MAX_IDS = 9;
static constexpr u32 LIFO_ENTRIES = 17;
static constexpr u32 ANALOG_MAX   = 32767;
static constexpr s32 DEADZONE     = 5000;

// ── HID Shared Memory Structures ─────────────────────────
// These match the official Nintendo HID shared memory format
// as documented on Switchbrew and implemented in yuzu/libnx.

// Section info: offset + size for each device type
// Lives in the header at offset 0x80
struct HidSectionInfo {
    u64 offset;       // Byte offset from start of shared memory
    u64 size;         // Size of the section in bytes
};

// Npad common state — this is what the game reads from the LIFO
// Layout matches HidNpadCommonState / NpadFullKeyState in libnx
struct NpadCommonState {
    s64 timestamp;        // 0x00  — sample timestamp (ns or ticks)
    s64 count;            // 0x08  — entry count / sequence number
    u32 attributes;       // 0x10  — NpadAttribute bitmask
    u32 pad;              // 0x14
    u64 buttons;          // 0x18  — NpadButton bitmask
    s32 l_stick_x;        // 0x20
    s32 l_stick_y;        // 0x24
    s32 r_stick_x;        // 0x28
    s32 r_stick_y;        // 0x2C
};
static_assert(sizeof(NpadCommonState) == 0x30,
              "NpadCommonState must be 0x30 bytes");

// LIFO ring buffer: LIFO header + entries
// The header has timestamp/meta, then 17 atomic storage entries
struct NpadLifo {
    u64 timestamp_tick;                             // 0x00
    u64 max_entry_index;                            // 0x08  = LIFO_ENTRIES - 1
    u64 start_index;                                // 0x10
    u64 end_index;                                  // 0x18
    u8  _reserved[0x28];                            // 0x20  LIFO internal metadata
    NpadCommonState entries[LIFO_ENTRIES];           // 0x48
};
static_assert(sizeof(NpadLifo) == 0x48 + LIFO_ENTRIES * sizeof(NpadCommonState),
              "NpadLifo size mismatch");

// Per-controller internal state: one LIFO per style variant
struct NpadInternalState {
    NpadLifo fullkey;
    NpadLifo handheld;
    NpadLifo joy_dual;
    NpadLifo joy_left;
    NpadLifo joy_right;
    NpadLifo palma;
};

// Per-NpadId entry
struct NpadEntry {
    u32 style_set;              // bitmask of supported NpadStyleTag
    u32 _pad;
    u64 _reserved[2];
    NpadInternalState state;
};

// Top-level shared memory header
// Sections array at offset 0x80 (standard for HID format)
struct HidSharedMemoryHeader {
    u64 revision;                           // 0x00
    u32 format_info;                        // 0x08  1 = basic, other values = extended
    u32 _pad0;
    u64 _reserved0[12];                     // 0x10-0x6F
    u64 _pad1[2];                           // 0x70-0x7F  alignment to 0x80
    HidSectionInfo sections[HID_NUM_TYPES]; // 0x80
};
static_assert(sizeof(HidSharedMemoryHeader) == 0x80 + HID_NUM_TYPES * sizeof(HidSectionInfo),
              "HidSharedMemoryHeader section offset must be 0x80");

// ── Input Manager ────────────────────────────────────────
class InputManager {
public:
    static InputManager& Instance() {
        static InputManager m;
        return m;
    }

    void Initialize() {
        bool expected = false;
        if (!initialized_.compare_exchange_strong(expected, true)) return;
        LOG_INFO("Input: initializing GCController + keyboard...");

        // ── GCController setup ───────────────────────
        [GCController startWirelessControllerDiscoveryWithCompletionHandler:nil];

        // Store observer tokens for proper cleanup later
        connect_observer_ = [[NSNotificationCenter defaultCenter]
            addObserverForName:GCControllerDidConnectNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification* note) {
            GCController* ctrl = note.object;
            LOG_INFO("Input: controller connected: %s",
                     ctrl.vendorName ? [ctrl.vendorName UTF8String] : "unknown");
            setupController(ctrl);
            has_controller_.store(true, std::memory_order_release);
        }];

        disconnect_observer_ = [[NSNotificationCenter defaultCenter]
            addObserverForName:GCControllerDidDisconnectNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification*) {
            LOG_INFO("Input: controller disconnected");
            has_controller_.store(false, std::memory_order_release);
        }];

        // Check for already-connected controllers
        has_controller_.store(false, std::memory_order_release);
        for (GCController* ctrl in [GCController controllers]) {
            setupController(ctrl);
            has_controller_.store(true, std::memory_order_release);
            LOG_INFO("Input: found existing controller: %s",
                     ctrl.vendorName ? [ctrl.vendorName UTF8String] : "unknown");
        }

        initialized_ = true;
        LOG_INFO("Input: ready (controller=%s, keyboard fallback=%s)",
                 has_controller_.load() ? "yes" : "no",
                 has_controller_.load() ? "disabled" : "active");
    }

    void Shutdown() {
        if (!initialized_) return;
        if (connect_observer_) {
            [[NSNotificationCenter defaultCenter] removeObserver:connect_observer_];
            connect_observer_ = nil;
        }
        if (disconnect_observer_) {
            [[NSNotificationCenter defaultCenter] removeObserver:disconnect_observer_];
            disconnect_observer_ = nil;
        }
        initialized_ = false;
    }

    void Poll() {
        // Keyboard polling — only active when no controller is connected
        if (!has_controller_.load(std::memory_order_acquire)) {
            PollKeyboard();
        }
    }

    // Read current controller state (thread-safe via atomics)
    void GetState(u64& buttons, s32& lx, s32& ly, s32& rx, s32& ry) const {
        buttons = buttons_.load(std::memory_order_acquire);
        lx = left_x_.load(std::memory_order_acquire);
        ly = left_y_.load(std::memory_order_acquire);
        rx = right_x_.load(std::memory_order_acquire);
        ry = right_y_.load(std::memory_order_acquire);
    }

    // Write controller state to HID shared memory in libnx-compatible format
    void WriteToHidSharedMemory(u8* mem, u64 size) {
        if (!mem || size < sizeof(HidSharedMemoryHeader) + sizeof(NpadEntry)) {
            LOG_WARN("HID shared memory too small (%llu bytes, need %zu)",
                     size, sizeof(HidSharedMemoryHeader) + sizeof(NpadEntry));
            return;
        }

        auto* hdr = reinterpret_cast<HidSharedMemoryHeader*>(mem);

        // Thread-safe one-time header init
        std::call_once(header_init_flag_, [&]() {
            InitHeader(hdr);
        });

        // Find Npad section
        u64 npad_offset = hdr->sections[HID_Type_Npad].offset;
        u64 npad_size   = hdr->sections[HID_Type_Npad].size;
        if (npad_offset == 0 || npad_offset + npad_size > size) {
            LOG_WARN("HID: invalid Npad section offset=%llu size=%llu", npad_offset, npad_size);
            return;
        }

        // ── Write Npad state ──────────────────────────
        u64 buttons;
        s32 lx, ly, rx, ry;
        GetState(buttons, lx, ly, rx, ry);

        u64 now = mach_absolute_time();
        u64 count = sequence_count_.fetch_add(1, std::memory_order_relaxed);

        // Player 1 (index 0) in Handheld mode
        auto* entry = reinterpret_cast<NpadEntry*>(mem + npad_offset);
        NpadLifo& lifo = entry->state.handheld;

        // Thread-safe LIFO ring buffer update
        {
            std::lock_guard<std::mutex> lock(lifo_mutex_);
            u64 idx = (lifo.end_index + 1) % LIFO_ENTRIES;
            lifo.timestamp_tick = now;
            lifo.end_index = idx;
            if (lifo.start_index == lifo.end_index) {
                lifo.start_index = (lifo.start_index + 1) % LIFO_ENTRIES;
            }

            NpadCommonState& state = lifo.entries[idx];
            state.timestamp  = (s64)now;
            state.count      = (s64)count;
            state.attributes = 0x1;  // bit 0: is_connected = true
            state.buttons    = buttons;
            state.l_stick_x  = lx;
            state.l_stick_y  = ly;
            state.r_stick_x  = rx;
            state.r_stick_y  = ry;
        }
    }

    bool HasController() const { return has_controller_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> initialized_{false};
    std::atomic<bool> has_controller_{false};
    std::atomic<u64> buttons_{0};
    std::atomic<s32> left_x_{0};
    std::atomic<s32> left_y_{0};
    std::atomic<s32> right_x_{0};
    std::atomic<s32> right_y_{0};
    std::atomic<u64> sequence_count_{0};
    std::mutex lifo_mutex_;
    std::once_flag header_init_flag_;

    id connect_observer_ = nil;
    id disconnect_observer_ = nil;

    // ── GCController callback setup ───────────────────
    void setupController(GCController* ctrl) {
        GCExtendedGamepad* ext = ctrl.extendedGamepad;
        if (!ext) {
            LOG_INFO("Input: controller has no extendedGamepad profile");
            return;
        }

        // D-Pad
        ext.dpad.valueChangedHandler = ^(GCControllerDirectionPad* dpad, float x, float y) {
            u64 mask = 0;
            if (dpad.up.pressed)    mask |= Npad_Up;
            if (dpad.down.pressed)  mask |= Npad_Down;
            if (dpad.left.pressed)  mask |= Npad_Left;
            if (dpad.right.pressed) mask |= Npad_Right;
            UpdateButtons(mask, false);
        };

        // Face buttons
        ext.buttonA.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
            UpdateButtons(Npad_A, pressed);
        };
        ext.buttonB.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
            UpdateButtons(Npad_B, pressed);
        };
        ext.buttonX.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
            UpdateButtons(Npad_X, pressed);
        };
        ext.buttonY.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
            UpdateButtons(Npad_Y, pressed);
        };

        // Shoulder buttons
        ext.leftShoulder.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
            UpdateButtons(Npad_L, pressed);
        };
        ext.rightShoulder.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
            UpdateButtons(Npad_R, pressed);
        };

        // Triggers (pressed when value > threshold, e.g. > 0.5)
        ext.leftTrigger.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
            UpdateButtons(Npad_ZL, val > 0.3f);
        };
        ext.rightTrigger.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
            UpdateButtons(Npad_ZR, val > 0.3f);
        };

        // Menu buttons
        ext.buttonMenu.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
            UpdateButtons(Npad_Plus, pressed);
        };
        if (ext.buttonOptions) {
            ext.buttonOptions.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
                UpdateButtons(Npad_Minus, pressed);
            };
        }

        // Stick buttons (L3/R3)
        if (ext.leftThumbstickButton) {
            ext.leftThumbstickButton.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
                UpdateButtons(Npad_StickL, pressed);
            };
        }
        if (ext.rightThumbstickButton) {
            ext.rightThumbstickButton.valueChangedHandler = ^(GCControllerButtonInput* btn, float val, bool pressed) {
                UpdateButtons(Npad_StickR, pressed);
            };
        }

        // Analog sticks (with stick-direction pseudo-buttons)
        ext.leftThumbstick.valueChangedHandler = ^(GCControllerDirectionPad* stick, float x, float y) {
            left_x_.store(ClampStick(x), std::memory_order_release);
            left_y_.store(ClampStick(y), std::memory_order_release);
            // Atomically replace stick-direction pseudo-buttons:
            // clear all 4 left-stick bits, then set active ones
            u64 stick_bits = 0;
            if (x < -0.5f)  stick_bits |= Npad_StickLLeft;
            if (x > 0.5f)   stick_bits |= Npad_StickLRight;
            if (y < -0.5f)  stick_bits |= Npad_StickLDown;
            if (y > 0.5f)   stick_bits |= Npad_StickLUp;
            constexpr u64 L_STICK_ALL = Npad_StickLLeft | Npad_StickLUp
                                      | Npad_StickLRight | Npad_StickLDown;
            u64 cur = buttons_.load(std::memory_order_acquire);
            buttons_.store((cur & ~L_STICK_ALL) | stick_bits, std::memory_order_release);
        };

        ext.rightThumbstick.valueChangedHandler = ^(GCControllerDirectionPad* stick, float x, float y) {
            right_x_.store(ClampStick(x), std::memory_order_release);
            right_y_.store(ClampStick(y), std::memory_order_release);
            u64 stick_bits = 0;
            if (x < -0.5f)  stick_bits |= Npad_StickRLeft;
            if (x > 0.5f)   stick_bits |= Npad_StickRRight;
            if (y < -0.5f)  stick_bits |= Npad_StickRDown;
            if (y > 0.5f)   stick_bits |= Npad_StickRUp;
            constexpr u64 R_STICK_ALL = Npad_StickRLeft | Npad_StickRUp
                                      | Npad_StickRRight | Npad_StickRDown;
            u64 cur = buttons_.load(std::memory_order_acquire);
            buttons_.store((cur & ~R_STICK_ALL) | stick_bits, std::memory_order_release);
        };

        LOG_INFO("Input: controller callbacks registered");
    }

    void UpdateButtons(u64 mask, bool pressed) {
        u64 cur = buttons_.load(std::memory_order_acquire);
        u64 next = pressed ? (cur | mask) : (cur & ~mask);
        buttons_.store(next, std::memory_order_release);
    }

    static s32 ClampStick(float val) {
        // GCController gives -1.0 .. 1.0 range
        // Convert to Switch analog range: -32768 .. 32767
        if (val > 1.0f)  val = 1.0f;
        if (val < -1.0f) val = -1.0f;
        s32 raw = (s32)(val * (float)ANALOG_MAX);
        if (raw >= -(s32)DEADZONE && raw <= (s32)DEADZONE) raw = 0;
        return raw;
    }

    // ── Keyboard input (fallback when no controller) ───
    void PollKeyboard() {
        u64 mask = 0;

        // Arrow keys → D-Pad
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_UpArrow))
            mask |= Npad_Up;
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_DownArrow))
            mask |= Npad_Down;
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_LeftArrow))
            mask |= Npad_Left;
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_RightArrow))
            mask |= Npad_Right;

        // WASD → Left stick
        bool w = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_W);
        bool a = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_A);
        bool s = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_S);
        bool d = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_D);

        // Face buttons: Z/X → B/A,  1/2 → Y/X
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_Z))
            mask |= Npad_B;
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_X))
            mask |= Npad_A;
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_1))
            mask |= Npad_Y;
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_2))
            mask |= Npad_X;

        // Enter → Plus, Backspace → Minus
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_Return))
            mask |= Npad_Plus;
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_Delete))
            mask |= Npad_Minus;

        // Q/E → L/R,  Shift+Q/Shift+E → ZL/ZR
        bool shift = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_Shift)
                  || CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_RightShift);
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_Q))
            mask |= shift ? Npad_ZL : Npad_L;
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_E))
            mask |= shift ? Npad_ZR : Npad_R;

        buttons_.store(mask, std::memory_order_release);

        // Left stick: WASD
        s32 stick_x = 0, stick_y = 0;
        if (d) stick_x += ANALOG_MAX;
        if (a) stick_x -= ANALOG_MAX;
        if (w) stick_y += ANALOG_MAX;
        if (s) stick_y -= ANALOG_MAX;
        left_x_.store(stick_x, std::memory_order_release);
        left_y_.store(stick_y, std::memory_order_release);

        // Stick-direction pseudo-buttons from keyboard sticks
        u64 stick_mask = 0;
        if (stick_x < -DEADZONE)  stick_mask |= Npad_StickLLeft;
        if (stick_x > DEADZONE)   stick_mask |= Npad_StickLRight;
        if (stick_y < -DEADZONE)  stick_mask |= Npad_StickLDown;
        if (stick_y > DEADZONE)   stick_mask |= Npad_StickLUp;
        u64 cur = buttons_.load(std::memory_order_acquire);
        buttons_.store(cur | stick_mask, std::memory_order_release);

        // Right stick: IJKL
        bool i = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_I);
        bool j = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_J);
        bool k = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_K);
        bool l = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_L);
        s32 rx = 0, ry = 0;
        if (l) rx += ANALOG_MAX;
        if (j) rx -= ANALOG_MAX;
        if (i) ry += ANALOG_MAX;
        if (k) ry -= ANALOG_MAX;
        right_x_.store(rx, std::memory_order_release);
        right_y_.store(ry, std::memory_order_release);

        // Right-stick pseudo-buttons from IJKL
        stick_mask = 0;
        if (rx < -DEADZONE)  stick_mask |= Npad_StickRLeft;
        if (rx > DEADZONE)   stick_mask |= Npad_StickRRight;
        if (ry < -DEADZONE)  stick_mask |= Npad_StickRDown;
        if (ry > DEADZONE)   stick_mask |= Npad_StickRUp;
        cur = buttons_.load(std::memory_order_acquire);
        buttons_.store(cur | stick_mask, std::memory_order_release);
    }

    // ── One-time header initialization ────────────────
    // Called via std::call_once, safe from any thread
    void InitHeader(HidSharedMemoryHeader* hdr) {
        std::memset(hdr, 0, sizeof(HidSharedMemoryHeader));
        hdr->revision = 1;
        hdr->format_info = 0;  // 0 = basic format (sections at 0x80)

        // Npad section begins right after the header, page-aligned
        u64 npad_offset = sizeof(HidSharedMemoryHeader);
        npad_offset = (npad_offset + 0xFFF) & ~0xFFFULL;
        u64 npad_size = sizeof(NpadEntry) * NPAD_MAX_IDS;

        hdr->sections[HID_Type_Npad] = {npad_offset, npad_size};

        // Zero-initialize all Npad entries
        auto* entries = reinterpret_cast<NpadEntry*>(
            reinterpret_cast<u8*>(hdr) + npad_offset);
        std::memset(entries, 0, (size_t)npad_size);

        // Player 1: Handheld mode
        entries[0].style_set = Style_Handheld;
        auto& hh = entries[0].state.handheld;
        hh.max_entry_index = LIFO_ENTRIES - 1;
        hh.start_index = 0;
        hh.end_index = LIFO_ENTRIES - 1;  // will wrap to 0 on first write

        LOG_INFO("HID header: Npad section at 0x%llx (%u entries x %u ids = %llu bytes)",
                 npad_offset, (u32)sizeof(NpadEntry), NPAD_MAX_IDS, npad_size);
    }
};

// ── C-API wrappers ──────────────────────────────────────────
extern "C" {

void Input_Initialize() {
    InputManager::Instance().Initialize();
}

void Input_Shutdown() {
    InputManager::Instance().Shutdown();
}

void Input_Poll() {
    InputManager::Instance().Poll();
}

bool Input_HasController() {
    return InputManager::Instance().HasController();
}

void Input_WriteToHidSharedMemory(u8* mem, u64 size) {
    InputManager::Instance().WriteToHidSharedMemory(mem, size);
}

}
