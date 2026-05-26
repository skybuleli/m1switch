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
           ((u64)buf[off+4]<<32) | ((u64)buf[off+5]<<40) |
           ((u64)buf[off+6]<<48) | ((u64)buf[off+7]<<56);
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
                case DT_SYMTAB:  dyn.symtab_off = val; break;                case DT_STRTAB:   dyn.strtab_off = val; break;
                case DT_STRSZ:    dyn.strtab_sz  = val; break;
                }
            }
            LOG_INFO("DYN: rela=0x%llx relasz=0x%llx relaent=0x%llx symtab=0x%llx strtab=0x%llx",
                     dyn.rela_off, dyn.rela_sz, dyn.rela_ent,
                     dyn.symtab_off, dyn.strtab_off);
        }
    }

    // ── Map segments (all before relocations) ─────────────
    // 重要: Apple Silicon 使用 16K 硬件页。
    // 段地址必须与原始 NRO 布局一致（PC-relative ADRP 引用依赖段间相对位置不变）。
    // 但 mach_vm_map 要求地址 16K 对齐，所以将映射大小上取整到 16K，
    // 并允许后续映射与前一映射的末尾 16K 填充区域重叠（使用 VM_FLAGS_OVERWRITE）。
    u64 text_addr     = NRO_TEXT_BASE;
    u64 text_page_sz  = AlignUp(static_cast<u64>(text_size), HOST_PAGE);
    u64 rodata_addr   = NRO_TEXT_BASE + static_cast<u64>(rodata_start);
    u64 rodata_page_sz = AlignUp(static_cast<u64>(rodata_size), HOST_PAGE);
    u64 data_addr     = NRO_TEXT_BASE + static_cast<u64>(data_start);
    u64 data_page_sz  = AlignUp(static_cast<u64>(data_size), HOST_PAGE);
    u64 bss_addr      = AlignUp(data_addr + data_page_sz, HOST_PAGE);
    u64 bss_page_sz   = AlignUp(static_cast<u64>(bss_size), HOST_PAGE);

    // 1. .text → RW (will switch to RX after patching)
    {
        Result r = memory_.MapPhysical(text_addr, text_page_sz, Memory::Permission::RW,
                                        buffer.data() + text_start);
        if (Failed(r)) return r;
    }

    // 2. .rodata → RW (will switch to R after relocations)
    if (rodata_size > 0) {
        Result r = memory_.MapPhysical(rodata_addr, rodata_page_sz, Memory::Permission::RW,
                                        buffer.data() + rodata_start);
        if (Failed(r)) return r;
    }

    // 3. .data → RW
    if (data_size > 0) {
        Result r = memory_.MapPhysical(data_addr, data_page_sz, Memory::Permission::RW,
                                        buffer.data() + data_start);
        if (Failed(r)) return r;
    }

    // 4. .bss → RW (zero-filled)
    if (bss_size > 0) {
        Result r = memory_.MapPhysical(bss_addr, bss_page_sz, Memory::Permission::RW);
        if (Failed(r)) return r;
        auto* ptr = memory_.Pointer(bss_addr);
        if (ptr) std::memset(ptr, 0, bss_size);
        info.bss_address = bss_addr;
        info.bss_size = bss_size;
    }

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
    // .text → RX (no more writes)
    {
        Result r = memory_.Protect(text_addr, text_page_sz, Memory::Permission::RX);
        if (Failed(r)) LOG_WARN("Protect .text RX failed: %d", (int)r);
        else LOG_INFO("Protected .text as RX (%llu bytes)", text_page_sz);
    }
    // .rodata → R (no more writes after relocations)
    if (rodata_size > 0) {
        Result r = memory_.Protect(rodata_addr, rodata_page_sz, Memory::Permission::R);
        if (Failed(r)) LOG_WARN("Protect .rodata R failed: %d", (int)r);
        else LOG_INFO("Protected .rodata as R (%llu bytes)", rodata_page_sz);
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
