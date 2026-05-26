// ═══════════════════════════════════════════════════════════
// Loader Subsystem — NSP/XCI/PFS0 Format Parsing
// ═══════════════════════════════════════════════════════════
//
// Parses Nintendo Switch package formats:
//   - NSP (.nsp) = PFS0 archive containing NCAs
//   - XCI (.xci) = Game card image containing HFS0 + NCAs
//   - PFS0      = Nintendo's packed archive format
//
// Provides access to contained NCA files and their RomFS data.

#include "common/Log.h"
#include "common/Types.h"
#include "memory/Memory.h"
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <memory>
#include <algorithm>

// ═══════════════════════════════════════════════════════════
// PFS0 Archive Parser
// ═══════════════════════════════════════════════════════════
//
// PFS0 format:
//   Header: magic "PFS0" (4B), num_files (4B), string_table_size (4B), padding (4B)
//   File entries (num_files × 24B):
//     offset (16B), size (8B)
//   String table: null-terminated filenames
//   Data: concatenated file data

struct Pfs0Header {
    u32 magic;
    u32 num_files;
    u32 string_table_size;
    u32 padding;
};

struct Pfs0FileEntry {
    u64 offset;  // relative to end of string table
    u64 size;
    u64 padding; // 16-byte aligned entry
};

// Parse a PFS0 archive from buffer
// Returns file count, fills in entries vector
static bool ParsePfs0(const u8* buffer, size_t buffer_size,
                       std::vector<Pfs0FileEntry>& entries,
                       std::vector<std::string>& names) {
    entries.clear();
    names.clear();

    if (buffer_size < sizeof(Pfs0Header)) return false;

    Pfs0Header header;
    std::memcpy(&header, buffer, sizeof(Pfs0Header));

    if (header.magic != 0x30534650) { // "PFS0" in little-endian
        LOG_WARN("PFS0: bad magic 0x%08x", header.magic);
        return false;
    }

    u32 num_files = header.num_files;
    u32 strtab_size = header.string_table_size;

    LOG_INFO("PFS0: %u files, string table %u bytes", num_files, strtab_size);

    // File entries start after header
    const u8* entry_base = buffer + sizeof(Pfs0Header);
    // String table start
    const u8* strtab = entry_base + num_files * 16;  // 16 bytes per entry

    size_t needed = sizeof(Pfs0Header) + num_files * 16 + strtab_size;
    if (buffer_size < needed) return false;

    for (u32 i = 0; i < num_files; i++) {
        Pfs0FileEntry entry;
        std::memcpy(&entry.offset, entry_base + i * 16, 8);
        std::memcpy(&entry.size, entry_base + i * 16 + 8, 8);

        // String table entries: read null-terminated name
        u32 str_off = 0;
        // Find name by scanning from string table start
        for (u32 s = 0; s < i; s++) {
            str_off += (u32)names[s].size() + 1;
        }
        std::string name;
        if (str_off < strtab_size) {
            name = reinterpret_cast<const char*>(strtab + str_off);
        }

        // File offset = end of string table + stored offset
        entry.offset += needed;

        LOG_DEBUG("PFS0[%u]: '%s' offset=0x%llx size=%llu",
                  i, name.c_str(), entry.offset, entry.size);

        entries.push_back(entry);
        names.push_back(name);
    }

    return true;
}

// ═══════════════════════════════════════════════════════════
// NCA Header Parser (minimal)
// ═══════════════════════════════════════════════════════════
//
// NCA format (Nintendo Content Archive):
//   Header (0x200 bytes):
//     0x000: 4B  magic "NCA3" (optional: "NCA2", "NCA4")
//     0x008: 8B  title_id
//     0x010: 4B  sdk_version
//     0x014: 1B  content_type (0=Program, 1=Meta, 2=Control, 3=Manual, 4=Data)
//     0x020: 4B  distribution_type
//     0x030: 8B  size
//     0x200: Section table entries (×4 or ×6)
//     0x340: Section entry: media_offset, media_size, ...
//
// Content types:
//   0 = Program (main executable + RomFS)
//   1 = Meta (update/patch)
//   2 = Control (icon, name, metadata)

enum class NcaContentType : u8 {
    Program = 0,
    Meta    = 1,
    Control = 2,
    Manual  = 3,
    Data    = 4,
};

