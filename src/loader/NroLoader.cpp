#include "loader/NroLoader.h"
#include "cpu/NativeExec.h"

#include <cstring>
#include <fstream>
#include <unordered_map>

// ── NRO constants ───────────────────────────────────────────
constexpr u32 NRO0_MAGIC   = 0x304F524E;  // "NRO0"
constexpr u32 MOD0_MAGIC   = 0x30444F4D;  // "MOD0"
constexpr u64 NRO_TEXT_BASE = 0x40000000;

// ARM64 relocation types
constexpr u32 R_AARCH64_ABS64     = 257;
constexpr u32 R_AARCH64_GLOB_DAT  = 1025;
constexpr u32 R_AARCH64_RELATIVE  = 1027;

// DT (Dynamic Tag) constants
constexpr s64 DT_NULL     = 0;
constexpr s64 DT_RELA     = 7;
constexpr s64 DT_RELASZ   = 8;
constexpr s64 DT_RELAENT  = 9;
constexpr s64 DT_SYMTAB   = 6;
constexpr s64 DT_STRTAB   = 5;
constexpr s64 DT_STRSZ    = 10;
constexpr s64 DT_INIT     = 12;
constexpr s64 DT_FINI     = 13;
constexpr s64 DT_PLTGOT   = 3;

struct Elf64_Dyn  { s64 tag; u64 val; };
struct Elf64_Rela { u64 off; u64 info; s64 add; };
struct Elf64_Sym  { u32 name; u8 info; u8 other; u16 shndx; u64 val; u64 size; };

// ── Read helpers ───────────────────────────────────────────
static u32 Read32(const u8* buf, size_t sz, u64 off) {
    if (off + 4 > sz) return 0;
    return (u32)buf[off] | ((u32)buf[off+1]<<8) |
           ((u32)buf[off+2]<<16) | ((u32)buf[off+3]<<24);
}

static u64 Read64(const u8* buf, size_t sz, u64 off) {
    if (off + 8 > sz) return 0;
    return (u64)buf[off] | ((u64)buf[off+1]<<8) |
           ((u64)buf[off+2]<<16) | ((u64)buf[off+3]<<24) |
           ((u64)buf[off+4]<<24) | ((u64)buf[off+5]<<40) |
           ((u64)buf[off+6]<<48) | ((u64)buf[off+7]<<56);
}

NroLoader::NroLoader(Memory& memory) : memory_(memory) {}

// ── Parse header ────────────────────────────────────────────
bool NroLoader::ParseHeader(std::span<const u8> buffer,
                            NroPackedHeader& header) {
    if (buffer.size() < sizeof(NroPackedHeader)) return false;

    header_off_ = 0x10;
    if (buffer.size() < 0x10 + sizeof(NroPackedHeader)) return false;
    const u8* d = buffer.data() + header_off_;

    header.magic        = Read32(buffer.data(), buffer.size(), header_off_ + 0);
    header.version      = Read32(buffer.data(), buffer.size(), header_off_ + 4);
    header.size         = Read32(buffer.data(), buffer.size(), header_off_ + 8);
    header.flags        = Read32(buffer.data(), buffer.size(), header_off_ + 12);
    header.text_start   = Read32(buffer.data(), buffer.size(), header_off_ + 16);
    header.text_size    = Read32(buffer.data(), buffer.size(), header_off_ + 20);
    header.rodata_start = Read32(buffer.data(), buffer.size(), header_off_ + 24);
    header.rodata_size  = Read32(buffer.data(), buffer.size(), header_off_ + 28);
    header.data_start   = Read32(buffer.data(), buffer.size(), header_off_ + 32);
    header.data_size    = Read32(buffer.data(), buffer.size(), header_off_ + 36);
    header.bss_size     = Read32(buffer.data(), buffer.size(), header_off_ + 40);
    header.reserved_0   = Read32(buffer.data(), buffer.size(), header_off_ + 44);
    if (header_off_ + 0x30 + 32 <= buffer.size())
        std::memcpy(header.build_id, buffer.data() + header_off_ + 0x30, 32);

    return header.magic == NRO0_MAGIC;
}

