/*
 * ct_crypto.cc
 *
 * Copyright 2009-2025
 * Giuseppe Penone <giuspen@gmail.com>
 * Evgenii Gurianov <https://github.com/txe>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "ct_crypto.h"

#include <cstring>
#include <cstdint>
#include <mutex>
#include <vector>

extern "C" {
#include "Aes.h"
#include "Sha256.h"
}

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <cstdio>
#endif

namespace CtCrypto {

namespace {

constexpr size_t HMAC_BLOCK_BYTES = 64u; // SHA-256 compression block

const Byte* as_bytes(const std::string& str)
{
    return reinterpret_cast<const Byte*>(str.data());
}

// AesGenTables must run once before any other AES call
void aes_tables_init_once()
{
    static std::once_flag s_onceFlag;
    std::call_once(s_onceFlag, []{ AesGenTables(); });
}

// The 7-Zip CBC state is a single UInt32 array holding iv + keyMode + roundKeys
// and it must be 16 byte aligned, hence the spare words and the manual offset;
// this mirrors NCrypto::CAesCbcCoder in src/7za/CPP/7zip/Crypto/MyAes.cpp
class AesCbcState
{
public:
    AesCbcState(const std::string& key, const std::string& iv, const bool encrypt)
    {
        aes_tables_init_once();
        const uintptr_t addr = reinterpret_cast<uintptr_t>(_aes);
        _offset = static_cast<unsigned>(((0u - addr) & 0xFu) / sizeof(UInt32));
        const AES_SET_KEY_FUNC setKeyFunc = encrypt ? Aes_SetKey_Enc : Aes_SetKey_Dec;
        setKeyFunc(_aes + _offset + 4, as_bytes(key), static_cast<unsigned>(key.size()));
        AesCbc_Init(_aes + _offset, as_bytes(iv));
        _codeFunc = encrypt ? g_AesCbc_Encode : g_AesCbc_Decode;
    }
    ~AesCbcState() { memset(_aes, 0, sizeof(_aes)); }

    AesCbcState(const AesCbcState&) = delete;
    AesCbcState& operator=(const AesCbcState&) = delete;

    void code_in_place(Byte* pData, const size_t numBlocks) { _codeFunc(_aes + _offset, pData, numBlocks); }

private:
    UInt32 _aes[AES_NUM_IVMRK_WORDS + 3];
    unsigned _offset{0u};
    AES_CODE_FUNC _codeFunc{nullptr};
};

// The AES code functions want a 16 byte aligned data pointer, which neither
// std::string nor std::vector guarantees, so work in an over allocated buffer
class AlignedBuffer
{
public:
    explicit AlignedBuffer(const size_t numBytes) : _storage(numBytes + BLOCK_BYTES, 0)
    {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(_storage.data());
        _pAligned = _storage.data() + ((0u - addr) & (BLOCK_BYTES - 1u));
    }
    ~AlignedBuffer() { memset(_storage.data(), 0, _storage.size()); }

    Byte* data() { return _pAligned; }

private:
    std::vector<Byte> _storage;
    Byte* _pAligned{nullptr};
};

// HMAC with the key schedule computed once. PBKDF2 calls the mac millions of
// times with the same key, so folding the ipad/opad blocks into a reusable
// SHA-256 state removes two block compressions and three allocations per call.
class HmacSha256Key
{
public:
    explicit HmacSha256Key(const std::string& key)
    {
        std::string paddedKey = key.size() > HMAC_BLOCK_BYTES ? sha256(key) : key;
        paddedKey.resize(HMAC_BLOCK_BYTES, '\0');

        Byte innerPad[HMAC_BLOCK_BYTES];
        Byte outerPad[HMAC_BLOCK_BYTES];
        for (size_t i = 0; i < HMAC_BLOCK_BYTES; ++i) {
            const Byte keyByte = static_cast<Byte>(paddedKey[i]);
            innerPad[i] = static_cast<Byte>(keyByte ^ 0x36u);
            outerPad[i] = static_cast<Byte>(keyByte ^ 0x5Cu);
        }
        wipe(paddedKey);

        Sha256_Init(&_innerState);
        Sha256_Update(&_innerState, innerPad, HMAC_BLOCK_BYTES);
        Sha256_Init(&_outerState);
        Sha256_Update(&_outerState, outerPad, HMAC_BLOCK_BYTES);

        memset(innerPad, 0, sizeof(innerPad));
        memset(outerPad, 0, sizeof(outerPad));
    }
    ~HmacSha256Key()
    {
        memset(&_innerState, 0, sizeof(_innerState));
        memset(&_outerState, 0, sizeof(_outerState));
    }

    HmacSha256Key(const HmacSha256Key&) = delete;
    HmacSha256Key& operator=(const HmacSha256Key&) = delete;

    void mac_into(const Byte* pData, const size_t numBytes, Byte* pDigestOut) const
    {
        CSha256 sha = _innerState;
        Sha256_Update(&sha, pData, numBytes);
        Byte innerDigest[SHA256_BYTES];
        Sha256_Final(&sha, innerDigest);

        sha = _outerState;
        Sha256_Update(&sha, innerDigest, SHA256_BYTES);
        Sha256_Final(&sha, pDigestOut);
        memset(innerDigest, 0, sizeof(innerDigest));
    }

private:
    CSha256 _innerState;
    CSha256 _outerState;
};

void append_uint32_be(std::string& rStr, const uint32_t value)
{
    rStr.push_back(static_cast<char>((value >> 24) & 0xFF));
    rStr.push_back(static_cast<char>((value >> 16) & 0xFF));
    rStr.push_back(static_cast<char>((value >> 8) & 0xFF));
    rStr.push_back(static_cast<char>(value & 0xFF));
}

// Comparison that does not leak where the first difference is
bool equal_constant_time(const std::string& lhs, const std::string& rhs)
{
    if (lhs.size() != rhs.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < lhs.size(); ++i) {
        diff = static_cast<unsigned char>(diff | (static_cast<unsigned char>(lhs[i]) ^ static_cast<unsigned char>(rhs[i])));
    }
    return 0 == diff;
}

// The bytes that the mac authenticates; every field is fixed size or last,
// so the concatenation cannot be ambiguous
std::string mac_input(const CtEncryptedEnvelope& envelope)
{
    std::string macInput;
    macInput.reserve(8u + envelope.kdfSalt.size() + envelope.iv.size() + envelope.payload.size());
    append_uint32_be(macInput, static_cast<uint32_t>(envelope.version));
    append_uint32_be(macInput, envelope.kdfIterations);
    macInput += envelope.kdfSalt;
    macInput += envelope.iv;
    macInput += envelope.payload;
    return macInput;
}

} // anonymous namespace

std::string sha256(const std::string& data)
{
    CSha256 sha;
    Sha256_Init(&sha);
    Sha256_Update(&sha, as_bytes(data), data.size());
    std::string digest(SHA256_BYTES, '\0');
    Sha256_Final(&sha, reinterpret_cast<Byte*>(&digest[0]));
    return digest;
}

std::string hmac_sha256(const std::string& key, const std::string& data)
{
    const HmacSha256Key hmacKey{key};
    std::string digest(SHA256_BYTES, '\0');
    hmacKey.mac_into(as_bytes(data), data.size(), reinterpret_cast<Byte*>(&digest[0]));
    return digest;
}

std::string pbkdf2_hmac_sha256(const std::string& password,
                               const std::string& salt,
                               const unsigned iterations,
                               const size_t outLen)
{
    if (0u == iterations or 0u == outLen) return std::string{};

    const HmacSha256Key hmacKey{password};
    std::string derived;
    derived.reserve(outLen);

    Byte blockU[SHA256_BYTES];
    Byte blockT[SHA256_BYTES];
    uint32_t blockIndex = 1u;
    while (derived.size() < outLen) {
        std::string saltAndIndex = salt;
        append_uint32_be(saltAndIndex, blockIndex);
        hmacKey.mac_into(as_bytes(saltAndIndex), saltAndIndex.size(), blockU);
        memcpy(blockT, blockU, SHA256_BYTES);

        for (unsigned iteration = 1u; iteration < iterations; ++iteration) {
            // in place is safe: mac_into consumes its input before writing the digest
            hmacKey.mac_into(blockU, SHA256_BYTES, blockU);
            for (size_t i = 0; i < SHA256_BYTES; ++i) blockT[i] = static_cast<Byte>(blockT[i] ^ blockU[i]);
        }
        derived.append(reinterpret_cast<const char*>(blockT), SHA256_BYTES);
        ++blockIndex;
    }
    memset(blockU, 0, sizeof(blockU));
    memset(blockT, 0, sizeof(blockT));

    derived.resize(outLen);
    return derived;
}

bool random_bytes(const size_t num, std::string& rOut)
{
    rOut.assign(num, '\0');
    if (0u == num) return true;
#ifdef _WIN32
    const NTSTATUS status = BCryptGenRandom(nullptr,
                                            reinterpret_cast<PUCHAR>(&rOut[0]),
                                            static_cast<ULONG>(num),
                                            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return 0 == status;
#else
    FILE* pFile = fopen("/dev/urandom", "rb");
    if (not pFile) return false;
    const size_t numRead = fread(&rOut[0], 1u, num, pFile);
    fclose(pFile);
    return numRead == num;
#endif
}

bool aes256_cbc_encrypt(const std::string& key, const std::string& iv,
                        const std::string& plain, std::string& rCipher)
{
    if (KEY_BYTES != key.size() or IV_BYTES != iv.size()) return false;
    if (0u != plain.size() % BLOCK_BYTES) return false;
    if (plain.empty()) { rCipher.clear(); return true; }

    AlignedBuffer buffer{plain.size()};
    memcpy(buffer.data(), plain.data(), plain.size());
    AesCbcState state{key, iv, true/*encrypt*/};
    state.code_in_place(buffer.data(), plain.size() / BLOCK_BYTES);
    rCipher.assign(reinterpret_cast<const char*>(buffer.data()), plain.size());
    return true;
}