struct NcaSectionEntry {
    u32 media_offset;     // in media units (512B)
    u32 media_size;       // in media units (512B)
    u8  type;             // section type
};

struct NcaHeader {
    u32 magic;
    u8  _unknown1[4];
    u64 title_id;
    u32 sdk_ver;
    u8  content_type;
    u8  _pad[3];
    u32 crypto_type;
    u8  _unknown2[0x20 - 0x1C];
    u64 size;
    // Sections follow at 0x200
    u8  sections_[0x200];
};

// Parse NCA header and return the first Program/RomFS section offset
static bool ParseNca(const u8* buffer, size_t buffer_size,
                      u64& romfs_offset, u64& romfs_size,
                      u64& exefs_offset, u64& exefs_size,
                      u64& nca_size) {
    romfs_offset = 0;
    romfs_size = 0;
    exefs_offset = 0;
    exefs_size = 0;
    nca_size = 0;

    if (buffer_size < 0x400) {
        LOG_WARN("NCA: buffer too small (%zu)", buffer_size);
        return false;
    }

    u32 magic;
    std::memcpy(&magic, buffer, 4);
    if (magic != 0x3341434E && magic != 0x3241434E) { // "NCA3"/"NCA2"
        LOG_WARN("NCA: bad magic 0x%08x", magic);
        return false;
    }

    u8 content_type;
    std::memcpy(&nca_size, buffer + 0x30, 8);
    std::memcpy(&content_type, buffer + 0x14, 1);

    LOG_INFO("NCA: type=%u size=%llu", (u32)content_type, nca_size);

    // Parse section table (at offset 0x200 in the NCA)
    // Each section entry is 16 bytes
    for (int i = 0; i < 4; i++) {
        const u8* sec = buffer + 0x200 + i * 16;

        u32 sec_offset;
        u32 sec_size;
        u8  sec_type;
        std::memcpy(&sec_offset, sec, 4);
        std::memcpy(&sec_size, sec + 4, 4);
        std::memcpy(&sec_type, sec + 12, 1);

        if (sec_offset == 0 || sec_size == 0) continue;

        u64 abs_offset = (u64)sec_offset * 512; // media units → bytes
        u64 abs_size = (u64)sec_size * 512;

        LOG_DEBUG("NCA section[%d]: type=%u offset=0x%llx size=0x%llx",
                  i, sec_type, abs_offset, abs_size);

        // Section types: 0 = Program (has ExeFS and RomFS)
        if (sec_type == 0 && abs_offset > 0 && abs_size > 0) {
            // The section contains both ExeFS (at start) and RomFS (after ExeFS)
            // ExeFS header is at abs_offset, RomFS follows
            u64 sec_base = abs_offset;

            // ExeFS header (0x200 bytes) at section base
            // RomFS offset at the end of ExeFS
            u64 exefs_header = sec_base;
            u64 exefs_size_from_hdr = 0;
            if (buffer_size > abs_offset + 0x200) {
                // Try to read RomFS offset from the Ivfc header inside ExeFS-ish area
                // For simplicity, estimate RomFS as remaining space after ExeFS
                exefs_size = std::min(abs_size, (u64)0x800000); // 8 MB max
                romfs_offset = sec_base + 0x200;
                romfs_size = abs_size - 0x200;
            }

            if (exefs_size == 0) {
                exefs_size = abs_size;
                romfs_offset = sec_base + abs_size;
                romfs_size = 0;
            }
        }
    }

    LOG_INFO("NCA: ExeFS @ 0x%llx (%llu), RomFS @ 0x%llx (%llu)",
             exefs_offset, exefs_size, romfs_offset, romfs_size);

    return exefs_offset > 0 || romfs_offset > 0;
}

// ═══════════════════════════════════════════════════════════
// XCI Game Card Parser
// ═══════════════════════════════════════════════════════════
//
// XCI format:
//   Header (0x200 bytes):
//     0x000: magic "HEAD"
//     0x100: 4B  rom_size (log2 of size)
//     0x150: 8B  initial_data_offset (HFS0 partition offset)
//     0x158: 8B  initial_data_size
//   Partition data: HFS0 (similar to PFS0) containing NCAs

