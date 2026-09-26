// =============================================================================
// OES Enterprise — ibString prototype tests
//
// ibString is one handle on a shared, counted wchar text (wxChar-width); a copy
// shares it, a write gets its own. The other point is wxString PARITY for the
// operations the runtime relies on, so most tests
// compare an ibString op against the same wxString op on the same logical
// string (ASCII + Cyrillic + edge cases). Non-ASCII test data is built from
// explicit UTF-8 byte sequences so the result is independent of this source
// file's own encoding.
// =============================================================================

#include <gtest/gtest.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <wx/debug.h>   // wxSetAssertHandler — the Format refusal test
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
    EXPECT_EQ(s.Find(ibString(wxT("bc"))), 1);
    EXPECT_EQ(s.Find(ibString(wxT("xy"))), wxNOT_FOUND);
    EXPECT_EQ(s.Find(wxT('c')), 2);
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
// wxString's API, ported — the answers wx gives, on the same input
// ---------------------------------------------------------------------------

TEST(IbString, FindHasWxMeaning) {
    const ibString s(wxT("a.b.c"));
    EXPECT_EQ(s.Find(wxT('.')), 1);
    EXPECT_EQ(s.Find(wxT('.'), /*fromEnd*/ true), 3);
    EXPECT_EQ(s.Find(wxT('x')), wxNOT_FOUND);
    EXPECT_EQ(s.Find(ibString(wxT("b."))), 2);
    EXPECT_EQ(s.find(wxT('.'), 2), 3u);   // a search FROM a position is std's find
}

TEST(IbString, BeforeAfterParityWithWxString) {
    for (const wxChar* text : { wxT("key=value=more"), wxT("novalue"), wxT("") }) {
        const wxString w(text);
        const ibString s(w);
        EXPECT_TRUE(s.BeforeFirst(wxT('=')).ToWxString() == w.BeforeFirst(wxT('='))) << text;
        EXPECT_TRUE(s.AfterFirst(wxT('=')).ToWxString()  == w.AfterFirst(wxT('=')))  << text;
        EXPECT_TRUE(s.BeforeLast(wxT('=')).ToWxString()  == w.BeforeLast(wxT('=')))  << text;
        EXPECT_TRUE(s.AfterLast(wxT('=')).ToWxString()   == w.AfterLast(wxT('=')))   << text;
    }
}

TEST(IbString, TrimWorksInPlaceAsWxDoes) {
    ibString s(wxT("  abc  "));
    s.Trim();
    EXPECT_TRUE(s == wxT("  abc"));
    s.Trim(false);
    EXPECT_TRUE(s == wxT("abc"));
    EXPECT_TRUE(ibString(wxT(" x ")).TrimAll() == wxT("x"));   // TrimAll stays a copy
}

TEST(IbString, NumbersParityWithWxString) {
    for (const wxChar* text : { wxT("42"), wxT("-7"), wxT("12abc"), wxT(""), wxT("99999999999999999999") }) {
        long a = -1, b = -1;
        EXPECT_EQ(ibString(text).ToLong(&a), wxString(text).ToLong(&b)) << text;
        EXPECT_EQ(a, b) << text;
    }
    double d = 0;
    EXPECT_TRUE(ibString(wxT("3.25")).ToCDouble(&d));
    EXPECT_DOUBLE_EQ(d, 3.25);
    EXPECT_FALSE(ibString(wxT("3.25x")).ToCDouble(&d));
    EXPECT_TRUE(ibString(wxT("-12")).IsNumber());
    EXPECT_FALSE(ibString(wxT("1.5")).IsNumber());
}

TEST(IbString, MatchesParityWithWxString) {
    for (const wxChar* mask : { wxT("*.txt"), wxT("a?c*"), wxT("*"), wxT("abc"), wxT("*b*b*") }) {
        for (const wxChar* text : { wxT("file.txt"), wxT("abcdef"), wxT("abc"), wxT("bb"), wxT("") }) {
            EXPECT_EQ(ibString(text).Matches(mask), wxString(text).Matches(mask)) << mask << " / " << text;
        }
    }
}

TEST(IbString, LiteralOperatorsAreOurs) {
    const ibString s(wxT("name"));
    const ibString joined = s + wxT(".") + wxT('x');   // ibString all the way — no wxString in between
    EXPECT_TRUE(joined == wxT("name.x"));
    EXPECT_TRUE(wxT("name") == s);
    ibString built;
    built << wxT("n=") << 42 << wxT(' ') << 1.5;
    EXPECT_TRUE(built.ToWxString() == (wxString() << wxT("n=") << 42 << wxT(' ') << 1.5));
}

// ---------------------------------------------------------------------------
// Format — wxString::Format parity, with no wxString inside
// ---------------------------------------------------------------------------

TEST(IbString, FormatParityWithWxString) {
    EXPECT_TRUE(ibString::Format(wxT("%s=%d, %.2f%%"), ibString(wxT("rate")), 7, 3.14159).ToWxString()
        == wxString::Format(wxT("%s=%d, %.2f%%"), wxT("rate"), 7, 3.14159));
    EXPECT_TRUE(ibString::Format(wxT("[%5s|%-3d|%c]"), ibString(wxT("ab")), 7, wxT('z')).ToWxString()
        == wxT("[   ab|7  |z]"));
}