// ── Apply relocations ───────────────────────────────────────
// Called after segments are mapped into guest memory.
static void ApplyRelocations(const u8* buf, size_t buf_sz,
                              const Elf64_Rela* relas, u64 nrelas,
                              const Elf64_Sym* symtab, u64 nsyms,
                              const char* strtab,
                              u64 base_addr,  // guest address of .text
                              Memory& mem) {
    LOG_INFO("Applying %llu relocations (base=0x%llx)", (u64)nrelas, base_addr);

    for (u64 ri = 0; ri < nrelas; ri++) {
        u64 type = relas[ri].info & 0xFFFFFFFF;
        u64 sym  = relas[ri].info >> 32;
        u64 off  = relas[ri].off;   // offset from text base
        s64 add  = relas[ri].add;   // addend
        u64 tgt  = base_addr + off; // guest address to patch

        if (off >= buf_sz) {
            LOG_WARN("Reloc %llu: offset 0x%llx exceeds buffer", ri, off);
            continue;
        }

        switch (type) {
        case R_AARCH64_RELATIVE: {
            // B + A: base address + addend
            u64 val = base_addr + add;
            mem.Write(tgt, val);
            break;
        }
        case R_AARCH64_ABS64: {
            // S + A
            if (sym < nsyms) {
                u64 s_val = symtab[sym].val + add;
                mem.Write(tgt, s_val);
            }
            break;
        }
        case R_AARCH64_GLOB_DAT:
            // S + A (same as ABS64 in practice)
            if (sym < nsyms) {
                u64 s_val = symtab[sym].val;
                if (add) s_val += add;
                mem.Write(tgt, s_val);
            }
            break;
        default:
            LOG_WARN("Unsupported reloc type %llu", type);
            break;
        }
    }
    LOG_INFO("Relocations applied");
}