struct XciHeader {
    u8  magic[4];        // "HEAD"
    u8  _pad[0x100 - 4];
    u32 rom_size_log2;   // at offset 0x100
    u8  _pad2[0x150 - 0x104];
    u64 partition_offset; // at offset 0x150
    u64 partition_size;   // at offset 0x158
};

// ═══════════════════════════════════════════════════════════
// RomFS → FS service bridge
// ═══════════════════════════════════════════════════════════

// Pass RomFS data to the FS service so games can read from it
extern "C" void FsService_SetRomFS(std::span<const u8> data);

// ═══════════════════════════════════════════════════════════
// Main Loader API
// ═══════════════════════════════════════════════════════════

// Detect format and load a game package
// Returns true if RomFS was found and passed to FS service
bool Loader_LoadPackage(const std::string& path, Memory& memory) {
    LOG_INFO("Loader: loading %s", path.c_str());

    // Read entire file into memory (for now; memory-map for larger files later)
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Loader: cannot open %s", path.c_str());
        return false;
    }

    auto file_size = static_cast<size_t>(file.tellg());
    file.seekg(0);

    std::vector<u8> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()),
              static_cast<std::streamsize>(file_size));
    file.close();

    LOG_INFO("Loader: read %zu bytes", buffer.size());

    // ── Detect format by magic bytes ────────────────────
    if (buffer.size() < 4) return false;

    u32 magic;
    std::memcpy(&magic, buffer.data(), 4);

    // NRO format (handled by NroLoader separately)
    if (magic == 0x304F524E) { // "NRO0"
        LOG_INFO("Loader: NRO format detected (use NroLoader directly)");
        return false;
    }

    // PFS0 / NSP format ("PFS0")
    if (magic == 0x30534650) {
        LOG_INFO("Loader: PFS0/NSP format detected");

        std::vector<Pfs0FileEntry> entries;
        std::vector<std::string> names;

        if (!ParsePfs0(buffer.data(), buffer.size(), entries, names)) {
            LOG_ERROR("Loader: PFS0 parse failed");
            return false;
        }

        // Find the first Program NCA (or any .nca file)
        for (size_t i = 0; i < entries.size(); i++) {
            const auto& name = names[i];
            if (name.size() >= 4 &&
                name.substr(name.size() - 4) == ".nca") {
                LOG_INFO("Loader: found NCA: '%s'", name.c_str());

                u64 nca_off = entries[i].offset;
                u64 nca_sz = entries[i].size;

                if (nca_off + nca_sz <= buffer.size()) {
                    u64 romfs_off, romfs_sz;
                    u64 exefs_off, exefs_sz;
                    u64 nca_sz_parsed;

                    if (ParseNca(buffer.data() + nca_off,
                                  (size_t)nca_sz,
                                  romfs_off, romfs_sz,
                                  exefs_off, exefs_sz,
                                  nca_sz_parsed)) {
                        // Pass RomFS data to FS service
                        u64 romfs_abs = nca_off + romfs_off;
                        if (romfs_sz > 0 && romfs_abs + romfs_sz <= buffer.size()) {
                            std::span<const u8> romfs(
                                buffer.data() + romfs_abs,
                                (size_t)romfs_sz);
                            FsService_SetRomFS(romfs);
                            LOG_INFO("Loader: RomFS set (%zu bytes from NSP)",
                                     (size_t)romfs_sz);
                            return true;
                        }
                    }
                }
            }
        }

        LOG_WARN("Loader: no usable NCA found in PFS0");
        return false;
    }

    // XCI format ("HEAD")
    u32 xci_magic;
    std::memcpy(&xci_magic, buffer.data(), 4);
    if (xci_magic == 0x44414548) { // "HEAD"
        LOG_INFO("Loader: XCI format detected");

        if (buffer.size() < 0x200) return false;

        XciHeader hdr;
        std::memcpy(&hdr, buffer.data(), sizeof(XciHeader));

        u64 part_off = hdr.partition_offset;
        u64 part_sz  = hdr.partition_size;

        LOG_INFO("XCI: partition @ 0x%llx (%llu bytes)", part_off, part_sz);

        if (part_off > 0 && part_off + part_sz <= buffer.size()) {
            // The partition contains a HFS0 archive (same structure as PFS0)
            std::vector<Pfs0FileEntry> entries;
            std::vector<std::string> names;

            if (ParsePfs0(buffer.data() + part_off,
                          (size_t)part_sz, entries, names)) {
                // Find main Program NCA
                for (size_t i = 0; i < entries.size(); i++) {
                    const auto& name = names[i];
                    if (name.find(".nca") != std::string::npos) {
                        u64 nca_off = part_off + entries[i].offset;
                        u64 nca_sz = entries[i].size;

                        u64 romfs_off, romfs_sz;
                        u64 exefs_off, exefs_sz;
                        u64 nca_sz_parsed;

                        if (ParseNca(buffer.data() + nca_off,
                                      (size_t)nca_sz,
                                      romfs_off, romfs_sz,
                                      exefs_off, exefs_sz,
                                      nca_sz_parsed)) {
                            u64 romfs_abs = nca_off + romfs_off;
                            if (romfs_sz > 0 && romfs_abs + romfs_sz <= buffer.size()) {
                                std::span<const u8> romfs(
                                    buffer.data() + romfs_abs,
                                    (size_t)romfs_sz);
                                FsService_SetRomFS(romfs);
                                LOG_INFO("Loader: RomFS set (%zu bytes from XCI)",
                                         (size_t)romfs_sz);
                                return true;
                            }
                        }
                    }
                }
            }
        }

        LOG_WARN("Loader: no usable NCA found in XCI");
        return false;
    }

    LOG_WARN("Loader: unknown format (magic=0x%08x)", magic);
    return false;
}

