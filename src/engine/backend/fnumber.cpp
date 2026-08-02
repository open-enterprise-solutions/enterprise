#include <limits>
#include "backend/fnumber.h"

#include "backend/fileSystem/fs.h" // ibReaderMemory / ibWriterMemory

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

// Carry/borrow primitives.
//
// On MSVC targeting x86/x64 we use the documented _addcarry_u32 / _subborrow_u32
// intrinsics from <intrin.h>, which compile to single ADC/SBB instructions.
//
// On clang/gcc — x86 or otherwise — the portable 64-bit-wide fallback is used.
// On x86 those compilers reliably pattern-match it back to ADC/SBB; on ARM /
// AArch64 (Linux servers, Apple Silicon) it stays a 64-bit add+shift, which is
// still single-cycle on modern cores. Either way the surface stays portable
// — no platform-specific headers leak past this block.
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#  include <intrin.h>
#  define IB_USE_MSVC_CARRY_INTRIN 1
#endif

namespace
{
	inline unsigned char ibAddCarry32(unsigned char c_in, uint32_t a, uint32_t b, uint32_t* out)
	{
#if defined(IB_USE_MSVC_CARRY_INTRIN)
		return _addcarry_u32(c_in, a, b, out);
#else
		const uint64_t s = static_cast<uint64_t>(a) + b + c_in;
		*out = static_cast<uint32_t>(s);
		return static_cast<unsigned char>((s >> 32) & 1u);
#endif
	}

	inline unsigned char ibSubBorrow32(unsigned char b_in, uint32_t a, uint32_t b, uint32_t* out)
	{
#if defined(IB_USE_MSVC_CARRY_INTRIN)
		return _subborrow_u32(b_in, a, b, out);
#else
		const uint64_t d = static_cast<uint64_t>(a) - b - b_in;
		*out = static_cast<uint32_t>(d);
		return static_cast<unsigned char>((d >> 32) & 1u);
#endif
	}
}

// ibNumber::BigImpl — heap-tier storage and arithmetic, fully self-contained.
//
// Mantissa is stored sign-magnitude:
//   - `limbs` holds the absolute value in little-endian base-2^32 (limbs[0] is LSB).
//     Empty vector and a vector of all zeros both represent 0; we keep it trimmed.
//   - `negative` is the sign bit. Zero is canonicalised as not-negative.
//   - `exp` is the base-10 exponent. Value = (negative ? -1 : 1) * |limbs| * 10^exp.
//
// Algorithms are textbook schoolbook implementations (Add/Sub/Mul) and base-2
// long-division for Div. No dependency on ttmath here — bit packing of the
// inline tier and these routines are everything ibNumber uses.

struct ibNumber::BigImpl
{
	std::vector<uint32_t> limbs;
	bool                  negative;
	int32_t               exp;

	BigImpl() noexcept : negative(false), exp(0) {}

	// ---- magnitude utilities (operate on limbs only, ignore sign) ----------------

	static bool IsZeroMag(const std::vector<uint32_t>& v) noexcept
	{
		for (uint32_t l : v) if (l) return false;
		return true;
	}

	static void TrimMag(std::vector<uint32_t>& v) noexcept
	{
		while (!v.empty() && v.back() == 0) v.pop_back();
	}

	// Returns -1, 0, +1 for |a| vs |b|.
	static int CmpMag(const std::vector<uint32_t>& a,
	                  const std::vector<uint32_t>& b) noexcept
	{
		// Both are assumed trimmed; if not, normalise via TrimMag at call sites.
		if (a.size() != b.size())
			return a.size() < b.size() ? -1 : 1;
		for (size_t i = a.size(); i-- > 0; ) {
			if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
		}
		return 0;
	}

	// out = a + b (magnitudes). Uses ADC-style carry chain.
	static std::vector<uint32_t> AddMag(const std::vector<uint32_t>& a,
	                                    const std::vector<uint32_t>& b)
	{
		const size_t n = std::max(a.size(), b.size());
		std::vector<uint32_t> r;
		r.reserve(n + 1);
		unsigned char carry = 0;
		for (size_t i = 0; i < n; ++i) {
			const uint32_t av = i < a.size() ? a[i] : 0u;
			const uint32_t bv = i < b.size() ? b[i] : 0u;
			uint32_t sum;
			carry = ibAddCarry32(carry, av, bv, &sum);
			r.push_back(sum);
		}
		if (carry) r.push_back(1u);
		return r;
	}

	// out = a - b (magnitudes). Caller guarantees |a| >= |b|. SBB-style borrow chain.
	static std::vector<uint32_t> SubMag(const std::vector<uint32_t>& a,
	                                    const std::vector<uint32_t>& b)
	{
		std::vector<uint32_t> r;
		r.reserve(a.size());
		unsigned char borrow = 0;
		for (size_t i = 0; i < a.size(); ++i) {
			const uint32_t av = a[i];
			const uint32_t bv = i < b.size() ? b[i] : 0u;
			uint32_t diff;
			borrow = ibSubBorrow32(borrow, av, bv, &diff);
			r.push_back(diff);
		}
		TrimMag(r);
		return r;
	}

	// out = a * b (magnitudes). Schoolbook O(n*m).
	static std::vector<uint32_t> MulMag(const std::vector<uint32_t>& a,
	                                    const std::vector<uint32_t>& b)
	{
		if (IsZeroMag(a) || IsZeroMag(b)) return {};
		std::vector<uint32_t> r(a.size() + b.size(), 0u);
		for (size_t i = 0; i < a.size(); ++i) {
			uint64_t carry = 0;
			for (size_t j = 0; j < b.size(); ++j) {
				uint64_t cur = static_cast<uint64_t>(a[i]) * b[j] + r[i + j] + carry;
				r[i + j] = static_cast<uint32_t>(cur);
				carry    = cur >> 32;
			}
			r[i + b.size()] += static_cast<uint32_t>(carry);
		}
		TrimMag(r);
		return r;
	}

	// v *= s (small unsigned multiplier). Grows v if needed.
	static void MulMagSmall(std::vector<uint32_t>& v, uint32_t s)
	{
		if (s == 0) { v.clear(); return; }
		if (s == 1) return;
		uint64_t carry = 0;
		for (size_t i = 0; i < v.size(); ++i) {
			uint64_t cur = static_cast<uint64_t>(v[i]) * s + carry;
			v[i]  = static_cast<uint32_t>(cur);
			carry = cur >> 32;
		}
		while (carry) {
			v.push_back(static_cast<uint32_t>(carry));
			carry >>= 32;
		}
	}

	// v /= s, returns remainder. s != 0.
	static uint32_t DivMagSmall(std::vector<uint32_t>& v, uint32_t s)
	{
		uint64_t rem = 0;
		for (size_t i = v.size(); i-- > 0; ) {
			uint64_t cur = (rem << 32) | v[i];
			v[i] = static_cast<uint32_t>(cur / s);
			rem  = cur % s;
		}
		TrimMag(v);
		return static_cast<uint32_t>(rem);
	}

	// v <<= 1 (unsigned shift left by one bit). Grows by one limb if MSB carries out.
	static void ShiftLeft1Mag(std::vector<uint32_t>& v)
	{
		uint32_t carry = 0;
		for (size_t i = 0; i < v.size(); ++i) {
			uint32_t newCarry = v[i] >> 31;
			v[i] = (v[i] << 1) | carry;
			carry = newCarry;
		}
		if (carry) v.push_back(carry);
	}

