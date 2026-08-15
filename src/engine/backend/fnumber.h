#ifndef __NUMBER_T_H__
#define __NUMBER_T_H__

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>
#include <wx/string.h>
#include <wx/buffer.h>

#include "backend/backend.h"  // BACKEND_API

class ibReaderMemory; // backend/fileSystem/fs.h
class ibWriterMemory; // backend/fileSystem/fs.h

// ibNumber — exact-decimal number, 8 bytes total.
//
// Storage is a single 64-bit word with pointer tagging:
//
//   bit  0       : tag (1 = immediate, 0 = heap pointer to BigImpl)
//   bits 16:1    : exp10 (16 bits signed, ±32767)
//   bits 63:17   : mantissa (47 bits signed, ±70 trillion, ~14 decimal digits)
//
// Value = mantissa * 10^exp10. Numbers that fit those bounds use no heap memory
// at all — sizeof(ibNumber) == 8, alignment == 8.
//
// Numbers that don't fit (more than ~14 mantissa digits, |exp| > 32767, or
// arithmetic that overflows the inline range) live on the heap as a BigImpl
// — a sign-magnitude bignum with a std::vector<uint32_t> magnitude that grows
// by demand and a 32-bit exp10. Promotion happens automatically on assignment
// / arithmetic.
//
// Examples:
//   "0"                         — immediate (0, 0)
//   "456"                       — immediate (456, 0)
//   "0.01"                      — immediate (1, -2)
//   "765.3456754567765443343"   — heap BigImpl (mag=7653...3343, e=-19) — 22 digits
//
// Self-contained: no dependency on ttmath. All big-int arithmetic
// (Add/Sub/Mul/Div/Compare, decimal ToString/FromString) lives inside
// number.cpp as schoolbook routines on std::vector<uint32_t> limbs.
//
// Thread safety:
//   - Distinct ibNumber instances are fully independent — operations on
//     different objects from different threads are always safe (no shared
//     global / static state, no hidden cache).
//   - Concurrent **const-only** access to the same instance from multiple
//     threads is safe: every const method (ToString, Compare, IsZero, ToInt,
//     Serialize, etc.) reads `m_payload`, materialises a local BigImpl copy
//     via LoadBig, and never mutates shared state.
//   - Mixing reads and writes — or two writes — to the **same** instance
//     concurrently is **not** safe and requires external synchronisation,
//     mirroring the contract of std::vector / wxString. Use a different
//     instance per worker, or a mutex around the shared one.

class BACKEND_API ibNumber
{
public:
	ibNumber() noexcept;

	// Integer constructors are declared over the LANGUAGE types, not over the <cstdint>
	// aliases, and that is deliberate: `long` and `long long` are DISTINCT types even where
	// they are the same width, and an alias only ever names one of them.
	//
	// With int64_t/uint64_t alone there was a hole. On Windows int64_t IS long long, so every
	// integer had an exact match and nothing showed. On LP64 (Linux, macOS) int64_t is `long`,
	// leaving `long long` — which is what wxLongLong_t and every date value is — matched by
	// nothing: int, long and double all sat equally far away and the conversion was ambiguous.
	// Spelling all four signed/unsigned widths closes it on every platform at once, with no
	// per-callsite casts and no #ifdef, because no two of them collide anywhere.
	ibNumber(int v) noexcept;
	ibNumber(unsigned int v) noexcept;
	ibNumber(long v) noexcept;
	ibNumber(unsigned long v) noexcept;
	ibNumber(long long v) noexcept;
	ibNumber(unsigned long long v) noexcept;
	ibNumber(double v);
	explicit ibNumber(const wxString& s);
	ibNumber(const ibNumber& o);
	ibNumber(ibNumber&& o) noexcept;
	~ibNumber();

	ibNumber& operator=(const ibNumber& o);
	ibNumber& operator=(ibNumber&& o) noexcept;