// ═══════════════════════════════════════════════════════════
// ExeFS Parser
// ═══════════════════════════════════════════════════════════
//
// ExeFS is a simple embedded file system within an NCA section.
//   Header (0x200 bytes):
//     - File entries at offset 0x00 (up to 32 × 16 bytes):
//       name[8] + offset(u32) + size(u32)
//     - File data starts at offset 0x200
//
// Standard ExeFS files: "main" (NSO), "main.npdm", "rtld", "sdk"

struct ExeFsEntry {
    char name[9];       // 8-char name + NUL
    u32  offset;        // from start of ExeFS section
    u32  size;
};

// Parse ExeFS and find a file by name
// Returns true and fills `out_offset`/`out_size` if found
static bool FindExeFsEntry(const u8* exefs_data, size_t exefs_size,
                            const char* target_name,
                            u64& out_offset, u64& out_size) {
    out_offset = 0;
    out_size = 0;

    if (!exefs_data || exefs_size < 0x200) {
        LOG_WARN("ExeFS: too small (%zu)", exefs_size);
        return false;
    }

    // Scan file entries (starting at offset 0, each 16 bytes, up to 32)
    for (int i = 0; i < 32; i++) {
        u64 entry_off = (u64)i * 16;
        if (entry_off + 16 > exefs_size) break;

        char name[9] = {};
        std::memcpy(name, exefs_data + entry_off, 8);
        // Trim trailing spaces
        for (int c = 7; c >= 0; c--) {
            if (name[c] == ' ') name[c] = '\0';
        }

        u32 file_off, file_size;
        std::memcpy(&file_off, exefs_data + entry_off + 8, 4);
        std::memcpy(&file_size, exefs_data + entry_off + 12, 4);

        if (name[0] == '\0' || file_size == 0) continue;

        LOG_DEBUG("ExeFS[%d]: '%s' offset=0x%x size=%u", i, name, file_off, file_size);

        if (std::strcmp(name, target_name) == 0) {
            // File offset in ExeFS = file_off (where 0 = start of data after header)
            // The actual data starts at offset 0x200 in the ExeFS section
            u64 abs_offset = 0x200 + file_off;
            if (abs_offset + file_size <= exefs_size) {
                out_offset = abs_offset;
                out_size = file_size;
                LOG_INFO("ExeFS: found '%s' @ 0x%llx (%llu bytes)",
                         target_name, out_offset, out_size);
                return true;
            }
        }
    }

    LOG_WARN("ExeFS: '%s' not found", target_name);
    return false;
}

// ═══════════════════════════════════════════════════════════
// NSO Loading from NSP/XCI (full path)
// ═══════════════════════════════════════════════════════════
//
// Given an NSP or XCI file, finds the Program NCA, extracts
// the ExeFS, locates the "main" NSO, and loads it via NsoLoader.
//
// Returns true on success with load_info filled.