	// q = a / b, r = a % b. Base-2 long division — O(bits(a) * limbs).
	// Sufficient for our scale (typical mantissa under a few hundred digits).
	static void DivModMag(const std::vector<uint32_t>& a,
	                      const std::vector<uint32_t>& b,
	                      std::vector<uint32_t>& q,
	                      std::vector<uint32_t>& r)
	{
		q.clear();
		r.clear();
		if (IsZeroMag(b)) {
			throw std::runtime_error("ibNumber: division by zero");
		}
		if (CmpMag(a, b) < 0) { r = a; TrimMag(r); return; }

		// Find topmost set bit in a.
		size_t topLimb = a.size() - 1;
		while (topLimb > 0 && a[topLimb] == 0) --topLimb;
		int topBit = 31;
		while (topBit >= 0 && (a[topLimb] & (1u << topBit)) == 0) --topBit;
		const long long totalBits = static_cast<long long>(topLimb) * 32 + topBit + 1;

		q.assign((static_cast<size_t>(totalBits) + 31) / 32, 0u);

		for (long long bit = totalBits - 1; bit >= 0; --bit) {
			ShiftLeft1Mag(r);
			if (a[bit / 32] & (1u << (bit % 32))) {
				if (r.empty()) r.push_back(0);
				r[0] |= 1u;
			}
			if (CmpMag(r, b) >= 0) {
				r = SubMag(r, b);
				const size_t qIdx = static_cast<size_t>(bit) / 32;
				if (qIdx >= q.size()) q.resize(qIdx + 1, 0u);
				q[qIdx] |= (1u << (bit % 32));
			}
		}
		TrimMag(q);
		TrimMag(r);
	}

	// ---- signed operations on BigImpl --------------------------------------------

	bool IsZero() const noexcept { return IsZeroMag(limbs); }

	void SetZero() noexcept { limbs.clear(); negative = false; }

	void ChangeSign() noexcept
	{
		if (!IsZero()) negative = !negative;
	}

	// Returns -1, 0, +1 for *this vs other (sign-aware).
	int Compare(const BigImpl& other) const noexcept
	{
		const bool az = IsZero(), bz = other.IsZero();
		if (az && bz) return 0;
		if (az)       return other.negative ? +1 : -1;
		if (bz)       return negative      ? -1 : +1;
		if (negative != other.negative) return negative ? -1 : +1;
		const int c = CmpMag(limbs, other.limbs);
		return negative ? -c : c;
	}

	void Add(const BigImpl& rhs)
	{
		if (negative == rhs.negative) {
			limbs = AddMag(limbs, rhs.limbs);
		} else {
			const int c = CmpMag(limbs, rhs.limbs);
			if (c >= 0) {
				limbs = SubMag(limbs, rhs.limbs);
			} else {
				limbs = SubMag(rhs.limbs, limbs);
				negative = rhs.negative;
			}
		}
		if (IsZeroMag(limbs)) negative = false;
		TrimMag(limbs);
	}

	void Sub(const BigImpl& rhs)
	{
		BigImpl t = rhs;
		t.ChangeSign();
		Add(t);
	}

	void Mul(const BigImpl& rhs)
	{
		const bool resNeg = (negative != rhs.negative);
		limbs = MulMag(limbs, rhs.limbs);
		negative = (IsZeroMag(limbs) ? false : resNeg);
	}

	// *this = quotient; remainder discarded.
	void Div(const BigImpl& rhs)
	{
		const bool resNeg = (negative != rhs.negative);
		std::vector<uint32_t> q, r;
		DivModMag(limbs, rhs.limbs, q, r);
		limbs = std::move(q);
		negative = (IsZeroMag(limbs) ? false : resNeg);
	}

	// |this| *= 10^k (k >= 0). Uses 10^9 chunks (largest power of 10 fitting uint32).
	void MulMagPow10(int32_t k)
	{
		if (k <= 0 || IsZeroMag(limbs)) return;
		while (k >= 9) { MulMagSmall(limbs, 1000000000u); k -= 9; }
		while (k > 0) { MulMagSmall(limbs, 10u);          --k;   }
	}

	// ---- conversions -------------------------------------------------------------

	void FromInt64(int64_t v)
	{
		limbs.clear();
		if (v == 0) { negative = false; exp = 0; return; }
		uint64_t mag;
		if (v < 0) { negative = true;  mag = static_cast<uint64_t>(-(v + 1)) + 1; }
		else       { negative = false; mag = static_cast<uint64_t>(v); }
		limbs.push_back(static_cast<uint32_t>(mag));
		if (mag >> 32) limbs.push_back(static_cast<uint32_t>(mag >> 32));
		exp = 0;
	}

	// Returns true on success. Fails if magnitude doesn't fit.
	bool MagToInt64(int64_t& out) const noexcept
	{
		if (limbs.size() > 2) return false;
		uint64_t mag = 0;
		if (limbs.size() >= 1) mag  = limbs[0];
		if (limbs.size() >= 2) mag |= static_cast<uint64_t>(limbs[1]) << 32;
		if (negative) {
			static const uint64_t kAbsMin = static_cast<uint64_t>(INT64_MAX) + 1ULL;
			if (mag >  kAbsMin) return false;
			if (mag == kAbsMin) { out = INT64_MIN; return true; }
			out = -static_cast<int64_t>(mag);
		} else {
			if (mag > static_cast<uint64_t>(INT64_MAX)) return false;
			out = static_cast<int64_t>(mag);
		}
		return true;
	}

};

namespace
{
	// Aligns two BigImpl operands to a common exp10 by scaling whichever has the larger
	// exp up by powers of 10. After this call a.exp == b.exp and both still represent
	// their original numerical values exactly.
	void AlignExp(ibNumber::BigImpl& a, ibNumber::BigImpl& b)
	{
		if (a.exp == b.exp) return;
		if (a.exp > b.exp) {
			a.MulMagPow10(a.exp - b.exp);
			a.exp = b.exp;
		} else {
			b.MulMagPow10(b.exp - a.exp);
			b.exp = a.exp;
		}
	}

	// Decimal parser shared by ibNumber(const wxString&) and FromString().
	// Accepts: optional sign, integer digits, optional '.' + fractional digits,
	// optional [eE][+-]?digits. Returns true if at least one digit was consumed.
	// On failure leaves `out` zeroed.
	//
	// Walks wxString's underlying wchar_t buffer directly — no UTF-8 conversion,
	// since digits, sign, decimal point and exponent marker are all ASCII and
	// fit in single wchar_t code units.
	bool TryParseString(const wxString& s, ibNumber::BigImpl& out)
	{
		out.limbs.clear();
		out.negative = false;
		out.exp = 0;

		const wchar_t* p = s.wc_str();
		if (!p) return false;

		while (*p == L' ' || *p == L'\t') ++p;

		bool negative = false;
		if (*p == L'+') ++p;
		else if (*p == L'-') { negative = true; ++p; }

		int32_t exp10 = 0;
		bool sawDigit = false;
		bool sawPoint = false;

		while (*p) {
			if (*p >= L'0' && *p <= L'9') {
				ibNumber::BigImpl::MulMagSmall(out.limbs, 10u);
				const uint32_t d = static_cast<uint32_t>(*p - L'0');
				if (d != 0) {
					if (out.limbs.empty()) out.limbs.push_back(0u);
					uint64_t cur = static_cast<uint64_t>(out.limbs[0]) + d;
					out.limbs[0] = static_cast<uint32_t>(cur);
					uint64_t carry = cur >> 32;
					size_t i = 1;
					while (carry) {
						if (i >= out.limbs.size()) out.limbs.push_back(0u);
						uint64_t cur2 = static_cast<uint64_t>(out.limbs[i]) + carry;
						out.limbs[i] = static_cast<uint32_t>(cur2);
						carry = cur2 >> 32;
						++i;
					}
				}
				if (sawPoint) --exp10;
				sawDigit = true;
				++p;
			} else if (*p == L'.' && !sawPoint) {
				sawPoint = true;
				++p;
			} else {
				break;
			}
		}

		if (*p == L'e' || *p == L'E') {
			++p;
			bool eneg = false;
			if (*p == L'+') ++p;
			else if (*p == L'-') { eneg = true; ++p; }
			int32_t eVal = 0;
			while (*p >= L'0' && *p <= L'9') { eVal = eVal * 10 + (*p - L'0'); ++p; }
			exp10 += eneg ? -eVal : eVal;
		}

		if (!sawDigit) {
			out.limbs.clear();
			return false;
		}
		ibNumber::BigImpl::TrimMag(out.limbs);
		out.negative = negative && !out.IsZero();
		out.exp = exp10;
		return true;
	}
}

// ---- bit packing ---------------------------------------------------------------------

