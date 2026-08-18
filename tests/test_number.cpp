// =============================================================================
// OES Enterprise — ibNumber tests
//
// Tests the lazy-grow exact-decimal ibNumber class from
// src/engine/backend/number.h. Covers layout, parser, ToString round-trips,
// arithmetic, comparisons across tiers, rounding/truncation, conversions,
// math compat (Pow/Sqrt/Abs/IsSign), buffer (Get/SetBuffer) round-trips,
// 128-bit raw integer round-trips, and stream operators.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/fnumber.h"

#include <climits>
#include <sstream>

// ===========================================================================
// Layout
// ===========================================================================

TEST(NumberLayout, SizeofIs8) {
    EXPECT_EQ(sizeof(ibNumber), 8u);
}

TEST(NumberLayout, AlignofIs8) {
    EXPECT_EQ(alignof(ibNumber), 8u);
}

// ===========================================================================
// Constructors and immediate/heap split
// ===========================================================================

TEST(NumberCtor, DefaultIsZero) {
    ibNumber z;
    EXPECT_TRUE(z.IsZero());
    EXPECT_FALSE(z.IsHeap());
}

TEST(NumberCtor, IntPositiveImmediate) {
    ibNumber n(456);
    EXPECT_FALSE(n.IsHeap());
    EXPECT_EQ(n.ToString(), wxT("456"));
}

TEST(NumberCtor, IntNegative) {
    ibNumber n(-1234567);
    EXPECT_EQ(n.ToString(), wxT("-1234567"));
    EXPECT_TRUE(n.IsSign());
}

TEST(NumberCtor, UnsignedIntMax) {
    ibNumber n(static_cast<unsigned int>(4294967295u));
    EXPECT_EQ(n.ToString(), wxT("4294967295"));
}

TEST(NumberCtor, Int64Min) {
    ibNumber n(static_cast<int64_t>(INT64_MIN));
    EXPECT_EQ(n.ToString(), wxT("-9223372036854775808"));
    EXPECT_TRUE(n.IsHeap());
}

TEST(NumberCtor, Int64Max) {
    ibNumber n(static_cast<int64_t>(INT64_MAX));
    EXPECT_EQ(n.ToString(), wxT("9223372036854775807"));
    EXPECT_TRUE(n.IsHeap());
}

TEST(NumberCtor, Uint64Max) {
    ibNumber n(static_cast<uint64_t>(0xFFFFFFFFFFFFFFFFULL));
    EXPECT_EQ(n.ToString(), wxT("18446744073709551615"));
}

TEST(NumberCtor, ImmediateBoundaries) {
    const int64_t kImmMax = (1LL << 46) - 1;
    const int64_t kImmMin = -(1LL << 46);
    EXPECT_FALSE(ibNumber(kImmMax).IsHeap());
    EXPECT_FALSE(ibNumber(kImmMin).IsHeap());

    ibNumber overflowed(kImmMax + 1);
    EXPECT_TRUE(overflowed.IsHeap());
    EXPECT_EQ(overflowed.ToString(), wxT("70368744177664"));
}

// ===========================================================================
// Parser edge cases
// ===========================================================================

TEST(NumberParser, EmptyStringFails) {
    ibNumber n;
    EXPECT_FALSE(n.FromString(wxEmptyString));
    EXPECT_TRUE(n.IsZero());
}

TEST(NumberParser, MinusOnlyFails) {
    ibNumber n;
    EXPECT_FALSE(n.FromString(wxString(wxT("-"))));
}

TEST(NumberParser, DotOnlyFails) {
    ibNumber n;
    EXPECT_FALSE(n.FromString(wxString(wxT("."))));
}

TEST(NumberParser, GarbageFails) {
    ibNumber n;
    EXPECT_FALSE(n.FromString(wxString(wxT("abc"))));
}

TEST(NumberParser, PlusPrefixOk) {
    ibNumber n;
    EXPECT_TRUE(n.FromString(wxString(wxT("+5"))));
    EXPECT_EQ(n, ibNumber(5));
}

TEST(NumberParser, WhitespaceTolerated) {
    ibNumber n;
    EXPECT_TRUE(n.FromString(wxString(wxT("  42 "))));
    EXPECT_EQ(n, ibNumber(42));
}

TEST(NumberParser, ScientificPositive) {
    ibNumber a(wxString(wxT("1.5e2")));
    EXPECT_EQ(a, ibNumber(150));
}

