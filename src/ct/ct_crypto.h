/*
 * ct_crypto.h
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

#pragma once

#include <string>
#include <cstddef>

// Symmetric encryption for the password protected tree areas.
// Built on the AES-256 and SHA-256 primitives of the bundled 7-Zip sources
// (src/7za/C/Aes.c, src/7za/C/Sha256.c); no external crypto dependency.
// All std::string values here are raw byte buffers, not text.
namespace CtCrypto {

constexpr size_t   SHA256_BYTES = 32u;
constexpr size_t   BLOCK_BYTES = 16u;      // AES block
constexpr size_t   SALT_BYTES = 16u;
constexpr size_t   IV_BYTES = 16u;
constexpr size_t   MAC_BYTES = 32u;
constexpr size_t   KEY_BYTES = 32u;        // AES-256
constexpr unsigned DEFAULT_KDF_ITERATIONS = 600000u;
constexpr int      ENVELOPE_VERSION = 1;

// Hashing and key derivation
std::string sha256(const std::string& data);
std::string hmac_sha256(const std::string& key, const std::string& data);
std::string pbkdf2_hmac_sha256(const std::string& password,
                               const std::string& salt,
                               const unsigned iterations,
                               const size_t outLen);

// Cryptographically secure random bytes; false if the system source failed
bool random_bytes(const size_t num, std::string& rOut);

// Raw AES-256-CBC, no padding: the data length must be a multiple of BLOCK_BYTES.
// Exposed so that the unit tests can run the published NIST vectors.
bool aes256_cbc_encrypt(const std::string& key, const std::string& iv,
                        const std::string& plain, std::string& rCipher);
bool aes256_cbc_decrypt(const std::string& key, const std::string& iv,
                        const std::string& cipher, std::string& rPlain);

// One encrypted blob, mapping one to one onto the sqlite protected_area columns
struct CtEncryptedEnvelope
{
    int         version{ENVELOPE_VERSION};
    unsigned    kdfIterations{0u};
    std::string kdfSalt;
    std::string iv;
    std::string mac;
    std::string payload;
};

// The key material a password derives to, KEY_BYTES + MAC_BYTES long.
// Deriving is deliberately expensive, so an unlocked area should hold this and
// re-seal with seal_with_key() rather than paying for the derivation again.
std::string derive_key(const std::string& password,
                       const std::string& salt,
                       const unsigned iterations);

// Re-seal with key material already derived. The envelope must already carry
// the kdfSalt and kdfIterations that key was derived from; only a fresh iv is
// generated. Cheap, unlike seal().
bool seal_with_key(const std::string& plaintext,
                   const std::string& derivedKey,
                   CtEncryptedEnvelope& rEnvelope);

bool unseal_with_key(const CtEncryptedEnvelope& envelope,
                     const std::string& derivedKey,
                     std::string& rPlaintext);

// Encrypt then MAC. Generates a fresh random salt and iv on every call.
bool seal(const std::string& plaintext,
          const std::string& password,
          CtEncryptedEnvelope& rEnvelope,
          const unsigned iterations = DEFAULT_KDF_ITERATIONS);

// Verifies the mac before decrypting; false means wrong password or tampering.
bool unseal(const CtEncryptedEnvelope& envelope,
            const std::string& password,
            std::string& rPlaintext);

// Overwrite a secret in place before releasing it
void wipe(std::string& rSecret);

} // namespace CtCrypto