bool aes256_cbc_decrypt(const std::string& key, const std::string& iv,
                        const std::string& cipher, std::string& rPlain)
{
    if (KEY_BYTES != key.size() or IV_BYTES != iv.size()) return false;
    if (0u != cipher.size() % BLOCK_BYTES) return false;
    if (cipher.empty()) { rPlain.clear(); return true; }

    AlignedBuffer buffer{cipher.size()};
    memcpy(buffer.data(), cipher.data(), cipher.size());
    AesCbcState state{key, iv, false/*decrypt*/};
    state.code_in_place(buffer.data(), cipher.size() / BLOCK_BYTES);
    rPlain.assign(reinterpret_cast<const char*>(buffer.data()), cipher.size());
    return true;
}

std::string derive_key(const std::string& password,
                       const std::string& salt,
                       const unsigned iterations)
{
    return pbkdf2_hmac_sha256(password, salt, iterations, KEY_BYTES + MAC_BYTES);
}

bool seal_with_key(const std::string& plaintext,
                   const std::string& derivedKey,
                   CtEncryptedEnvelope& rEnvelope)
{
    if (derivedKey.size() != KEY_BYTES + MAC_BYTES) return false;
    if (SALT_BYTES != rEnvelope.kdfSalt.size()) return false;
    if (0u == rEnvelope.kdfIterations) return false;

    rEnvelope.version = ENVELOPE_VERSION;
    // a fresh iv every time, the salt and therefore the key stay as they are
    if (not random_bytes(IV_BYTES, rEnvelope.iv)) return false;

    std::string encKey = derivedKey.substr(0, KEY_BYTES);
    std::string macKey = derivedKey.substr(KEY_BYTES);

    // PKCS#7: always pad, a full extra block when already aligned
    std::string padded = plaintext;
    const size_t padLen = BLOCK_BYTES - (padded.size() % BLOCK_BYTES);
    padded.append(padLen, static_cast<char>(padLen));

    const bool encrypted = aes256_cbc_encrypt(encKey, rEnvelope.iv, padded, rEnvelope.payload);
    wipe(padded);
    wipe(encKey);
    if (not encrypted) { wipe(macKey); return false; }

    rEnvelope.mac = hmac_sha256(macKey, mac_input(rEnvelope));
    wipe(macKey);
    return true;
}