TEST(NumberParser, ScientificNegativeExp) {
    ibNumber b(wxString(wxT("1.5e-2")));
    EXPECT_EQ(b, ibNumber(wxString(wxT("0.015"))));
}

TEST(NumberParser, GarbageTailStopsAtDigits) {
    ibNumber n;
    EXPECT_TRUE(n.FromString(wxString(wxT("12x"))));
    EXPECT_EQ(n, ibNumber(12));
}

// ===========================================================================
// ToString / round-trip
// ===========================================================================

TEST(NumberToString, Zero) {
    EXPECT_EQ(ibNumber(0).ToString(), wxT("0"));
}

TEST(NumberToString, SmallFraction) {
    EXPECT_EQ(ibNumber(wxString(wxT("0.01"))).ToString(), wxT("0.01"));
}

TEST(NumberToString, NegativeFraction) {
    EXPECT_EQ(ibNumber(wxString(wxT("-0.0001"))).ToString(), wxT("-0.0001"));
}

TEST(NumberToString, TenToEighteen) {
    ibNumber n(wxString(wxT("1000000000000000000")));
    EXPECT_EQ(n.ToString(), wxT("1000000000000000000"));
}

TEST(NumberToString, HeapTier22Digits) {
    const wxString s = wxT("765.3456754567765443343");
    ibNumber n(s);
    EXPECT_TRUE(n.IsHeap());
    EXPECT_EQ(n.ToString(), s);
}

TEST(NumberToString, TwoHundredDigitsRoundTrip) {
    wxString s; s.reserve(202);
    for (int i = 0; i < 100; ++i) s += wxChar(wxT('1') + (i % 9));
    s += wxT('.');
    for (int i = 0; i < 100; ++i) s += wxChar(wxT('0') + ((i + 3) % 10));
    ibNumber n(s);
    EXPECT_TRUE(n.IsHeap());
    EXPECT_EQ(n.ToString(), s);
}

TEST(NumberToString, NegZeroCanonical) {
    ibNumber n(wxString(wxT("-0")));
    EXPECT_EQ(n.ToString(), wxT("0"));
    EXPECT_FALSE(n.IsSign());
}

TEST(NumberToString, LeadingZerosDropped) {
    ibNumber n(wxString(wxT("00123")));
    EXPECT_EQ(n.ToString(), wxT("123"));
}

// ===========================================================================
// Arithmetic
// ===========================================================================

TEST(NumberArith, ExactDecimalAdd) {
    ibNumber a(wxString(wxT("0.1")));
    ibNumber b(wxString(wxT("0.2")));
    EXPECT_EQ(a + b, ibNumber(wxString(wxT("0.3"))));
}

TEST(NumberArith, ZeroAdd) {
    EXPECT_TRUE((ibNumber(0) + ibNumber(0)).IsZero());
    EXPECT_TRUE((ibNumber(0) - ibNumber(0)).IsZero());
    EXPECT_TRUE((ibNumber(0) * ibNumber(0)).IsZero());
}

TEST(NumberArith, MulSignRules) {
    EXPECT_EQ(ibNumber(-3) * ibNumber(-4), ibNumber(12));
    EXPECT_EQ(ibNumber(-3) * ibNumber(4),  ibNumber(-12));
    EXPECT_EQ(ibNumber(3)  * ibNumber(-4), ibNumber(-12));
    EXPECT_TRUE((ibNumber(0) * ibNumber(-7)).IsZero());
}

TEST(NumberArith, CarryAcrossLimb) {
    // 2^32-1 + 1 = 2^32 — exercises carry from limb 0 into limb 1
    ibNumber a(wxString(wxT("4294967295")));
    EXPECT_EQ(a + ibNumber(1), ibNumber(wxString(wxT("4294967296"))));
}

TEST(NumberArith, MultiLimbAdd) {
    // 2^64 + 2^64 = 2 * 2^64 — multi-limb carry
    ibNumber a(wxString(wxT("18446744073709551616")));
    EXPECT_EQ(a + a, ibNumber(wxString(wxT("36893488147419103232"))));
}

TEST(NumberArith, BigSelfSubIsZero) {
    ibNumber a(wxString(wxT("123456789012345")));
    EXPECT_TRUE((a - a).IsZero());
}

