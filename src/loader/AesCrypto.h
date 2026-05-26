#pragma once

#include "common/Types.h"
#include <vector>
#include <cstddef>

struct AesEcbCtx;
struct AesCtrCtx;

class AesCrypto {
public:
    static bool DecryptEcb(const u8* ciphertext, u8* plaintext, size_t size,
                           const u8* key);

    static bool DecryptCtr(const u8* ciphertext, u8* plaintext, size_t size,
                           const u8* key, const u8* iv, size_t offset);

    static bool DecryptXts(const u8* ciphertext, u8* plaintext, size_t size,
                           const u8* key1, const u8* key2,
                           u64 sector_number, size_t sector_size);

    static void MulGF128(u8* dst, const u8* src);

private:
    static bool AesEcbEncryptBlock(const u8* in, u8* out, const u8* key);
};