	// Hot/cold split, same shape as Compare below. The immediate-integer path is
	// a gate plus one int64 op and lands in the header so any caller can inline
	// it; the BigImpl path stays in fnumber.cpp behind AddBig / SubBig / MulBig.
	// Results are bit-identical either way — the fast path only fires when it
	// fits immediate, which is the same condition the cold path would settle on.
	ibNumber& operator+=(const ibNumber& rhs) {
		// 47-bit + 47-bit can't overflow int64; store if the sum fits immediate.
		int64_t am, bm;
		if (TryImmInts(rhs, am, bm)) {
			const int64_t r = am + bm;
			if (CanBeImmediate(r, 0)) { StoreImmediate(r, 0); return *this; }
		}
		return AddBig(rhs);
	}
	ibNumber& operator-=(const ibNumber& rhs) {
		int64_t am, bm;
		if (TryImmInts(rhs, am, bm)) {
			const int64_t r = am - bm;
			if (CanBeImmediate(r, 0)) { StoreImmediate(r, 0); return *this; }
		}
		return SubBig(rhs);
	}
	ibNumber& operator*=(const ibNumber& rhs) {
		// int32-range operands keep the product below 2^62 (no int64 overflow);
		// anything wider falls through to the BigImpl multiply.
		int64_t am, bm;
		if (TryImmInts(rhs, am, bm)
			&& am >= INT32_MIN && am <= INT32_MAX && bm >= INT32_MIN && bm <= INT32_MAX) {
			const int64_t r = am * bm;
			if (CanBeImmediate(r, 0)) { StoreImmediate(r, 0); return *this; }
		}
		return MulBig(rhs);
	}
	ibNumber& operator/=(const ibNumber& rhs);
	ibNumber& operator%=(const ibNumber& rhs);
	ibNumber& operator++();           // pre-increment
	ibNumber  operator++(int);        // post-increment
	ibNumber  operator-() const;

	friend ibNumber operator+(ibNumber a, const ibNumber& b) { a += b; return a; }
	friend ibNumber operator-(ibNumber a, const ibNumber& b) { a -= b; return a; }
	friend ibNumber operator*(ibNumber a, const ibNumber& b) { a *= b; return a; }
	friend ibNumber operator/(ibNumber a, const ibNumber& b) { a /= b; return a; }
	friend ibNumber operator%(ibNumber a, const ibNumber& b) { a %= b; return a; }

	// Hot/cold split. The immediate-integer path is TryImmInts plus one three-way
	// compare, and EVERY loop condition (`i < n`) in every script goes through
	// here; keeping it in the header lets the caller's compiler see it without
	// depending on whole-program view. The BigImpl branch stays out of line —
	// inlining it would put a decimal long-compare into every call site for the
	// rare case. Same shape TryImmInts itself already uses.
	// FORCED, because `inline` was not honoured where it mattered most. The
	// disassembly of ibValue::CompareValueLS showed a real `call` to this
	// function on its number arm — the arm every keyed lookup and every sort of
	// numbers goes through — while the hot/cold split above exists precisely so
	// the caller sees the immediate path. The body forced here is the gate plus
	// one int64 compare; CompareBig stays out of line, so what gets pasted into
	// a caller is small.
	IB_FORCEINLINE int Compare(const ibNumber& rhs) const {
		int64_t am, bm;
		if (TryImmInts(rhs, am, bm))
			return (am > bm) - (am < bm);
		return CompareBig(rhs);
	}

	bool operator==(const ibNumber& r) const { return Compare(r) == 0; }
	bool operator!=(const ibNumber& r) const { return Compare(r) != 0; }
	bool operator<(const ibNumber& r)  const { return Compare(r) <  0; }
	bool operator>(const ibNumber& r)  const { return Compare(r) >  0; }
	bool operator<=(const ibNumber& r) const { return Compare(r) <= 0; }
	bool operator>=(const ibNumber& r) const { return Compare(r) >= 0; }

	bool         IsZero()    const;
	bool         IsHeap()    const { return (m_payload & 1ULL) == 0; }
	bool         IsNan()     const { return false; } // exact decimal — no NaN state
	bool         IsSign()    const; // true if value is negative
	void         ChangeSign();      // flip sign in place
	ibNumber     Abs()       const; // |value|
	void         SetZero();
	void         FromInt(int v);
	int          ToInt()     const; // truncates fractional part; clamped on overflow
	unsigned int ToUInt()    const; // truncates fractional part; clamped on overflow
	// Out-parameter form: 0 on success, 1 on overflow (ttmath-compat). Declared over the
	// LANGUAGE types for the same reason as the constructors above — `long` and `long long`
	// are distinct types, and an int64_t/uint64_t pair only ever names one of the two. A
	// caller holding a wxLongLong_t (that is `long long`) could not bind to an int64_t& on
	// LP64, where int64_t is `long`. Four spellings collide nowhere and bind everywhere.
	//
	// Where the target is NARROWER than 64 bits (Windows `long`), a value that does not fit
	// reports overflow rather than truncating silently — same contract as the 64-bit form.
	int ToInt(long&               out) const;
	int ToInt(long long&          out) const;
	int ToInt(unsigned long&      out) const;
	int ToInt(unsigned long long& out) const;
	int64_t      ToInt64()   const; // throws on overflow / non-integer
	double       ToDouble()  const; // lossy for values outside double's precision
	float        ToFloat()   const; // lossy; truncated double
	wxString     ToString()  const;
	std::wstring ToWString() const;