TEST(NumberArith, ExactProduct) {
    EXPECT_EQ(ibNumber(wxString(wxT("12.5"))) * ibNumber(8), ibNumber(100));
}

TEST(NumberArith, OneThirdHasManyDigits) {
    ibNumber c = ibNumber(1) / ibNumber(3);
    EXPECT_TRUE(c.ToString().StartsWith(wxT("0.3333333333")));
}

// --- the DIVISION RULE: dividend's digits + kDivExtraDigits, last digit rounded, trailing zeros
// --- trimmed, kMaxDivFracDigits as the stop for a chain. An endless fraction has to be stopped
// --- SOMEWHERE, and where it stops must not depend on how the operands happen to be written.

TEST(NumberDivision, IntegerOperandsGetTheFixedRoom) {
    // 0 fractional digits in -> exactly kDivExtraDigits out.
    const ibNumber c = ibNumber(1) / ibNumber(3);
    const wxString text = c.ToString();
    ASSERT_TRUE(text.StartsWith(wxT("0.")));
    EXPECT_EQ(text.length() - 2, static_cast<size_t>(ibNumber::kDivExtraDigits));
}

TEST(NumberDivision, RoomIsAddedToTheDividendsOwnLength) {
    // A dividend carrying 4 fractional digits keeps them and gains the room on top.
    const ibNumber c = ibNumber(wxString(wxT("0.1234"))) / ibNumber(3);
    const wxString text = c.ToString();
    ASSERT_TRUE(text.StartsWith(wxT("0.")));
    EXPECT_EQ(text.length() - 2, static_cast<size_t>(4 + ibNumber::kDivExtraDigits));
}

TEST(NumberDivision, LastDigitIsRoundedNotTruncated) {
    // 2/3 = 0.666…67, never 0.666…66 — truncation biases every proportion downwards.
    const wxString text = (ibNumber(2) / ibNumber(3)).ToString();
    EXPECT_TRUE(text.EndsWith(wxT("7")));
}

TEST(NumberDivision, ExactQuotientKeepsNoTrailingZeros) {
    // 1/8 is 0.125 — not 0.125 followed by however many zeros the rule asked for. The next division
    // measures ITS room from this length, so the zeros would compound.
    EXPECT_EQ((ibNumber(1) / ibNumber(8)).ToString(), wxString(wxT("0.125")));
}

TEST(NumberDivision, EqualOperandsWrittenDifferentlyDivideEqually) {
    // 0.10 and 0.1000000 are one number in two spellings: measured on the written form they would
    // produce quotients of different length, which are then not equal to each other.
    const ibNumber a = ibNumber(wxString(wxT("0.10")))      / ibNumber(3);
    const ibNumber b = ibNumber(wxString(wxT("0.1000000"))) / ibNumber(3);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a.ToString(), b.ToString());
}

TEST(NumberDivision, DivisorLengthDoesNotChangeTheQuotientsLength) {
    // The room is the DIVIDEND's, so a long divisor must not shorten (or lengthen) the answer.
    const wxString viaShort = (ibNumber(1) / ibNumber(wxString(wxT("3")))).ToString();
    const wxString viaLong  = (ibNumber(1) / ibNumber(wxString(wxT("3.0000000000")))).ToString();
    EXPECT_EQ(viaShort.length(), viaLong.length());
}

TEST(NumberDivision, ChainStopsAtTheCeiling) {
    // Each division adds its room; the ceiling is what keeps a chain from climbing forever. It never
    // shortens the input — only declines to invent more digits.
    ibNumber v(1);
    for (int i = 0; i < 40; ++i)
        v = v / ibNumber(3);
    const wxString text = v.ToString();
    const size_t point = text.Find(wxT('.'));
    ASSERT_NE(point, wxString::npos);
    EXPECT_LE(text.length() - point - 1, static_cast<size_t>(ibNumber::kMaxDivFracDigits));
}

TEST(NumberDivision, QuotientAndRemainderReconstructTheDividend) {
    // What a division drops is the REMAINDER, and `%` returns it exactly — the pair still adds up.
    const ibNumber a(17), b(5);
    EXPECT_EQ((a / b).Trunc() * b + (a % b), a);
}

TEST(NumberArith, DivByZeroThrows) {
    EXPECT_THROW({ ibNumber c = ibNumber(5) / ibNumber(0); (void)c; },
                 std::runtime_error);
}

