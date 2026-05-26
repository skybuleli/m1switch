// ═══════════════════════════════════════════════════════════
// NSO Loader Implementation
// ═══════════════════════════════════════════════════════════

#include "loader/NsoLoader.h"
#include "cpu/NativeExec.h"
#include <cstring>
#include <fstream>
#include <algorithm>

// ── LZ4 decompression stub ─────────────────────────────────
// NSO segments may be LZ4 compressed. For now we handle the
// uncompressed-only case and flag compressed segments.
//
// TODO: Add real LZ4 decompression (bundled or system library).
// macOS includes libcompression which can handle LZ4.

namespace {

// Detect if data is LZ4-compressed by checking the magic byte
// sequence at the start of the segment.
bool IsLZ4Compressed(const u8* data, size_t size) {
    // LZ4 block magic: 0x184D2204 (little-endian)
    if (size < 4) return false;
    u32 magic;
    std::memcpy(&magic, data, 4);
    return magic == 0x184D2204;
}

// Simple LZ4 block decompression (raw blocks, no frame header).
// Falls back to memcpy for uncompressed data.
size_t DecompressLZ4(const u8* src, size_t src_size,
                      u8* dst, size_t dst_size) {
    if (!IsLZ4Compressed(src, src_size)) {
        // Not compressed — just copy
        size_t copy = std::min(src_size, dst_size);
        std::memcpy(dst, src, copy);
        return copy;
    }

    // Skip the LZ4 magic (first 4 bytes)
    const u8* in = src + 4;
    size_t in_size = src_size - 4;
    u8* out = dst;
    size_t out_pos = 0;
    size_t in_pos = 0;

    // Simple LZ4 block decoder (supports standard sequences)
    while (in_pos < in_size && out_pos < dst_size) {
        u8 token = in[in_pos++];
        // Literal length
        u32 lit_len = (token >> 4) & 0xF;
        if (lit_len == 15) {
            u8 extra;
            do {
                extra = in[in_pos++];
                lit_len += extra;
            } while (extra == 255 && in_pos < in_size);
        }
        // Copy literals
        if (lit_len > 0) {
            size_t to_copy = std::min((size_t)lit_len, dst_size - out_pos);
            if (in_pos + to_copy > in_size) break;
            std::memcpy(out + out_pos, in + in_pos, to_copy);
            in_pos += lit_len;
            out_pos += to_copy;
        }
        if (in_pos >= in_size) break;

        // Match offset + length
        u16 offset = (u16)in[in_pos] | ((u16)in[in_pos + 1] << 8);
        in_pos += 2;
        if (offset == 0) break; // invalid

        u32 match_len = (token & 0xF) + 4;
        if (match_len == 19) {
            u8 extra;
            do {
                extra = in[in_pos++];
                match_len += extra;
            } while (extra == 255 && in_pos < in_size);
        }

        // Copy match (may overlap — byte-by-byte for safety)
        for (u32 i = 0; i < match_len && out_pos < dst_size; i++) {
            out[out_pos] = out[out_pos - offset];
            out_pos++;
        }
    }

    return out_pos;
}

} // anonymous namespace

NsoLoader::NsoLoader(Memory& memory) : memory_(memory) {}

// ═══════════════════════════════════════════════════════════
// Header Parsing
// ═══════════════════════════════════════════════════════════

bool NsoLoader::ParseHeader(std::span<const u8> buffer, NsoHeader& header) {
    if (buffer.size() < sizeof(NsoHeader)) return false;

    std::memcpy(&header, buffer.data(), sizeof(NsoHeader));

    if (header.magic != 0x304F534E) { // "NSO0" in LE
        LOG_WARN("NSO: bad magic 0x%08x (expected NSO0)", header.magic);
        return false;
    }

    LOG_INFO("NSO: version=%u flags=0x%x", header.version, header.flags);
    LOG_INFO("  .text:  offset=0x%llx mem=0x%llx size=%llu",
             header.text_file_off, header.text_mem_off, header.text_size);
    LOG_INFO("  .rodata: offset=0x%llx mem=0x%llx size=%llu",
             header.ro_file_off, header.ro_mem_off, header.ro_size);
    LOG_INFO("  .data:  offset=0x%llx mem=0x%llx size=%llu bss=%u",
             header.data_file_off, header.data_mem_off, header.data_size,
             header.bss_size);
    LOG_INFO("  mod_offset=0x%x build_id=%02x%02x...",
             header.mod_offset, header.build_id[0], header.build_id[1]);

    return true;
}

// ═══════════════════════════════════════════════════════════
// Decompress & Map
// ═══════════════════════════════════════════════════════════

