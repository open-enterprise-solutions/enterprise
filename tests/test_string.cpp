// =============================================================================
// OES Enterprise — ibString prototype tests
//
// ibString is a thin wrapper over std::wstring (wxChar-width). The whole point
// is wxString PARITY for the operations the runtime relies on, so most tests
// compare an ibString op against the same wxString op on the same logical
// string (ASCII + Cyrillic + edge cases). Non-ASCII test data is built from
// explicit UTF-8 byte sequences so the result is independent of this source
// file's own encoding.
// =============================================================================

#include <gtest/gtest.h>
#include <iostream>
#include "backend/fstring.h"

namespace {
// "Привет" in UTF-8 (6 Cyrillic code points → 6 wxChars, 12 UTF-8 bytes).
const char* const kHelloRu = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
wxString WxRu() { return wxString::FromUTF8(kHelloRu); }
}

// ---------------------------------------------------------------------------
// Construction / conversion round-trips
// ---------------------------------------------------------------------------

TEST(IbString, DefaultIsEmpty) {
    ibString s;
    EXPECT_TRUE(s.IsEmpty());
    EXPECT_EQ(s.Len(), 0u);
    EXPECT_TRUE(s.ToWxString().IsEmpty());
}

TEST(IbString, RoundTripFromWxAscii) {
    wxString w = wxT("Hello, World");
    ibString s(w);
    EXPECT_TRUE(s.ToWxString() == w);
    EXPECT_EQ(s.Len(), w.length());
}

TEST(IbString, RoundTripFromWxCyrillic) {
    wxString w = WxRu();
    ibString s(w);
    EXPECT_TRUE(s.ToWxString() == w);
    EXPECT_EQ(s.Len(), w.length());   // 6 wxChars, not 12 bytes
}

TEST(IbString, FromWcharLiteral) {
    ibString s(L"abc");
    EXPECT_TRUE(s.ToWxString() == wxT("abc"));
    EXPECT_EQ(s.ToStdWString(), std::wstring(L"abc"));
}

TEST(IbString, Utf8RoundTrip) {
    // const char* ctor takes UTF-8 in (native codec); ToUtf8() gives it back.
    ibString s(kHelloRu);
    EXPECT_TRUE(s.ToWxString() == WxRu());
    EXPECT_EQ(s.ToUtf8(), std::string(kHelloRu));
}

TEST(IbString, Utf8AstralRoundTrip) {
    // U+1F600 (4-byte UTF-8) — exercises the surrogate-pair path on UTF-16
    // wchar platforms; must round-trip and match wxString's own decode.
    const char* const emoji = "\xF0\x9F\x98\x80";
    ibString s(emoji);
    EXPECT_EQ(s.ToUtf8(), std::string(emoji));
    EXPECT_TRUE(s.ToWxString() == wxString::FromUTF8(emoji));
}

TEST(IbString, Utf8MultilingualRoundTrip) {
    // Arabic (2-byte UTF-8) and Chinese (3-byte) — full Unicode coverage, not
    // a codepage. Each is BMP, so 1 wchar per char, matching wxString::Length.
    const char* const arabic  = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";  // مرحبا
    const char* const chinese = "\xE4\xBD\xA0\xE5\xA5\xBD";                   // 你好
    for (const char* u8 : { arabic, chinese }) {
        ibString s(u8);
        EXPECT_EQ(s.ToUtf8(), std::string(u8));
        const wxString w = wxString::FromUTF8(u8);
        EXPECT_TRUE(s.ToWxString() == w);
        EXPECT_EQ(s.Len(), w.length());
    }
}

// ---------------------------------------------------------------------------
// wxString parity — slicing/length over a corpus incl. Cyrillic + edges
// ---------------------------------------------------------------------------