	// Formatted decimal output. Pass field-by-field; defaults give the same
	// result as the bare ToString() above. Defined below the class.
	struct Format;
	wxString ToString(const Format& fmt) const;

	// Rounding (round-half-away-from-zero).
	ibNumber Round() const;          // to nearest integer
	ibNumber Round(int n) const;     // to n decimal places (n >= 0)
	ibNumber Trunc() const;          // truncate fractional part toward zero

	// Transcendental / power math. Computed on the EXACT decimal tier (~30
	// significant digits, matching operator/'s precision) — NOT through double:
	//   Pow(int)   — exact, repeated multiplication.
	//   Sqrt       — Newton-Raphson (Heron), double-seeded then refined.
	//   Exp        — Taylor series + exp(x)=exp(x/2^n)^(2^n) argument reduction.
	//   Ln         — Newton on f(y)=exp(y)-x (reuses Exp); double-seeded.
	//   Log(base)  — Ln(x)/Ln(base).
	//   Pow(frac)  — exp(n*ln x), x > 0.
	// double is used only to SEED the iterations / for out-of-range fallbacks.
	ibNumber Pow(int n) const;
	ibNumber Pow(const ibNumber& n) const;
	ibNumber Sqrt() const;
	ibNumber Log(const ibNumber& base) const;
	ibNumber Ln() const;
	ibNumber Exp() const;

	// Parses a decimal string ("123", "-1.5", "1.2e3"). Returns false if the
	// input has no digits or is otherwise unparseable; on failure value is
	// reset to zero.
	bool FromString(const wxString& s);

	// 128-bit raw integer extraction / loading. Bytes are little-endian
	// two's-complement. Used by DB layers (e.g. Firebird SQL_INT128). Caller is
	// responsible for ensuring the value is integer (use Round/Trunc or
	// pre-scale via *= 10 if there's a column scale factor).
	void To128Bytes(uint8_t out[16]) const;
	void From128Bytes(const uint8_t bytes[16]);

	// Binary buffer accessor — value-as-bytes, in OES property-getter style
	// (mirrors propertyForm's GetValueAsMemoryBuffer / SetValue pair).
	// Layout (little-endian):
	//   [1 byte ]  sign (0 = positive or zero, 1 = negative)
	//   [4 bytes]  exp10  (int32_t)
	//   [4 bytes]  limb_count (uint32_t)
	//   [4*N bytes] mantissa magnitude limbs (uint32_t each), LSB first
	// Minimum size = 9 bytes. Round-trip exact.
	//
	//   - Returning overload — inline use:
	//         writer.w_chunk(chunkNumber, value.GetBuffer());
	//   - Out-parameter overload — reuses caller's buffer capacity, clears it
	//     first, then writes. For tight loops over many numbers:
	//         wxMemoryBuffer buf;
	//         for (...) { n.GetBuffer(buf); writer.w_chunk(id, buf); }
	wxMemoryBuffer GetBuffer() const;
	void           GetBuffer(wxMemoryBuffer& out) const;
	bool           GetBuffer(ibWriterMemory& writer) const; // chunked stream write

	bool SetBuffer(const wxMemoryBuffer& in);
	bool SetBuffer(const void* data, size_t len);
	bool SetBuffer(const ibReaderMemory& reader); // chunked stream read

	// BigImpl is opaque to callers — fully defined in number.cpp.
	struct BigImpl;

private:
	// Bit packing of m_payload when bit 0 == 1 (immediate):
	//   mantissa: signed 47-bit value in bits [63:17]
	//   exp10   : signed 16-bit value in bits [16:1]
	static constexpr int     kImmMantBits = 47;
	static constexpr int     kImmExpBits  = 16;
	static constexpr int     kImmExpShift = 1;
	static constexpr int     kImmMantShift = kImmExpShift + kImmExpBits; // 17
	static constexpr int64_t kImmMantMax = (1LL << (kImmMantBits - 1)) - 1;       //  +2^46 - 1
	static constexpr int64_t kImmMantMin = -(1LL << (kImmMantBits - 1));          //  -2^46
	static constexpr int     kImmExpMax  = (1   << (kImmExpBits  - 1)) - 1;       //  +32767
	static constexpr int     kImmExpMin  = -(1  << (kImmExpBits  - 1));           //  -32768

	void Clear() noexcept;

