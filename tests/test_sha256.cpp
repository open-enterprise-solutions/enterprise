// =============================================================================
// OES Enterprise — SHA-256 tests
//
// ibSHA256 (backend/utils/sha256.hpp) is the primitive under PBKDF2 password
// hashing (see test_passwordHash.cpp). Pin it to the FIPS 180-4 known vectors,
// confirm the incremental Update/Final path equals the one-shot Hash, and guard
// determinism. A drift here silently changes every stored password hash.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/utils/sha256.hpp"

#include <cstdint>
#include <cstring>

namespace {

wxString Hex(const uint8_t* d, size_t n) {
    wxString s;
    for (size_t i = 0; i < n; ++i) s += wxString::Format(wxT("%02x"), (unsigned)d[i]);
    return s;
}

wxString Sha256Hex(const char* msg) {
    uint8_t out[ibSHA256::DIGEST_SIZE];
    ibSHA256::Hash(reinterpret_cast<const uint8_t*>(msg), std::strlen(msg), out);
    return Hex(out, ibSHA256::DIGEST_SIZE);
}

} // namespace

// --- FIPS 180-4 known vectors ----------------------------------------------

TEST(Sha256, EmptyVector) {
    EXPECT_EQ(Sha256Hex(""),
              wxT("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST(Sha256, AbcVector) {
    EXPECT_EQ(Sha256Hex("abc"),
              wxT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

TEST(Sha256, TwoBlockVector) {
    EXPECT_EQ(Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              wxT("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

// --- shape + determinism ----------------------------------------------------

TEST(Sha256, DigestSizeIs32) {
    EXPECT_EQ(ibSHA256::DIGEST_SIZE, 32u);
    EXPECT_EQ(Sha256Hex("anything").length(), 64u);   // 32 bytes -> 64 hex chars
}

TEST(Sha256, Deterministic) {
    EXPECT_EQ(Sha256Hex("repeat"), Sha256Hex("repeat"));
}

TEST(Sha256, DifferentInputsDiffer) {
    EXPECT_NE(Sha256Hex("a"), Sha256Hex("b"));
}

// --- incremental Update/Final == one-shot Hash ------------------------------

TEST(Sha256, IncrementalEqualsOneShot) {
    const char* a = "hello, ";
    const char* b = "world";
    ibSHA256 ctx;
    ctx.Update(reinterpret_cast<const uint8_t*>(a), std::strlen(a));
    ctx.Update(reinterpret_cast<const uint8_t*>(b), std::strlen(b));
    uint8_t incr[ibSHA256::DIGEST_SIZE];
    ctx.Final(incr);
    EXPECT_EQ(Hex(incr, ibSHA256::DIGEST_SIZE), Sha256Hex("hello, world"));
}