bool ibNumber::CanBeImmediate(int64_t mant, int32_t exp10) noexcept
{
	return mant >= kImmMantMin && mant <= kImmMantMax
	    && exp10 >= kImmExpMin && exp10 <= kImmExpMax;
}

uint64_t ibNumber::PackImmediate(int64_t mant, int32_t exp10) noexcept
{
	const uint64_t mantU = static_cast<uint64_t>(mant) & ((1ULL << kImmMantBits) - 1);
	const uint64_t expU  = static_cast<uint64_t>(exp10) & ((1ULL << kImmExpBits) - 1);
	return (mantU << kImmMantShift) | (expU << kImmExpShift) | 1ULL;
}

int64_t ibNumber::ImmMantissa() const
{
	// Arithmetic right-shift on int64_t sign-extends, restoring the 47-bit signed
	// mantissa to a full int64_t value.
	return static_cast<int64_t>(m_payload) >> kImmMantShift;
}

int ibNumber::ImmExp() const
{
	const uint64_t mask = (1ULL << kImmExpBits) - 1;
	int64_t e = static_cast<int64_t>((m_payload >> kImmExpShift) & mask);
	if (e & (1LL << (kImmExpBits - 1))) e -= (1LL << kImmExpBits);
	return static_cast<int>(e);
}

ibNumber::BigImpl* ibNumber::HeapPtr() const
{
	return reinterpret_cast<BigImpl*>(static_cast<uintptr_t>(m_payload));
}

void ibNumber::StoreImmediate(int64_t mant, int32_t exp10) noexcept
{
	m_payload = PackImmediate(mant, exp10);
}

void ibNumber::StoreHeap(BigImpl* p) noexcept
{
	m_payload = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
}

// ---- ctors / dtor / assignment -------------------------------------------------------

ibNumber::ibNumber() noexcept
	: m_payload(PackImmediate(0, 0))
{
}

ibNumber::ibNumber(int v) noexcept
	: m_payload(PackImmediate(v, 0))
{
}

// The two signed 64-bit spellings share one body. `long` and `long long` are different
// TYPES even when they are the same width, so both need their own constructor — see the
// note in fnumber.h. FromSigned64 is the single implementation.
void ibNumber::FromSigned64(int64_t v) noexcept
{
	if (CanBeImmediate(v, 0)) {
		m_payload = PackImmediate(v, 0);
		return;
	}
	BigImpl* p = new BigImpl();
	p->FromInt64(v);
	StoreHeap(p);
}

ibNumber::ibNumber(long v) noexcept
	: m_payload(0)
{
	FromSigned64(static_cast<int64_t>(v));
}

ibNumber::ibNumber(long long v) noexcept
	: m_payload(0)
{
	FromSigned64(static_cast<int64_t>(v));
}

ibNumber::ibNumber(unsigned int v) noexcept
	: m_payload(PackImmediate(static_cast<int64_t>(v), 0))
{
	// unsigned int max == 2^32 - 1 fits trivially in 47-bit immediate mantissa.
}

// Unsigned twin of FromSigned64 — same reason for two constructors over one body.
void ibNumber::FromUnsigned64(uint64_t v) noexcept
{
	if (v <= static_cast<uint64_t>(kImmMantMax)) {
		m_payload = PackImmediate(static_cast<int64_t>(v), 0);
		return;
	}
	BigImpl* p = new BigImpl();
	p->limbs.push_back(static_cast<uint32_t>(v));
	if (v >> 32) p->limbs.push_back(static_cast<uint32_t>(v >> 32));
	p->negative = false;
	p->exp      = 0;
	StoreHeap(p);
}

ibNumber::ibNumber(unsigned long v) noexcept
	: m_payload(0)
{
	FromUnsigned64(static_cast<uint64_t>(v));
}

ibNumber::ibNumber(unsigned long long v) noexcept
	: m_payload(0)
{
	FromUnsigned64(static_cast<uint64_t>(v));
}

ibNumber::ibNumber(double v)
	: m_payload(PackImmediate(0, 0))
{
	if (!std::isfinite(v)) return;  // NaN / Inf collapse to zero.
	wchar_t buf[64];
	const int n = std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%.17g", v);
	BigImpl big;
	if (n > 0 && TryParseString(wxString(buf, static_cast<size_t>(n)), big))
		StoreBig(big);
}

ibNumber::ibNumber(const wxString& s)
	: m_payload(PackImmediate(0, 0))
{
	BigImpl big;
	if (TryParseString(s, big)) StoreBig(big);
}

ibNumber::ibNumber(const ibNumber& o)
	: m_payload(0)
{
	if (o.IsImmediate()) {
		m_payload = o.m_payload;
	} else {
		StoreHeap(new BigImpl(*o.HeapPtr()));
	}
}

ibNumber::ibNumber(ibNumber&& o) noexcept
	: m_payload(o.m_payload)
{
	o.m_payload = PackImmediate(0, 0);
}

ibNumber::~ibNumber()
{
	Clear();
}

ibNumber& ibNumber::operator=(const ibNumber& o)
{
	if (this == &o) return *this;
	Clear();
	if (o.IsImmediate()) {
		m_payload = o.m_payload;
	} else {
		StoreHeap(new BigImpl(*o.HeapPtr()));
	}
	return *this;
}

ibNumber& ibNumber::operator=(ibNumber&& o) noexcept
{
	if (this == &o) return *this;
	Clear();
	m_payload = o.m_payload;
	o.m_payload = PackImmediate(0, 0);
	return *this;
}

void ibNumber::Clear() noexcept
{
	if (IsHeap()) {
		delete HeapPtr();
	}
	m_payload = PackImmediate(0, 0);
}

// ---- LoadBig / StoreBig --------------------------------------------------------------

void ibNumber::LoadBig(BigImpl& out) const
{
	if (IsImmediate()) {
		out.FromInt64(ImmMantissa());
		out.exp = ImmExp();
	} else {
		out = *HeapPtr();
	}
}

void ibNumber::StoreBig(const BigImpl& src)
{
	// Zero result — stay where we are, no churn:
	//   - If immediate: write immediate(0,0) directly, no Clear() call.
	//   - If heap: keep allocation, just zero the limbs in place. Saves
	//     delete + new on accumulator patterns that flap to/from zero
	//     (e.g. running sum that hits zero between additions).
	if (src.IsZero()) {
		if (IsHeap()) {
			BigImpl* hp = HeapPtr();
			hp->limbs.clear();
			hp->negative = false;
			hp->exp      = 0;
		} else {
			m_payload = PackImmediate(0, 0);
		}
		return;
	}

	int64_t m64;
	if (src.MagToInt64(m64) && CanBeImmediate(m64, src.exp)) {
		Clear();
		StoreImmediate(m64, src.exp);
		return;
	}
	if (IsHeap()) {
		*HeapPtr() = src;
		return;
	}
	StoreHeap(new BigImpl(src));
}

// ---- arithmetic ----------------------------------------------------------------------

ibNumber& ibNumber::operator+=(const ibNumber& rhs)
{
	// 47-bit + 47-bit can't overflow int64; store if the sum fits immediate.
	int64_t am, bm;
	if (TryImmInts(rhs, am, bm)) {
		const int64_t r = am + bm;
		if (CanBeImmediate(r, 0)) { StoreImmediate(r, 0); return *this; }
	}
	BigImpl a, b;
	LoadBig(a);
	rhs.LoadBig(b);
	AlignExp(a, b);
	a.Add(b);
	StoreBig(a);
	return *this;
}

ibNumber& ibNumber::operator-=(const ibNumber& rhs)
{
	int64_t am, bm;
	if (TryImmInts(rhs, am, bm)) {
		const int64_t r = am - bm;
		if (CanBeImmediate(r, 0)) { StoreImmediate(r, 0); return *this; }
	}
	BigImpl a, b;
	LoadBig(a);
	rhs.LoadBig(b);
	AlignExp(a, b);
	a.Sub(b);
	StoreBig(a);
	return *this;
}

