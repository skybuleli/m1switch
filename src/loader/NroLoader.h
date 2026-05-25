#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include "memory/Memory.h"

#include <vector>
#include <span>
#include <string>
#include <cstdio>

// ── NRO (Nintendo Relocatable Object) Loader ────────────────

struct NroSegment {
    u64 file_offset;
    u64 size;
    u64 guest_address;
    Memory::Permission perm;
};

struct NroLoadInfo {
    std::vector<NroSegment> segments;
    u64 entry_point;
    u64 bss_address;
    u64 bss_size;
    std::string build_id;
};

class NroLoader {
public:
    explicit NroLoader(Memory& memory);

    Result LoadFromFile(const std::string& path, NroLoadInfo& info);
    Result LoadFromBuffer(std::span<const u8> buffer, NroLoadInfo& info);

private:
    // Packed NRO header — on-disk layout is exactly 256 bytes
    struct NroPackedHeader {
        u32 magic;
        u32 version;
        u32 size;
        u32 flags;
        u32 text_start;
        u32 text_size;
        u32 rodata_start;
        u32 rodata_size;
        u32 data_start;
        u32 data_size;
        u32 bss_size;
        u32 reserved_0;
        u8  build_id[32];
        u8  reserved_1[176];
    } __attribute__((packed));
    static_assert(sizeof(NroPackedHeader) == 0x100,
                  "NRO header must be 256 bytes");

    bool ParseHeader(std::span<const u8> buffer, NroPackedHeader& header);

    u32 header_off_ = 0;
    Memory& memory_;
};