TEST(IbString, MidLeftRightParityWithWxString) {
    const wxString corpus[] = { wxT("Hello, World"), WxRu(), wxT(""), wxT("A") };
    for (const wxString& w : corpus) {
        ibString s(w);
        EXPECT_EQ(s.Len(), w.length());
        for (size_t i = 0; i <= w.length() + 1; ++i) {
            EXPECT_TRUE(s.Left(i).ToWxString()  == w.Left(i))  << "Left "  << i;
            EXPECT_TRUE(s.Right(i).ToWxString() == w.Right(i)) << "Right " << i;
            EXPECT_TRUE(s.Mid(i).ToWxString()   == w.Mid(i))   << "Mid "   << i;
            for (size_t n = 0; n <= w.length() + 1; ++n)
                EXPECT_TRUE(s.Mid(i, n).ToWxString() == w.Mid(i, n))
                    << "Mid " << i << "," << n;
        }
    }
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

TEST(IbString, FindAndContains) {
    ibString s(wxT("abcabc"));
    EXPECT_EQ(s.Find(ibString(wxT("bc"))), 1u);
    EXPECT_EQ(s.Find(ibString(wxT("xy"))), ibString::npos);
    EXPECT_EQ(s.Find(L'c'), 2u);
    EXPECT_TRUE(s.Contains(ibString(wxT("cab"))));
    EXPECT_FALSE(s.Contains(ibString(wxT("z"))));
}

TEST(IbString, StartsEndsWith) {
    ibString s(wxT("filename.txt"));
    EXPECT_TRUE(s.StartsWith(ibString(wxT("file"))));
    EXPECT_TRUE(s.EndsWith(ibString(wxT(".txt"))));
    EXPECT_FALSE(s.StartsWith(ibString(wxT("xxx"))));
    EXPECT_FALSE(s.EndsWith(ibString(wxT(".doc"))));
    EXPECT_TRUE(s.StartsWith(ibString()));   // empty prefix
}

// ---------------------------------------------------------------------------
// Concat / case / trim / compare
// ---------------------------------------------------------------------------

TEST(IbString, Concat) {
    ibString a(wxT("foo")), b(wxT("bar"));
    EXPECT_TRUE((a + b).ToWxString() == wxT("foobar"));
    a += b;
    EXPECT_TRUE(a.ToWxString() == wxT("foobar"));
}

TEST(IbString, LowerUpper) {
    EXPECT_TRUE(ibString(wxT("Hello")).Lower().ToWxString() == wxT("hello"));
    EXPECT_TRUE(ibString(wxT("Hello")).Upper().ToWxString() == wxT("HELLO"));
}

TEST(IbString, Trim) {
    EXPECT_TRUE(ibString(wxT("abc   ")).Trim().ToWxString()      == wxT("abc"));
    EXPECT_TRUE(ibString(wxT("   abc")).Trim(false).ToWxString() == wxT("abc"));
}

TEST(IbString, Comparison) {
    EXPECT_TRUE (ibString(wxT("abc")) == ibString(wxT("abc")));
    EXPECT_TRUE (ibString(wxT("abc")) != ibString(wxT("abd")));
    EXPECT_TRUE (ibString(wxT("abc")) <  ibString(wxT("abd")));
    EXPECT_TRUE (ibString(wxT("ABC")).IsSameAs(ibString(wxT("abc")), /*caseSensitive*/ false));
    EXPECT_FALSE(ibString(wxT("ABC")).IsSameAs(ibString(wxT("abc")), /*caseSensitive*/ true));
}

TEST(IbString, ClearEmpties) {
    ibString s(wxT("x"));
    EXPECT_FALSE(s.IsEmpty());
    s.Clear();
    EXPECT_TRUE(s.IsEmpty());
}

// ---------------------------------------------------------------------------
// Footprint guarantee — zero overhead over the store
// ---------------------------------------------------------------------------

TEST(IbString, SizeofIsZeroOverhead) {
    EXPECT_EQ(sizeof(ibString), sizeof(ibStringStore));
    std::cout << "[ ibString ] sizeof(ibString)=" << sizeof(ibString)
              << "  sizeof(std::wstring)=" << sizeof(std::wstring) << std::endl;
    SUCCEED();
}
