// ═══════════════════════════════════════════════════════════
// Audio Subsystem — Clean implementation
// ═══════════════════════════════════════════════════════════

#include "common/Log.h"
#include "common/Types.h"

// ── ADPCM Decoder (pure C++, no ObjC) ──────────────
class AdpcmDecoder {
public:
    static constexpr u32 FRAME_SIZE = 8;
    static constexpr u32 SAMPLES_PER_FRAME = 14;

    static void DecodeFrame(const u8* frame, s16* out, u32& out_idx) { (void)frame; (void)out; (void)out_idx; }
    static u32 DecodeFrames(const u8* f, u32 cnt, s16* pcm, u32 max) { (void)f; (void)cnt; (void)pcm; (void)max; return 0; }
};

// ── AudioManager (C++ only, initializes later) ────
class AudioManager {
public:
    static AudioManager& Instance() { static AudioManager m; return m; }
    void Initialize() { LOG_INFO("Audio: stub initialized"); }
    void Shutdown() {}
    u32 SubmitPcm(const s16* s, u32 c) { (void)s; (void)c; return 0; }
    u32 SubmitAdpcm(const u8* f, u32 c) { (void)f; (void)c; return 0; }
    void SetVolume(float v) { (void)v; }
    float GetVolume() const { return 1.0f; }
    bool IsActive() const { return false; }
    u32 FramesBuffered() const { return 0; }
};

extern "C" {
void Audio_Initialize() { AudioManager::Instance().Initialize(); }
void Audio_Shutdown() { AudioManager::Instance().Shutdown(); }
u32 Audio_SubmitPcm(const s16* s, u32 c) { return AudioManager::Instance().SubmitPcm(s, c); }
u32 Audio_SubmitAdpcm(const u8* f, u32 c) { return AudioManager::Instance().SubmitAdpcm(f, c); }
void Audio_SetVolume(float v) { AudioManager::Instance().SetVolume(v); }
float Audio_GetVolume() { return AudioManager::Instance().GetVolume(); }
bool Audio_IsActive() { return AudioManager::Instance().IsActive(); }
u32 Audio_FramesBuffered() { return AudioManager::Instance().FramesBuffered(); }
}