ibNumber& ibNumber::operator*=(const ibNumber& rhs)
{
	// int32-range operands keep the product below 2^62 (no int64 overflow);
	// anything wider falls through to the BigImpl multiply.
	int64_t am, bm;
	if (TryImmInts(rhs, am, bm)
		&& am >= INT32_MIN && am <= INT32_MAX && bm >= INT32_MIN && bm <= INT32_MAX) {
		const int64_t r = am * bm;
		if (CanBeImmediate(r, 0)) { StoreImmediate(r, 0); return *this; }
	}
	BigImpl a, b;
	LoadBig(a);
	rhs.LoadBig(b);
	a.Mul(b);
	a.exp += b.exp;
	StoreBig(a);
	return *this;
}

ibNumber& ibNumber::operator/=(const ibNumber& rhs)
{
	// Exact integer division only: a single int64 divide when the remainder is
	// zero and the quotient fits immediate (the common "divide evenly" case —
	// split a total, halve, scale). Non-exact divisions (proportions /
	// percentages) and any non-integer operand fall through to the exact-decimal
	// long division below; the result is bit-identical either way.
	int64_t am, bm;
	if (TryImmInts(rhs, am, bm) && bm != 0 && am % bm == 0) {
		const int64_t q = am / bm;
		if (CanBeImmediate(q, 0)) { StoreImmediate(q, 0); return *this; }
	}

	BigImpl a, b;
	LoadBig(a);
	rhs.LoadBig(b);

	// Inflate dividend so integer division yields ~30 fractional digits of result.
	const int32_t kExtra = 30;
	a.MulMagPow10(kExtra);
	a.exp -= kExtra;
	a.exp -= b.exp;
	a.Div(b);
	StoreBig(a);
	return *this;
}

ibNumber ibNumber::operator-() const
{
	BigImpl b;
	LoadBig(b);
	b.ChangeSign();
	ibNumber r;
	r.StoreBig(b);
	return r;
}

int ibNumber::Compare(const ibNumber& rhs) const
{
	// Fast path: both immediate integers — a direct int64 three-way compare, no
	// BigImpl. Every loop condition (`i < n`) and integer comparison hits this.
	int64_t am, bm;
	if (TryImmInts(rhs, am, bm))
		return (am > bm) - (am < bm);
	BigImpl a, b;
	LoadBig(a);
	rhs.LoadBig(b);
	AlignExp(a, b);
	return a.Compare(b);
}

bool ibNumber::IsZero() const
{
	if (IsImmediate()) return ImmMantissa() == 0;
	return HeapPtr()->IsZero();
}

int64_t ibNumber::ToInt64() const
{
	BigImpl b;
	LoadBig(b);
	if (b.exp > 0) {
		b.MulMagPow10(b.exp);
		b.exp = 0;
	} else if (b.exp < 0) {
		// Floor towards zero by integer-dividing magnitude by 10^|exp|.
		std::vector<uint32_t> div = { 1u };
		BigImpl divisor; divisor.limbs = div;
		divisor.MulMagPow10(-b.exp);
		std::vector<uint32_t> q, r;
		BigImpl::DivModMag(b.limbs, divisor.limbs, q, r);
		b.limbs = std::move(q);
		if (b.IsZero()) b.negative = false;
		b.exp = 0;
	}
	int64_t out;
	if (!b.MagToInt64(out)) {
		throw std::overflow_error("ibNumber::ToInt64 overflow");
	}
	return out;
}

int ibNumber::ToInt() const
{
	BigImpl b;
	LoadBig(b);
	// Truncate fractional part toward zero by dividing magnitude by 10^|exp|.
	if (b.exp < 0) {
		BigImpl divisor; divisor.limbs.push_back(1u);
		divisor.MulMagPow10(-b.exp);
		std::vector<uint32_t> q, r;
		BigImpl::DivModMag(b.limbs, divisor.limbs, q, r);
		b.limbs = std::move(q);
		if (b.IsZero()) b.negative = false;
		b.exp = 0;
	} else if (b.exp > 0) {
		b.MulMagPow10(b.exp);
		b.exp = 0;
	}
	int64_t v;
	if (!b.MagToInt64(v)) return b.negative ? INT_MIN : INT_MAX;
	if (v >  static_cast<int64_t>(INT_MAX)) return INT_MAX;
	if (v <  static_cast<int64_t>(INT_MIN)) return INT_MIN;
	return static_cast<int>(v);
}

unsigned int ibNumber::ToUInt() const
{
	BigImpl b;
	LoadBig(b);
	if (b.negative) return 0u;
	if (b.exp < 0) {
		BigImpl divisor; divisor.limbs.push_back(1u);
		divisor.MulMagPow10(-b.exp);
		std::vector<uint32_t> q, r;
		BigImpl::DivModMag(b.limbs, divisor.limbs, q, r);
		b.limbs = std::move(q);
		if (b.IsZero()) b.negative = false;
		b.exp = 0;
	} else if (b.exp > 0) {
		b.MulMagPow10(b.exp);
		b.exp = 0;
	}
	int64_t v;
	if (!b.MagToInt64(v)) return UINT_MAX;
	if (v < 0) return 0u;
	if (static_cast<uint64_t>(v) > static_cast<uint64_t>(UINT_MAX)) return UINT_MAX;
	return static_cast<unsigned int>(v);
}

int ibNumber::ToSigned64(int64_t& out) const
{
	BigImpl b;
	LoadBig(b);
	if (b.exp < 0) {
		BigImpl divisor; divisor.limbs.push_back(1u);
		divisor.MulMagPow10(-b.exp);
		std::vector<uint32_t> q, r;
		BigImpl::DivModMag(b.limbs, divisor.limbs, q, r);
		b.limbs = std::move(q);
		if (b.IsZero()) b.negative = false;
		b.exp = 0;
	} else if (b.exp > 0) {
		b.MulMagPow10(b.exp);
		b.exp = 0;
	}
	if (!b.MagToInt64(out)) return 1; // overflow (ttmath-compat semantics)
	return 0;
}

float ibNumber::ToFloat() const
{
	return static_cast<float>(ToDouble());
}

void ibNumber::SetZero()
{
	Clear();
}

ibNumber ibNumber::Round() const
{
	BigImpl b;
	LoadBig(b);
	if (b.exp >= 0) return *this;

	BigImpl divisor; divisor.limbs.push_back(1u);
	divisor.MulMagPow10(-b.exp);
	std::vector<uint32_t> q, r;
	BigImpl::DivModMag(b.limbs, divisor.limbs, q, r);

	// half-away-from-zero: if 2*r >= divisor, bump quotient by 1
	std::vector<uint32_t> r2 = BigImpl::AddMag(r, r);
	if (BigImpl::CmpMag(r2, divisor.limbs) >= 0) {
		std::vector<uint32_t> one; one.push_back(1u);
		q = BigImpl::AddMag(q, one);
	}

	BigImpl res;
	res.limbs   = std::move(q);
	res.negative = b.negative && !res.IsZero();
	res.exp     = 0;

	ibNumber out; out.StoreBig(res);
	return out;
}

ibNumber ibNumber::Trunc() const
{
	BigImpl b;
	LoadBig(b);
	if (b.exp < 0) {
		BigImpl divisor; divisor.limbs.push_back(1u);
		divisor.MulMagPow10(-b.exp);
		std::vector<uint32_t> q, r;
		BigImpl::DivModMag(b.limbs, divisor.limbs, q, r);
		b.limbs = std::move(q);
		if (b.IsZero()) b.negative = false;
		b.exp = 0;
	}
	ibNumber out;
	out.StoreBig(b);
	return out;
}

ibNumber ibNumber::Round(int n) const
{
	if (n < 0) n = 0;
	BigImpl b;
	LoadBig(b);
	if (b.exp >= -n) return *this;

	const int32_t toRemove = -n - b.exp; // > 0
	BigImpl divisor; divisor.limbs.push_back(1u);
	divisor.MulMagPow10(toRemove);
	std::vector<uint32_t> q, r;
	BigImpl::DivModMag(b.limbs, divisor.limbs, q, r);

	std::vector<uint32_t> r2 = BigImpl::AddMag(r, r);
	if (BigImpl::CmpMag(r2, divisor.limbs) >= 0) {
		std::vector<uint32_t> one; one.push_back(1u);
		q = BigImpl::AddMag(q, one);
	}

	BigImpl res;
	res.limbs    = std::move(q);
	res.negative = b.negative && !res.IsZero();
	res.exp      = -n;

	ibNumber out; out.StoreBig(res);
	return out;
}

