// =============================================================================
// OES Enterprise — password hashing tests
//
// Covers ibPasswordHash (backend/utils/passwordHash.hpp): PBKDF2-HMAC-SHA256
// storage format, verify (correct / wrong), per-hash salt, the legacy-MD5
// accept path, and the lazy-upgrade predicates (IsLegacy / NeedsRehash).
//
// Security-critical surface that previously had no coverage. See the
// "Password Hashing" note in enterprise/CLAUDE.md and ibApplicationInstance::
// AuthenticateUser for the live upgrade flow.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/utils/passwordHash.hpp"
#include "backend/utils/md5.hpp"

// ---------------------------------------------------------------------------
// Format + round-trip
// ---------------------------------------------------------------------------

TEST(PasswordHash, HashUsesPhcPbkdf2Format) {
    const wxString h = ibPasswordHash::Hash(wxT("correct horse"));
    EXPECT_TRUE(h.StartsWith(wxT("$pbkdf2-sha256$")));
    // PHC string is $pbkdf2-sha256$<iter>$<saltB64>$<hashB64> — four '$'.
    EXPECT_EQ(h.Freq(wxT('$')), 4);
}

TEST(PasswordHash, VerifyAcceptsCorrectPassword) {
    // Unicode round-trip: password built from explicit UTF-8 bytes (no raw
    // non-ASCII literal in source) = "s3cr3t-" + a Cyrillic word.
    const wxString pw = wxString::FromUTF8(
        "s3cr3t-\xD0\x9F\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB\xD1\x8C");
    const wxString h = ibPasswordHash::Hash(pw);
    EXPECT_TRUE(ibPasswordHash::Verify(pw, h));
}

TEST(PasswordHash, VerifyRejectsWrongPassword) {
    const wxString h = ibPasswordHash::Hash(wxT("s3cr3t"));
    EXPECT_FALSE(ibPasswordHash::Verify(wxT("s3cr3X"), h));
}

TEST(PasswordHash, VerifyRejectsEmptyAgainstNonEmpty) {
    const wxString h = ibPasswordHash::Hash(wxT("not-empty"));
    EXPECT_FALSE(ibPasswordHash::Verify(wxEmptyString, h));
}

TEST(PasswordHash, EmptyPasswordRoundTrips) {
    // An empty password that was actually hashed is a real PHC string.
    // That is a different value from a row whose hash column is blank.
    const wxString h = ibPasswordHash::Hash(wxEmptyString);
    EXPECT_FALSE(h.IsEmpty());
    EXPECT_TRUE(ibPasswordHash::Verify(wxEmptyString, h));
    EXPECT_FALSE(ibPasswordHash::Verify(wxT("x"), h));
}

TEST(PasswordHash, EmptyStoredHashMeansNoPassword) {
    EXPECT_TRUE(ibPasswordHash::Verify(wxEmptyString, wxEmptyString));
    EXPECT_FALSE(ibPasswordHash::Verify(wxT("x"), wxEmptyString));
}

TEST(PasswordToStore, Untouched_KeepsTheStoredHash) {
    const wxString stored = wxT("stored-hash");
    EXPECT_EQ(ibPasswordHash::PasswordToStore(stored, false, ibPasswordHash::Placeholder()), stored);
    EXPECT_EQ(ibPasswordHash::PasswordToStore(stored, false, wxEmptyString), stored);
}

TEST(PasswordToStore, Cleared_StoresNoPassword) {
    EXPECT_TRUE(ibPasswordHash::PasswordToStore(wxT("stored-hash"), true, wxEmptyString).IsEmpty());
}

TEST(PasswordToStore, Typed_HashesTheText) {
    const wxString stored = ibPasswordHash::PasswordToStore(wxT("stored-hash"), true, wxT("secret"));
    EXPECT_TRUE(ibPasswordHash::Verify(wxT("secret"), stored));
    EXPECT_FALSE(ibPasswordHash::Verify(wxT("stored-hash"), stored));
}

TEST(PasswordToStore, TypeAndDelete_StoresNoPassword) {
    // One character typed over the placeholder, then deleted: the box is empty.
    EXPECT_TRUE(ibPasswordHash::PasswordToStore(wxT("stored-hash"), true, wxEmptyString).IsEmpty());
}

TEST(PasswordToStore, PlaceholderLeftInTheBox_KeepsTheStoredHash) {
    const wxString stored = wxT("stored-hash");
    EXPECT_EQ(ibPasswordHash::PasswordToStore(stored, true, ibPasswordHash::Placeholder()), stored);
}

TEST(PasswordToStore, ReplacingThePlaceholder_HashesTheTypedText) {
    const wxString stored = ibPasswordHash::PasswordToStore(wxT("stored-hash"), true, wxT("new-secret"));
    EXPECT_TRUE(ibPasswordHash::Verify(wxT("new-secret"), stored));
    EXPECT_NE(stored, ibPasswordHash::Placeholder());
}

TEST(PasswordToStore, NewUserWithAnEmptyBox_StoresNoPassword) {
    EXPECT_TRUE(ibPasswordHash::PasswordToStore(wxEmptyString, false, wxEmptyString).IsEmpty());
}

// ---------------------------------------------------------------------------
// Salt — same password hashes to different strings each time
// ---------------------------------------------------------------------------

TEST(PasswordHash, SaltMakesHashesUnique) {
    const wxString a = ibPasswordHash::Hash(wxT("same-password"));
    const wxString b = ibPasswordHash::Hash(wxT("same-password"));
    EXPECT_NE(a, b);                                   // random per-hash salt
    EXPECT_TRUE(ibPasswordHash::Verify(wxT("same-password"), a));
    EXPECT_TRUE(ibPasswordHash::Verify(wxT("same-password"), b));
}

// ---------------------------------------------------------------------------
// Legacy MD5 accept path (pre-migration databases)
// ---------------------------------------------------------------------------

TEST(PasswordHash, VerifyAcceptsLegacyMd5) {
    // A pre-migration row stored a bare 32-hex MD5. Verify must still accept it
    // so existing users can log in (and then be upgraded).
    const wxString legacy = ibMD5::ComputeMd5(wxT("oldpass"));
    ASSERT_EQ(legacy.length(), 32u);
    EXPECT_TRUE(ibPasswordHash::Verify(wxT("oldpass"), legacy));
    EXPECT_FALSE(ibPasswordHash::Verify(wxT("wrongpass"), legacy));
}

TEST(PasswordHash, IsLegacyDetectsMd5) {
    const wxString legacy = ibMD5::ComputeMd5(wxT("x"));
    EXPECT_TRUE(ibPasswordHash::IsLegacy(legacy));
}

TEST(PasswordHash, IsLegacyRejectsPbkdf2) {
    const wxString modern = ibPasswordHash::Hash(wxT("x"));
    EXPECT_FALSE(ibPasswordHash::IsLegacy(modern));
}

// ---------------------------------------------------------------------------
// Lazy-upgrade predicate
// ---------------------------------------------------------------------------

TEST(PasswordHash, NeedsRehashTrueForLegacy) {
    const wxString legacy = ibMD5::ComputeMd5(wxT("x"));
    EXPECT_TRUE(ibPasswordHash::NeedsRehash(legacy));
}

TEST(PasswordHash, NeedsRehashFalseForFreshHash) {
    const wxString modern = ibPasswordHash::Hash(wxT("x"));
    EXPECT_FALSE(ibPasswordHash::NeedsRehash(modern));
}
