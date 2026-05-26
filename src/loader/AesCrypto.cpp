#include "loader/AesCrypto.h"
#include "common/Log.h"
#include <CommonCrypto/CommonCryptor.h>
#include <cstring>
#include <algorithm>

bool AesCrypto::AesEcbEncryptBlock(const u8* in, u8* out, const u8* key) {
    size_t moved = 0;
    CCCryptorStatus st = CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionECBMode,
                                  key, kCCKeySizeAES128, nullptr,
                                  in, 16, out, 16, &moved);
    return st == kCCSuccess && moved == 16;
}

bool AesCrypto::DecryptEcb(const u8* ciphertext, u8* plaintext, size_t size,
                            const u8* key) {
    if (size % 16 != 0) return false;

    size_t moved = 0;
    CCCryptorStatus st = CCCrypt(kCCDecrypt, kCCAlgorithmAES, kCCOptionECBMode,
                                  key, kCCKeySizeAES128, nullptr,
                                  ciphertext, size, plaintext, size, &moved);
    return st == kCCSuccess && moved == size;
}

bool AesCrypto::DecryptCtr(const u8* ciphertext, u8* plaintext, size_t size,
                            const u8* key, const u8* iv, size_t offset) {
    u8 counter[16];
    std::memcpy(counter, iv, 16);

    u64 block_offset = offset / 16;
    size_t byte_offset = offset % 16;

    for (int i = 0; i < 8 && block_offset > 0; i++) {
        u8 carry = 0;
        int idx = 15 - i;
        u16 sum = counter[idx] + (block_offset & 0xFF) + carry;
        counter[idx] = (u8)sum;
        carry = (u8)(sum >> 8);
        block_offset >>= 8;
    }

    std::vector<u8> keystream(16);
    size_t processed = 0;

    if (byte_offset > 0) {
        size_t first_block = std::min(size, 16 - byte_offset);
        if (!AesEcbEncryptBlock(counter, keystream.data(), key)) return false;
        for (size_t i = 0; i < first_block; i++) {
            plaintext[i] = ciphertext[i] ^ keystream[byte_offset + i];
        }
        processed = first_block;

        for (int i = 15; i >= 0; i--) {
            if (++counter[i] != 0) break;
        }
    }

    while (processed < size) {
        if (!AesEcbEncryptBlock(counter, keystream.data(), key)) return false;

        size_t remaining = std::min(size - processed, (size_t)16);
        for (size_t i = 0; i < remaining; i++) {
            plaintext[processed + i] = ciphertext[processed + i] ^ keystream[i];
        }
        processed += remaining;

        for (int i = 15; i >= 0; i--) {
            if (++counter[i] != 0) break;
        }
    }

    return true;
}

void AesCrypto::MulGF128(u8* dst, const u8* src) {
    u8 carry = 0;
    u8 tmp[16];
    std::memcpy(tmp, src, 16);

    for (int i = 0; i < 16; i++) {
        u8 byte = dst[15 - i];
        u8 new_carry = (tmp[15] & 0x80) ? 1 : 0;

        for (int j = 15; j >= 0; j--) {
            u8 next = (u8)(tmp[j] << 1);
            if (j < 15 && (tmp[j + 1] & 0x80)) next |= 1;
            tmp[j] = next;
        }

        if (carry) {
            u8 prev = 0;
            for (int j = 0; j < 16; j++) {
                u8 next = dst[j];
                dst[j] = prev ^ tmp[j];
                prev = next;
            }
        } else {
            std::memcpy(dst, tmp, 16);
        }

        carry = new_carry;

        if (carry) {
            for (int j = 0; j < 16; j++) {
                u8 next = dst[j];
                dst[j] = tmp[j];
                tmp[j] = next;
            }
        }
    }

    if (carry) {
        dst[15] ^= 0x87;
    }
}

bool AesCrypto::DecryptXts(const u8* ciphertext, u8* plaintext, size_t size,
                            const u8* key1, const u8* key2,
                            u64 sector_number, size_t sector_size) {
    if (size % 16 != 0 || sector_size % 16 != 0) return false;

    u64 num_sectors = size / sector_size;
    if (size % sector_size != 0) num_sectors++;

    size_t processed = 0;
    for (u64 sec = 0; sec < num_sectors; sec++) {
        size_t sec_offset = sec * sector_size;
        size_t sec_size = std::min(sector_size, size - sec_offset);

        u8 tweak[16] = {};
        tweak[0] = (u8)(sector_number & 0xFF);
        tweak[1] = (u8)((sector_number >> 8) & 0xFF);
        tweak[2] = (u8)((sector_number >> 16) & 0xFF);
        tweak[3] = (u8)((sector_number >> 24) & 0xFF);
        tweak[4] = (u8)((sector_number >> 32) & 0xFF);
        tweak[5] = (u8)((sector_number >> 40) & 0xFF);
        tweak[6] = (u8)((sector_number >> 48) & 0xFF);
        tweak[7] = (u8)((sector_number >> 56) & 0xFF);

        if (!AesEcbEncryptBlock(tweak, tweak, key2)) return false;

        for (size_t blk = 0; blk < sec_size; blk += 16) {
            u8 tmp[16];
            for (int i = 0; i < 16; i++)
                tmp[i] = ciphertext[sec_offset + blk + i] ^ tweak[i];

            if (!AesEcbEncryptBlock(tmp, tmp, key1)) return false;

            for (int i = 0; i < 16; i++)
                plaintext[sec_offset + blk + i] = tmp[i] ^ tweak[i];

            MulGF128(tweak, tweak);
        }

        sector_number++;
        processed += sec_size;
    }

    return true;
}