bool unseal_with_key(const CtEncryptedEnvelope& envelope,
                     const std::string& derivedKey,
                     std::string& rPlaintext)
{
    rPlaintext.clear();
    if (derivedKey.size() != KEY_BYTES + MAC_BYTES) return false;
    if (ENVELOPE_VERSION != envelope.version) return false;
    if (0u == envelope.kdfIterations) return false;
    if (SALT_BYTES != envelope.kdfSalt.size()) return false;
    if (IV_BYTES != envelope.iv.size()) return false;
    if (MAC_BYTES != envelope.mac.size()) return false;
    if (envelope.payload.empty() or 0u != envelope.payload.size() % BLOCK_BYTES) return false;

    std::string encKey = derivedKey.substr(0, KEY_BYTES);
    std::string macKey = derivedKey.substr(KEY_BYTES);

    // Authenticate before touching the ciphertext
    const std::string expectedMac = hmac_sha256(macKey, mac_input(envelope));
    wipe(macKey);
    if (not equal_constant_time(expectedMac, envelope.mac)) {
        wipe(encKey);
        return false;
    }

    std::string padded;
    const bool decrypted = aes256_cbc_decrypt(encKey, envelope.iv, envelope.payload, padded);
    wipe(encKey);
    if (not decrypted) return false;

    const size_t padLen = static_cast<unsigned char>(padded[padded.size() - 1u]);
    if (0u == padLen or padLen > BLOCK_BYTES or padLen > padded.size()) { wipe(padded); return false; }
    for (size_t i = padded.size() - padLen; i < padded.size(); ++i) {
        if (static_cast<unsigned char>(padded[i]) != padLen) { wipe(padded); return false; }
    }
    rPlaintext.assign(padded, 0, padded.size() - padLen);
    wipe(padded);
    return true;
}