ibNumber& ibNumber::operator++()
{
	*this += ibNumber(1);
	return *this;
}

ibNumber ibNumber::operator++(int)
{
	ibNumber prev(*this);
	*this += ibNumber(1);
	return prev;
}

bool ibNumber::IsSign() const
{
	if (IsImmediate()) return ImmMantissa() < 0;
	return HeapPtr()->negative;
}

void ibNumber::ChangeSign()
{
	BigImpl b;
	LoadBig(b);
	b.ChangeSign();
	StoreBig(b);
}

ibNumber ibNumber::Abs() const
{
	BigImpl b;
	LoadBig(b);
	b.negative = false;
	ibNumber r;
	r.StoreBig(b);
	return r;
}

void ibNumber::FromInt(int v)
{
	*this = ibNumber(v);
}

int ibNumber::ToUnsigned64(uint64_t& out) const
{
	BigImpl b;
	LoadBig(b);
	if (b.negative) return 1; // negative — overflow for unsigned
	if (b.exp < 0) {
		BigImpl divisor; divisor.limbs.push_back(1u);
		divisor.MulMagPow10(-b.exp);
		std::vector<uint32_t> q, r;
		BigImpl::DivModMag(b.limbs, divisor.limbs, q, r);
		b.limbs = std::move(q);
		b.exp = 0;
	} else if (b.exp > 0) {
		b.MulMagPow10(b.exp);
		b.exp = 0;
	}
	if (b.limbs.size() > 2) return 1;
	uint64_t v = 0;
	if (b.limbs.size() >= 1) v  = b.limbs[0];
	if (b.limbs.size() >= 2) v |= static_cast<uint64_t>(b.limbs[1]) << 32;
	out = v;
	return 0;
}

// The four public spellings over the two conversions above. `long` and `long long` are
// distinct types even at equal width, so each needs its own overload; where the target is
// narrower than 64 bits (Windows `long`), a value outside its range reports overflow
// instead of truncating. See the note in fnumber.h.
int ibNumber::ToInt(long& out) const
{
	int64_t v = 0;
	const int rc = ToSigned64(v);
	if (rc != 0) return rc;
	if (v < static_cast<int64_t>(std::numeric_limits<long>::min()) ||
	    v > static_cast<int64_t>(std::numeric_limits<long>::max()))
		return 1;
	out = static_cast<long>(v);
	return 0;
}

int ibNumber::ToInt(long long& out) const
{
	int64_t v = 0;
	const int rc = ToSigned64(v);
	if (rc == 0) out = static_cast<long long>(v);
	return rc;
}

int ibNumber::ToInt(unsigned long& out) const
{
	uint64_t v = 0;
	const int rc = ToUnsigned64(v);
	if (rc != 0) return rc;
	if (v > static_cast<uint64_t>(std::numeric_limits<unsigned long>::max()))
		return 1;
	out = static_cast<unsigned long>(v);
	return 0;
}

int ibNumber::ToInt(unsigned long long& out) const
{
	uint64_t v = 0;
	const int rc = ToUnsigned64(v);
	if (rc == 0) out = static_cast<unsigned long long>(v);
	return rc;
}

std::wstring ibNumber::ToWString() const
{
	return ToString().ToStdWstring();
}

ibNumber ibNumber::Pow(int n) const
{
	if (n == 0) return ibNumber(1);
	if (n < 0)  return ibNumber(1) / Pow(-n);
	ibNumber base(*this), result(1);
	while (n > 0) {
		if (n & 1) result *= base;
		n >>= 1;
		if (n > 0) base *= base;
	}
	return result;
}

// Working fractional precision carried through the transcendental series /
// Newton iterations below. A small margin over operator/'s ~30 (kExtra)
// fractional digits so intermediate rounding doesn't eat the reported tail.
// These functions restore the high-precision Sqrt/Ln/Exp/Log/Pow that ttmath
// used to provide and that the ttmath removal had shortcut to plain double.
static constexpr int kTransWorkScale = 34;

ibNumber ibNumber::Pow(const ibNumber& n) const
{
	int64_t k = 0;
	// ToInt() TRUNCATES and returns 0 (== success) for any in-range value, so
	// "conversion succeeded" is NOT "n is an integer" — e.g. 0.5 -> k=0, success.
	// Without the equality guard, a fractional exponent fell into the integer
	// path and 2^0.5 computed as 2^0 == 1. Require n to equal its truncation.
	if (n.ToInt(k) == 0 && k >= INT_MIN && k <= INT_MAX && n == ibNumber(k)) {
		return Pow(static_cast<int>(k));   // exact: repeated multiplication
	}
	// Non-integer exponent: x^n = exp(n * ln x), defined for x > 0. For the
	// domain edges (x <= 0 with a fractional power — complex / undefined) keep
	// the old double result rather than throw.
	if (IsSign() || IsZero())
		return ibNumber(std::pow(ToDouble(), n.ToDouble()));
	return (n * Ln()).Exp();
}

ibNumber ibNumber::Sqrt() const
{
	// Negative has no real square root; the runtime wrapper
	// (ibValueSystemFunction::Sqrt) rejects it before we get here.
	// Defensive: 0 for negative / zero input.
	if (IsSign() || IsZero())
		return ibNumber();

	// High-precision square root via the DIVISION-FREE Newton iteration for
	// the reciprocal root:
	//
	//     y_{n+1} = y_n * (3 - N*y_n^2) / 2     ->  y -> 1/sqrt(N)
	//     sqrt(N) = N * y
	//
	// Why not the plain Heron x<-(x+N/x)/2: that divides the SMALL dividend N
	// (e.g. 25.5, a 2-3 digit mantissa) by the HIGH-PRECISION iterate x.
	// operator/ inflates the dividend by a fixed kExtra(=30) digits, so once
	// x carries more than ~30 significant digits the integer divmod collapses
	// the quotient (eventually to 0), and Newton degenerates into halving x
	// every step -> result = seed * 2^-iters (the sqrt(25.5) -> 5.4e-19 bug).
	//
	// This form only ever multiplies (exact, precision bounded by Round) and
	// divides by the literal 2 (a 1-limb divisor — never the small/big-divisor
	// trap). double seeds 1/sqrt(N) to ~15 digits; quadratic convergence then
	// reaches the working scale in 1-3 steps.
	double sd = std::sqrt(ToDouble());
	if (!std::isfinite(sd) || sd <= 0.0)
		sd = 1.0;   // input outside double's range — any positive seed converges
	ibNumber y(1.0 / sd);

	const ibNumber three(3), two(2);
	ibNumber prev;
	for (int i = 0; i < 64; ++i) {            // cap is a backstop; common case breaks in 1-3
		ibNumber y2   = (y * y).Round(kTransWorkScale);            // y^2
		ibNumber corr = (three - (*this) * y2).Round(kTransWorkScale); // 3 - N*y^2
		ibNumber next = (y * corr / two).Round(kTransWorkScale);   // y*(3 - N*y^2)/2
		if (next == y)            break;      // fixed point
		if (next == prev) { y = next; break; }// precision-floor wobble — settle
		prev = y;
		y = next;
	}
	ibNumber result = ((*this) * y).Round(kTransWorkScale);   // sqrt(N) = N * (1/sqrt(N))

	// Snap exact perfect squares. N*(1/sqrt(N)) lands a hair off the integer
	// for a perfect square (1/sqrt(N) has no terminating decimal), e.g.
	// sqrt(144) -> 11.9999...808. Round-and-verify via EXACT integer multiply
	// makes it 12; a non-perfect square never passes (its root is irrational),
	// so this never corrupts a genuine result.
	ibNumber ri = result.Round(0);
	if (ri * ri == *this)
		return ri;
	return result;
}

