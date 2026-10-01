// =============================================================================
// OES Enterprise — MD5 tests
//
// ibMD5 (backend/utils/md5.hpp) is retained for metadata integrity only —
// NEVER for passwords (see test_passwordHash.cpp). These tests pin the
// algorithm to the standard RFC 1321 vectors and guard the cross-platform
// determinism the macOS port depended on:
//   * 6d1ebd96 — uint32 was 8 bytes on arm64 (LP64) -> uint32_t
//   * 57dd30ff — stack-buffer fix + hashing content.utf8_str() (not the
//                platform-dependent c_str() byte view)
// A drift in any of those flips a known vector below.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/utils/md5.hpp"

namespace {
// Compare case-insensitively so the test does not depend on the hex case the
// implementation happens to emit.
::testing::AssertionResult HexEq(const wxString& got, const char* expected) {
    if (got.IsSameAs(wxString::FromAscii(expected), false))
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "got " << got.ToStdString() << " expected " << expected;
}
} // namespace

// ---------------------------------------------------------------------------
// RFC 1321 known vectors (over the ASCII byte stream, identical under UTF-8,
// so they pin the core transform independent of encoding).
// ---------------------------------------------------------------------------

TEST(Md5, EmptyString) {
    EXPECT_TRUE(HexEq(ibMD5::ComputeMd5(wxEmptyString),
                      "d41d8cd98f00b204e9800998ecf8427e"));
}

TEST(Md5, Abc) {
    EXPECT_TRUE(HexEq(ibMD5::ComputeMd5(wxT("abc")),
                      "900150983cd24fb0d6963f7d28e17f72"));
}

TEST(Md5, MessageDigest) {
    EXPECT_TRUE(HexEq(ibMD5::ComputeMd5(wxT("message digest")),
                      "f96b697d7cb7938d525a2f31aaf161d0"));
}

TEST(Md5, Alphabet) {
    EXPECT_TRUE(HexEq(ibMD5::ComputeMd5(wxT("abcdefghijklmnopqrstuvwxyz")),
                      "c3fcd3d76192e4007dfb496cca67e13b"));
}

// ---------------------------------------------------------------------------
// Shape + determinism
// ---------------------------------------------------------------------------

TEST(Md5, Always32HexChars) {
    EXPECT_EQ(ibMD5::ComputeMd5(wxT("anything")).length(), 32u);
    EXPECT_EQ(ibMD5::ComputeMd5(wxEmptyString).length(), 32u);
}

TEST(Md5, Deterministic) {
    EXPECT_EQ(ibMD5::ComputeMd5(wxT("repeat")), ibMD5::ComputeMd5(wxT("repeat")));
}

TEST(Md5, DifferentInputsDiffer) {
    EXPECT_NE(ibMD5::ComputeMd5(wxT("a")), ibMD5::ComputeMd5(wxT("b")));
}

// Non-ASCII input exercises the utf8_str() path (the macOS fix). We do not
// assert a hand-computed constant here, but the hash must be a stable 32-hex
// string — and distinct from a near-miss — every run / platform. The inputs
// are the UTF-8 byte sequences for the Cyrillic words "test" and "tesu".
TEST(Md5, UnicodeStableAndDistinct) {
    const wxString test1 = wxString::FromUTF8("\xD1\x82\xD0\xB5\xD1\x81\xD1\x82"); // t e s t
    const wxString test2 = wxString::FromUTF8("\xD1\x82\xD0\xB5\xD1\x81\xD1\x83"); // t e s u
    const wxString cyr = ibMD5::ComputeMd5(test1);
    EXPECT_EQ(cyr.length(), 32u);
    EXPECT_EQ(cyr, ibMD5::ComputeMd5(test1));   // deterministic across calls
    EXPECT_NE(cyr, ibMD5::ComputeMd5(test2));   // one code point apart -> differs
}

// ---------------------------------------------------------------------------
// Keyed MD5 (HMAC-MD5)
// ---------------------------------------------------------------------------

TEST(Md5, KeyedDeterministicAndKeyDependent) {
    const wxString k1 = ibMD5::ComputeKeyedMd5(wxT("data"), wxT("key1"));
    EXPECT_EQ(k1.length(), 32u);
    EXPECT_EQ(k1, ibMD5::ComputeKeyedMd5(wxT("data"), wxT("key1")));   // deterministic
    EXPECT_NE(k1, ibMD5::ComputeKeyedMd5(wxT("data"), wxT("key2")));   // key matters
    EXPECT_NE(k1, ibMD5::ComputeMd5(wxT("data")));                     // keyed != plain
}
