// ── AudioOut IPC Service ────────────────────────────────────
// Implements the audout: service for Switch games.
// Routes audio output from the guest to the host AudioManager.
//
// Reference: Switchbrew audout: service

#include "services/Ipc.h"
#include "common/Log.h"
#include <cstring>
#include <vector>

// ── Audio subsystem C API ──────────────────────────────────
extern "C" {
u32 Audio_SubmitPcm(const s16* samples, u32 frame_count);
u32 Audio_SubmitAdpcm(const u8* frames, u32 frame_count);
void Audio_SetVolume(float vol);
bool Audio_IsActive();
}

class AudioOutService : public ServiceBase {
public:
    AudioOutService() {
        IpcManager::Instance().RegisterService("audout:", this);
        IpcManager::Instance().RegisterService("audren:", this);
    }

    const char* Name() const override { return "audout:"; }

    bool HandleCommand(u32 cmd_id, const u8* in_data, size_t in_size,
                       u8* out_data, size_t* out_size) override {
        switch (cmd_id) {
        case 0:  return HandleInitialize(in_data, in_size, out_data, out_size);
        case 1:  return HandleOpenAudioOut(in_data, in_size, out_data, out_size);
        case 2:  return HandleListAudioOuts(in_data, in_size, out_data, out_size);
        case 3:  return HandleStartAudioOut(in_data, in_size, out_data, out_size);
        case 4:  return HandleStopAudioOut(in_data, in_size, out_data, out_size);
        case 5:  return HandleAppendAudioOutBuffer(in_data, in_size, out_data, out_size);
        case 6:  return HandleRegisterBufferEvent(in_data, in_size, out_data, out_size);
        case 7:  return HandleGetAudioOutState(in_data, in_size, out_data, out_size);
        case 8:  return HandleFlushAudioOutBuffers(in_data, in_size, out_data, out_size);
        default:
            LOG_TRACE("audout: unhandled cmd %u", cmd_id);
            *out_size = 0;
            return true;
        }
    }

private:
    struct AudioOutDevice {
        u32 sample_rate = 48000;
        u32 channel_count = 2;
        u32 pcm_format = 0; // 0 = PCM16, 1 = PCM8, 2 = ADPCM
        u32 state = 0;      // 0 = started, 1 = stopped
    };

    AudioOutDevice device_;

    // ── 0: Initialize ─────────────────────────────────────
    bool HandleInitialize(const u8* in, size_t in_sz,
                           u8* out, size_t* out_sz) {
        LOG_DEBUG("audout: Initialize");
        *out_sz = 0;
        return true;
    }

    // ── 1: OpenAudioOut ───────────────────────────────────
    bool HandleOpenAudioOut(const u8* in, size_t in_sz,
                             u8* out, size_t* out_sz) {
        // Input: device name (null-terminated string)
        // Output: audio out handle, sample rate, channel count, format, state
        LOG_INFO("audout: OpenAudioOut");

        // Default device parameters
        u32 handle = 0x600;  // AudioOut handle
        u32 sample_rate = device_.sample_rate;
        u32 channel_count = device_.channel_count;
        u32 pcm_format = device_.pcm_format;
        u32 state = device_.state;

        // Parse input for requested format (simplified)
        if (in_sz > 0) {
            const char* name = reinterpret_cast<const char*>(in);
            LOG_DEBUG("audout: opening '%s'", name);
        }

        if (*out_sz >= 32) {
            // Write output data
            out[0] = (u8)(handle); out[1] = (u8)(handle >> 8);
            out[2] = (u8)(handle >> 16); out[3] = (u8)(handle >> 24);
            // Sample rate at offset 8
            out[8] = (u8)(sample_rate); out[9] = (u8)(sample_rate >> 8);
            out[10] = (u8)(sample_rate >> 16); out[11] = (u8)(sample_rate >> 24);
            // Channel count at offset 12
            out[12] = (u8)(channel_count); out[13] = 0;
            // PCM format at offset 14
            out[14] = (u8)(pcm_format); out[15] = 0;
            // State at offset 16
            out[16] = (u8)(state);
            *out_sz = 32;
        }

        LOG_INFO("audout: opened %u Hz %u ch fmt=%u",
                 sample_rate, channel_count, pcm_format);
        return true;
    }