TEST(NumberArith, Modulo) {
    EXPECT_EQ(ibNumber(10) % ibNumber(3),  ibNumber(1));
    EXPECT_EQ(ibNumber(-10) % ibNumber(3), ibNumber(-1));
}

TEST(NumberArith, PreIncrement) {
    ibNumber a(5);
    EXPECT_EQ(++a, ibNumber(6));
    EXPECT_EQ(a, ibNumber(6));
}

TEST(NumberArith, PostIncrement) {
    ibNumber b(5);
    EXPECT_EQ(b++, ibNumber(5));
    EXPECT_EQ(b, ibNumber(6));
}

// ===========================================================================
// Compare across tiers
// ===========================================================================

TEST(NumberCompare, SmallVsBigPositive) {
    ibNumber tiny(5);
    ibNumber bigPos(wxString(wxT("123456789012345678901234567890")));
    EXPECT_LT(tiny, bigPos);
    EXPECT_GT(bigPos, tiny);
}

TEST(NumberCompare, BigNegativeOrdering) {
    ibNumber tiny(5);
    ibNumber bigPos(wxString(wxT("123456789012345678901234567890")));
    ibNumber bigNeg(wxString(wxT("-123456789012345678901234567890")));
    EXPECT_LT(bigNeg, tiny);
    EXPECT_LT(bigNeg, bigPos);
    EXPECT_EQ(-bigPos, bigNeg);
}

TEST(NumberCompare, ExpNormalize) {
    EXPECT_EQ(ibNumber(1), ibNumber(wxString(wxT("1.0"))));
}

TEST(NumberCompare, ZeroEqualsNegZero) {
    EXPECT_EQ(ibNumber(0), ibNumber(wxString(wxT("-0"))));
}

// ===========================================================================
// Rounding
// ===========================================================================

TEST(NumberRound, HalfAwayFromZero) {
    EXPECT_EQ(ibNumber(wxString(wxT("1.5"))).Round(),  ibNumber(2));
    EXPECT_EQ(ibNumber(wxString(wxT("2.5"))).Round(),  ibNumber(3));
    EXPECT_EQ(ibNumber(wxString(wxT("-1.5"))).Round(), ibNumber(-2));
}

TEST(NumberRound, BelowHalf) {
    EXPECT_TRUE(ibNumber(wxString(wxT("0.4"))).Round().IsZero());
}

TEST(NumberRound, AboveHalf) {
    EXPECT_EQ(ibNumber(wxString(wxT("0.6"))).Round(), ibNumber(1));
}

TEST(NumberRound, RoundToNDigits) {
    EXPECT_EQ(ibNumber(wxString(wxT("1.234"))).Round(2),
              ibNumber(wxString(wxT("1.23"))));
    EXPECT_EQ(ibNumber(wxString(wxT("1.235"))).Round(2),
              ibNumber(wxString(wxT("1.24"))));
}

TEST(NumberRound, Trunc) {
    EXPECT_EQ(ibNumber(wxString(wxT("1.99"))).Trunc(),  ibNumber(1));
    EXPECT_EQ(ibNumber(wxString(wxT("-1.99"))).Trunc(), ibNumber(-1));
    EXPECT_TRUE(ibNumber(wxString(wxT("0.5"))).Trunc().IsZero());
}

// ===========================================================================
// Conversions
// ===========================================================================

TEST(NumberConv, ToInt) {
    EXPECT_EQ(ibNumber(456).ToInt(),  456);
    EXPECT_EQ(ibNumber(-456).ToInt(), -456);
    EXPECT_EQ(ibNumber(wxString(wxT("1.99"))).ToInt(), 1); // truncated
    EXPECT_EQ(ibNumber(static_cast<int64_t>(INT64_MAX)).ToInt(), INT_MAX);
}

TEST(NumberConv, ToUInt) {
    EXPECT_EQ(ibNumber(-5).ToUInt(),  0u);
    EXPECT_EQ(ibNumber(456).ToUInt(), 456u);
}

TEST(NumberConv, ToInt64Out) {
    int64_t out = -1;
    EXPECT_EQ(ibNumber(static_cast<int64_t>(INT64_MAX)).ToInt(out), 0);
    EXPECT_EQ(out, INT64_MAX);

    EXPECT_NE(ibNumber(wxString(wxT("99999999999999999999"))).ToInt(out), 0);
}