	bool     IsImmediate() const { return (m_payload & 1ULL) != 0; }
	// Inline, NOT out-of-line: TryImmInts below calls ImmExp() twice and
	// ImmMantissa() twice, and TryImmInts is the gate of every arithmetic and
	// comparison fast path — so a body left in the .cpp costs four cross-module
	// calls on every number operation the interpreter performs, to run a shift
	// and a mask. Whole-program optimisation removes those calls, but only where
	// it is enabled (MSVC Release); the header form holds on every toolchain and
	// every configuration, which is the point.
	int64_t ImmMantissa() const {
		// Arithmetic right-shift on int64_t sign-extends, restoring the 47-bit
		// signed mantissa to a full int64_t value.
		return static_cast<int64_t>(m_payload) >> kImmMantShift;
	}
	int ImmExp() const {
		const uint64_t mask = (1ULL << kImmExpBits) - 1;
		int64_t e = static_cast<int64_t>((m_payload >> kImmExpShift) & mask);
		if (e & (1LL << (kImmExpBits - 1))) e -= (1LL << kImmExpBits);
		return static_cast<int>(e);
	}

	BigImpl* HeapPtr()     const;   // cold — reached through LoadBig, not the fast path

	// Both operands inline integers (exp10 == 0)? Returns their mantissas in
	// (a, b). Shared gate for the immediate-integer fast paths in the
	// arithmetic / comparison operators (operator+=, *=, /=, Compare, ...).
	bool TryImmInts(const ibNumber& rhs, int64_t& a, int64_t& b) const {
		if (IsImmediate() && rhs.IsImmediate() && ImmExp() == 0 && rhs.ImmExp() == 0) {
			a = ImmMantissa();
			b = rhs.ImmMantissa();
			return true;
		}
		return false;
	}

	// Cold half of Compare — both operands are not plain immediate integers.
	int CompareBig(const ibNumber& rhs) const;

	// Inline for the same reason as ImmMantissa / ImmExp above: they are the
	// STORE half of every arithmetic fast path (operator+= and friends, now
	// defined in this header), so leaving them in the .cpp would put the call
	// back on the path the split exists to shorten.
	static bool CanBeImmediate(int64_t mant, int32_t exp10) noexcept {
		return mant >= kImmMantMin && mant <= kImmMantMax
		    && exp10 >= kImmExpMin && exp10 <= kImmExpMax;
	}
	static uint64_t PackImmediate(int64_t mant, int32_t exp10) noexcept {
		const uint64_t mantU = static_cast<uint64_t>(mant) & ((1ULL << kImmMantBits) - 1);
		const uint64_t expU  = static_cast<uint64_t>(exp10) & ((1ULL << kImmExpBits) - 1);
		return (mantU << kImmMantShift) | (expU << kImmExpShift) | 1ULL;
	}

	void StoreImmediate(int64_t mant, int32_t exp10) noexcept {
		m_payload = PackImmediate(mant, exp10);
	}

	// Cold halves — both operands outside the immediate-integer tier, so the
	// BigImpl machinery (LoadBig / AlignExp / StoreBig) runs. Deliberately left
	// out of line: inlining a decimal long-add into every call site would trade
	// the rare case against the instruction cache the common case lives in.
	ibNumber& AddBig(const ibNumber& rhs);
	ibNumber& SubBig(const ibNumber& rhs);
	ibNumber& MulBig(const ibNumber& rhs);
	void StoreHeap(BigImpl* p) noexcept;

	// One body per signedness, shared by the long / long long constructor pair above.
	void FromSigned64(int64_t v) noexcept;
	void FromUnsigned64(uint64_t v) noexcept;

	// … and the same for the ToInt family: one conversion, four public spellings over it.
	int ToSigned64(int64_t& out) const;
	int ToUnsigned64(uint64_t& out) const;

	// Materialises current value into a stack-allocated BigImpl (no extra heap).
	void LoadBig(BigImpl& out) const;
	// Stores `src`. If it fits inline, packs as immediate; otherwise allocates heap.
	void StoreBig(const BigImpl& src);

	uint64_t m_payload;
};

struct ibNumber::Format
{
	int    fracDigits   = -1;        // round to N decimal places (-1 = no rounding)
	int    precision    = -1;        // cap total significant digits, trim trailing zeros (-1 = off)
	wxChar decimalSep   = wxT('.');  // decimal separator
	wxChar groupSep     = 0;         // thousand-group separator (0 = no grouping)
	int    groupSize    = 0;         // digits per group (0 = no grouping)
	int    minIntDigits = 0;         // pad integer part with leading '0' to this width (0 = off)
	                                 //   sign sits OUTSIDE the padding: -5 with width 4 → "-0005",
	                                 //   not "-005" (printf "%04d" semantics differ here)
};

static_assert(sizeof(uint64_t) == 8, "ibNumber assumes uint64_t is 8 bytes");

// Stream insertion — writes via ToString(); used by code that builds messages
// or query fragments through std::stringstream.
BACKEND_API std::ostream&  operator<<(std::ostream&  os, const ibNumber& n);
BACKEND_API std::wostream& operator<<(std::wostream& os, const ibNumber& n);

#endif