bool seal(const std::string& plaintext,
          const std::string& password,
          CtEncryptedEnvelope& rEnvelope,
          const unsigned iterations)
{
    if (0u == iterations) return false;
    if (not random_bytes(SALT_BYTES, rEnvelope.kdfSalt)) return false;
    rEnvelope.kdfIterations = iterations;

    std::string derived = derive_key(password, rEnvelope.kdfSalt, iterations);
    const bool sealed = seal_with_key(plaintext, derived, rEnvelope);
    wipe(derived);
    return sealed;
}

bool unseal(const CtEncryptedEnvelope& envelope,
            const std::string& password,
            std::string& rPlaintext)
{
    rPlaintext.clear();
    if (0u == envelope.kdfIterations or SALT_BYTES != envelope.kdfSalt.size()) return false;

    std::string derived = derive_key(password, envelope.kdfSalt, envelope.kdfIterations);
    const bool unsealed = unseal_with_key(envelope, derived, rPlaintext);
    wipe(derived);
    return unsealed;
}

void wipe(std::string& rSecret)
{
    if (rSecret.empty()) return;
    volatile char* pData = &rSecret[0];
    const size_t numBytes = rSecret.size();
    for (size_t i = 0; i < numBytes; ++i) pData[i] = '\0';
    rSecret.clear();
}

} // namespace CtCrypto
