#include "loader/NroLoader.h"

#include <cstring>
#include <fstream>

// ── NRO magic ───────────────────────────────────────────────
constexpr u32 NRO0_MAGIC = 0x304F524E;  // "NRO0"
constexpr u64 NRO_TEXT_BASE = 0x40000000;  // Guest address for .text

// ── Constructor ─────────────────────────────────────────────
NroLoader::NroLoader(Memory& memory) : memory_(memory) {}

// ── Parse header ────────────────────────────────────────────
bool NroLoader::ParseHeader(std::span<const u8> buffer,
                            NroPackedHeader& header) {
    if (buffer.size() < sizeof(NroPackedHeader)) return false;

    // Real NRO files have a 16-byte preamble (B instruction) before the header
    // so the actual NRO0 header starts at offset 0x10
    if (buffer.size() < 0x10 + sizeof(NroPackedHeader)) return false;
    const u8* d = buffer.data() + 0x10;
    header.magic        = (u32)d[0] | ((u32)d[1] << 8) | ((u32)d[2] << 16) | ((u32)d[3] << 24);
    header.version      = (u32)d[4] | ((u32)d[5] << 8) | ((u32)d[6] << 16) | ((u32)d[7] << 24);
    header.size         = (u32)d[8] | ((u32)d[9] << 8) | ((u32)d[10] << 16) | ((u32)d[11] << 24);
    header.flags        = (u32)d[12] | ((u32)d[13] << 8) | ((u32)d[14] << 16) | ((u32)d[15] << 24);
    header.text_start   = (u32)d[16] | ((u32)d[17] << 8) | ((u32)d[18] << 16) | ((u32)d[19] << 24);
    header.text_size    = (u32)d[20] | ((u32)d[21] << 8) | ((u32)d[22] << 16) | ((u32)d[23] << 24);
    header.rodata_start = (u32)d[24] | ((u32)d[25] << 8) | ((u32)d[26] << 16) | ((u32)d[27] << 24);
    header.rodata_size  = (u32)d[28] | ((u32)d[29] << 8) | ((u32)d[30] << 16) | ((u32)d[31] << 24);
    header.data_start   = (u32)d[32] | ((u32)d[33] << 8) | ((u32)d[34] << 16) | ((u32)d[35] << 24);
    header.data_size    = (u32)d[36] | ((u32)d[37] << 8) | ((u32)d[38] << 16) | ((u32)d[39] << 24);
    header.bss_size     = (u32)d[40] | ((u32)d[41] << 8) | ((u32)d[42] << 16) | ((u32)d[43] << 24);
    header.reserved_0   = (u32)d[44] | ((u32)d[45] << 8) | ((u32)d[46] << 16) | ((u32)d[47] << 24);
    std::memcpy(header.build_id, d + 0x30, 32);

    LOG_TRACE("ParseHeader: magic=0x%08x", header.magic);

    if (header.magic != NRO0_MAGIC) {
        LOG_ERROR("Bad NRO magic: 0x%08x (expected 0x%08x)",
                  header.magic, NRO0_MAGIC);
        return false;
    }
    return true;
}

// ── Load from file ──────────────────────────────────────────
Result NroLoader::LoadFromFile(const std::string& path, NroLoadInfo& info) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Cannot open: %s", path.c_str());
        return Result::NotFound;
    }

    auto file_size = static_cast<size_t>(file.tellg());
    file.seekg(0);

    std::vector<u8> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()),
              static_cast<std::streamsize>(file_size));
    file.close();

    return LoadFromBuffer(buffer, info);
}

// ── Load from buffer ────────────────────────────────────────
Result NroLoader::LoadFromBuffer(std::span<const u8> buffer,
                                 NroLoadInfo& info) {
    info = {};

    NroPackedHeader header;
    if (!ParseHeader(buffer, header)) return Result::InvalidArgument;

    LOG_TRACE("NRO parsed: magic=0x%08x", header.magic);

    // Build ID as hex (0x20 bytes × 2 hex chars + NUL = 65)
    char hex[65];
    for (int i = 0; i < 0x20; i++) {
        snprintf(hex + i * 2, 3, "%02x", header.build_id[i]);
    }
    hex[64] = '\0';
    info.build_id = hex;

    u32 text_start   = header.text_start;
    u32 text_size    = header.text_size;
    u32 rodata_start = header.rodata_start;
    u32 rodata_size  = header.rodata_size;
    u32 data_start   = header.data_start;
    u32 data_size    = header.data_size;
    u32 bss_size     = header.bss_size;

    LOG_INFO("NRO: magic=0x%08x size=%u", header.magic, header.size);
    LOG_INFO("  .text:   offset=%u size=%u", text_start, text_size);
    LOG_INFO("  .rodata: offset=%u size=%u", rodata_start, rodata_size);
    LOG_INFO("  .data:   offset=%u size=%u", data_start, data_size);
    LOG_INFO("  .bss:   size=%u", bss_size);

    // Validate segment ranges
    if (text_start + text_size > buffer.size() ||
        rodata_start + rodata_size > buffer.size() ||
        data_start + data_size > buffer.size()) {
        LOG_ERROR("NRO segment exceeds buffer (%zu)", buffer.size());
        return Result::InvalidArgument;
    }

    // ── Map segments ────────────────────────────────────
    u64 addr = NRO_TEXT_BASE;

    // .text → RX
    {
        u64 size_aligned = AlignUp<u64>(text_size, 0x1000);
        Result r = memory_.MapPhysical(addr, size_aligned,
                                        Memory::Permission::RX,
                                        buffer.data() + text_start);
        if (Failed(r)) return r;
        info.segments.push_back(
            {text_start, text_size, addr, Memory::Permission::RX});
        addr += size_aligned;
    }

    // .rodata → R
    if (rodata_size > 0) {
        u64 size_aligned = AlignUp<u64>(rodata_size, 0x1000);
        Result r = memory_.MapPhysical(addr, size_aligned,
                                        Memory::Permission::R,
                                        buffer.data() + rodata_start);
        if (Failed(r)) return r;
        info.segments.push_back(
            {rodata_start, rodata_size, addr, Memory::Permission::R});
        addr += size_aligned;
    }

    // .data → RW
    if (data_size > 0) {
        u64 size_aligned = AlignUp<u64>(data_size, 0x1000);
        Result r = memory_.MapPhysical(addr, size_aligned,
                                        Memory::Permission::RW,
                                        buffer.data() + data_start);
        if (Failed(r)) return r;
        info.segments.push_back(
            {data_start, data_size, addr, Memory::Permission::RW});
        addr += size_aligned;
    }

    // .bss → RW (zero-filled)
    if (bss_size > 0) {
        u64 bss_aligned = AlignUp<u64>(bss_size, 0x1000);
        Result r = memory_.MapPhysical(addr, bss_aligned,
                                        Memory::Permission::RW);
        if (Failed(r)) return r;
        auto* ptr = memory_.Pointer(addr);
        if (ptr) std::memset(ptr, 0, bss_size);
    }

    info.entry_point = NRO_TEXT_BASE;

    LOG_INFO("Entry: 0x%llx, %zu segment(s) mapped",
             info.entry_point, info.segments.size());

    return Result::Success;
}
