// ═══════════════════════════════════════════════════════════
// Audio Subsystem — CoreAudio playback with ADPCM decoder
// ═══════════════════════════════════════════════════════════

#include "common/Log.h"
#include "common/Types.h"

#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AudioUnit.h>

#include <atomic>
#include <vector>
#include <cstring>
#include <cmath>

// ── Constants ──────────────────────────────────────────────
static constexpr u32 SAMPLE_RATE = 48000;
static constexpr u32 CHANNELS = 2;
static constexpr u32 BITS_PER_SAMPLE = 16;
static constexpr u32 BYTES_PER_FRAME = CHANNELS * (BITS_PER_SAMPLE / 8);
static constexpr u32 RING_BUF_CAPACITY = 48000 * 4;  // ~4 seconds of stereo
static constexpr u32 RING_BUF_MASK = RING_BUF_CAPACITY - 1;

// ── IMA-ADPCM Decoder ─────────────────────────────────────
// Nintendo Switch ADPCM: 8-byte frame → 14 PCM16 samples
//
// Frame format:
//   [0-1]  s16 predictor (initial sample)
//   [2]    s8  step_index (clamped 0-88)
//   [3]    reserved/padding
//   [4-7]  8 nibbles (4-bit), each encodes one sample delta
//
// Each nibble: sign/magnitude encoded sample delta
//
// Standard IMA-ADPCM step table (89 entries)
class AdpcmDecoder {
public:
    static constexpr u32 FRAME_SIZE = 8;
    static constexpr u32 SAMPLES_PER_FRAME = 14;

    static s32 DecodeNibble(s32 nibble, s32 step) {
        s32 diff = 0;
        s32 step_half = step >> 1;
        if (nibble & 4) diff += step;
        if (nibble & 2) diff += step_half;
        if (nibble & 1) diff += step_half >> 1;
        diff += step_half >> 2;
        return (nibble & 8) ? -diff : diff;
    }

    static s32 NextStep(s32 nibble, s32 step_index) {
        static const s8 step_table[] = {
            -1, -1, -1, -1, 2, 4, 6, 8,
        };
        s32 next = step_index + step_table[nibble & 7];
        if (next < 0) next = 0;
        if (next >= STEP_TABLE_SIZE) next = STEP_TABLE_SIZE - 1;
        return next;
    }

    static void DecodeFrame(const u8* frame, s16* out, u32& out_idx) {
        s32 predictor = (s32)(s16)(frame[0] | (frame[1] << 8));
        s32 step_index = (s32)(s8)frame[2];
        if (step_index < 0) step_index = 0;
        if (step_index >= STEP_TABLE_SIZE) step_index = STEP_TABLE_SIZE - 1;

        out[out_idx++] = (s16)predictor;

        for (int i = 0; i < 7; i++) {
            u8 byte = frame[4 + i];
            // High nibble first
            for (int n = 1; n >= 0; n--) {
                s32 nibble = (byte >> (n * 4)) & 0xF;
                s32 step = sStepTable[step_index];
                s32 diff = DecodeNibble(nibble, step);
                predictor += diff;
                if (predictor > 32767) predictor = 32767;
                if (predictor < -32768) predictor = -32768;
                step_index = NextStep(nibble, step_index);
                out[out_idx++] = (s16)predictor;
            }
        }
    }

    static u32 DecodeFrames(const u8* frames, u32 count,
                             s16* pcm, u32 max_samples) {
        u32 out_idx = 0;
        for (u32 i = 0; i < count && out_idx + SAMPLES_PER_FRAME <= max_samples; i++) {
            DecodeFrame(frames + i * FRAME_SIZE, pcm, out_idx);
        }
        return out_idx;
    }

