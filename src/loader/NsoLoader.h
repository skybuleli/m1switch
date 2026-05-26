#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include <span>
#include <vector>
#include <string>

// ═══════════════════════════════════════════════════════════
// NSO (Nintendo Switch Object) Loader
// ═══════════════════════════════════════════════════════════
//
// The NSO0 format is the standard executable format for
// commercial Switch games. It contains up to 3 segments:
//   .text   — executable code
//   .rodata — read-only data (constants, strings)
//   .data   — read-write data + BSS
//
// Each segment may be LZ4 compressed. The loader handles
// decompression, memory mapping, and SVC patching.

struct NsoSegment {
    u64 guest_address = 0;
    u64 size = 0;            // decompressed size
    u64 compressed_size = 0; // 0 = uncompressed
    Memory::Permission perm = Memory::Permission::None;
    u64 file_offset = 0;     // within the NSO file
};

struct NsoLoadInfo {
    std::vector<NsoSegment> segments;
    u64 entry_point = 0;
    u64 base_address = 0;    // where the module is loaded
    u64 bss_address = 0;
    u64 bss_size = 0;
    std::string build_id;

    // MOD3 / MOD0 offset within the loaded module
    u32 mod_offset = 0;
};

class NsoLoader {
public:
    explicit NsoLoader(Memory& memory);
    ~NsoLoader() = default;

    // Load an NSO from an in-memory buffer
    Result LoadFromBuffer(std::span<const u8> buffer, NsoLoadInfo& info,
                           u64 base_address);

    // Load an NSO from a file
    Result LoadFromFile(const std::string& path, NsoLoadInfo& info,
                         u64 base_address);

private:
    // NSO0 header structure (0x100 bytes, packed to match on-disk format)
    struct NsoHeader {
        u32 magic;           // 0x00: "NSO0"
        u32 version;         // 0x04
        u32 reserved;        // 0x08
        u32 flags;           // 0x0C
        u64 text_file_off;   // 0x10: .text file offset (relative to NSO start)
        u64 text_mem_off;    // 0x18: .text memory offset
        u64 text_size;       // 0x20: .text decompressed size
        u64 ro_file_off;     // 0x28: .rodata file offset
        u64 ro_mem_off;      // 0x30: .rodata memory offset
        u64 ro_size;         // 0x38: .rodata decompressed size
        u64 data_file_off;   // 0x40: .data file offset
        u64 data_mem_off;    // 0x48: .data memory offset
        u64 data_size;       // 0x50: .data decompressed size
        u32 bss_size;        // 0x58
        u32 mod_offset;      // 0x5C: Module object offset
        u8  _pad1[4];        // 0x60: reserved
        u32 text_comp_size;  // 0x64: .text compressed size (0 = uncompressed)
        u32 ro_comp_size;    // 0x68
        u32 data_comp_size;  // 0x6C
        u8  _pad2[0x10];     // 0x70-0x7F: reserved
        u8  build_id[0x20];  // 0x80: SHA256
        u8  _pad3[0x60];     // 0xA0-0xFF: reserved
    } __attribute__((packed));
    static_assert(sizeof(NsoHeader) == 0x100, "NSO header must be 256 bytes");

    bool ParseHeader(std::span<const u8> buffer, NsoHeader& header);
    Result DecompressAndMap(std::span<const u8> buffer,
                             const NsoHeader& header,
                             u64 base_address, NsoLoadInfo& info);

    Memory& memory_;
};