// ── Load from file ──────────────────────────────────────────
Result NroLoader::LoadFromFile(const std::string& path, NroLoadInfo& info) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return Result::NotFound;

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
    if (!ParseHeader(buffer, header)) {
        LOG_ERROR("Bad NRO magic");
        return Result::InvalidArgument;
    }

    // Build ID
    char hex[65];
    for (int i = 0; i < 0x20; i++)
        snprintf(hex + i * 2, 3, "%02x", header.build_id[i]);
    hex[64] = '\0';
    info.build_id = hex;

    u32 text_start   = header_off_ + header.text_start;
    u32 text_size    = header.text_size;
    u32 rodata_start = header_off_ + header.rodata_start;
    u32 rodata_size  = header.rodata_size;
    u32 data_start   = header_off_ + header.data_start;
    u32 data_size    = header.data_size;
    u32 bss_size     = header.bss_size;

    LOG_INFO("NRO: magic=0x%08x size=%u", header.magic, header.size);
    LOG_INFO("  .text:   offset=%u size=%u", text_start, text_size);
    LOG_INFO("  .rodata: offset=%u size=%u", rodata_start, rodata_size);
    LOG_INFO("  .data:   offset=%u size=%u", data_start, data_size);
    LOG_INFO("  .bss:   size=%u", bss_size);

    // ── Search for MOD0 in text segment ─────────────────
    u32 mod0_off = 0;
    for (u32 i = text_start; i + 0x20 <= text_start + text_size && i + 0x20 <= buffer.size(); i += 4) {
        if (Read32(buffer.data(), buffer.size(), i) == MOD0_MAGIC) {
            mod0_off = i;
            break;
        }
    }
    LOG_INFO("MOD0 @ 0x%x", mod0_off);

    // Parse MOD0
    u32 dyn_off = 0;
    if (mod0_off > 0) {
        dyn_off = Read32(buffer.data(), buffer.size(), mod0_off + 4);
        LOG_INFO("MOD0: dyn_off=0x%x (abs=0x%x)", dyn_off, mod0_off + dyn_off);
    }

    // ── Map segments ────────────────────────────────────
    u64 addr = NRO_TEXT_BASE;

    // .text → RW (will switch to RX after patching + relocation)
    {
        u64 sz = AlignUp<u64>(text_size, 0x1000);
        Result r = memory_.MapPhysical(addr, sz, Memory::Permission::RW,
                                        buffer.data() + text_start);
        if (Failed(r)) return r;

        // Patch SVCs and apply relocations while writable
        u8* ptr = memory_.Pointer(addr);
        if (ptr) {
            std::vector<std::pair<u32,u32>> svc_map;
            NativeExec::PatchSVCs(ptr, text_size, svc_map);
            LOG_INFO("Patched %zu SVCs", svc_map.size());
        }

        memory_.Protect(addr, sz, Memory::Permission::RX);
        info.segments.push_back({text_start, text_size, addr, Memory::Permission::RX});
        addr += sz;
    }

    // .rodata → R
    if (rodata_size > 0) {
        u64 sz = AlignUp<u64>(rodata_size, 0x1000);
        Result r = memory_.MapPhysical(addr, sz, Memory::Permission::R,
                                        buffer.data() + rodata_start);
        if (Failed(r)) return r;
        info.segments.push_back({rodata_start, rodata_size, addr, Memory::Permission::R});
        addr += sz;
    }

    // .data → RW
    if (data_size > 0) {
        u64 sz = AlignUp<u64>(data_size, 0x1000);
        Result r = memory_.MapPhysical(addr, sz, Memory::Permission::RW,
                                        buffer.data() + data_start);
        if (Failed(r)) return r;
        info.segments.push_back({data_start, data_size, addr, Memory::Permission::RW});
        addr += sz;
    }

    // .bss → RW
    if (bss_size > 0) {
        u64 sz = AlignUp<u64>(bss_size, 0x1000);
        Result r = memory_.MapPhysical(addr, sz, Memory::Permission::RW);
        if (Failed(r)) return r;
        auto* ptr = memory_.Pointer(addr);
        if (ptr) std::memset(ptr, 0, bss_size);
        info.bss_address = addr;
        info.bss_size = bss_size;
    }

    // ── Parse dynamic section and apply relocations ────
    if (dyn_off > 0) {
        u32 dyn_abs = mod0_off + dyn_off;  // absolute file offset
        if (dyn_abs + 16 <= buffer.size()) {
            // Count DYNS
            int ndyns = 0;
            while (dyn_abs + (ndyns+1)*16 <= buffer.size()) {
                s64 tag = (s64)Read64(buffer.data(), buffer.size(), dyn_abs + ndyns*16);
                if (tag == DT_NULL) break;
                ndyns++;
            }
            LOG_INFO("DT_NULL at dyn[%d], %d entries", ndyns, ndyns);

            // Extract key entries
            u64 rela_off = 0, rela_sz = 0, rela_ent = 0;
            u64 symtab_off = 0, strtab_off = 0, strtab_sz = 0;

            for (int i = 0; i < ndyns; i++) {
                s64 tag = (s64)Read64(buffer.data(), buffer.size(), dyn_abs + i*16);
                u64 val = Read64(buffer.data(), buffer.size(), dyn_abs + i*16 + 8);
                switch (tag) {
                case DT_RELA:    rela_off  = val; break;
                case DT_RELASZ:  rela_sz   = val; break;
                case DT_RELAENT: rela_ent  = val; break;
                case DT_SYMTAB:  symtab_off = val; break;
                case DT_STRTAB:  strtab_off = val; break;
                case DT_STRSZ:   strtab_sz = val; break;
                }
            }

            LOG_INFO("DT: rela=0x%llx relasz=%llu symtab=0x%llx strtab=0x%llx sz=%llu",
                     rela_off, rela_sz, symtab_off, strtab_off, strtab_sz);

            // Apply relocations
            if (rela_off > 0 && rela_sz > 0 && rela_ent >= 24) {
                u64 rela_file_off = text_start + rela_off;
                u64 nrelas = rela_sz / rela_ent;
                u64 sym_file_off = symtab_off ? (text_start + symtab_off) : 0;
                u64 str_file_off = strtab_off ? (text_start + strtab_off) : 0;
                u64 nsyms = sym_file_off ? ((str_file_off - sym_file_off) / 24) : 0;

                LOG_INFO("Applying %llu relocations...", nrelas);

                u64 base_guest = NRO_TEXT_BASE;
                u8* text_ptr = memory_.Pointer(NRO_TEXT_BASE);

                for (u64 ri = 0; ri < nrelas && ri < 100000; ri++) {
                    u64 ro = rela_file_off + ri * rela_ent;
                    if (ro + 24 > buffer.size()) break;

                    u64 r_off  = Read64(buffer.data(), buffer.size(), ro);
                    u64 r_info = Read64(buffer.data(), buffer.size(), ro + 8);
                    s64 r_add  = (s64)Read64(buffer.data(), buffer.size(), ro + 16);
                    u64 type   = r_info & 0xFFFFFFFF;
                    u64 sym    = r_info >> 32;

                    u64 patch_addr = base_guest + r_off;

                    switch (type) {
                    case R_AARCH64_RELATIVE: {
                        u64 val = base_guest + r_add;
                        memory_.Write(patch_addr, val);
                        break;
                    }
                    case R_AARCH64_ABS64:
                    case R_AARCH64_GLOB_DAT: {
                        if (sym_file_off && sym < nsyms) {
                            u64 sym_off = sym_file_off + sym * 24;
                            u64 s_val = Read64(buffer.data(), buffer.size(), sym_off + 8);
                            if (type == R_AARCH64_ABS64) s_val += r_add;
                            memory_.Write(patch_addr, s_val);
                        }
                        break;
                    }
                    }
                }
                LOG_INFO("Applied %llu relocations", nrelas);
            }
        }
    }

    // Entry point
    info.entry_point = NRO_TEXT_BASE + 0x100;
    LOG_INFO("Entry: 0x%llx (%zu segments)", info.entry_point, info.segments.size());
    return Result::Success;
}
