// =============================================================================
// OES Enterprise — the database string converter: UTF-8 both ways
//
// Every text field of every row read, every statement's text and every string
// parameter bound go through ibDatabaseStringConverter. Both directions are now
// written out by hand, each in one pass — exactly the kind of code that is right
// for the text anyone tries and wrong at the edges — so the edges are pinned:
// each width of sequence (1-4 bytes, the 4-byte one a surrogate pair where
// wchar_t is 16 bits), the empty string, that the encoded LENGTH agrees with the
// bytes handed over, and that ill-formed input still comes out the way the
// converter's road always made it — the new road is a shorter way to the same
// answer, never a second opinion about the bytes.
//
// The source stays pure ASCII: inputs are spelled as bytes, expected strings are
// built from code points (Cp), so no assertion depends on this file's encoding.
// =============================================================================

#include <gtest/gtest.h>
#include <cstring>
#include <initializer_list>
#include <string>

#include "backend/databaseLayer/databaseStringConverter.h"

namespace {

wxString Cp(std::initializer_list<int> cps)
{
    wxString s;
    for (int cp : cps) s += wxUniChar(cp);
    return s;
}

wxString Read(const char* bytes)
{
    return ibDatabaseStringConverter::ConvertFromUnicodeStream(bytes);
}

std::string Write(const wxString& text)
{
    const wxCharBuffer bytes = ibDatabaseStringConverter::ConvertToUnicodeStream(text);
    return std::string(bytes.data(), bytes.length());
}

} // namespace

// --- reading: UTF-8 in, a string out ---------------------------------------

TEST(DatabaseStringConverter, AsciiComesThroughAsIs) {
    EXPECT_EQ(Read("Code 42"), wxT("Code 42"));
}

TEST(DatabaseStringConverter, EmptyIsEmpty) {
    EXPECT_TRUE(Read("").IsEmpty());
    EXPECT_TRUE(Write(wxString()).empty());
    EXPECT_EQ(ibDatabaseStringConverter::GetEncodedStreamLength(wxString()), 0u);
}

// A NULL FIELD HAS NO BYTES: SQLite (sqlite3_column_text) and MySQL answer a NULL column with a null
// pointer, and it reads as the empty string, as it always did — the one-pass reader took it straight to
// strlen and brought the process down reading the audit log (2026-09-12).
TEST(DatabaseStringConverter, NullBufferReadsAsEmpty) {
    EXPECT_TRUE(Read(nullptr).IsEmpty());
}

TEST(DatabaseStringConverter, TwoByteSequences) {
    // "Ko" in Cyrillic, then a Ukrainian i — a name as the payroll base spells it.
    EXPECT_EQ(Read("\xD0\x9A\xD0\xBE \xD0\x86"), Cp({ 0x041A, 0x043E, 0x20, 0x0406 }));
}

TEST(DatabaseStringConverter, ThreeByteSequence) {
    EXPECT_EQ(Read("\xE2\x82\xAC" "5"), Cp({ 0x20AC, 0x35 }));   // the euro sign, then a digit
}

TEST(DatabaseStringConverter, FourByteSequenceIsOneCharacter) {
    const wxString s = Read("\xF0\x9F\x98\x80");   // U+1F600, past the BMP
    const wchar_t* w = s.wc_str();
    if (sizeof(wchar_t) == 2) {
        ASSERT_EQ(s.length(), 2u);   // a surrogate pair where wchar_t is sixteen bits
        EXPECT_EQ(static_cast<unsigned>(w[0]), 0xD83Du);
        EXPECT_EQ(static_cast<unsigned>(w[1]), 0xDE00u);
    } else {
        ASSERT_EQ(s.length(), 1u);
        EXPECT_EQ(static_cast<unsigned>(w[0]), 0x1F600u);
    }
}

// Ill-formed input — a cut-off tail, an overlong form, a stray continuation byte — is not decoded
// here: it comes out exactly as the converter's road always made it.
TEST(DatabaseStringConverter, IllFormedInputTakesTheOldRoad) {
    for (const char* bytes : { "\xC3", "\xC0\xAF", "a\x80z" }) {
        wxString old(wxConvUTF8.cMB2WC(bytes));
        if (old == wxEmptyString)
            old << wxString(bytes);
        EXPECT_EQ(Read(bytes), old) << "input of " << std::strlen(bytes) << " bytes";
    }
}

// --- writing: a string in, UTF-8 out ---------------------------------------

TEST(DatabaseStringConverter, WritesEachWidthOfSequence) {
    EXPECT_EQ(Write(wxT("Code 42")), "Code 42");
    EXPECT_EQ(Write(Cp({ 0x041A, 0x043E })), "\xD0\x9A\xD0\xBE");
    EXPECT_EQ(Write(Cp({ 0x20AC })), "\xE2\x82\xAC");
    EXPECT_EQ(Write(Read("\xF0\x9F\x98\x80")), "\xF0\x9F\x98\x80");   // a pair becomes one four-byte character
}

// The length a parameter is bound with and the bytes it is bound from are asked for separately — they
// must agree, byte for byte, or a value is cut short or read past its end.
TEST(DatabaseStringConverter, TheLengthIsTheLengthOfTheBytes) {
    for (const wxString& text : { wxString(wxT("x")), Cp({ 0x041A, 0x043E, 0x20, 0x0406 }),
                                  Cp({ 0x20AC, 0x35 }), Read("\xF0\x9F\x98\x80" "a") }) {
        EXPECT_EQ(ibDatabaseStringConverter::GetEncodedStreamLength(text), Write(text).size());
    }
}

TEST(DatabaseStringConverter, WhatIsWrittenReadsBackTheSame) {
    const wxString text = Cp({ 0x0414, 0x0435, 0x043C, 0x0027, 0x044F, 0x043D, 0x0435, 0x043D, 0x043A, 0x043E, 0x20AC });
    EXPECT_EQ(Read(Write(text).c_str()), text);
}
