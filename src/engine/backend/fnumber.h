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
	ibNumber(int v) noexcept;
	ibNumber(unsigned int v) noexcept;
	ibNumber(int64_t v) noexcept;
	ibNumber(uint64_t v) noexcept;
	ibNumber(double v);
	explicit ibNumber(const wxString& s);
	ibNumber(const ibNumber& o);
	ibNumber(ibNumber&& o) noexcept;
	~ibNumber();

	ibNumber& operator=(const ibNumber& o);
	ibNumber& operator=(ibNumber&& o) noexcept;

	ibNumber& operator+=(const ibNumber& rhs);
	ibNumber& operator-=(const ibNumber& rhs);
	ibNumber& operator*=(const ibNumber& rhs);
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

	int  Compare(const ibNumber& rhs) const;

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
	int          ToInt(int64_t&  out) const; // ttmath-compat: 0 on success, 1 on overflow
	int          ToInt(uint64_t& out) const; // unsigned variant; 1 on overflow / negative
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
	int64_t  ImmMantissa() const;
	int      ImmExp()      const;
	BigImpl* HeapPtr()     const;

	static bool     CanBeImmediate(int64_t mant, int32_t exp10) noexcept;
	static uint64_t PackImmediate(int64_t mant, int32_t exp10) noexcept;

	void StoreImmediate(int64_t mant, int32_t exp10) noexcept;
	void StoreHeap(BigImpl* p) noexcept;

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