#include "loader/NsoLoader.h"

// NSO base address for guest memory (same as NRO for now)
static constexpr u64 NSO_BASE_ADDRESS = 0x40000000;

// Check if buffer contains an NSO binary at the given offset
static bool IsNSO(const u8* data, size_t size, u64 offset) {
    if (offset + 4 > size) return false;
    u32 magic;
    std::memcpy(&magic, data + offset, 4);
    return magic == 0x304F534E; // "NSO0"
}

// Load executable from NSP/XCI package
// Returns true and fills load_info on success
bool Loader_LoadExecutable(const std::string& path, Memory& memory,
                            NsoLoadInfo& load_info) {
    LOG_INFO("Loader: loading executable from %s", path.c_str());

    // Read entire file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Loader: cannot open %s", path.c_str());
        return false;
    }

    auto file_size = static_cast<size_t>(file.tellg());
    file.seekg(0);

    std::vector<u8> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()),
              static_cast<std::streamsize>(file_size));
    file.close();

    if (buffer.size() < 4) return false;

    u32 magic;
    std::memcpy(&magic, buffer.data(), 4);

    // ── NRO format (direct NSO loading) ────────────────
    if (magic == 0x304F524E) { // "NRO0"
        LOG_INFO("Loader: NRO format — use NroLoader instead");
        return false;
    }

    // ── PFS0 / NSP format ─────────────────────────────
    if (magic == 0x30534650) { // "PFS0"
        LOG_INFO("Loader: PFS0/NSP — extracting NSO...");

        std::vector<Pfs0FileEntry> entries;
        std::vector<std::string> names;

        if (!ParsePfs0(buffer.data(), buffer.size(), entries, names)) {
            LOG_ERROR("Loader: PFS0 parse failed");
            return false;
        }

        // Find Program NCA
        for (size_t i = 0; i < entries.size(); i++) {
            const auto& name = names[i];
            if (name.size() < 4) continue;
            std::string ext = name.substr(name.size() - 4);

            if (ext == ".nca") {
                LOG_INFO("Loader: checking NCA: '%s'", name.c_str());

                u64 nca_off = entries[i].offset;
                u64 nca_sz  = entries[i].size;

                if (nca_off + 0x400 > buffer.size()) continue;

                // Check NCA magic
                u32 nca_magic;
                std::memcpy(&nca_magic, buffer.data() + nca_off, 4);
                if (nca_magic != 0x3341434E && nca_magic != 0x3241434E) continue;

                // Parse NCA sections to find ExeFS
                u64 romfs_off, romfs_sz;
                u64 exefs_off, exefs_sz;
                u64 nca_parsed_size;

                if (!ParseNca(buffer.data() + nca_off,
                              (size_t)std::min(nca_sz, (u64)(buffer.size() - nca_off)),
                              romfs_off, romfs_sz,
                              exefs_off, exefs_sz,
                              nca_parsed_size)) {
                    continue;
                }

                // Set RomFS if found
                if (romfs_sz > 0) {
                    u64 romfs_abs = nca_off + romfs_off;
                    if (romfs_abs + romfs_sz <= buffer.size()) {
                        std::span<const u8> romfs_data(
                            buffer.data() + romfs_abs, (size_t)romfs_sz);
                        FsService_SetRomFS(romfs_data);
                        LOG_INFO("Loader: RomFS set (%zu bytes)", (size_t)romfs_sz);
                    }
                }

                // Look for NSO in ExeFS
                if (exefs_sz > 0) {
                    u64 exefs_abs = nca_off + exefs_off;
                    if (exefs_abs + 0x200 <= buffer.size()) {
                        // Find "main" NSO in ExeFS
                        u64 nso_off, nso_sz;
                        if (FindExeFsEntry(buffer.data() + exefs_abs,
                                            (size_t)exefs_sz,
                                            "main", nso_off, nso_sz)) {
                            u64 nso_abs = exefs_abs + nso_off;

                            if (IsNSO(buffer.data(), buffer.size(), nso_abs)) {
                                LOG_INFO("Loader: found NSO @ 0x%llx (%llu bytes)",
                                         nso_abs, nso_sz);

                                // Load NSO into guest memory
                                NsoLoader nso_loader(memory);
                                std::span<const u8> nso_buf(
                                    buffer.data() + nso_abs, (size_t)nso_sz);
                                Result r = nso_loader.LoadFromBuffer(
                                    nso_buf, load_info, NSO_BASE_ADDRESS);

                                if (!Failed(r)) {
                                    LOG_INFO("Loader: NSO loaded successfully: "
                                             "entry=0x%llx, %zu segments",
                                             load_info.entry_point,
                                             load_info.segments.size());
                                    return true;
                                } else {
                                    LOG_ERROR("Loader: NSO load failed: %u",
                                              (u32)r);
                                }
                            } else {
                                LOG_WARN("Loader: ExeFS 'main' is not NSO format");
                            }
                        }
                    }
                }
            }
        }

        LOG_WARN("Loader: no executable found in NSP");
        return false;
    }

    // ── XCI format ────────────────────────────────────
    u32 xci_check;
    std::memcpy(&xci_check, buffer.data(), 4);
    if (xci_check == 0x44414548) { // "HEAD"
        LOG_INFO("Loader: XCI format detected");

        if (buffer.size() < 0x200) return false;

        u64 part_off, part_sz;
        std::memcpy(&part_off, buffer.data() + 0x150, 8);
        std::memcpy(&part_sz, buffer.data() + 0x158, 8);

        LOG_INFO("XCI: partition @ 0x%llx (%llu bytes)", part_off, part_sz);

        if (part_off + part_sz > buffer.size()) {
            LOG_WARN("XCI: partition extends past file");
            return false;
        }

        // Parse HFS0 partition (same as PFS0)
        std::vector<Pfs0FileEntry> entries;
        std::vector<std::string> names;

        if (!ParsePfs0(buffer.data() + part_off,
                       (size_t)part_sz, entries, names)) {
            LOG_WARN("XCI: partition parse failed");
            return false;
        }

        // Find first Program NCA
        for (size_t i = 0; i < entries.size(); i++) {
            const auto& name = names[i];
            if (name.find(".nca") == std::string::npos) continue;

            u64 nca_off = part_off + entries[i].offset;
            u64 nca_sz  = entries[i].size;

            u64 romfs_off, romfs_sz, exefs_off, exefs_sz, nca_parsed_sz;
            if (!ParseNca(buffer.data() + nca_off, (size_t)nca_sz,
                          romfs_off, romfs_sz,
                          exefs_off, exefs_sz,
                          nca_parsed_sz)) continue;

            // Set RomFS
            if (romfs_sz > 0) {
                u64 romfs_abs = nca_off + romfs_off;
                if (romfs_abs + romfs_sz <= buffer.size()) {
                    std::span<const u8> rf(buffer.data() + romfs_abs, (size_t)romfs_sz);
                    FsService_SetRomFS(rf);
                }
            }

            // Find NSO in ExeFS
            if (exefs_sz > 0) {
                u64 exefs_abs = nca_off + exefs_off;
                u64 nso_off, nso_sz;
                if (FindExeFsEntry(buffer.data() + exefs_abs,
                                    (size_t)exefs_sz,
                                    "main", nso_off, nso_sz)) {
                    u64 nso_abs = exefs_abs + nso_off;
                    if (IsNSO(buffer.data(), buffer.size(), nso_abs)) {
                        NsoLoader nso_loader(memory);
                        std::span<const u8> nso_buf(
                            buffer.data() + nso_abs, (size_t)nso_sz);
                        Result r = nso_loader.LoadFromBuffer(
                            nso_buf, load_info, NSO_BASE_ADDRESS);
                        if (!Failed(r)) {
                            LOG_INFO("Loader: NSO from XCI loaded, entry=0x%llx",
                                     load_info.entry_point);
                            return true;
                        }
                    }
                }
            }
        }

        LOG_WARN("Loader: no executable found in XCI");
        return false;
    }

    // ── NSO0 direct ──────────────────────────────────
    if (magic == 0x304F534E) { // "NSO0"
        LOG_INFO("Loader: direct NSO0 format");
        NsoLoader nso_loader(memory);
        Result r = nso_loader.LoadFromBuffer(buffer, load_info, NSO_BASE_ADDRESS);
        return !Failed(r);
    }

    LOG_WARN("Loader: unknown format (magic=0x%08x)", magic);
    return false;
}
