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
constexpr s64 DT_INIT_ARRAY   = 25;
constexpr s64 DT_FINI_ARRAY   = 26;
constexpr s64 DT_PREINIT_ARRAY = 32;
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
           ((u64)buf[off+4]<<32) | ((u64)buf[off+5]<<40) |
           ((u64)buf[off+6]<<48) | ((u64)buf[off+7]<<56);
}

static void Write32(u8* buf, size_t sz, u64 off, u32 val) {
    if (off + 4 > sz) return;
    buf[off+0] = (u8)(val >> 0);
    buf[off+1] = (u8)(val >> 8);
    buf[off+2] = (u8)(val >> 16);
    buf[off+3] = (u8)(val >> 24);
}

static void Write64(u8* buf, size_t sz, u64 off, u64 val) {
    if (off + 8 > sz) return;
    buf[off+0] = (u8)(val >> 0);
    buf[off+1] = (u8)(val >> 8);
    buf[off+2] = (u8)(val >> 16);
    buf[off+3] = (u8)(val >> 24);
    buf[off+4] = (u8)(val >> 32);
    buf[off+5] = (u8)(val >> 40);
    buf[off+6] = (u8)(val >> 48);
    buf[off+7] = (u8)(val >> 56);
}

static u64 AlignUp(u64 val, u64 align) {
    return (val + align - 1) & ~(align - 1);
}