    static u32 EncodeNibble(s32 diff, s32 step) {
        s32 half = step >> 1;
        u32 nibble = 0;
        if (diff < 0) { nibble |= 8; diff = -diff; }
        if (diff >= step) { nibble |= 4; diff -= step; }
        if (diff >= half) { nibble |= 2; diff -= half; }
        if (diff >= (half >> 1)) { nibble |= 1; }
        return nibble;
    }

private:
    static constexpr s32 sStepTable[] = {
        7,8,9,10,11,12,13,14,16,17,
        19,21,23,25,28,31,34,37,41,45,
        50,55,60,66,73,80,88,97,107,118,
        130,143,157,173,190,209,230,253,279,307,
        337,371,408,449,494,544,598,658,724,796,
        876,963,1060,1166,1282,1411,1552,1707,1878,2066,
        2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
        5892,6481,7129,7841,8624,9486,10434,11477,12625,13887,
        15276,16803,18483,20331,22364,24600,27060,29766,32742,
    };
    static constexpr s32 STEP_TABLE_SIZE = sizeof(sStepTable) / sizeof(sStepTable[0]);
};

// ── Lock-free ring buffer ─────────────────────────────────
template<typename T>
class RingBuffer {
public:
    RingBuffer() : buf_(RING_BUF_CAPACITY) {}

    u32 Write(const T* data, u32 count) {
        u32 wp = write_pos_.load(std::memory_order_relaxed);
        u32 rp = read_pos_.load(std::memory_order_acquire);
        u32 used = (wp - rp) & RING_BUF_MASK;
        u32 space = RING_BUF_CAPACITY - used - 1;
        u32 to_write = std::min(count, space);

        for (u32 i = 0; i < to_write; i++) {
            buf_[(wp + i) & RING_BUF_MASK] = data[i];
        }
        write_pos_.store((wp + to_write) & RING_BUF_MASK,
                         std::memory_order_release);
        return to_write;
    }

    u32 Read(T* data, u32 count) {
        u32 rp = read_pos_.load(std::memory_order_relaxed);
        u32 wp = write_pos_.load(std::memory_order_acquire);
        u32 used = (wp - rp) & RING_BUF_MASK;
        u32 to_read = std::min(count, used);

        for (u32 i = 0; i < to_read; i++) {
            data[i] = buf_[(rp + i) & RING_BUF_MASK];
        }
        read_pos_.store((rp + to_read) & RING_BUF_MASK,
                        std::memory_order_release);
        return to_read;
    }

    u32 Available() const {
        u32 wp = write_pos_.load(std::memory_order_acquire);
        u32 rp = read_pos_.load(std::memory_order_acquire);
        return (wp - rp) & RING_BUF_MASK;
    }

    void Clear() {
        read_pos_.store(write_pos_.load(std::memory_order_relaxed),
                        std::memory_order_release);
    }

private:
    std::vector<T> buf_;
    std::atomic<u32> read_pos_{0};
    std::atomic<u32> write_pos_{0};
};

// ── CoreAudio Playback ─────────────────────────────────────
class CoreAudioPlayer {
public:
    CoreAudioPlayer() = default;
    ~CoreAudioPlayer() { Shutdown(); }

    bool Initialize() {
        // 描述音频格式
        AudioStreamBasicDescription fmt = {};
        fmt.mSampleRate = SAMPLE_RATE;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsSignedInteger
                         | kAudioFormatFlagIsPacked;
        fmt.mBitsPerChannel = BITS_PER_SAMPLE;
        fmt.mChannelsPerFrame = CHANNELS;
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerPacket = BYTES_PER_FRAME;
        fmt.mBytesPerFrame = BYTES_PER_FRAME;

        // 创建 AudioQueue
        OSStatus st = AudioQueueNewOutput(
            &fmt,
            AudioCallback,
            this,
            nullptr,  // 主线程 run loop
            kCFRunLoopCommonModes,
            0,
            &queue_);
        if (st != noErr) {
            LOG_ERROR("AudioQueueNewOutput failed: %d", (int)st);
            return false;
        }

        // 分配缓冲区并预填静音
        for (int i = 0; i < kNumBuffers; i++) {
            AudioQueueAllocateBuffer(queue_, kBufferSize, &buffers_[i]);
            std::memset(buffers_[i]->mAudioData, 0, kBufferSize);
            buffers_[i]->mAudioDataByteSize = kBufferSize;
            AudioQueueEnqueueBuffer(queue_, buffers_[i], 0, nullptr);
        }

        return true;
    }

    void Shutdown() {
        if (queue_) {
            AudioQueueStop(queue_, true);
            AudioQueueDispose(queue_, false);
            queue_ = nullptr;
        }
    }