TEST(NumberConv, ToDouble) {
    double d = ibNumber(wxString(wxT("3.14"))).ToDouble();
    EXPECT_GT(d, 3.139);
    EXPECT_LT(d, 3.141);
}

TEST(NumberConv, ToFloat) {
    EXPECT_EQ(ibNumber(wxString(wxT("2.5"))).ToFloat(), 2.5f);
}

// ===========================================================================
// Math compat
// ===========================================================================

TEST(NumberMath, PowSmallInt) {
    EXPECT_EQ(ibNumber(2).Pow(10), ibNumber(1024));
    EXPECT_EQ(ibNumber(2).Pow(32),
              ibNumber(static_cast<int64_t>(4294967296LL)));
}

TEST(NumberMath, PowZero) {
    EXPECT_EQ(ibNumber(7).Pow(0), ibNumber(1));
}

TEST(NumberMath, PowNegativeExponent) {
    EXPECT_EQ(ibNumber(4).Pow(-2), ibNumber(wxString(wxT("0.0625"))));
}

TEST(NumberMath, SqrtApproxFour) {
    ibNumber s = ibNumber(16).Sqrt();
    EXPECT_LT((s - ibNumber(4)).Abs(), ibNumber(wxString(wxT("0.01"))));
}

// --- High-precision transcendentals -----------------------------------------
// Restored on the exact decimal tier after the ttmath removal had shortcut
// Sqrt/Ln/Exp/Log/Pow to plain double. The known-constant checks use a 1e-18
// tolerance: passing it means >18 correct digits, which double's ~15-16 could
// never deliver — proof the result is no longer double-capped.

TEST(NumberMath, SqrtTwoBeyondDouble) {
    ibNumber s = ibNumber(2).Sqrt();
    ibNumber known(wxString(wxT("1.4142135623730950488")));   // 19 digits after the point
    EXPECT_LT((s - known).Abs(), ibNumber(wxString(wxT("1e-18"))));
}

TEST(NumberMath, SqrtSquaredRecoversInput) {
    ibNumber s = ibNumber(2).Sqrt();
    EXPECT_LT((s * s - ibNumber(2)).Abs(), ibNumber(wxString(wxT("1e-26"))));
}

TEST(NumberMath, SqrtPerfectSquareExact) {
    EXPECT_EQ(ibNumber(144).Sqrt(), ibNumber(12));
    EXPECT_EQ(ibNumber(0).Sqrt(),   ibNumber(0));
}

TEST(NumberMath, ExpOneIsEBeyondDouble) {
    ibNumber e = ibNumber(1).Exp();
    ibNumber known(wxString(wxT("2.7182818284590452353")));   // e, 19 digits after the point
    EXPECT_LT((e - known).Abs(), ibNumber(wxString(wxT("1e-18"))));
}

TEST(NumberMath, ExpZeroIsOne) {
    EXPECT_EQ(ibNumber(0).Exp(), ibNumber(1));
}

TEST(NumberMath, LnTwoBeyondDouble) {
    ibNumber l = ibNumber(2).Ln();
    ibNumber known(wxString(wxT("0.6931471805599453094")));   // ln 2, 19 digits after the point
    EXPECT_LT((l - known).Abs(), ibNumber(wxString(wxT("1e-18"))));
}

TEST(NumberMath, LnOneIsZero) {
    EXPECT_EQ(ibNumber(1).Ln(), ibNumber(0));
}

TEST(NumberMath, LnNonPositiveGuarded) {
    EXPECT_EQ(ibNumber(0).Ln(),  ibNumber(0));
    EXPECT_EQ(ibNumber(-5).Ln(), ibNumber(0));
}

TEST(NumberMath, ExpLnRoundTrip) {           // ln(exp(x)) == x
    ibNumber x(wxString(wxT("12.345")));
    EXPECT_LT((x.Exp().Ln() - x).Abs(), ibNumber(wxString(wxT("1e-22"))));
}

TEST(NumberMath, LnExpRoundTrip) {           // exp(ln(x)) == x
    ibNumber x(wxString(wxT("3.5")));
    EXPECT_LT((x.Ln().Exp() - x).Abs(), ibNumber(wxString(wxT("1e-22"))));
}

TEST(NumberMath, Log10OfThousandIsThree) {
    EXPECT_LT((ibNumber(1000).Log(ibNumber(10)) - ibNumber(3)).Abs(),
              ibNumber(wxString(wxT("1e-22"))));
}