TEST(IbString, FormatReadsTheWidthTheArgumentHas) {
    // `%d` given a 64-bit count and `%ld` given an int both print the number — the argument decides.
    const long long big = 1LL << 40;
    EXPECT_TRUE(ibString::Format(wxT("%d|%ld|%zu"), big, 7, size_t(9)).ToWxString() == wxT("1099511627776|7|9"));
}

TEST(IbString, FormatKeepsCyrillic) {
    EXPECT_TRUE(ibString::Format(wxT("<%s>"), ibString(WxRu())).ToWxString() == wxT("<") + WxRu() + wxT(">"));
}

TEST(IbString, FormatGrowsPastItsFirstGuess) {
    const ibString longText(wxString(wxT('x'), 5000));
    EXPECT_EQ(ibString::Format(wxT("%s!"), longText).Len(), 5001u);
}

TEST(IbString, FormatRefusesArgumentsTheFormatDoesNotAnswer) {
    const wxAssertHandler_t previous = wxSetAssertHandler(nullptr);   // the refusal asserts; the answer is what is tested
    EXPECT_TRUE(ibString::Format(wxT("%d"), ibString(wxT("x"))).ToWxString() == wxT("%d"));
    EXPECT_TRUE(ibString::Format(wxT("%s %s"), ibString(wxT("x"))).ToWxString() == wxT("%s %s"));
    wxSetAssertHandler(previous);
}

// ---------------------------------------------------------------------------
// Footprint guarantee — one handle on a shared text
// ---------------------------------------------------------------------------

TEST(IbString, SizeofIsOneHandle) {
    EXPECT_EQ(sizeof(ibString), sizeof(void*));
    std::cout << "[ ibString ] sizeof(ibString)=" << sizeof(ibString)
              << "  sizeof(std::wstring)=" << sizeof(std::wstring) << std::endl;
    SUCCEED();
}

// ---------------------------------------------------------------------------
// The text is shared — a copy is one more owner, a write gets a text of its own
// ---------------------------------------------------------------------------

TEST(IbString, CopySharesTheText) {
    const ibString a(wxT("a text longer than the short-string buffer"));
    const ibString b(a);
    ibString c;
    c = a;
    EXPECT_EQ(a.wc_str(), b.wc_str());   // the same characters, not equal ones
    EXPECT_EQ(a.wc_str(), c.wc_str());
}

TEST(IbString, EveryWriteGetsItsOwnText) {
    const ibString original(wxT("  Shared Text  "));
    const auto writes = {
        +[](ibString& s) { s += wxT("!"); },
        +[](ibString& s) { s[2] = wxT('x'); },
        +[](ibString& s) { s.SetChar(2, wxT('x')); },
        +[](ibString& s) { s.Replace(wxT("Text"), wxT("Word")); },
        +[](ibString& s) { s.Trim(); },
        +[](ibString& s) { s.MakeUpper(); },
        +[](ibString& s) { s.erase(0, 2); },
        +[](ibString& s) { s.insert(0, wxT(">")); },
        +[](ibString& s) { s.Clear(); },
    };
    for (auto write : writes) {
        ibString copy(original);
        write(copy);
        EXPECT_TRUE(original == wxT("  Shared Text  "));   // the other owner never sees it
        EXPECT_NE(copy.wc_str(), original.wc_str());
    }
}

TEST(IbString, NothingToChangeKeepsTheTextShared) {
    const ibString a(wxT("nothing to trim or replace here"));
    ibString b(a);
    b.Trim();
    b.Trim(false);
    EXPECT_EQ(b.Replace(wxT("absent"), wxT("x")), 0u);
    EXPECT_EQ(a.wc_str(), b.wc_str());
}

TEST(IbString, AppendingToNothingTakesTheOtherText) {
    const ibString a(wxT("the whole of it"));
    ibString r;
    r += a;
    EXPECT_EQ(r.wc_str(), a.wc_str());
    EXPECT_TRUE((ibString() + a) == a);
}

TEST(IbString, ItselfOnBothSides) {
    ibString s(wxT("abc"));
    s = s;
    EXPECT_TRUE(s == wxT("abc"));
    s += s;
    EXPECT_TRUE(s == wxT("abcabc"));
    ibString t(s);
    EXPECT_EQ(t.Replace(wxT("b"), t), 2u);   // `to` is the string being changed
    EXPECT_TRUE(t == wxT("aabcabccaabcabcc"));
    EXPECT_TRUE(s == wxT("abcabc"));
}

TEST(IbString, EmptyHoldsNothing) {
    const ibString e, n(static_cast<const wchar_t*>(nullptr)), z(wxT(""));
    for (const ibString* s : { &e, &n, &z }) {
        EXPECT_TRUE(s->IsEmpty());
        EXPECT_STREQ(s->wc_str(), wxT(""));
    }
}

TEST(IbString, SharedAcrossThreads) {
    const ibString shared(wxT("one text, many threads, each writing to its own copy"));
    std::vector<std::thread> threads;
    std::atomic<int> wrong{ 0 };
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&shared, &wrong] {
            for (int i = 0; i < 20000; ++i) {
                ibString copy(shared);
                if (i % 2 == 0) copy += wxT("!");
                if (!copy.StartsWith(wxT("one text"))) ++wrong;
            }
        });
    }
    for (std::thread& t : threads) t.join();
    EXPECT_EQ(wrong.load(), 0);
    EXPECT_TRUE(shared == wxT("one text, many threads, each writing to its own copy"));
}