ibNumber ibNumber::Log(const ibNumber& base) const
{
	// log_base(x) = ln(x) / ln(base). Domain: x > 0, base > 0 and != 1.
	if (IsSign() || IsZero()) return ibNumber();
	if (base.IsSign() || base.IsZero() || base == ibNumber(1)) return ibNumber();
	ibNumber r = (Ln() / base.Ln()).Round(kTransWorkScale);

	// Snap exact integer powers: the irrational ln-ratio otherwise lands a hair
	// off (Log10(100) -> 1.9999...). Verify via EXACT integer Pow — only a true
	// base^k == x passes, so a non-power result is left untouched.
	int64_t k = 0;
	ibNumber rk = r.Round(0);
	if (rk.ToInt(k) == 0 && k >= INT_MIN && k <= INT_MAX && base.Pow(static_cast<int>(k)) == *this)
		return rk;
	return r;
}

ibNumber ibNumber::Ln() const
{
	// Domain x > 0; non-positive → 0 (runtime wrappers guard / report).
	if (IsSign() || IsZero()) return ibNumber();
	if (*this == ibNumber(1)) return ibNumber();   // ln(1) = 0 exact

	// Newton-Raphson for f(y) = exp(y) - x:  y <- y - 1 + x*exp(-y).
	// Quadratic convergence near the root; reuses the high-precision Exp
	// below as its primitive (no separate Taylor-for-ln machinery).
	//
	// Seed with the double logarithm (~15 correct digits → 1-2 steps to the
	// working precision). For x outside double's range, seed from the decimal
	// magnitude ≈ (intDigits - 1) * ln 10 so Newton still starts near the root.
	double seed = std::log(ToDouble());
	if (!std::isfinite(seed)) {
		const wxString s = Abs().ToString();
		const int dot = s.Find(wxT('.'));
		const int intDigits = (dot == wxNOT_FOUND) ? static_cast<int>(s.length()) : dot;
		seed = (intDigits - 1) * 2.302585092994046;   // (digits - 1) * ln(10)
	}
	ibNumber y(seed);

	const ibNumber one(1);
	ibNumber prev;
	for (int i = 0; i < 64; ++i) {           // cap is a backstop; common case breaks in 1-2
		ibNumber negY(y); negY.ChangeSign();
		ibNumber next = (y + (*this) * negY.Exp() - one).Round(kTransWorkScale);
		if (next == y) break;                // converged
		if (next == prev) { y = next; break; }   // precision-floor wobble — settle
		prev = y;
		y = next;
	}
	return y.Round(kTransWorkScale);
}

ibNumber ibNumber::Exp() const
{
	if (IsZero()) return ibNumber(1);

	// Out of the exact tier's representable range (|x| huge): exp would over/
	// underflow ibNumber's ±32767 decimal exponent anyway — let double give
	// the limiting value (inf → huge, -inf → 0).
	const double mag = std::fabs(ToDouble());
	if (!std::isfinite(mag) || mag > 75000.0)
		return ibNumber(std::exp(ToDouble()));

	// Argument reduction: exp(x) = exp(x / 2^n) ^ (2^n). Halve until the
	// reduced argument's magnitude is < ~0.5 so the Taylor series converges
	// in ~30 terms; square the partial result back n times afterwards.
	int n = 0;
	for (double m = mag; m > 0.5 && n < 4096; m *= 0.5) ++n;

	const ibNumber two(2);
	ibNumber r(*this);
	for (int i = 0; i < n; ++i) r /= two;
	r = r.Round(kTransWorkScale);

	// Taylor: exp(r) = Σ r^k / k!  with term_k = term_{k-1} * r / k. Rounding
	// each term to the working scale both bounds digit growth and provides the
	// convergence test (term rounds to zero once below the working precision).
	ibNumber sum(1), term(1);
	for (int k = 1; k < 4096; ++k) {
		term *= r;
		term /= ibNumber(k);
		term = term.Round(kTransWorkScale);
		if (term.IsZero()) break;
		sum += term;
		sum = sum.Round(kTransWorkScale);
	}

	for (int i = 0; i < n; ++i) {            // undo the halving: square n times
		sum *= sum;
		sum = sum.Round(kTransWorkScale);
	}
	return sum.Round(kTransWorkScale);
}

ibNumber& ibNumber::operator%=(const ibNumber& rhs)
{
	BigImpl a, b;
	LoadBig(a);
	rhs.LoadBig(b);
	AlignExp(a, b);

	std::vector<uint32_t> q, r;
	BigImpl::DivModMag(a.limbs, b.limbs, q, r);
	a.limbs = std::move(r);
	if (a.IsZero()) a.negative = false;
	StoreBig(a);
	return *this;
}

double ibNumber::ToDouble() const
{
	// Direct mantissa→double, no string round-trip.
	// 1. Shrink magnitude to <= 2 limbs by dividing off 10^9 chunks; bump exp.
	// 2. Convert remaining 64-bit magnitude to double (lossy if > 2^53).
	// 3. Multiply by 10^exp via std::pow (one fp multiply, sub-ulp for typical exp).
	BigImpl b;
	LoadBig(b);

	std::vector<uint32_t> mag = b.limbs;
	int32_t expAdj = b.exp;
	while (mag.size() > 2) {
		BigImpl::DivMagSmall(mag, 1000000000u);
		expAdj += 9;
	}

	uint64_t lo = 0;
	if (mag.size() >= 1) lo  = mag[0];
	if (mag.size() >= 2) lo |= static_cast<uint64_t>(mag[1]) << 32;

	double d = static_cast<double>(lo);
	if (expAdj != 0) d *= std::pow(10.0, expAdj);
	return b.negative ? -d : d;
}

wxString ibNumber::ToString() const
{
	// Fast path: immediate-tier integer (mantissa fits in int64, exp == 0).
	// This is by far the most common case for DB write paths and tight loops.
	// We write wide-char directly into a wxString-compatible buffer — wxString
	// is a wrapper over std::wstring on the Unicode build, so wchar_t→wxString
	// is a direct copy without character-set conversion.
	if (IsImmediate() && ImmExp() == 0) {
		wchar_t buf[32];
		const int64_t m = ImmMantissa();
		const int n = std::swprintf(buf, sizeof(buf) / sizeof(buf[0]),
		                            L"%lld", static_cast<long long>(m));
		return wxString(buf, n > 0 ? static_cast<size_t>(n) : 0u);
	}

	BigImpl b;
	LoadBig(b);
	if (b.IsZero()) return wxT("0");

	// Build digit string directly into a back-filled wchar_t buffer.
	// No prepend, no per-char Append, no string-conversion.
	const size_t cap = b.limbs.size() * 10 + 32;
	std::vector<wchar_t> buf(cap);
	wchar_t* end = buf.data() + cap;
	wchar_t* p   = end;

	std::vector<uint32_t> tmp = b.limbs;
	while (!BigImpl::IsZeroMag(tmp)) {
		uint32_t rem = BigImpl::DivMagSmall(tmp, 1000000000u);
		// 9 digits LSB-first, walking backwards.
		for (int i = 0; i < 9; ++i) {
			*--p = static_cast<wchar_t>(L'0' + (rem % 10));
			rem /= 10;
		}
	}
	// Trim leading zeros from the top 9-digit chunk.
	while (p < end - 1 && *p == L'0') ++p;

	const size_t magLen = static_cast<size_t>(end - p);
	wxString out;

	if (b.exp >= 0) {
		out.reserve(magLen + static_cast<size_t>(b.exp) + 1);
		if (b.negative) out += wxT('-');
		out += wxString(p, magLen);
		if (b.exp > 0) out.append(static_cast<size_t>(b.exp), wxT('0'));
	} else {
		const size_t fracLen = static_cast<size_t>(-b.exp);
		if (b.negative) out += wxT('-');
		if (magLen > fracLen) {
			const size_t intLen = magLen - fracLen;
			out += wxString(p,          intLen);
			out += wxT('.');
			out += wxString(p + intLen, fracLen);
		} else {
			out += wxT("0.");
			out.append(fracLen - magLen, wxT('0'));
			out += wxString(p, magLen);
		}
		// Trim trailing zeros after the decimal point — they're
		// semantically redundant (5.00 == 5; 0.50 == 0.5). Division
		// in ttmath/ibNumber produces high-precision results like
		// 3.000000000000000000000000000000 even for integer-valued
		// quotients; without trim, Message(15/5) prints 30 zeros.
		// Only fires on the b.exp < 0 branch — pure integers with
		// shifted exponent (1000000 = mantissa 1, exp +6) never enter
		// here, so their trailing zeros stay (they're significant).
		while (!out.IsEmpty() && out.Last() == wxT('0'))
			out.RemoveLast();
		if (!out.IsEmpty() && out.Last() == wxT('.'))
			out.RemoveLast();
	}
	return out;
}