    // ── 2: ListAudioOuts ──────────────────────────────────
    bool HandleListAudioOuts(const u8* in, size_t in_sz,
                              u8* out, size_t* out_sz) {
        LOG_DEBUG("audout: ListAudioOuts");
        if (*out_sz >= 16) {
            // Return "Device1" as the only audio device
            const char* name = "Device1";
            size_t len = std::min(strlen(name), (size_t)*out_sz - 4);
            // First 4 bytes: count
            out[0] = 1;
            std::memcpy(out + 4, name, len);
            *out_sz = 4 + (u32)len;
        }
        return true;
    }

    // ── 3: StartAudioOut ──────────────────────────────────
    bool HandleStartAudioOut(const u8* in, size_t in_sz,
                              u8* out, size_t* out_sz) {
        LOG_DEBUG("audout: StartAudioOut");
        device_.state = 0; // started
        *out_sz = 0;
        return true;
    }

    // ── 4: StopAudioOut ───────────────────────────────────
    bool HandleStopAudioOut(const u8* in, size_t in_sz,
                             u8* out, size_t* out_sz) {
        LOG_DEBUG("audout: StopAudioOut");
        device_.state = 1; // stopped
        *out_sz = 0;
        return true;
    }

    // ── 5: AppendAudioOutBuffer ───────────────────────────
    bool HandleAppendAudioOutBuffer(const u8* in, size_t in_sz,
                                     u8* out, size_t* out_sz) {
        // Input: audio data buffer descriptor + PCM/ADPCM data
        // Output: buffer released event
        LOG_TRACE("audout: AppendAudioOutBuffer (sz=%zu)", in_sz);

        if (in_sz >= 16 && Audio_IsActive()) {
            // Parse buffer descriptor:
            // offset 0: buffer address (u64)
            // offset 8: buffer size (u64)
            u64 buf_addr = 0, buf_size = 0;
            std::memcpy(&buf_addr, in, sizeof(u64));
            std::memcpy(&buf_size, in + 8, sizeof(u64));

            if (buf_addr > 0 && buf_size > 0) {
                // The audio data follows the descriptor in the IPC buffer
                // For simplicity, assume PCM16 data starts after the 16-byte header
                u32 data_offset = 16;
                if (in_sz > data_offset) {
                    u32 data_size = (u32)(in_sz - data_offset);
                    u32 frame_count = data_size / (2 * sizeof(s16)); // stereo 16-bit

                    if (device_.pcm_format == 2) {
                        // ADPCM format
                        u32 adpcm_frames = data_size / 8; // 8 bytes per ADPCM frame
                        Audio_SubmitAdpcm(in + data_offset, adpcm_frames);
                    } else {
                        // PCM16 format
                        Audio_SubmitPcm(
                            reinterpret_cast<const s16*>(in + data_offset),
                            frame_count);
                    }
                }
            }
        }

        *out_sz = 0;
        return true;
    }

    // ── 6: RegisterBufferEvent ────────────────────────────
    bool HandleRegisterBufferEvent(const u8* in, size_t in_sz,
                                    u8* out, size_t* out_sz) {
        LOG_DEBUG("audout: RegisterBufferEvent");
        *out_sz = 0;
        return true;
    }

    // ── 7: GetAudioOutState ────────────────────────────────
    bool HandleGetAudioOutState(const u8* in, size_t in_sz,
                                 u8* out, size_t* out_sz) {
        LOG_TRACE("audout: GetAudioOutState");
        if (*out_sz >= 4) {
            out[0] = (u8)device_.state;
            *out_sz = 4;
        }
        return true;
    }

    // ── 8: FlushAudioOutBuffers ───────────────────────────
    bool HandleFlushAudioOutBuffers(const u8* in, size_t in_sz,
                                     u8* out, size_t* out_sz) {
        LOG_DEBUG("audout: FlushAudioOutBuffers");
        *out_sz = 0;
        return true;
    }
};

static AudioOutService g_audio_out_service;

void ServiceAudioOut_Init() {
    LOG_INFO("AudioOut service ready");
    (void)g_audio_out_service;
}