TEST(NumberMath, PowHalfEqualsSqrt) {        // 2^0.5 == sqrt(2)
    ibNumber p = ibNumber(2).Pow(ibNumber(wxString(wxT("0.5"))));
    EXPECT_LT((p - ibNumber(2).Sqrt()).Abs(), ibNumber(wxString(wxT("1e-22"))));
}

TEST(NumberMath, AbsAndSign) {
    EXPECT_EQ(ibNumber(wxString(wxT("-7.5"))).Abs(),
              ibNumber(wxString(wxT("7.5"))));

    ibNumber b(3);
    b.ChangeSign();
    EXPECT_EQ(b, ibNumber(-3));

    EXPECT_TRUE(ibNumber(-3).IsSign());
    EXPECT_FALSE(ibNumber(0).IsSign());
    EXPECT_FALSE(ibNumber(3).IsSign());
}

// ===========================================================================
// Get/SetBuffer (binary serialisation)
// ===========================================================================

TEST(NumberBuffer, ZeroEncodesEmpty) {
    // Zero is encoded as an EMPTY buffer (compact zero-encoding, fnumber.cpp
    // GetBuffer) — no 9-byte header; SetBuffer recovers zero from len == 0.
    wxMemoryBuffer blob = ibNumber().GetBuffer();
    EXPECT_EQ(blob.GetDataLen(), 0u);
}

TEST(NumberBuffer, RoundTripZero) {
    wxMemoryBuffer blob = ibNumber().GetBuffer();
    ibNumber b(7);
    EXPECT_TRUE(b.SetBuffer(blob));
    EXPECT_TRUE(b.IsZero());
}

TEST(NumberBuffer, RoundTripFractional) {
    ibNumber a(wxString(wxT("-7654321.0987654321")));
    wxMemoryBuffer blob = a.GetBuffer();
    ibNumber b;
    EXPECT_TRUE(b.SetBuffer(blob));
    EXPECT_EQ(b, a);
}

TEST(NumberBuffer, RoundTripFiftyDigits) {
    const wxString s = wxT("12345678901234567890123456789012345678901234567890");
    ibNumber a(s);
    wxMemoryBuffer blob = a.GetBuffer();
    ibNumber b;
    b.SetBuffer(blob);
    EXPECT_EQ(b, a);
}

TEST(NumberBuffer, NullPointerRejected) {
    ibNumber b(7);
    // len == 0 is the compact zero-encoding: (any ptr, 0) -> zero, returns true.
    EXPECT_TRUE(b.SetBuffer(nullptr, 0));
    EXPECT_TRUE(b.IsZero());
    // A non-zero length with a null pointer is malformed and must be rejected.
    EXPECT_FALSE(b.SetBuffer(nullptr, 5));
}

TEST(NumberBuffer, OutParamReuseOverwrites) {
    wxMemoryBuffer mb;
    ibNumber(42).GetBuffer(mb);
    const size_t firstLen = mb.GetDataLen();

    ibNumber(100).GetBuffer(mb);
    EXPECT_EQ(mb.GetDataLen(), firstLen);  // overwrite, not append

    ibNumber check;
    check.SetBuffer(mb);
    EXPECT_EQ(check, ibNumber(100));
}

// ===========================================================================
// 128-byte raw integer
// ===========================================================================

namespace {
    wxString Hex16(const uint8_t buf[16]) {
        wxString s;
        for (int i = 15; i >= 0; --i) s += wxString::Format(wxT("%02X"), buf[i]);
        return s;
    }
}

TEST(Number128, ZeroAllZeroBytes) {
    uint8_t buf[16] = {0};
    ibNumber(0).To128Bytes(buf);
    EXPECT_EQ(Hex16(buf), wxT("00000000000000000000000000000000"));
}

TEST(Number128, MinusOneAllFFBytes) {
    uint8_t buf[16];
    ibNumber a(static_cast<int64_t>(-1));
    a.To128Bytes(buf);
    EXPECT_EQ(Hex16(buf), wxT("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"));

    ibNumber b;
    b.From128Bytes(buf);
    EXPECT_EQ(b, a);
}

TEST(Number128, ThirtyDigitPositiveRoundTrip) {
    ibNumber a(wxString(wxT("123456789012345678901234567890")));
    uint8_t buf[16];
    a.To128Bytes(buf);
    ibNumber b;
    b.From128Bytes(buf);
    EXPECT_EQ(b, a);
}