wxString ibNumber::ToString(const Format& fmt) const
{
	// Single-pass formatter: generates the magnitude digits once into a wchar_t
	// scratch buffer, then walks it forward emitting sign, int part with group
	// separators, custom decimal separator, fraction part with leading zeros.
	// No std::string, no intermediate wxString splits, no Find/Mid.

	BigImpl b;
	LoadBig(b);

	// Apply rounding on the local BigImpl. We cheat by going through a temporary
	// ibNumber (cheap — Round packs back into immediate when result fits).
	if (fmt.fracDigits >= 0) {
		ibNumber tmp;
		tmp.StoreBig(b);
		tmp = tmp.Round(fmt.fracDigits);
		tmp.LoadBig(b);
	}

	// Materialise magnitude digits LSB-first → walk back-to-front.
	const size_t cap = b.limbs.size() * 10 + 32;
	std::vector<wchar_t> buf(cap);
	wchar_t* end = buf.data() + cap;
	wchar_t* p   = end;

	if (b.IsZero()) {
		*--p = L'0';
	} else {
		std::vector<uint32_t> tmp = b.limbs;
		while (!BigImpl::IsZeroMag(tmp)) {
			uint32_t rem = BigImpl::DivMagSmall(tmp, 1000000000u);
			for (int i = 0; i < 9; ++i) {
				*--p = static_cast<wchar_t>(L'0' + (rem % 10));
				rem /= 10;
			}
		}
		while (p < end - 1 && *p == L'0') ++p;
	}
	const size_t magLen = static_cast<size_t>(end - p);

	// Layout:
	//   exp >= 0: int = magLen digits at p, plus `exp` trailing zeros; no fraction.
	//   exp <  0: e = -exp.
	//             if magLen > e: int = first (magLen - e) digits, frac = last e.
	//             else:           int = 0 (write "0" placeholder), frac has
	//                             (e - magLen) leading zeros, then magLen digits.
	size_t intDigitsAtP = 0;     // how many digits at p belong to integer part
	size_t intTrailingZeros = 0; // for exp > 0
	size_t fracLeadingZeros = 0;
	size_t fracDigitsAtP    = 0;
	const wchar_t* fracStart = p;

	bool hasFraction = false;
	if (b.exp >= 0) {
		intDigitsAtP     = magLen;
		intTrailingZeros = static_cast<size_t>(b.exp);
	} else {
		const size_t e = static_cast<size_t>(-b.exp);
		hasFraction = true;
		if (magLen > e) {
			intDigitsAtP    = magLen - e;
			fracDigitsAtP   = e;
			fracStart       = p + intDigitsAtP;
		} else {
			intDigitsAtP     = 0;
			fracDigitsAtP    = magLen;
			fracLeadingZeros = e - magLen;
			fracStart        = p;
		}
	}
	const size_t intLen = intDigitsAtP + intTrailingZeros;

	// `precision` (cap total significant digits, trim trailing fraction zeros)
	// is folded in *after* the layout — operates on what we'd emit.
	size_t fracLeadingEmit = fracLeadingZeros;
	size_t fracDigitsEmit  = fracDigitsAtP;
	if (fmt.precision >= 0 && hasFraction) {
		const int allowed = fmt.precision - static_cast<int>(intLen);
		if (allowed < 0) {
			fracLeadingEmit = 0;
			fracDigitsEmit  = 0;
			hasFraction     = false;
		} else if (static_cast<size_t>(allowed) < fracLeadingEmit + fracDigitsEmit) {
			if (static_cast<size_t>(allowed) <= fracLeadingEmit) {
				fracLeadingEmit = static_cast<size_t>(allowed);
				fracDigitsEmit  = 0;
			} else {
				fracDigitsEmit = static_cast<size_t>(allowed) - fracLeadingEmit;
			}
		}
		// Trim trailing zeros from fraction.
		while (fracDigitsEmit > 0 && fracStart[fracDigitsEmit - 1] == L'0') --fracDigitsEmit;
		if (fracDigitsEmit == 0 && fracLeadingEmit == 0) hasFraction = false;
	}

	// minIntDigits: pad integer part with leading '0' to this width.
	// "0" placeholder for an empty int part counts as one digit.
	const size_t baseIntEmit = (intLen == 0) ? 1u : intLen;
	const size_t padCount = (fmt.minIntDigits > 0
	                         && static_cast<size_t>(fmt.minIntDigits) > baseIntEmit)
	    ? static_cast<size_t>(fmt.minIntDigits) - baseIntEmit
	    : 0;
	const size_t emitIntLen = baseIntEmit + padCount;

	// Pre-size output.
	const bool useGroups = (fmt.groupSize > 0 && fmt.groupSep != 0
	                        && emitIntLen > static_cast<size_t>(fmt.groupSize));
	const size_t groupSepCount = useGroups
	    ? (emitIntLen - 1) / static_cast<size_t>(fmt.groupSize)
	    : 0;
	const size_t totalSize  = (b.negative && !b.IsZero() ? 1u : 0u)
	                        + emitIntLen + groupSepCount
	                        + (hasFraction ? 1u : 0u)
	                        + fracLeadingEmit + fracDigitsEmit;

	wxString result;
	result.reserve(totalSize);

	if (b.negative && !b.IsZero()) result += wxT('-');

	// Integer part — leading-zero pad, then digits, with optional group separator.
	if (useGroups) {
		for (size_t i = 0; i < emitIntLen; ++i) {
			if (i > 0 && (emitIntLen - i) % static_cast<size_t>(fmt.groupSize) == 0)
				result += fmt.groupSep;
			if (i < padCount)                       result += wxT('0');
			else if (intLen == 0)                   result += wxT('0');     // base placeholder
			else {
				const size_t j = i - padCount;
				result += (j < intDigitsAtP) ? p[j] : wxT('0');             // trailing zeros for exp>0
			}
		}
	} else {
		// Fast path — append leading pad, slice, trailing zeros without per-char condition.
		// append(ptr, n) writes straight into reserved storage; wxString(ptr, n)
		// would allocate a temporary first.
		if (padCount > 0)         result.append(padCount, wxT('0'));
		if (intLen == 0)          result += wxT('0');
		else {
			if (intDigitsAtP > 0)     result.append(p, intDigitsAtP);
			if (intTrailingZeros > 0) result.append(intTrailingZeros, wxT('0'));
		}
	}

	if (hasFraction) {
		result += fmt.decimalSep;
		if (fracLeadingEmit > 0) result.append(fracLeadingEmit, wxT('0'));
		if (fracDigitsEmit > 0)  result.append(fracStart, fracDigitsEmit);
	}
	return result;
}

bool ibNumber::FromString(const wxString& s)
{
	BigImpl big;
	if (!TryParseString(s, big)) {
		Clear();
		return false;
	}
	StoreBig(big);
	return true;
}

// ---- BLOB serialisation --------------------------------------------------------------
//
// Layout (little-endian, fixed):
//   [1 byte]    sign (0 = positive/zero, 1 = negative)
//   [4 bytes]   exp10 (int32_t)
//   [4 bytes]   limb_count (uint32_t)
//   [4*N bytes] limbs uint32_t LE, LSB first
// Min size = 9 bytes (zero — limb_count == 0). Round-trip exact.

