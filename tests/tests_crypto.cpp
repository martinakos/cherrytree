/*
 * tests_crypto.cpp
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
#include "gtest/gtest.h"

#include <set>
#include <string>

namespace {

std::string from_hex(const std::string& hex)
{
    std::string bytes;
    bytes.reserve(hex.size() / 2u);
    for (size_t i = 0; i + 1u < hex.size(); i += 2u) {
        bytes.push_back(static_cast<char>(std::stoul(hex.substr(i, 2u), nullptr, 16)));
    }
    return bytes;
}

std::string repeat_hex(const std::string& unit, const size_t times)
{
    std::string hex;
    hex.reserve(unit.size() * times);
    for (size_t i = 0; i < times; ++i) hex += unit;
    return hex;
}

std::string to_hex(const std::string& bytes)
{
    static const char* digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2u);
    for (const char ch : bytes) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 0x0F]);
    }
    return hex;
}

} // anonymous namespace

// FIPS 180-2 / NIST published SHA-256 vectors
TEST(CtCryptoTest, Sha256KnownVectors)
{
    EXPECT_EQ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              to_hex(CtCrypto::sha256("")));
    EXPECT_EQ("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              to_hex(CtCrypto::sha256("abc")));
    EXPECT_EQ("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              to_hex(CtCrypto::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")));
}

// RFC 4231 HMAC-SHA-256 test cases
TEST(CtCryptoTest, HmacSha256Rfc4231)
{
    // Case 1
    EXPECT_EQ("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
              to_hex(CtCrypto::hmac_sha256(from_hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"), "Hi There")));
    // Case 2 (key shorter than the block)
    EXPECT_EQ("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
              to_hex(CtCrypto::hmac_sha256("Jefe", "what do ya want for nothing?")));
    // Case 3
    EXPECT_EQ("773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
              to_hex(CtCrypto::hmac_sha256(from_hex(repeat_hex("aa", 20u)),
                                           from_hex(repeat_hex("dd", 50u)))));
    // Case 4
    EXPECT_EQ("82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
              to_hex(CtCrypto::hmac_sha256(from_hex("0102030405060708090a0b0c0d0e0f10111213141516171819"),
                                           from_hex(repeat_hex("cd", 50u)))));
    // Case 6 (key longer than the block, so it is hashed first)
    EXPECT_EQ("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
              to_hex(CtCrypto::hmac_sha256(from_hex(repeat_hex("aa", 131u)),
                                           "Test Using Larger Than Block-Size Key - Hash Key First")));
    // Case 7
    EXPECT_EQ("9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2",
              to_hex(CtCrypto::hmac_sha256(from_hex(repeat_hex("aa", 131u)),
                                           "This is a test using a larger than block-size key and a larger "
                                           "than block-size data. The key needs to be hashed before being "
                                           "used by the HMAC algorithm.")));
}

// RFC 7914 section 11 PBKDF2-HMAC-SHA-256 vectors
TEST(CtCryptoTest, Pbkdf2HmacSha256Rfc7914)
{
    EXPECT_EQ("55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
              "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783",
              to_hex(CtCrypto::pbkdf2_hmac_sha256("passwd", "salt", 1u, 64u)));

    EXPECT_EQ("4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
              "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d",
              to_hex(CtCrypto::pbkdf2_hmac_sha256("Password", "NaCl", 80000u, 64u)));
}

// NIST SP 800-38A F.2.5 / F.2.6, CBC-AES256
TEST(CtCryptoTest, Aes256CbcNistVectors)
{
    const std::string key = from_hex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    const std::string iv = from_hex("000102030405060708090a0b0c0d0e0f");
    const std::string plain = from_hex("6bc1bee22e409f96e93d7e117393172a"
                                       "ae2d8a571e03ac9c9eb76fac45af8e51"
                                       "30c81c46a35ce411e5fbc1191a0a52ef"
                                       "f69f2445df4f9b17ad2b417be66c3710");
    const std::string expected = "f58c4c04d6e5f1ba779eabfb5f7bfbd6"
                                 "9cfc4e967edb808d679f777bc6702c7d"
                                 "39f23369a9d9bacfa530e26304231461"
                                 "b2eb05e2c39be9fcda6c19078c6a9d1b";

    std::string cipher;
    ASSERT_TRUE(CtCrypto::aes256_cbc_encrypt(key, iv, plain, cipher));
    EXPECT_EQ(expected, to_hex(cipher));

    std::string roundTrip;
    ASSERT_TRUE(CtCrypto::aes256_cbc_decrypt(key, iv, cipher, roundTrip));
    EXPECT_EQ(plain, roundTrip);
}

TEST(CtCryptoTest, Aes256CbcRejectsBadSizes)
{
    const std::string key(CtCrypto::KEY_BYTES, 'k');
    const std::string iv(CtCrypto::IV_BYTES, 'i');
    std::string out;
    EXPECT_FALSE(CtCrypto::aes256_cbc_encrypt(key, iv, std::string(17u, 'x'), out));  // not a block multiple
    EXPECT_FALSE(CtCrypto::aes256_cbc_encrypt(std::string(16u, 'k'), iv, std::string(16u, 'x'), out)); // AES-128 key
    EXPECT_FALSE(CtCrypto::aes256_cbc_encrypt(key, std::string(8u, 'i'), std::string(16u, 'x'), out)); // short iv
}

TEST(CtCryptoTest, RandomBytesAreSizedAndVary)
{
    std::string first;
    std::string second;
    ASSERT_TRUE(CtCrypto::random_bytes(32u, first));
    ASSERT_TRUE(CtCrypto::random_bytes(32u, second));
    EXPECT_EQ(32u, first.size());
    EXPECT_EQ(32u, second.size());
    EXPECT_NE(first, second);
    EXPECT_NE(std::string(32u, '\0'), first);
}

// A low iteration count keeps the round trip tests quick; the KDF itself is
// covered by the published vectors above
constexpr unsigned TEST_ITERATIONS = 1000u;

TEST(CtCryptoTest, SealUnsealRoundTrip)
{
    const std::string plaintext = "<node name=\"secret\">très secret ✓</node>";
    CtCrypto::CtEncryptedEnvelope envelope;
    ASSERT_TRUE(CtCrypto::seal(plaintext, "correct horse", envelope, TEST_ITERATIONS));

    EXPECT_EQ(CtCrypto::ENVELOPE_VERSION, envelope.version);
    EXPECT_EQ(TEST_ITERATIONS, envelope.kdfIterations);
    EXPECT_EQ(CtCrypto::SALT_BYTES, envelope.kdfSalt.size());
    EXPECT_EQ(CtCrypto::IV_BYTES, envelope.iv.size());
    EXPECT_EQ(CtCrypto::MAC_BYTES, envelope.mac.size());
    EXPECT_EQ(0u, envelope.payload.size() % CtCrypto::BLOCK_BYTES);
    // the plaintext must not survive anywhere in the blob
    EXPECT_EQ(std::string::npos, envelope.payload.find("secret"));

    std::string recovered;
    ASSERT_TRUE(CtCrypto::unseal(envelope, "correct horse", recovered));
    EXPECT_EQ(plaintext, recovered);
}

TEST(CtCryptoTest, SealHandlesEmptyAndBlockAlignedPlaintext)
{
    for (const std::string& plaintext : {std::string{},
                                        std::string(1u, 'a'),
                                        std::string(16u, 'b'),
                                        std::string(32u, 'c'),
                                        std::string(4096u, 'd')}) {
        CtCrypto::CtEncryptedEnvelope envelope;
        ASSERT_TRUE(CtCrypto::seal(plaintext, "pw", envelope, TEST_ITERATIONS));
        // PKCS#7 always adds padding, so the blob grows even when already aligned
        EXPECT_GT(envelope.payload.size(), plaintext.size());
        std::string recovered;
        ASSERT_TRUE(CtCrypto::unseal(envelope, "pw", recovered));
        EXPECT_EQ(plaintext, recovered);
    }
}

TEST(CtCryptoTest, SealUsesFreshSaltAndIvEveryTime)
{
    std::set<std::string> salts;
    std::set<std::string> ivs;
    std::set<std::string> payloads;
    for (int i = 0; i < 8; ++i) {
        CtCrypto::CtEncryptedEnvelope envelope;
        ASSERT_TRUE(CtCrypto::seal("same plaintext every time", "same password", envelope, TEST_ITERATIONS));
        salts.insert(envelope.kdfSalt);
        ivs.insert(envelope.iv);
        payloads.insert(envelope.payload);
    }
    EXPECT_EQ(8u, salts.size());
    EXPECT_EQ(8u, ivs.size());
    EXPECT_EQ(8u, payloads.size());
}

TEST(CtCryptoTest, UnsealRejectsWrongPassword)
{
    CtCrypto::CtEncryptedEnvelope envelope;
    ASSERT_TRUE(CtCrypto::seal("top secret", "right password", envelope, TEST_ITERATIONS));

    std::string recovered{"not cleared"};
    EXPECT_FALSE(CtCrypto::unseal(envelope, "wrong password", recovered));
    EXPECT_TRUE(recovered.empty());
    EXPECT_FALSE(CtCrypto::unseal(envelope, "", recovered));
    EXPECT_FALSE(CtCrypto::unseal(envelope, "right password ", recovered));
}

TEST(CtCryptoTest, UnsealRejectsTamperingInEveryField)
{
    CtCrypto::CtEncryptedEnvelope original;
    ASSERT_TRUE(CtCrypto::seal("the quick brown fox jumps over the lazy dog", "pw", original, TEST_ITERATIONS));

    std::string recovered;
    ASSERT_TRUE(CtCrypto::unseal(original, "pw", recovered));

    // flip a single bit in each field in turn
    {
        CtCrypto::CtEncryptedEnvelope tampered = original;
        tampered.payload[0] = static_cast<char>(tampered.payload[0] ^ 0x01);
        EXPECT_FALSE(CtCrypto::unseal(tampered, "pw", recovered));
    }
    {
        CtCrypto::CtEncryptedEnvelope tampered = original;
        tampered.payload[tampered.payload.size() - 1u] = static_cast<char>(tampered.payload.back() ^ 0x80);
        EXPECT_FALSE(CtCrypto::unseal(tampered, "pw", recovered));
    }
    {
        CtCrypto::CtEncryptedEnvelope tampered = original;
        tampered.iv[3] = static_cast<char>(tampered.iv[3] ^ 0x01);
        EXPECT_FALSE(CtCrypto::unseal(tampered, "pw", recovered));
    }
    {
        CtCrypto::CtEncryptedEnvelope tampered = original;
        tampered.kdfSalt[7] = static_cast<char>(tampered.kdfSalt[7] ^ 0x01);
        EXPECT_FALSE(CtCrypto::unseal(tampered, "pw", recovered));
    }
    {
        CtCrypto::CtEncryptedEnvelope tampered = original;
        tampered.mac[31] = static_cast<char>(tampered.mac[31] ^ 0x01);
        EXPECT_FALSE(CtCrypto::unseal(tampered, "pw", recovered));
    }
    {
        CtCrypto::CtEncryptedEnvelope tampered = original;
        tampered.kdfIterations = original.kdfIterations + 1u;
        EXPECT_FALSE(CtCrypto::unseal(tampered, "pw", recovered));
    }
    {
        CtCrypto::CtEncryptedEnvelope tampered = original;
        tampered.version = original.version + 1;
        EXPECT_FALSE(CtCrypto::unseal(tampered, "pw", recovered));
    }
    // the untouched envelope still opens
    EXPECT_TRUE(CtCrypto::unseal(original, "pw", recovered));
}

TEST(CtCryptoTest, UnsealRejectsMalformedEnvelope)
{
    CtCrypto::CtEncryptedEnvelope original;
    ASSERT_TRUE(CtCrypto::seal("payload", "pw", original, TEST_ITERATIONS));
    std::string recovered;

    CtCrypto::CtEncryptedEnvelope truncatedPayload = original;
    truncatedPayload.payload.resize(truncatedPayload.payload.size() - 1u);
    EXPECT_FALSE(CtCrypto::unseal(truncatedPayload, "pw", recovered));

    CtCrypto::CtEncryptedEnvelope emptyPayload = original;
    emptyPayload.payload.clear();
    EXPECT_FALSE(CtCrypto::unseal(emptyPayload, "pw", recovered));

    CtCrypto::CtEncryptedEnvelope shortSalt = original;
    shortSalt.kdfSalt.resize(4u);
    EXPECT_FALSE(CtCrypto::unseal(shortSalt, "pw", recovered));

    CtCrypto::CtEncryptedEnvelope zeroIters = original;
    zeroIters.kdfIterations = 0u;
    EXPECT_FALSE(CtCrypto::unseal(zeroIters, "pw", recovered));
}

TEST(CtCryptoTest, DeriveKeyIsDeterministicAndSaltDependent)
{
    const std::string saltA(CtCrypto::SALT_BYTES, 'A');
    const std::string saltB(CtCrypto::SALT_BYTES, 'B');
    const std::string keyA1 = CtCrypto::derive_key("pw", saltA, TEST_ITERATIONS);
    const std::string keyA2 = CtCrypto::derive_key("pw", saltA, TEST_ITERATIONS);
    const std::string keyB = CtCrypto::derive_key("pw", saltB, TEST_ITERATIONS);
    const std::string keyOther = CtCrypto::derive_key("other", saltA, TEST_ITERATIONS);

    EXPECT_EQ(CtCrypto::KEY_BYTES + CtCrypto::MAC_BYTES, keyA1.size());
    EXPECT_EQ(keyA1, keyA2);          // same inputs, same key
    EXPECT_NE(keyA1, keyB);           // salt changes the key
    EXPECT_NE(keyA1, keyOther);       // password changes the key
}

// Re-sealing while an area is unlocked must not repeat the expensive derivation
TEST(CtCryptoTest, SealWithKeyRoundTripsAndKeepsTheSalt)
{
    CtCrypto::CtEncryptedEnvelope envelope;
    ASSERT_TRUE(CtCrypto::seal("first version", "pw", envelope, TEST_ITERATIONS));
    const std::string originalSalt = envelope.kdfSalt;
    const std::string originalIv = envelope.iv;

    const std::string derivedKey = CtCrypto::derive_key("pw", envelope.kdfSalt, envelope.kdfIterations);
    ASSERT_TRUE(CtCrypto::seal_with_key("second version, rather longer than the first", derivedKey, envelope));

    EXPECT_EQ(originalSalt, envelope.kdfSalt);   // salt is kept, the key stays valid
    EXPECT_NE(originalIv, envelope.iv);          // but the iv is fresh

    std::string recovered;
    ASSERT_TRUE(CtCrypto::unseal_with_key(envelope, derivedKey, recovered));
    EXPECT_EQ("second version, rather longer than the first", recovered);

    // and it is still openable with the password alone
    ASSERT_TRUE(CtCrypto::unseal(envelope, "pw", recovered));
    EXPECT_EQ("second version, rather longer than the first", recovered);
}

TEST(CtCryptoTest, KeyBasedApiRejectsBadInputs)
{
    CtCrypto::CtEncryptedEnvelope envelope;
    ASSERT_TRUE(CtCrypto::seal("payload", "pw", envelope, TEST_ITERATIONS));
    const std::string goodKey = CtCrypto::derive_key("pw", envelope.kdfSalt, envelope.kdfIterations);
    const std::string wrongKey = CtCrypto::derive_key("nope", envelope.kdfSalt, envelope.kdfIterations);

    std::string recovered;
    EXPECT_FALSE(CtCrypto::unseal_with_key(envelope, wrongKey, recovered));
    EXPECT_FALSE(CtCrypto::unseal_with_key(envelope, std::string(10u, 'k'), recovered));
    EXPECT_TRUE(CtCrypto::unseal_with_key(envelope, goodKey, recovered));

    // seal_with_key needs a salt and an iteration count already in place
    CtCrypto::CtEncryptedEnvelope fresh;
    EXPECT_FALSE(CtCrypto::seal_with_key("x", goodKey, fresh));
}

TEST(CtCryptoTest, WipeClearsTheSecret)
{
    std::string secret = "hunter2";
    CtCrypto::wipe(secret);
    EXPECT_TRUE(secret.empty());
}

// ── menu layout migration ───────────────────────────────────────────────────
// A saved menu layout wins over the compiled default, so an entry added by a
// new version has to be spliced into configs that predate it.

#include "ct_config.h"

namespace {

const char PROTECT_GROUP[]{"{TreeProtectSubMenu,tree_node_protect,tree_node_change_password,"
                           "tree_node_unprotect,separator,tree_lock_protected,},separator,"};
const char PROTECT_ANCHOR[]{"child_nodes_inherit_syntax,separator,"};

void ensure_protect(std::string& rUiList)
{
    CtConfig::ensure_ui_list_has_group(rUiList, "tree_node_protect", PROTECT_GROUP, PROTECT_ANCHOR);
}

} // anonymous namespace

TEST(CtConfigUiListTest, SplicesTheGroupAfterTheAnchor)
{
    // this is the shape of a real config written before the feature existed
    std::string uiList = "go_node_next,go_node_prev,separator,tree_node_prop,tree_node_toggle_ro,"
                         "tree_node_link,child_nodes_inherit_syntax,separator,"
                         "{BookmarksSubMenu,},node_bookmark,node_unbookmark,separator,tree_node_del";
    ensure_protect(uiList);

    EXPECT_NE(std::string::npos, uiList.find("tree_node_protect"));
    EXPECT_NE(std::string::npos, uiList.find("tree_lock_protected"));
    // inserted at the anchor, not appended at the end
    EXPECT_LT(uiList.find("TreeProtectSubMenu"), uiList.find("BookmarksSubMenu"));
    EXPECT_GT(uiList.find("TreeProtectSubMenu"), uiList.find("child_nodes_inherit_syntax"));
    // and the rest of the customised layout is untouched
    EXPECT_NE(std::string::npos, uiList.find("go_node_next,go_node_prev"));
    EXPECT_NE(std::string::npos, uiList.find("tree_node_del"));
}

TEST(CtConfigUiListTest, DoesNothingWhenAlreadyPresent)
{
    std::string uiList = "tree_node_prop,child_nodes_inherit_syntax,separator,";
    uiList += PROTECT_GROUP;
    const std::string before = uiList;
    ensure_protect(uiList);
    EXPECT_EQ(before, uiList) << "must not insert the group twice";
}

TEST(CtConfigUiListTest, AppendsWhenTheAnchorIsMissing)
{
    std::string uiList = "tree_add_node,tree_node_del";
    ensure_protect(uiList);
    EXPECT_NE(std::string::npos, uiList.find("tree_node_protect"));
    EXPECT_NE(std::string::npos, uiList.find("tree_add_node,tree_node_del"));
}

TEST(CtConfigUiListTest, LeavesAnEmptyListAlone)
{
    // empty means "use the compiled default", which already has the group
    std::string uiList;
    ensure_protect(uiList);
    EXPECT_TRUE(uiList.empty());
}


TEST(CtConfigUiListTest, HandlesARealSavedConfigLayout)
{
    std::string uiList = "go_node_next,go_node_prev,separator,tree_add_node,tree_add_subnode,tree_dup_node,tree_dup_node_subnodes,tree_shared_node,tree_copy_node_subnodes,tree_paste_node_subnodes,tree_node_date_root,tree_node_date_sel,separator,tree_node_prop,tree_node_toggle_ro,tree_node_link,child_nodes_inherit_syntax,separator,{BookmarksSubMenu,},node_bookmark,node_unbookmark,separator,nodes_all_expand,nodes_all_collapse,separator,{TreeMoveSubMenu,tree_node_up,tree_node_down,tree_node_left,tree_node_right,tree_node_new_father,},{TreeSortSubMenu,tree_sibl_sort_asc,tree_sibl_sort_desc,separator,tree_all_sort_asc,tree_all_sort_desc,},separator,tree_node_del";
    ASSERT_EQ(std::string::npos, uiList.find("tree_node_protect")) << "fixture should predate the feature";
    ensure_protect(uiList);
    EXPECT_NE(std::string::npos, uiList.find("tree_node_protect"));
    EXPECT_NE(std::string::npos, uiList.find("tree_lock_protected"));
    EXPECT_LT(uiList.find("TreeProtectSubMenu"), uiList.find("BookmarksSubMenu"));
}