TEST(Number128, ThirtyDigitNegativeRoundTrip) {
    ibNumber a(wxString(wxT("-123456789012345678901234567890")));
    uint8_t buf[16];
    a.To128Bytes(buf);
    ibNumber b;
    b.From128Bytes(buf);
    EXPECT_EQ(b, a);
}

// ===========================================================================
// Stream operators
// ===========================================================================

TEST(NumberStream, OstreamFraction) {
    std::stringstream ss;
    ss << ibNumber(wxString(wxT("3.14")));
    EXPECT_EQ(ss.str(), "3.14");
}

TEST(NumberStream, OstreamNegativeInt) {
    std::stringstream ss;
    ss << ibNumber(-42);
    EXPECT_EQ(ss.str(), "-42");
}

// -------------------------------------------------------------------- Format

TEST(NumberFormat, DefaultsMatchBareToString) {
    ibNumber n(wxString(wxT("12345.678")));
    ibNumber::Format fmt;
    EXPECT_EQ(n.ToString(fmt), n.ToString());
}

TEST(NumberFormat, FracDigitsRounds) {
    ibNumber n(wxString(wxT("1.23456")));
    ibNumber::Format fmt; fmt.fracDigits = 2;
    EXPECT_EQ(n.ToString(fmt), wxT("1.23"));
}

TEST(NumberFormat, GroupSepThousands) {
    ibNumber n(1234567);
    ibNumber::Format fmt; fmt.groupSep = wxT(' '); fmt.groupSize = 3;
    EXPECT_EQ(n.ToString(fmt), wxT("1 234 567"));
}

// minIntDigits — leading-zero pad on the integer part.

TEST(NumberFormat, PadSmallNumberToFour) {
    ibNumber n(7);
    ibNumber::Format fmt; fmt.minIntDigits = 4;
    EXPECT_EQ(n.ToString(fmt), wxT("0007"));
}

TEST(NumberFormat, PadZeroToFour) {
    ibNumber n;
    ibNumber::Format fmt; fmt.minIntDigits = 4;
    EXPECT_EQ(n.ToString(fmt), wxT("0000"));
}

TEST(NumberFormat, PadEqualToActualNoChange) {
    ibNumber n(1234);
    ibNumber::Format fmt; fmt.minIntDigits = 4;
    EXPECT_EQ(n.ToString(fmt), wxT("1234"));
}

TEST(NumberFormat, PadShorterThanActualDoesNotTruncate) {
    ibNumber n(98765);
    ibNumber::Format fmt; fmt.minIntDigits = 3;
    EXPECT_EQ(n.ToString(fmt), wxT("98765"));
}

TEST(NumberFormat, PadOffWhenZero) {
    ibNumber n(7);
    ibNumber::Format fmt; fmt.minIntDigits = 0;
    EXPECT_EQ(n.ToString(fmt), wxT("7"));
}

TEST(NumberFormat, PadNegativeSignOutside) {
    // sign sits OUTSIDE the padding (printf "%04d" semantics differ here)
    ibNumber n(-5);
    ibNumber::Format fmt; fmt.minIntDigits = 4;
    EXPECT_EQ(n.ToString(fmt), wxT("-0005"));
}

TEST(NumberFormat, PadHeapTierBigCounter) {
    // bigger than int64 — printf-route would have overflowed; ibNumber path
    // stays exact.
    ibNumber n(wxString(wxT("99999999999999999999")));   // 20 digits
    ibNumber::Format fmt; fmt.minIntDigits = 24;
    EXPECT_EQ(n.ToString(fmt), wxT("000099999999999999999999"));
}

TEST(NumberFormat, PadCombinedWithFracDigits) {
    ibNumber n(wxString(wxT("3.14159")));
    ibNumber::Format fmt; fmt.minIntDigits = 4; fmt.fracDigits = 2;
    EXPECT_EQ(n.ToString(fmt), wxT("0003.14"));
}

TEST(NumberFormat, PadCombinedWithGroups) {
    // padding zeros count as digits for separator placement
    ibNumber n(7);
    ibNumber::Format fmt;
    fmt.minIntDigits = 7;
    fmt.groupSep = wxT(','); fmt.groupSize = 3;
    EXPECT_EQ(n.ToString(fmt), wxT("0,000,007"));
}