void ibNumber::GetBuffer(wxMemoryBuffer& out) const
{
	// Zero is the most common runtime value (cleared records, default-init
	// fields). Encode it as an empty buffer — no header bytes, no allocation,
	// no I/O on chunked writes. SetBuffer recovers zero from len == 0.
	out.SetDataLen(0);
	if (IsZero()) return;

	BigImpl b;
	LoadBig(b);
	BigImpl::TrimMag(b.limbs);

	const uint32_t cnt   = static_cast<uint32_t>(b.limbs.size());
	const size_t   bytes = static_cast<size_t>(9) + static_cast<size_t>(cnt) * 4;

	uint8_t* dst = static_cast<uint8_t*>(out.GetWriteBuf(bytes));

	*dst++ = b.negative ? 1u : 0u;

	const uint32_t expU = static_cast<uint32_t>(b.exp);
	*dst++ = static_cast<uint8_t>( expU        & 0xFF);
	*dst++ = static_cast<uint8_t>((expU >>  8) & 0xFF);
	*dst++ = static_cast<uint8_t>((expU >> 16) & 0xFF);
	*dst++ = static_cast<uint8_t>((expU >> 24) & 0xFF);

	*dst++ = static_cast<uint8_t>( cnt        & 0xFF);
	*dst++ = static_cast<uint8_t>((cnt >>  8) & 0xFF);
	*dst++ = static_cast<uint8_t>((cnt >> 16) & 0xFF);
	*dst++ = static_cast<uint8_t>((cnt >> 24) & 0xFF);

	for (uint32_t l : b.limbs) {
		*dst++ = static_cast<uint8_t>( l        & 0xFF);
		*dst++ = static_cast<uint8_t>((l >>  8) & 0xFF);
		*dst++ = static_cast<uint8_t>((l >> 16) & 0xFF);
		*dst++ = static_cast<uint8_t>((l >> 24) & 0xFF);
	}
	out.UngetWriteBuf(bytes);
}

wxMemoryBuffer ibNumber::GetBuffer() const
{
	wxMemoryBuffer out;
	GetBuffer(out);
	return out;
}

bool ibNumber::SetBuffer(const void* data, size_t len)
{
	// Empty buffer → zero, mirroring GetBuffer's compact encoding.
	if (len == 0) { SetZero(); return true; }
	if (data == nullptr || len < 9) return false;
	const uint8_t* p = static_cast<const uint8_t*>(data);

	BigImpl b;
	b.negative = (p[0] != 0);

	const uint32_t expU = static_cast<uint32_t>(p[1])
	                   | (static_cast<uint32_t>(p[2]) <<  8)
	                   | (static_cast<uint32_t>(p[3]) << 16)
	                   | (static_cast<uint32_t>(p[4]) << 24);
	b.exp = static_cast<int32_t>(expU);

	const uint32_t cnt = static_cast<uint32_t>(p[5])
	                   | (static_cast<uint32_t>(p[6]) <<  8)
	                   | (static_cast<uint32_t>(p[7]) << 16)
	                   | (static_cast<uint32_t>(p[8]) << 24);

	if (len < static_cast<size_t>(9) + static_cast<size_t>(cnt) * 4) return false;

	b.limbs.reserve(cnt);
	const uint8_t* lp = p + 9;
	for (uint32_t i = 0; i < cnt; ++i) {
		const uint32_t l = static_cast<uint32_t>(lp[0])
		                | (static_cast<uint32_t>(lp[1]) <<  8)
		                | (static_cast<uint32_t>(lp[2]) << 16)
		                | (static_cast<uint32_t>(lp[3]) << 24);
		b.limbs.push_back(l);
		lp += 4;
	}
	BigImpl::TrimMag(b.limbs);
	if (b.IsZero()) b.negative = false;

	StoreBig(b);
	return true;
}

bool ibNumber::SetBuffer(const wxMemoryBuffer& in)
{
	return SetBuffer(in.GetData(), in.GetDataLen());
}

// Chunk-ID owned by ibNumber: callers don't pick it. Stream R/W of an
// ibNumber wraps the blob in a w_chunk/r_chunk envelope with this ID, so
// readers find the exact same chunk the writer produced.
namespace { constexpr uint64_t kIbNumberChunk = 0x023456555ULL; }

bool ibNumber::GetBuffer(ibWriterMemory& writer) const
{
	wxMemoryBuffer buf;
	GetBuffer(buf);
	if (buf.GetDataLen() == 0) return false;
	writer.w_chunk(kIbNumberChunk, buf);
	return true;
}

bool ibNumber::SetBuffer(const ibReaderMemory& reader)
{
	wxMemoryBuffer buf;
	if (!reader.r_chunk(kIbNumberChunk, buf)) return false;
	return SetBuffer(buf);
}

// ---- 128-bit raw integer ------------------------------------------------------------

void ibNumber::To128Bytes(uint8_t out[16]) const
{
	BigImpl b;
	LoadBig(b);

	// Scale to integer (exp10 == 0). Truncate toward zero, like ToInt64 path.
	if (b.exp > 0) {
		b.MulMagPow10(b.exp);
		b.exp = 0;
	} else if (b.exp < 0) {
		BigImpl divisor; divisor.limbs.push_back(1u);
		divisor.MulMagPow10(-b.exp);
		std::vector<uint32_t> q, r;
		BigImpl::DivModMag(b.limbs, divisor.limbs, q, r);
		b.limbs = std::move(q);
		if (b.IsZero()) b.negative = false;
		b.exp = 0;
	}

	// Pad / truncate magnitude to exactly 4 limbs (128 bits).
	uint32_t limbs[4] = { 0, 0, 0, 0 };
	const size_t n = std::min<size_t>(b.limbs.size(), 4);
	for (size_t i = 0; i < n; ++i) limbs[i] = b.limbs[i];

	// Two's-complement encoding for negative values.
	if (b.negative) {
		uint32_t inv[4] = { ~limbs[0], ~limbs[1], ~limbs[2], ~limbs[3] };
		unsigned char carry = 1;
		for (int i = 0; i < 4; ++i) {
			carry = ibAddCarry32(carry, inv[i], 0u, &limbs[i]);
		}
	}

	for (int i = 0; i < 4; ++i) {
		out[i * 4 + 0] = static_cast<uint8_t>( limbs[i]        & 0xFFu);
		out[i * 4 + 1] = static_cast<uint8_t>((limbs[i] >>  8) & 0xFFu);
		out[i * 4 + 2] = static_cast<uint8_t>((limbs[i] >> 16) & 0xFFu);
		out[i * 4 + 3] = static_cast<uint8_t>((limbs[i] >> 24) & 0xFFu);
	}
}

void ibNumber::From128Bytes(const uint8_t bytes[16])
{
	uint32_t limbs[4];
	for (int i = 0; i < 4; ++i) {
		limbs[i] =  static_cast<uint32_t>(bytes[i * 4 + 0])
		         | (static_cast<uint32_t>(bytes[i * 4 + 1]) <<  8)
		         | (static_cast<uint32_t>(bytes[i * 4 + 2]) << 16)
		         | (static_cast<uint32_t>(bytes[i * 4 + 3]) << 24);
	}

	const bool negative = (limbs[3] & 0x80000000u) != 0;
	if (negative) {
		// Decode two's-complement: magnitude = ~(x - 1).
		unsigned char borrow = 1;
		uint32_t tmp[4];
		for (int i = 0; i < 4; ++i) {
			borrow = ibSubBorrow32(borrow, limbs[i], 0u, &tmp[i]);
			limbs[i] = ~tmp[i];
		}
	}

	BigImpl b;
	b.negative = negative;
	b.exp      = 0;
	b.limbs.assign(limbs, limbs + 4);
	BigImpl::TrimMag(b.limbs);
	if (b.IsZero()) b.negative = false;
	StoreBig(b);
}

// ---- stream insertion ----------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const ibNumber& n)
{
	const wxString s = n.ToString();
	const wxScopedCharBuffer utf = s.utf8_str();
	return os << utf.data();
}

std::wostream& operator<<(std::wostream& os, const ibNumber& n)
{
	return os << n.ToString().ToStdWstring();
}