Result NsoLoader::DecompressAndMap(std::span<const u8> buffer,
                                    const NsoHeader& header,
                                    u64 base_address,
                                    NsoLoadInfo& info) {
    // ── Segment descriptors using new header fields ────
    u32 comp_sizes[3] = { header.text_comp_size,
                          header.ro_comp_size,
                          header.data_comp_size };

    struct SegDesc {
        u64 file_off;
        u64 mem_off;
        u64 decomp_size;
        u32 comp_size;  // 0 = uncompressed
        Memory::Permission perm;
    };

    SegDesc segs[3] = {
        {header.text_file_off,  header.text_mem_off,  header.text_size,
         comp_sizes[0], Memory::Permission::RX},
        {header.ro_file_off,    header.ro_mem_off,    header.ro_size,
         comp_sizes[1], Memory::Permission::R},
        {header.data_file_off,  header.data_mem_off,  header.data_size,
         comp_sizes[2], Memory::Permission::RW},
    };

    // Calculate total memory footprint
    u64 total_end = 0;
    for (auto& seg : segs) {
        u64 end = seg.mem_off + seg.decomp_size;
        if (end > total_end) total_end = end;
    }

    // Add BSS
    u64 bss_start = total_end;
    u64 bss_end = total_end + AlignUp<u64>(header.bss_size, 0x1000);

    // Update address space base for alignment
    u64 alloc_base = base_address;

    LOG_INFO("NSO: mapping at 0x%llx, total=0x%llx", alloc_base, bss_end);

    for (int i = 0; i < 3; i++) {
        auto& seg = segs[i];
        if (seg.decomp_size == 0) continue;

        u64 guest_addr = alloc_base + seg.mem_off;
        u64 map_size = AlignUp<u64>(seg.decomp_size, 0x1000);

        // Read compressed data from buffer
        if (seg.file_off >= buffer.size()) {
            LOG_WARN("NSO segment %d: file offset 0x%llx out of range", i, seg.file_off);
            continue;
        }
        u64 max_size = buffer.size() - seg.file_off;

        // Determine compressed size
        u64 comp_data_size = seg.decomp_size;
        if (seg.comp_size > 0 && seg.comp_size < seg.decomp_size) {
            comp_data_size = seg.comp_size;
        }

        // Allocate and decompress
        Result r = memory_.MapPhysical(guest_addr, map_size,
                                        Memory::Permission::RW);
        if (Failed(r)) return r;

        u8* dst = memory_.Pointer(guest_addr);
        if (!dst) return Result::OutOfMemory;

        const u8* src = buffer.data() + seg.file_off;
        size_t src_size = std::min((size_t)comp_data_size, (size_t)max_size);

        // Decompress (or copy if uncompressed)
        size_t written = DecompressLZ4(src, src_size, dst, (size_t)seg.decomp_size);
        LOG_INFO("NSO segment %d: mem=0x%llx size=%llu (comp=%llu → decomp=%zu)",
                 i, guest_addr, map_size, comp_data_size, written);

        // Patch SVCs in .text (segment 0)
        if (i == 0 && written > 0) {
            std::vector<std::pair<u32, u32>> svc_map;
            NativeExec::PatchSVCs(dst, (u32)std::min(written, (size_t)seg.decomp_size),
                                   svc_map);
            LOG_INFO("NSO: patched %zu SVCs in .text", svc_map.size());
        }

        // Set final permissions
        memory_.Protect(guest_addr, map_size, seg.perm);

        info.segments.push_back({
            guest_addr,
            seg.decomp_size,
            0,                 // compressed_size (0 = decompressed in memory)
            seg.perm,
            seg.file_off
        });
    }

    // ── BSS (zeroed) ────────────────────────────────────
    if (header.bss_size > 0) {
        u64 bss_addr = alloc_base + bss_start;
        u64 bss_map_size = AlignUp<u64>(header.bss_size, 0x1000);
        Result r = memory_.MapPhysical(bss_addr, bss_map_size,
                                        Memory::Permission::RW);
        if (Failed(r)) return r;

        u8* bss_ptr = memory_.Pointer(bss_addr);
        if (bss_ptr) std::memset(bss_ptr, 0, bss_map_size);

        info.bss_address = bss_addr;
        info.bss_size = header.bss_size;
        LOG_INFO("NSO: BSS at 0x%llx (%llu bytes)", bss_addr, (u64)header.bss_size);
    }

    // ── Entry point ────────────────────────────────────
    // The NSO entry is at the module base.
    // The MOD3/MOD0 header within .text contains additional metadata,
    // but for execution we start at the base (the CRT0-style init).
    info.entry_point = alloc_base;
    info.base_address = alloc_base;
    info.mod_offset = header.mod_offset;

    char hex[65];
    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", header.build_id[i]);
    hex[64] = '\0';
    info.build_id = hex;

    LOG_INFO("NSO: entry = 0x%llx, build_id = %s", info.entry_point, hex);
    return Result::Success;
}

// ═══════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════

Result NsoLoader::LoadFromBuffer(std::span<const u8> buffer,
                                  NsoLoadInfo& info,
                                  u64 base_address) {
    info = {};

    NsoHeader header;
    if (!ParseHeader(buffer, header)) {
        return Result::InvalidArgument;
    }

    return DecompressAndMap(buffer, header, base_address, info);
}

Result NsoLoader::LoadFromFile(const std::string& path,
                                NsoLoadInfo& info,
                                u64 base_address) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return Result::NotFound;

    auto file_size = static_cast<size_t>(file.tellg());
    file.seekg(0);

    std::vector<u8> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()),
              static_cast<std::streamsize>(file_size));
    file.close();

    return LoadFromBuffer(buffer, info, base_address);
}