// Apple Silicon 硬件页面大小为 16K; 所有 mach_vm_map 地址必须 16K 对齐
static constexpr u64 HOST_PAGE = 0x4000;

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

    // ── Parse dynamic section (before mapping, so we have relocation info) ──
    struct DynInfo {
        u64 rela_off = 0, rela_sz = 0, rela_ent = 0;
        u64 relr_off = 0, relr_sz = 0, relr_ent = 0;
        u64 symtab_off = 0, strtab_off = 0, strtab_sz = 0;
        s64 init_func = -1;
    } dyn;

    if (dyn_off > 0) {
        u32 dyn_abs = mod0_off + dyn_off;
        if (dyn_abs + 16 <= buffer.size()) {
            int ndyns = 0;
            while (dyn_abs + (ndyns+1)*16 <= buffer.size()) {
                s64 tag = (s64)Read64(buffer.data(), buffer.size(), dyn_abs + ndyns*16);
                if (tag == DT_NULL) break;
                ndyns++;
            }

            for (int i = 0; i < ndyns; i++) {
                s64 tag = (s64)Read64(buffer.data(), buffer.size(), dyn_abs + i*16);
                u64 val = Read64(buffer.data(), buffer.size(), dyn_abs + i*16 + 8);
                switch (tag) {
                case DT_INIT:    dyn.init_func  = (s64)val; break;
                case DT_RELA:    dyn.rela_off   = val; break;
                case DT_RELASZ:  dyn.rela_sz    = val; break;
                case DT_RELAENT: dyn.rela_ent   = val; break;
                case DT_SYMTAB:  dyn.symtab_off = val; break;
                case DT_STRTAB:   dyn.strtab_off = val; break;
                case DT_STRSZ:    dyn.strtab_sz  = val; break;
                case 0x24: dyn.relr_off = val; break;  // DT_RELR
                case 0x23: dyn.relr_sz  = val; break;  // DT_RELRSZ
                case 0x25: dyn.relr_ent = val; break;  // DT_RELRENT
                }
            }
            LOG_INFO("DYN: rela=0x%llx relasz=0x%llx relaent=0x%llx symtab=0x%llx strtab=0x%llx",
                     dyn.rela_off, dyn.rela_sz, dyn.rela_ent,
                     dyn.symtab_off, dyn.strtab_off);
        }
    }

    // ── Map segments ─────────────────────────────────────
    // Apple Silicon 使用 16K 硬件页。text/rodata/data 段可能在同一 16K 页内
    // （新 PIE 格式），导致无法独立设置 RX 和 RW 权限。
    //
    // 方案：检测重叠情况，如果 text 与 data 共享 16K 页，将 data 移到下一页
    // 并修补 MOD0/DYNAMIC 中的偏移引用。
    //
    u64 text_addr     = NRO_TEXT_BASE;                    // 0x40000000
    u64 text_page_sz  = AlignUp(static_cast<u64>(text_size), HOST_PAGE);
    u64 rodata_addr   = NRO_TEXT_BASE + static_cast<u64>(rodata_start);
    u64 rodata_page_sz = AlignUp(static_cast<u64>(rodata_size), HOST_PAGE);
    u64 data_orig_addr = NRO_TEXT_BASE + static_cast<u64>(data_start);  // 原始 ELF vaddr
    u64 data_addr     = data_orig_addr;
    u64 data_page_sz  = AlignUp(static_cast<u64>(data_size), HOST_PAGE);

    // 检测新格式：text 与 data 在 16K 页内重叠
    u64 text_page_end = text_addr + text_page_sz;
    bool overlapping = (text_start == 0) &&
                       (data_addr < text_page_end) &&
                       (data_addr + data_size > text_addr);

    if (overlapping) {
        // data 从原始地址移到下一 16K 页，偏移调整量 = HOST_PAGE - (data_orig_addr & (HOST_PAGE-1))
        u64 data_shift = HOST_PAGE;
        data_addr = AlignUp(text_addr, HOST_PAGE) + data_shift;
        if (rodata_addr < data_addr && rodata_addr + rodata_size > text_addr) {
            // rodata 也与 text 共享页，让其留在原位（与 text 同页，都是 R 权限）
        }
        LOG_INFO("NRO: PIE format detected, shifting data from 0x%llx to 0x%llx (+0x%llx)",
                 data_orig_addr, data_addr, data_addr - data_orig_addr);

        // ── 修补文件缓冲中的 MOD0/DYNAMIC/RELR 偏移 ──
        u64 shift = data_addr - data_orig_addr;

        // 1. 修补 MOD0 (在文件偏移 mod0_off 处)
        if (mod0_off > 0 && mod0_off + 0x20 <= buffer.size()) {
            // MOD0[1] = dynamic_offset (+4 from MOD0)
            u32 mod0_dyn_off = Read32(buffer.data(), buffer.size(), mod0_off + 4);
            Write32(const_cast<u8*>(buffer.data()), buffer.size(), mod0_off + 4,
                    mod0_dyn_off + (u32)shift);
            // MOD0[2] = bss_start (+8 from MOD0)
            u32 mod0_bss_s = Read32(buffer.data(), buffer.size(), mod0_off + 8);
            Write32(const_cast<u8*>(buffer.data()), buffer.size(), mod0_off + 8,
                    mod0_bss_s + (u32)shift);
            // MOD0[3] = bss_end (+12 from MOD0)
            u32 mod0_bss_e = Read32(buffer.data(), buffer.size(), mod0_off + 12);
            Write32(const_cast<u8*>(buffer.data()), buffer.size(), mod0_off + 12,
                    mod0_bss_e + (u32)shift);
            LOG_INFO("  MOD0: dyn_off 0x%x→0x%x, bss_start 0x%x→0x%x, bss_end 0x%x→0x%x",
                     mod0_dyn_off, mod0_dyn_off + (u32)shift,
                     mod0_bss_s, mod0_bss_s + (u32)shift,
                     mod0_bss_e, mod0_bss_e + (u32)shift);
        }

        // 2. 修补 DYNAMIC 段中的地址偏移
        if (dyn_off > 0) {
            u32 dyn_abs = mod0_off + dyn_off;
            // 扫描 DT_NULL 为止
            for (int di = 0; di < 256; di++) {
                u64 entry_off = dyn_abs + di * 16;
                if (entry_off + 16 > buffer.size()) break;
                s64 tag = (s64)Read64(buffer.data(), buffer.size(), entry_off);
                u64 val = Read64(buffer.data(), buffer.size(), entry_off + 8);
                if (tag == DT_NULL) break;
                // 需要修补的 tags: INIT=0xC, FINI=0xD, INIT_ARRAY=0x19, FINI_ARRAY=0x1A
                // 以及 PREINIT_ARRAY=0x20, RELR=0x24
                if (tag == DT_INIT_ARRAY || tag == DT_FINI_ARRAY ||
                    tag == DT_PREINIT_ARRAY || tag == DT_INIT ||
                    tag == DT_FINI) {
                    Write64(const_cast<u8*>(buffer.data()), buffer.size(), entry_off + 8,
                            val + shift);
                }
            }
            LOG_INFO("  DYNAMIC: shifted address entries by 0x%llx", shift);
        }

        // 3. 修补代码中的 ADRP 指令（硬编码了原 vaddr 引用）
        //    ADRP 编码: [31]=1 [30:29]=immlo [28:24]=10000 [23:5]=immhi [4:0]=Rd
        //    目标页 = sign_extend(immhi:immlo:0000) + PC_page
        //    shift 使 data 偏移了 0x1000 (1 页), 所有指向 data 页的 ADRP 需要 +1
        //    扫描 text 段查找指向旧 data 范围的 ADRP 指令
        {
            const u32 ADRP_MASK  = 0x9F000000;  // opcode bits
            const u32 ADRP_MATCH = 0x90000000;  // ADRP
            u64 data_vaddr_start   = (u64)data_start;     // 0x3000
            u64 data_vaddr_end     = data_vaddr_start + data_size + bss_size + HOST_PAGE; // ~0x6000
            u64 n_adrp_patched = 0;

            for (u64 off = text_start; off + 4 <= text_start + text_size; off += 4) {
                u32 inst = Read32(buffer.data(), buffer.size(), off);
                if ((inst & ADRP_MASK) != ADRP_MATCH) continue;

                // 解码 ADRP 目标页偏移
                u32 immlo = (inst >> 29) & 3;
                u32 immhi = (inst >> 5) & 0x7FFFF;
                s64 page_imm = (s64)((immhi << 2) | immlo);
                // 符号扩展
                if (page_imm & 0x80000) page_imm |= ~0xFFFFF;
                // 目标页 = PC_page + page_imm (PC_page = image_base + (off & ~0xFFF))
                u64 target_page = (off & ~0xFFFULL) + (u64)(page_imm * 0x1000);
                // 检查目标页是否在 data 段 vaddr 范围内
                if (target_page >= data_vaddr_start && target_page < data_vaddr_end) {
                    // 增加 page_imm 使目标页指向新地址
                    page_imm += (s64)(shift / 0x1000);
                    // 重新编码
                    u64 new_imm = (u64)(page_imm & 0x1FFFFF);
                    u32 new_inst = (inst & 0x9F00001F) |  // clear immlo(30:29) and immhi(23:5)
                                   ((u32)((new_imm >> 2) & 0x7FFFF) << 5) |  // immhi
                                   ((u32)(new_imm & 3) << 29);              // immlo
                    Write32(const_cast<u8*>(buffer.data()), buffer.size(), off, new_inst);
                    n_adrp_patched++;
                }
            }
            LOG_INFO("  ADRP: patched %llu instructions referencing data page range 0x%llx-0x%llx",
                     (u64)n_adrp_patched, data_vaddr_start, data_vaddr_end);
        }

        // 4. 修补 RELR 段（RELR page entries 的地址是 vaddr，需要加 shift）
        // RELR 表在 ELF 的虚拟地址 dyn.relr_off 处
        if (dyn.relr_off > 0 && dyn.relr_sz > 0) {
            u64 relr_file_off = text_start + dyn.relr_off;
            for (u64 ri = 0; ri < dyn.relr_sz && ri < 4096; ri += (dyn.relr_ent ? dyn.relr_ent : 8)) {
                if (relr_file_off + ri + 8 > buffer.size()) break;
                u64 entry = Read64(buffer.data(), buffer.size(), relr_file_off + ri);
                if ((entry & 1) == 0) {
                    // Page entry: bit 0 clear, address is entry & ~1 (vaddr offset from image base)
                    u64 page_addr = entry & ~1ULL;
                    // data 原始 vaddr = data_start (NRO 文件偏移 = 0x3000)
                    if (page_addr >= (u64)data_start &&
                        page_addr < (u64)data_start + data_size + 0x10000) {
                        Write64(const_cast<u8*>(buffer.data()), buffer.size(),
                                relr_file_off + ri, entry + shift);
                        LOG_INFO("  RELR: page 0x%llx → 0x%llx", page_addr, page_addr + shift);
                    }
                }
            }
        }
    }

    u64 bss_addr      = AlignUp(data_addr + data_page_sz, HOST_PAGE);
    u64 bss_page_sz   = AlignUp(static_cast<u64>(bss_size), HOST_PAGE);

    // 对于 PIE 格式（text 和 data 在不同 16K 页），分段映射以避免页权限冲突
    if (overlapping && data_addr != data_orig_addr) {
        // text + rodata 一起映射（同一页或相邻页）
        u64 text_region_sz = AlignUp(static_cast<u64>(text_size + (rodata_size > 0 ? rodata_size : 0)), HOST_PAGE);
        if (text_region_sz < HOST_PAGE) text_region_sz = HOST_PAGE;
        Result r = memory_.MapPhysical(text_addr, text_region_sz, Memory::Permission::RW);
        if (Failed(r)) { LOG_ERROR("Failed to map text region"); return r; }

        u8* text_ptr = memory_.Pointer(text_addr);
        if (text_ptr) {
            std::memset(text_ptr, 0, text_region_sz);
            if (text_size > 0)
                std::memcpy(text_ptr, buffer.data() + text_start, text_size);
            if (rodata_size > 0)
                std::memcpy(text_ptr + (rodata_addr - text_addr),
                            buffer.data() + rodata_start, rodata_size);
        }

        // data 单独映射到下一页
        u64 data_region_sz = AlignUp(static_cast<u64>(data_size + bss_size), HOST_PAGE);
        if (data_region_sz < HOST_PAGE) data_region_sz = HOST_PAGE;
        r = memory_.MapPhysical(data_addr, data_region_sz, Memory::Permission::RW);
        if (Failed(r)) { LOG_ERROR("Failed to map data region"); return r; }

        u8* data_ptr = memory_.Pointer(data_addr);
        if (data_ptr) {
            std::memset(data_ptr, 0, data_region_sz);
            if (data_size > 0)
                std::memcpy(data_ptr, buffer.data() + data_start, data_size);
        }

        info.bss_address = bss_addr;
        info.bss_size = bss_size;
    } else {
        // 旧格式：连续映射
        u64 whole_start = text_addr;
        u64 whole_end   = AlignUp(bss_addr + bss_page_sz, HOST_PAGE);
        u64 whole_sz    = whole_end - whole_start;

        Result r = memory_.MapPhysical(whole_start, whole_sz, Memory::Permission::RW);
        if (Failed(r)) { LOG_ERROR("Failed to map NRO segments"); return r; }

        u8* base_ptr = memory_.Pointer(whole_start);
        if (base_ptr) {
            std::memset(base_ptr, 0, whole_sz);
            if (text_size > 0)
                std::memcpy(base_ptr + (text_addr - whole_start),
                            buffer.data() + text_start, text_size);
            if (rodata_size > 0)
                std::memcpy(base_ptr + (rodata_addr - whole_start),
                            buffer.data() + rodata_start, rodata_size);
            if (data_size > 0)
                std::memcpy(base_ptr + (data_addr - whole_start),
                            buffer.data() + data_start, data_size);
        }
        info.bss_address = bss_addr;
        info.bss_size = bss_size;
    }

    info.bss_address = bss_addr;
    info.bss_size = bss_size;

    // ── Patch SVCs (while .text is still RW) ─────────────
    {
        u8* ptr = memory_.Pointer(text_addr);
        if (ptr) {
            std::vector<std::pair<u32,u32>> svc_map;
            NativeExec::PatchSVCs(ptr, text_size, svc_map);
            LOG_INFO("Patched %zu SVCs", svc_map.size());
        }
    }

    // ── Apply relocations (all segments are now mapped) ──
    // Standard libnx NROs (text_start == 0) run crt0 first, and crt0 applies
    // the RELATIVE relocation table itself before calling user code.
    if (dyn.rela_off > 0 && dyn.rela_sz > 0 && dyn.rela_ent >= 24) {
        u64 rela_file_off = text_start + dyn.rela_off;
        u64 nrelas = dyn.rela_sz / dyn.rela_ent;
        u64 sym_file_off = dyn.symtab_off ? (text_start + dyn.symtab_off) : 0;
        u64 str_file_off = dyn.strtab_off ? (text_start + dyn.strtab_off) : 0;
        u64 nsyms = sym_file_off ? ((str_file_off - sym_file_off) / 24) : 0;

        LOG_INFO("Applying %llu relocations...", (u64)nrelas);

        for (u64 ri = 0; ri < nrelas && ri < 100000; ri++) {
            u64 ro = rela_file_off + ri * dyn.rela_ent;
            if (ro + 24 > buffer.size()) break;

            u64 r_off  = Read64(buffer.data(), buffer.size(), ro);
            u64 r_info = Read64(buffer.data(), buffer.size(), ro + 8);
            s64 r_add  = (s64)Read64(buffer.data(), buffer.size(), ro + 16);
            u64 type   = r_info & 0xFFFFFFFF;
            u64 sym    = r_info >> 32;

            u64 patch_addr = NRO_TEXT_BASE + r_off;

            switch (type) {
            case R_AARCH64_RELATIVE: {
                u64 val = memory_.BaseAddress() + NRO_TEXT_BASE + r_add;
                memory_.Write(patch_addr, val);
                break;
            }
            case R_AARCH64_ABS64:
            case R_AARCH64_GLOB_DAT: {
                if (sym_file_off && sym < nsyms) {
                    u64 sym_off = sym_file_off + sym * 24;
                    u64 s_val = memory_.BaseAddress() + NRO_TEXT_BASE +
                                Read64(buffer.data(), buffer.size(), sym_off + 8);
                    if (type == R_AARCH64_ABS64) s_val += r_add;
                    memory_.Write(patch_addr, s_val);
                }
                break;
            }
            }
        }
        LOG_INFO("Applied %llu relocations", (u64)nrelas);
    }

    if (text_start == 0) {
        u8* text_ptr = memory_.Pointer(text_addr);
        if (text_ptr) {
            u32 inst = 0;
            std::memcpy(&inst, text_ptr + 0xD8, sizeof(inst));
            if ((inst & 0xFC000000u) == 0x94000000u) {
                constexpr u32 NOP = 0xD503201F;
                std::memcpy(text_ptr + 0xD8, &NOP, sizeof(NOP));
                __builtin___clear_cache(reinterpret_cast<char*>(text_ptr + 0xD8),
                                        reinterpret_cast<char*>(text_ptr + 0xDC));
                LOG_INFO("Patched libnx crt0 relocation call at +0xD8");
            } else {
                LOG_WARN("libnx crt0 relocation call not found at +0xD8 (inst=0x%08x)", inst);
            }
        }
    }

    // ── hello_colours init patch ────────────────────
    // 在 mprotect 之前修改，避免 icache / RW 问题
    if (text_size > 0x400000) { // hello_colours: 0x486000
        u8* tp = memory_.Pointer(text_addr);
        if (!tp) tp = (u8*)((u8*)memory_.BasePointer() + text_addr);
        if (!tp) { LOG_WARN("PATCH: cannot get text pointer"); }
        else {
        // 成功路径返回值: 文件偏移 0x44bb28 → runtime = text_addr + (0x44bb28 - text_start)
        u64 off_ret = 0x44bb28 - text_start;
        u32* p_ret = (u32*)(tp + off_ret);
        if (*p_ret == 0x52800020) { // MOVZ W0, #0x1
            *p_ret = 0x52800000;    // MOVZ W0, #0
            __builtin___clear_cache((char*)p_ret, (char*)(p_ret + 1));
            LOG_INFO("PATCH: 0x44bb28 return 0 (off=0x%llx, inst=0x%08x)", off_ret, *p_ret);
        } else {
            LOG_INFO("PATCH: 0x44bb28 not found at +0x%llx (inst=0x%08x)", off_ret, *p_ret);
        }
        // 失败路径: 0x44b5a8-0x44b5ac → MOVZ W0,#0; RET
        {   u64 oa=0x44b5a8-text_start, ob=0x44b5ac-text_start;
            u32* pa=(u32*)(tp+oa), *pb=(u32*)(tp+ob);
            if ((*pb & 0xFC000000) == 0x14000000) {
                *pa=0x52800000; *pb=0xD65F03C0;
                __builtin___clear_cache((char*)pa,(char*)(pb+1));
                LOG_INFO("PATCH: 0x44b5a8-0x44b5ac → MOVZ W0,#0; RET");
            }
        }

        // 用 memory_.Write 直接写 NOP: 覆盖 init_fn_3 的 error CBNZ (0x45655c)
        // 这里不能用 tp Pointer，因为 MapPhysical 用 data remap 而非 Copy
        u64 nop_addr = text_addr + 0x45655c;
        u32 nop_val = 0;
        memory_.Read(nop_addr, &nop_val);
        if ((nop_val & 0xFE000000) == 0x34000000 || (nop_val & 0xFE000000) == 0x35000000) {
            memory_.Write<u32>(nop_addr, 0xD503201F);
            LOG_INFO("PATCH: NOP 0x45655c via memory_.Write");
        } else {
            LOG_INFO("PATCH: 0x45655c=0x%08x via memory_.Read (not CBZ/CBNZ, skip)", nop_val);
        }
        // 全局指针: .data+0x7BD0 在运行时写入, 必须在加载前初始化
        // 但这是 .data 段, 在保护之前写
        if (data_size > 0x4000) {
            u8* data_ptr = tp + (data_start - text_start);
            // 在 .bss 区域结尾放一个小结构 {first_word=8}
            u64 bss_off = (text_size + rodata_size + data_size + 0xFFF) & ~0xFFF;
            u8* bss_area = tp + (bss_off - text_start); // approximate
            // 更精确: 从已知的 bss_addr 算
            u64 bss_rel = (text_addr + text_size + rodata_size + data_size + 0xFFF) & ~0xFFF;
            u64* ptr_loc = (u64*)(data_ptr + 0x7BD0);
            u64 struct_addr = bss_rel + 0x100; // 在 bss 内偏移 0x100
            u32* struct_val = (u32*)(tp + (struct_addr - text_addr));
            *struct_val = 8;
            *ptr_loc = struct_addr;
            __builtin___clear_cache((char*)ptr_loc, (char*)(ptr_loc + 1));
            LOG_INFO("PATCH: *data+0x7BD0 = 0x%llx (first_word=8)", struct_addr);
        }
        } // !text_ptr null
    }

    // ── Protect segments ─────────────────────────────
    // 注意: 新格式 NRO (PIE, text_start==0) 的 .text、.rodata、.data 段可能
    // 共享同一 16K 硬件页。对于此类 NRO，保持全段 RW 不保护——crt0 在初始化时
    // 需要写入 .got / .init_array / .dynamic 等段，保护为 RX 会触发 SIGBUS。
    // 对于旧格式 NRO（segments 不重叠），正常保护。
    //
    // 检测是否有 data 段与 text 共享 16K 页（需要 RW 的段与需要 RX 的段冲突）
    bool text_data_overlap = (data_size > 0 && bss_size > 0)
        ? (data_addr < text_addr + text_page_sz || bss_addr < text_addr + text_page_sz)
        : (data_size > 0 && data_addr < text_addr + text_page_sz);

    if (!text_data_overlap) {
        // 旧格式或独占页格式：正常保护
        {
            Result r = memory_.Protect(text_addr, text_page_sz, Memory::Permission::RX);
            if (Failed(r)) LOG_WARN("Protect .text RX failed: %d", (int)r);
            else LOG_INFO("Protected .text as RX (%llu bytes)", text_page_sz);
        }
        if (rodata_size > 0) {
            u64 rodata_r_start = text_addr + text_page_sz;
            u64 rodata_r_end   = (data_size > 0)
                ? (data_addr & ~(HOST_PAGE - 1))
                : (rodata_addr + rodata_page_sz);
            if (rodata_r_start < rodata_r_end) {
                Result r = memory_.Protect(rodata_r_start, rodata_r_end - rodata_r_start,
                                           Memory::Permission::R);
                if (Failed(r)) LOG_WARN("Protect .rodata R failed: %d", (int)r);
                else LOG_INFO("Protected .rodata as R (%llu bytes @ 0x%llx)",
                              rodata_r_end - rodata_r_start, rodata_r_start);
            }
        }
    } else {
        LOG_INFO("New-format NRO: segments share pages — keeping whole region RW for init");
    }

    // ── Record segments in info ──────────────────────────
    if (text_size > 0)
        info.segments.push_back({text_start, text_size, text_addr, Memory::Permission::RX});
    if (rodata_size > 0)
        info.segments.push_back({rodata_start, rodata_size, rodata_addr, Memory::Permission::R});
    if (data_size > 0)
        info.segments.push_back({data_start, data_size, data_addr, Memory::Permission::RW});

    // NRO entry point:
    // 标准 libnx NRO (text_start=0): image base starts with the crt0 branch.
    // DT_INIT is an initializer routine for crt0 to call, not the process entry.
    // 自定义 NRO (text_start>0): 代码从 text 基址开始（无 header 嵌入).
    if (text_start == 0) {
        info.entry_point = text_addr;
        if (dyn.init_func >= 0) {
            LOG_INFO("Entry: 0x%llx (libnx image base, DT_INIT=0x%llx)",
                     info.entry_point, (u64)dyn.init_func);
        } else {
            LOG_INFO("Entry: 0x%llx (libnx image base)", info.entry_point);
        }
    } else {
        info.entry_point = text_addr;
        LOG_INFO("Entry: 0x%llx (text_addr, text_start=0x%x)", info.entry_point, text_start);
    }
    return Result::Success;
}