    void Start() {
        if (queue_ && !started_) {
            AudioQueueStart(queue_, nullptr);
            started_ = true;
        }
    }

    void Stop() {
        if (queue_ && started_) {
            AudioQueueStop(queue_, true);
            started_ = false;
        }
    }

    void SetVolume(float vol) {
        if (queue_) {
            AudioQueueSetParameter(queue_, kAudioQueueParam_Volume, vol);
        }
    }

    RingBuffer<s16>& GetRing() { return ring_; }

private:
    static constexpr int kNumBuffers = 3;
    static constexpr u32 kBufferSize = 8192;  // 4096 frames

    static void AudioCallback(void* ctx, AudioQueueRef, AudioQueueBufferRef buf) {
        auto* self = static_cast<CoreAudioPlayer*>(ctx);
        u32 frames = kBufferSize / BYTES_PER_FRAME;
        s16* audio_buf = static_cast<s16*>(buf->mAudioData);

        u32 read = self->ring_.Read(audio_buf, frames * CHANNELS);
        // Fill remaining with silence
        if (read < frames * CHANNELS) {
            std::memset(audio_buf + read, 0,
                       (frames * CHANNELS - read) * sizeof(s16));
        }

        buf->mAudioDataByteSize = kBufferSize;
        AudioQueueEnqueueBuffer(self->queue_, buf, 0, nullptr);
    }

    AudioQueueRef queue_ = nullptr;
    AudioQueueBufferRef buffers_[kNumBuffers];
    bool started_ = false;
    RingBuffer<s16> ring_;
};

// ── AudioManager ─────────────────────────────────────────
class AudioManager {
public:
    static AudioManager& Instance() {
        static AudioManager m;
        return m;
    }

    void Initialize() {
        if (initialized_) return;
        if (player_.Initialize()) {
            player_.SetVolume(1.0f);
            player_.Start();
            initialized_ = true;
            LOG_INFO("Audio: CoreAudio initialized (%d Hz, %d ch, %d-bit)",
                     SAMPLE_RATE, CHANNELS, BITS_PER_SAMPLE);
        } else {
            LOG_ERROR("Audio: CoreAudio initialization failed");
        }
    }

    void Shutdown() {
        if (!initialized_) return;
        player_.Stop();
        player_.Shutdown();
        initialized_ = false;
        LOG_INFO("Audio: shutdown");
    }

    u32 SubmitPcm(const s16* samples, u32 frame_count) {
        if (!initialized_ || !samples || frame_count == 0) return 0;
        u32 total_samples = frame_count * CHANNELS;
        return player_.GetRing().Write(samples, total_samples);
    }

    u32 SubmitAdpcm(const u8* frames, u32 frame_count) {
        if (!initialized_ || !frames || frame_count == 0) return 0;

        // Decode ADPCM frames to PCM16
        u32 max_samples = frame_count * AdpcmDecoder::SAMPLES_PER_FRAME * CHANNELS;
        // Stack-allocate for small payloads, heap for large
        std::vector<s16> pcm(max_samples);
        u32 decoded = AdpcmDecoder::DecodeFrames(frames, frame_count,
                                                   pcm.data(), max_samples);
        // Duplicate to stereo if needed
        if (CHANNELS == 2 && decoded > 0) {
            // PCM16 mono → stereo: duplicate each sample
            std::vector<s16> stereo(decoded * 2);
            for (u32 i = 0; i < decoded; i++) {
                stereo[i * 2] = pcm[i];
                stereo[i * 2 + 1] = pcm[i];
            }
            return player_.GetRing().Write(stereo.data(), decoded * 2);
        }
        return player_.GetRing().Write(pcm.data(), decoded);
    }

    void SetVolume(float v) {
        if (initialized_) player_.SetVolume(v);
    }

    float GetVolume() const {
        return 1.0f;  // simplified
    }

    bool IsActive() const { return initialized_; }

    u32 FramesBuffered() const {
        if (!initialized_) return 0;
        return const_cast<CoreAudioPlayer&>(player_).GetRing().Available() / CHANNELS;
    }

private:
    AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    CoreAudioPlayer player_;
    bool initialized_ = false;
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
