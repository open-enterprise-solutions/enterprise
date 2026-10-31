// =============================================================================
// ibString — the module: the text behind the facade (fstring.h).
//
// How a text is MADE, written into and freed lives here and nowhere else: Impl — one block
// holding the count of the ibStrings that share it, the length, the room and the characters,
// taken from the per-thread pool (core/memory/memoryPool.h). The header shows one pointer and
// the block's header (Shared), so copying and reading need no call into this module.
// =============================================================================

#include "core/fstring.h"
#include "core/memory/memoryPool.h"   // the blocks a text is made in

#include <atomic>
#include <bit>           // std::countr_zero, std::endian — TerminatedLength reads a word for four characters
#if defined(__IBMSW__)
#include <intrin.h>      // _BitScanForward(64) — the place of TerminatedLength's zero
#endif
#include <cerrno>        // ToLong & co read ERANGE, as wx does
#include <climits>       // ToInt's range
#include <cstdarg>       // Print — the arguments Format hands on
#include <cwchar>        // vswprintf, wcstol & co, wcslen — the C library's wide side, at the boundary
#include <functional>    // std::less — is an appended piece inside this very text
#include <locale>
#if defined(__IBOSX__)
#include <xlocale.h>     // newlocale / uselocale — Print's own UTF-8 locale
#endif
#include <new>
#include <sstream>       // ToCDouble — the classic locale, whatever the process has set
#include <stdexcept>     // std::out_of_range — insert past the end, as basic_string refused it

namespace {
using Traits = std::char_traits<ibChar>;   // copy / move / assign / length — the characters in their block
} // namespace

// --- the text and its owners ------------------------------------------------------
//
// ⭐ ONE BLOCK PER TEXT: the owners' count, the length, the room, then the characters and their terminator —
// one allocation from the pool. It was the count in one block with a std::basic_string in it, and the characters
// in a second (445a49364): a new text paid two allocations, a slice two, a concatenation three (the copy, then
// its growth).
// The header (the count, the length, the room — Shared, in fstring.h, so reading is inline) and then the
// characters: making and freeing the block are this module's.
struct ibString::Impl : ibString::Shared
{
	explicit Impl(size_t cap) noexcept : Shared(1, cap) { Chars()[0] = ibT('\0'); }

	static size_t BytesFor(size_t cap) noexcept { return sizeof(Shared) + (cap + 1) * sizeof(ibChar); }

	// Room for `cap` characters at least — and for as many more as the pool's block holds anyway. The class is
	// counted once: it says which block and how big it is.
	//
	// FORCED, with the disassembly in hand (2026-10-28, Release without LTO): left to the inliner, Make and Copy
	// stayed calls, so a short text was made through four frames — the constructor, Copy, Make, AllocateClass. Pasted
	// in, a 54-character text made and let go takes 29 ns where it took 37 (IbStringBench, Windows x64).
	static IB_FORCEINLINE Impl* Make(size_t cap) {
		const size_t asked = BytesFor(cap);
		const int c = ibMemoryPool::detail::ClassOf(asked);
		const size_t bytes = c < 0 ? asked : ibMemoryPool::detail::kClasses[c];
		void* const place = c < 0 ? ::operator new(bytes) : ibMemoryPool::AllocateClass(c);
		return new (place) Impl((bytes - sizeof(Shared)) / sizeof(ibChar) - 1);
	}
	static IB_FORCEINLINE Impl* Copy(const ibChar* text, size_t len, size_t cap) {
		Impl* const impl = Make(cap < len ? len : cap);
		if (len != 0) Traits::copy(impl->Chars(), text, len);
		impl->SetLength(len);
		return impl;
	}
	void SetLength(size_t len) noexcept { m_len = len; Chars()[len] = ibT('\0'); }

	static Impl* Of(Shared* shared) noexcept { return static_cast<Impl*>(shared); }
	bool Alone() const noexcept { return m_refCount.load(std::memory_order_acquire) == 1; }
};

void ibString::Free(Shared* shared) noexcept
{
	Impl* const impl = Impl::Of(shared);
	const size_t bytes = Impl::BytesFor(impl->m_cap);
	impl->~Impl();
	ibMemoryPool::Deallocate(impl, bytes);
}

// A text somebody else holds is copied first, with exactly the room asked for — which is what a concatenation
// asks, so `a + b` is one block; one of its own without room grows by half again, so a string appended to in a
// loop is copied a logarithmic number of times.
ibChar* ibString::Own(size_t room)
{
	Impl* const held = m_impl != nullptr ? Impl::Of(m_impl) : nullptr;
	const size_t len = held != nullptr ? held->m_len : 0;
	if (room < len) room = len;
	if (held != nullptr && held->Alone()) {
		if (held->m_cap >= room) return held->Chars();
		const size_t grown = held->m_cap + held->m_cap / 2;
		if (room < grown) room = grown;
	}
	Impl* const own = Impl::Copy(held != nullptr ? held->Chars() : nullptr, len, room);
	Release(m_impl);
	m_impl = own;
	return own->Chars();
}

void ibString::SetLength(size_t length) noexcept { Impl::Of(m_impl)->SetLength(length); }

namespace {

bool IsSpace(ibChar c) noexcept {
	return c == ibT(' ') || c == ibT('\t') || c == ibT('\n') || c == ibT('\r') || c == ibT('\f') || c == ibT('\v');
}

// A piece of a text, made a text of its own — one block, the characters copied once.
ibString Slice(std::u16string_view piece) { return ibString(piece.data(), piece.size()); }

// WHERE A TERMINATED TEXT ENDS, four characters at a step. One at a time was two thirds of making a 54-character
// text: 31 ns, 11 with the length handed in, and 16 read this way (IbStringBench, Windows x64, 2026-10-28). Every
// read is a whole aligned word, so it never reaches into another page — but it may take up to three characters
// before the text and after its terminator, which the sanitizers would report as an overflow: hence the marks. The
// characters before the text are made non-zero; of the characters the test marks, the lowest is a true zero — a
// borrow only runs upward — so its place is the length, with no character-by-character loop at either end. FORCED
// on Windows, where MSVC left it a call (a four-character text paid ~0.7 ns for it); GCC refuses to force a function
// across a sanitizer mark in the ASan job, and pastes one this small in by itself in a release build.
#if defined(__IBMSW__)
__declspec(no_sanitize_address) IB_FORCEINLINE
#else
__attribute__((no_sanitize("address", "thread")))
#endif
size_t TerminatedLength(const ibChar* s) noexcept {
	static_assert(std::endian::native == std::endian::little, "the lowest bits are the first character");
#if defined(__IBMSW__)
	typedef uint64_t Word;
#else
	typedef uint64_t __attribute__((may_alias)) Word;   // read as characters elsewhere, as a word here
#endif
	constexpr uint64_t kLow  = 0x0001000100010001ull;   // one in each character
	constexpr uint64_t kHigh = 0x8000800080008000ull;   // a character's top bit — where the test marks a zero
	const uintptr_t at = reinterpret_cast<uintptr_t>(s);
	const Word* w = reinterpret_cast<const Word*>(at & ~static_cast<uintptr_t>(sizeof(Word) - 1));
	uint64_t word = *w | ((uint64_t{ 1 } << ((at & (sizeof(Word) - 1)) * 8)) - 1);   // the characters before s
	for (;;) {
		const uint64_t zero = (word - kLow) & ~word & kHigh;
		if (zero != 0) {
#if defined(__IBMSW__)
			unsigned long bit;
#  if defined(_WIN64)
			_BitScanForward64(&bit, zero);   // std::countr_zero asks the CPU for tzcnt at every call (__isa_available)
#  else
			if (!_BitScanForward(&bit, static_cast<unsigned long>(zero))) {   // 32-bit Windows: a half at a time
				_BitScanForward(&bit, static_cast<unsigned long>(zero >> 32));
				bit += 32;
			}
#  endif
#else
			const unsigned bit = static_cast<unsigned>(std::countr_zero(zero));
#endif
			const ibChar* const end = reinterpret_cast<const ibChar*>(w) + bit / 16;   // unsigned: a shift
			return static_cast<size_t>(end - s);
		}
		word = *++w;
	}
}

// …and a wide one, the boundary's (wxT, std::wstring): where wchar_t is two bytes it is this very text, read the same
// way — the naive scan was there too; where it is four, the C library's own.
#if WCHAR_MAX <= 0xFFFF
IB_FORCEINLINE size_t TerminatedLength(const wchar_t* s) noexcept { return TerminatedLength(reinterpret_cast<const ibChar*>(s)); }
#else
size_t TerminatedLength(const wchar_t* s) noexcept { return std::wcslen(s); }
#endif

// Two texts the same, a case-insensitive pair folded only where the characters differ — matching
// characters need no folding, and names that are the same usually match exactly.
bool SameText(const ibChar* a, size_t an, const ibChar* b, size_t bn, bool caseSensitive) noexcept {
	if (an != bn) return false;
	for (size_t i = 0; i < an; ++i) {
		if (a[i] == b[i]) continue;
		if (caseSensitive || ibToLower(a[i]) != ibToLower(b[i])) return false;
	}
	return true;
}

// The code point at `i` — a surrogate pair read as the one character it is, `i` left on its second half.
uint32_t CodePoint(std::u16string_view text, size_t& i) noexcept {
	const uint32_t unit = text[i];
	if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < text.size()) {
		const uint32_t low = text[i + 1];
		if (low >= 0xDC00 && low <= 0xDFFF) {
			++i;
			return 0x10000u + ((unit - 0xD800u) << 10) + (low - 0xDC00u);
		}
	}
	return unit;
}

size_t Utf8Length(uint32_t cp) noexcept {
	return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
}

char* EncodeUtf8(uint32_t cp, char* at) noexcept {
	if (cp < 0x80) {
		*at++ = static_cast<char>(cp);
	} else if (cp < 0x800) {
		*at++ = static_cast<char>(0xC0 | (cp >> 6));
		*at++ = static_cast<char>(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		*at++ = static_cast<char>(0xE0 | (cp >> 12));
		*at++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		*at++ = static_cast<char>(0x80 | (cp & 0x3F));
	} else {
		*at++ = static_cast<char>(0xF0 | (cp >> 18));
		*at++ = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		*at++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		*at++ = static_cast<char>(0x80 | (cp & 0x3F));
	}
	return at;
}

// A wide text as UTF-16 units. Where wchar_t is two bytes (Windows) it IS UTF-16, read where it lies; where it
// is four (Linux, macOS) it is UTF-32, and a character past the basic plane becomes a surrogate pair — counted,
// then laid down through a pointer, as ToUtf8 does.
#if WCHAR_MAX <= 0xFFFF
std::u16string_view Utf16(const wchar_t* s, size_t n) noexcept {
	return std::u16string_view(reinterpret_cast<const ibChar*>(s), n);
}
#else
size_t Utf16Length(uint32_t cp) noexcept { return cp > 0xFFFF ? 2 : 1; }

ibChar* EncodeUtf16(uint32_t cp, ibChar* at) noexcept {
	if (cp > 0xFFFF) {
		cp -= 0x10000;
		*at++ = static_cast<ibChar>(0xD800 + (cp >> 10));
		*at++ = static_cast<ibChar>(0xDC00 + (cp & 0x3FF));
	}
	else *at++ = static_cast<ibChar>(cp);
	return at;
}

// Nearly every text has all its characters in the basic plane, and then it is as long in UTF-16 as it was: the
// characters' bits ORed together tell it in one plain loop, which a compiler vectorises.
size_t Utf16Length(const wchar_t* s, size_t n) noexcept {
	uint32_t bits = 0;
	for (size_t i = 0; i < n; ++i) bits |= static_cast<uint32_t>(s[i]);
	if (bits <= 0xFFFF) return n;
	size_t units = 0;
	for (size_t i = 0; i < n; ++i) units += Utf16Length(static_cast<uint32_t>(s[i]));
	return units;
}

// The units laid down at `at`; `units` is Utf16Length(s, n). As many units as characters means no pair to make,
// and the characters are narrowed straight across — the other plain loop.
void EncodeUtf16(const wchar_t* s, size_t n, size_t units, ibChar* at) noexcept {
	if (units == n) {
		for (size_t i = 0; i < n; ++i) at[i] = static_cast<ibChar>(s[i]);
		return;
	}
	for (size_t i = 0; i < n; ++i) at = EncodeUtf16(static_cast<uint32_t>(s[i]), at);
}

std::u16string Utf16(const wchar_t* s, size_t n) {
	const size_t units = Utf16Length(s, n);
	std::u16string text(units, ibT('\0'));
	EncodeUtf16(s, n, units, text.data());
	return text;
}
#endif

// wx's ToNumeric: nothing parsed → false and *val untouched; parsed but not to the end → *val set,
// false; out of range → false.
template <class T, class R>
bool ToNumeric(const wchar_t* start, T* val, R (*convert)(const wchar_t*, wchar_t**, int), int base) {
	if (val == nullptr) return false;
	wchar_t* end = nullptr;
	const int saved = errno;
	errno = 0;
	const R result = convert(start, &end, base);
	const bool range = errno == ERANGE;
	errno = saved;
	if (end == start || range) return false;
	*val = static_cast<T>(result);
	return *end == L'\0';
}

} // namespace

// --- construction / copy / move ---------------------------------------------------

ibString::ibString(const ibChar* s)
{
	if (s != nullptr && *s != ibT('\0')) { const size_t n = TerminatedLength(s); m_impl = Impl::Copy(s, n, n); }
}
ibString::ibString(const ibChar* s, size_t n) { if (s != nullptr && n != 0) m_impl = Impl::Copy(s, n, n); }
ibString::ibString(ibChar c, size_t count)
{
	if (count == 0) return;
	Impl* const impl = Impl::Make(count);
	Traits::assign(impl->Chars(), count, c);
	impl->SetLength(count);
	m_impl = impl;
}
ibString::ibString(const wchar_t* ws) : ibString(ws, ws != nullptr ? TerminatedLength(ws) : 0) {}
ibString::ibString(const wchar_t* ws, size_t n)
{
	if (ws == nullptr || n == 0) return;
#if WCHAR_MAX <= 0xFFFF
	m_impl = Impl::Copy(reinterpret_cast<const ibChar*>(ws), n, n);
#else
	// Into the block itself: every text from wx crosses here, and a text made on the way to the block paid a
	// second allocation each time (100 ns against 21 for the bench's 54 characters, 2026-10-28).
	const size_t units = Utf16Length(ws, n);
	Impl* const impl = Impl::Make(units);
	EncodeUtf16(ws, n, units, impl->Chars());
	impl->SetLength(units);
	m_impl = impl;
#endif
}
ibString::ibString(const std::wstring& ws) : ibString(ws.data(), ws.size()) {}
ibString::ibString(const char* utf8) { if (utf8) SetUtf8(utf8, std::char_traits<char>::length(utf8)); }
// wx's own wide storage, by reference where the build keeps one.
ibString::ibString(const wxString& s) : ibString(s.ToStdWstring()) {}

// --- conversions -----------------------------------------------------------------

wxString ibString::ToWxString() const
{
#if WCHAR_MAX <= 0xFFFF
	return wxString(wc_str(), Len());
#else
	return wxString(wc_str());
#endif
}

#if WCHAR_MAX > 0xFFFF
ibWideBuffer ibString::wc_str() const
{
	const std::u16string_view text = View();
	ibWideBuffer wide;
	wide.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i)
		wide.push_back(static_cast<wchar_t>(CodePoint(text, i)));
	return wide;
}
#endif

// Counted, then laid down through a pointer: appended a byte at a time, a Debug build paid a call for every one,
// and a script's JSON text runs to tens of megabytes.
std::string ibString::ToUtf8() const
{
	const std::u16string_view text = View();
	size_t bytes = 0;
	for (size_t i = 0; i < text.size(); ++i)
		bytes += Utf8Length(CodePoint(text, i));
	std::string out(bytes, '\0');
	char* at = &out[0];
	for (size_t i = 0; i < text.size(); ++i)
		at = EncodeUtf8(CodePoint(text, i), at);
	return out;
}

std::ostream& operator<<(std::ostream& out, const ibString& s)
{
	return out << s.ToUTF8();
}

// Decoded straight into the block: a UTF-8 sequence is never fewer bytes than the characters it makes (four
// bytes are two UTF-16 units at most), so `n` characters of room are enough and nothing grows on the way.
void ibString::SetUtf8(const char* p, size_t n)
{
	Clear();
	if (n == 0) return;
	ibChar* const chars = Own(n);
	size_t len = 0;
	for (size_t i = 0; i < n; ) {
		const unsigned char b = static_cast<unsigned char>(p[i++]);
		uint32_t cp; int extra;
		if      (b < 0x80)        { cp = b;        extra = 0; }
		else if ((b >> 5) == 0x6) { cp = b & 0x1F; extra = 1; }
		else if ((b >> 4) == 0xE) { cp = b & 0x0F; extra = 2; }
		else if ((b >> 3) == 0x1E){ cp = b & 0x07; extra = 3; }
		else                      { cp = 0xFFFD;   extra = 0; }  // invalid lead
		for (int k = 0; k < extra && i < n; ++k) {
			const unsigned char cb = static_cast<unsigned char>(p[i]);
			if ((cb >> 6) != 0x2) break;                         // invalid continuation
			cp = (cp << 6) | (cb & 0x3F);
			++i;
		}
		if (cp > 0xFFFF) {                                       // past the basic plane: a surrogate pair
			cp -= 0x10000;
			chars[len++] = static_cast<ibChar>(0xD800 + (cp >> 10));
			chars[len++] = static_cast<ibChar>(0xDC00 + (cp & 0x3FF));
			continue;
		}
		chars[len++] = static_cast<ibChar>(cp);
	}
	SetLength(len);
	// …and a text of characters longer than a byte has room for its BYTES: Cyrillic twice what it takes, CJK
	// three times, for as long as the string lives - and a string a parser made lives in the value it built.
	// A LONG one moves to a block of its own size, once: its block is past the pool (a plain allocation of
	// exactly what was asked), so the room is real memory. A short one stays: inside the pool the slack is one
	// size class at most and the block goes back to be reused, while the move is a second allocation and a
	// copy on every decode - "Privet" x4 asks twice the room by its bytes as by its characters, and moving it
	// cost the decode about 30% (2026-09-29, measured on a 4-byte character). An ASCII text never moves either.
	if (len < n && Impl::BytesFor(Impl::Of(m_impl)->m_cap) > ibMemoryPool::detail::kClasses[ibMemoryPool::detail::kNum - 1])
		*this = ibString(chars, len);
}

ibString ibString::FromUTF8(const char* s, size_t n)
{
	ibString r;
	if (s != nullptr) r.SetUtf8(s, n == npos ? std::char_traits<char>::length(s) : n);
	return r;
}

void ibString::AppendCodepoint(uint32_t cp)
{
	if (cp > 0xFFFF) {                                           // past the basic plane: a surrogate pair
		cp -= 0x10000;
		push_back(static_cast<ibChar>(0xD800 + (cp >> 10)));
		push_back(static_cast<ibChar>(0xDC00 + (cp & 0x3FF)));
		return;
	}
	push_back(static_cast<ibChar>(cp));
}

// --- query / element access ---------------------------------------------------------

bool ibString::IsBlank() const noexcept
{
	for (ibChar c : View()) if (!IsSpace(c)) return false;
	return true;
}

void ibString::Clear() noexcept
{
	if (m_impl != nullptr && Impl::Of(m_impl)->Alone()) Impl::Of(m_impl)->SetLength(0);
	else { Release(m_impl); m_impl = nullptr; }
}

ibChar& ibString::operator[](size_t i)       { return Own()[i]; }
void      ibString::SetChar(size_t i, ibChar c) { Own()[i] = c; }
ibChar* ibString::begin() { return IsEmpty() ? nullptr : Own(); }
ibChar* ibString::end()   { return IsEmpty() ? nullptr : Own() + Len(); }

// --- the std::basic_string spelling ----------------------------------------------------

size_t ibString::find_first_of(const ibString& set, size_t start) const { return View().find_first_of(set.View(), start); }
size_t ibString::find_last_of(const ibString& set, size_t start) const  { return View().find_last_of(set.View(), start); }
size_t ibString::find_first_not_of(const ibString& set, size_t start) const { return View().find_first_not_of(set.View(), start); }
ibString ibString::substr(size_t pos, size_t count) const { return Slice(View().substr(pos, count)); }

ibString& ibString::erase(size_t pos, size_t count)
{
	const size_t len = Len();
	if (pos >= len) return *this;
	if (count > len - pos) count = len - pos;
	if (count == 0) return *this;
	ibChar* const chars = Own();
	Traits::move(chars + pos, chars + pos + count, len - pos - count);
	SetLength(len - count);
	return *this;
}

ibString& ibString::insert(size_t pos, const ibString& s)
{
	if (s.IsEmpty()) return *this;
	const size_t len = Len();
	if (pos > len) throw std::out_of_range("ibString::insert");
	const ibString piece(s);   // `s` may be this very string: held, the text is copied before it is written
	const size_t n = piece.Len();
	ibChar* const chars = Own(len + n);
	Traits::move(chars + pos + n, chars + pos, len - pos);
	Traits::copy(chars + pos, piece.c_str(), n);
	SetLength(len + n);
	return *this;
}

// The characters may lie in this very text (`s.append(s.c_str(), n)`), which may have to move: then they are
// taken out first.
ibString& ibString::append(const ibChar* s, size_t n)
{
	if (n == 0) return *this;
	const size_t len = Len();
	if (m_impl != nullptr) {
		const ibChar* const own = Impl::Of(m_impl)->Chars();
		if (std::less_equal<const ibChar*>()(own, s) && std::less<const ibChar*>()(s, own + len + 1)) {
			const std::u16string piece(s, n);
			return append(piece.data(), n);
		}
	}
	ibChar* const chars = Own(len + n);
	Traits::copy(chars + len, s, n);
	SetLength(len + n);
	return *this;
}

void ibString::push_back(ibChar c)
{
	const size_t len = Len();
	ibChar* const chars = Own(len + 1);
	chars[len] = c;
	SetLength(len + 1);
}

void ibString::reserve(size_t n) { if (n > Len()) Own(n); }

void ibString::resize(size_t n, ibChar c)
{
	const size_t len = Len();
	if (n == len) return;
	ibChar* const chars = Own(n);
	if (n > len) Traits::assign(chars + len, n - len, c);
	SetLength(n);
}

// --- slicing / search ---------------------------------------------------------------------

ibString ibString::Mid(size_t first, size_t count) const
{
	const std::u16string_view text = View();
	if (first >= text.size()) return ibString();
	return Slice(text.substr(first, count));
}
ibString ibString::Left(size_t count) const { return Slice(View().substr(0, count)); }
ibString ibString::Right(size_t count) const
{
	const std::u16string_view text = View();
	return count >= text.size() ? *this : Slice(text.substr(text.size() - count));
}
size_t ibString::Freq(ibChar c) const { size_t n = 0; for (ibChar x : View()) if (x == c) ++n; return n; }

// `rest` may be this very string: what is returned is made before it is written.
bool ibString::StartsWith(const ibString& p, ibString* rest) const
{
	const std::u16string_view text = View(), prefix = p.View();
	if (text.size() < prefix.size() || text.compare(0, prefix.size(), prefix) != 0) return false;
	if (rest != nullptr) *rest = Slice(text.substr(prefix.size()));
	return true;
}

bool ibString::EndsWith(const ibString& s, ibString* rest) const
{
	const std::u16string_view text = View(), suffix = s.View();
	if (text.size() < suffix.size() || text.compare(text.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
	if (rest != nullptr) *rest = Slice(text.substr(0, text.size() - suffix.size()));
	return true;
}

ibString ibString::BeforeFirst(ibChar c, ibString* rest) const
{
	const std::u16string_view text = View();
	const size_t at = text.find(c);
	// The answer is taken BEFORE `rest` is touched: `rest` may be this very string (s.BeforeFirst(c, &s), the walk
	// along a list), and clearing it first answered an empty string for the list's last item.
	if (at == npos) { ibString whole = *this; if (rest != nullptr) rest->Clear(); return whole; }
	ibString before = Slice(text.substr(0, at));
	if (rest != nullptr) *rest = Slice(text.substr(at + 1));
	return before;
}

ibString ibString::AfterFirst(ibChar c) const
{
	const std::u16string_view text = View();
	const size_t at = text.find(c);
	return at == npos ? ibString() : Slice(text.substr(at + 1));
}

ibString ibString::BeforeLast(ibChar c, ibString* rest) const
{
	const std::u16string_view text = View();
	const size_t at = text.rfind(c);
	if (at == npos) { if (rest != nullptr) *rest = *this; return ibString(); }
	ibString before = Slice(text.substr(0, at));
	if (rest != nullptr) *rest = Slice(text.substr(at + 1));
	return before;
}

ibString ibString::AfterLast(ibChar c) const
{
	const std::u16string_view text = View();
	const size_t at = text.rfind(c);
	return at == npos ? *this : Slice(text.substr(at + 1));
}

bool ibString::Matches(const ibString& maskText) const
{
	const ibChar* mask = maskText.c_str();
	const ibChar* text = c_str();
	const ibChar* lastStarInText = nullptr;   // where '*' last matched, to backtrack to
	const ibChar* lastStarInMask = nullptr;
match:
	for (; *mask != ibT('\0'); ++mask, ++text) {
		switch (*mask) {
		case ibT('?'):
			if (*text == ibT('\0')) return false;
			break;
		case ibT('*'): {
			lastStarInText = text;
			lastStarInMask = mask;
			while (*mask == ibT('*') || *mask == ibT('?')) ++mask;   // metacharacters right after it add nothing
			if (*mask == ibT('\0')) return true;
			const std::u16string_view rest(mask);
			const size_t nextMeta = rest.find_first_of(ibT("*?"));
			const size_t run = nextMeta != npos ? nextMeta : rest.size();
			const size_t found = std::u16string_view(text).find(rest.substr(0, run));
			if (found == npos) return false;
			text += found + run - 1;   // -1: the loop steps past it
			mask += run - 1;
			break;
		}
		default:
			if (*mask != *text) return false;
			break;
		}
	}
	if (*text == ibT('\0')) return true;
	if (lastStarInText != nullptr) {   // failed: let the last '*' swallow one more character
		text = lastStarInText + 1;
		mask = lastStarInMask;
		lastStarInText = nullptr;
		goto match;
	}
	return false;
}

// --- case ------------------------------------------------------------------------------------

ibString ibString::Lower() const { ibString r(c_str(), Len()); r.MakeLower(); return r; }
ibString ibString::Upper() const { ibString r(c_str(), Len()); r.MakeUpper(); return r; }

ibString& ibString::MakeLower()
{
	if (IsEmpty()) return *this;
	ibChar* const chars = Own();
	for (size_t i = 0, n = Len(); i < n; ++i) chars[i] = ibToLower(chars[i]);
	return *this;
}

ibString& ibString::MakeUpper()
{
	if (IsEmpty()) return *this;
	ibChar* const chars = Own();
	for (size_t i = 0, n = Len(); i < n; ++i) chars[i] = ibToUpper(chars[i]);
	return *this;
}

// --- trim / pad -------------------------------------------------------------------------------

ibString& ibString::Trim(bool fromRight)
{
	const std::u16string_view text = View();
	size_t b = 0, e = text.size();
	if (fromRight) { while (e > b && IsSpace(text[e - 1])) --e; }
	else           { while (b < e && IsSpace(text[b]))     ++b; }
	if (e - b == text.size()) return *this;
	return fromRight ? erase(e) : erase(0, b);
}

ibString ibString::TrimAll() const
{
	const std::u16string_view text = View();
	size_t b = 0, e = text.size();
	while (b < e && IsSpace(text[b]))     ++b;
	while (e > b && IsSpace(text[e - 1])) --e;
	return b == 0 && e == text.size() ? *this : Slice(text.substr(b, e - b));
}

ibString ibString::Strip(int how) const
{
	ibString s(*this);
	if (how & leading)  s.Trim(false);
	if (how & trailing) s.Trim(true);
	return s;
}

ibString& ibString::Pad(size_t count, ibChar c, bool fromRight)
{
	if (count == 0) return *this;
	if (fromRight) return Append(c, count);
	const size_t len = Len();
	ibChar* const chars = Own(len + count);
	Traits::move(chars + count, chars, len);
	Traits::assign(chars, count, c);
	SetLength(len + count);
	return *this;
}

ibString& ibString::Truncate(size_t len)     { if (len < Len()) erase(len); return *this; }
ibString& ibString::RemoveLast(size_t n)     { if (n != 0 && !IsEmpty()) erase(n < Len() ? Len() - n : 0); return *this; }

// --- numbers (ported from wxString) — the C library reads them wide -------------------------------

bool ibString::IsNumber() const
{
	const std::u16string_view text = View();
	size_t i = (!text.empty() && (text[0] == ibT('-') || text[0] == ibT('+'))) ? 1 : 0;
	for (; i < text.size(); ++i) if (text[i] < ibT('0') || text[i] > ibT('9')) return false;
	return true;
}

bool ibString::ToLong(long* val, int base) const { return ToNumeric(wc_str(), val, &std::wcstol, base); }
bool ibString::ToULong(unsigned long* val, int base) const { return ToNumeric(wc_str(), val, &std::wcstoul, base); }
bool ibString::ToLongLong(long long* val, int base) const { return ToNumeric(wc_str(), val, &std::wcstoll, base); }
bool ibString::ToULongLong(unsigned long long* val, int base) const { return ToNumeric(wc_str(), val, &std::wcstoull, base); }
bool ibString::ToInt(int* val, int base) const {
	long long wide = 0;
	if (!ToLongLong(&wide, base) || wide < INT_MIN || wide > INT_MAX) return false;
	if (val != nullptr) *val = static_cast<int>(wide);
	return true;
}
bool ibString::ToUInt(unsigned int* val, int base) const {
	unsigned long long wide = 0;
	if (!ToULongLong(&wide, base) || wide > UINT_MAX) return false;
	if (val != nullptr) *val = static_cast<unsigned int>(wide);
	return true;
}
bool ibString::ToDouble(double* val) const {
	return ToNumeric(wc_str(), val, +[](const wchar_t* s, wchar_t** end, int) { return std::wcstod(s, end); }, 0);
}
bool ibString::ToCDouble(double* val) const {
	if (val == nullptr) return false;
	std::wistringstream in(wc_str());
	in.imbue(std::locale::classic());
	double result = 0;
	in >> result;
	if (in.fail()) return false;
	*val = result;
	return in.eof() || in.peek() == std::char_traits<wchar_t>::eof();
}

// --- mutation / concat ------------------------------------------------------------------------------

ibString& ibString::operator+=(const ibString& o)
{
	if (o.IsEmpty()) return *this;
	if (IsEmpty()) return *this = o;
	return append(o.c_str(), o.Len());   // `o` may be this very string — append takes it out first
}

ibString& ibString::operator+=(const ibChar* s) { if (s != nullptr && *s != ibT('\0')) append(s, TerminatedLength(s)); return *this; }
ibString& ibString::operator+=(const wchar_t* s)
{
	if (s != nullptr && *s != L'\0') { const auto units = Utf16(s, TerminatedLength(s)); append(units.data(), units.size()); }
	return *this;
}
ibString& ibString::operator+=(ibChar c) { push_back(c); return *this; }
ibString& ibString::operator+=(const wxString& s)
{
	if (s.empty()) return *this;
	const auto& wide = s.ToStdWstring();   // wx's own wide storage, by reference where the build keeps one
	const auto units = Utf16(wide.data(), wide.size());
	return append(units.data(), units.size());
}

ibString& ibString::Append(ibChar c, size_t count)
{
	if (count == 0) return *this;
	const size_t len = Len();
	ibChar* const chars = Own(len + count);
	Traits::assign(chars + len, count, c);
	SetLength(len + count);
	return *this;
}

// Written into a new text piece by piece and put in place at the end. `from` / `to` are held for the call, so
// either may be this very string.
size_t ibString::Replace(const ibString& fromText, const ibString& toText, bool replaceAll)
{
	const ibString from(fromText), to(toText);
	const std::u16string_view what = from.View(), with = to.View(), text = View();
	if (what.empty()) return 0;
	size_t at = text.find(what);
	if (at == npos) return 0;
	ibString result;
	result.reserve(text.size() + (with.size() > what.size() ? with.size() - what.size() : 0));
	size_t count = 0, done = 0;
	do {
		result.append(text.data() + done, at - done);
		result.append(with.data(), with.size());
		done = at + what.size();
		++count;
	} while (replaceAll && (at = text.find(what, done)) != npos);
	result.append(text.data() + done, text.size() - done);
	*this = std::move(result);
	return count;
}

// --- comparison ----------------------------------------------------------------------------------------

bool ibString::operator==(const wchar_t* s) const
{
	if (s == nullptr) s = L"";
	const auto units = Utf16(s, TerminatedLength(s));
	return View() == std::u16string_view(units);
}

bool ibString::operator==(const wxString& s) const
{
	const auto& wide = s.ToStdWstring();
	const auto units = Utf16(wide.data(), wide.size());
	return View() == std::u16string_view(units);
}

bool operator==(const wxString& a, const ibChar* b)
{
	const auto& wide = a.ToStdWstring();   // wx's own wide storage, by reference where the build keeps one
	const auto units = Utf16(wide.data(), wide.size());
	return std::u16string_view(units) == std::u16string_view(b != nullptr ? b : ibT(""));
}

// Decoded as SetUtf8 decodes — the text the bytes would make — and compared a character at a time; an ASCII word, every
// protocol's, is a byte against a character.
bool ibString::operator==(const char* utf8) const noexcept
{
	const ibChar* chars = c_str();
	const ibChar* const end = chars + Len();
	const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8 != nullptr ? utf8 : "");
	while (*p != 0) {
		const unsigned char b = *p++;
		uint32_t cp; int extra;
		if      (b < 0x80)        { cp = b;        extra = 0; }
		else if ((b >> 5) == 0x6) { cp = b & 0x1F; extra = 1; }
		else if ((b >> 4) == 0xE) { cp = b & 0x0F; extra = 2; }
		else if ((b >> 3) == 0x1E){ cp = b & 0x07; extra = 3; }
		else                      { cp = 0xFFFD;   extra = 0; }  // invalid lead
		for (int k = 0; k < extra && *p != 0; ++k) {
			if ((*p >> 6) != 0x2) break;                       // invalid continuation
			cp = (cp << 6) | (*p++ & 0x3F);
		}
		if (cp > 0xFFFF) {                                     // past the basic plane: a surrogate pair
			cp -= 0x10000;
			if (end - chars < 2 || chars[0] != static_cast<ibChar>(0xD800 + (cp >> 10))
				|| chars[1] != static_cast<ibChar>(0xDC00 + (cp & 0x3FF)))
				return false;
			chars += 2;
			continue;
		}
		if (chars == end || *chars != static_cast<ibChar>(cp))
			return false;
		++chars;
	}
	return chars == end;
}

bool ibString::IsSameAs(const ibString& o, bool caseSensitive) const
{
	if (m_impl == o.m_impl) return true;   // one text
	return SameText(c_str(), Len(), o.c_str(), o.Len(), caseSensitive);
}

bool ibString::IsSameAs(const ibChar* s, bool caseSensitive) const
{
	if (s == nullptr) s = ibT("");
	return SameText(c_str(), Len(), s, TerminatedLength(s), caseSensitive);
}

bool ibString::IsSameAs(const wchar_t* s, bool caseSensitive) const
{
	if (s == nullptr) s = L"";
	const auto units = Utf16(s, TerminatedLength(s));
	return SameText(c_str(), Len(), units.data(), units.size(), caseSensitive);
}

bool ibString::IsSameAs(const wxString& s, bool caseSensitive) const
{
	const auto& wide = s.ToStdWstring();   // wx's own wide storage, by reference where the build keeps one
	const auto units = Utf16(wide.data(), wide.size());
	return SameText(c_str(), Len(), units.data(), units.size(), caseSensitive);
}

bool ibString::IsSameAs(ibChar c, bool caseSensitive) const
{
	const std::u16string_view text = View();
	return text.size() == 1 && (caseSensitive ? text[0] == c : ibToLower(text[0]) == ibToLower(c));
}

// The order of a case-folded index (stringUtils' ibStringCaseFoldLess): folded only where the characters
// differ, so names that share a long prefix compare at the price of the one character where they part.
int ibString::CmpNoCase(const ibString& o) const
{
	if (m_impl == o.m_impl) return 0;   // one text
	const std::u16string_view a = View(), b = o.View();
	const size_t n = a.size() < b.size() ? a.size() : b.size();
	for (size_t i = 0; i < n; ++i) {
		if (a[i] == b[i]) continue;
		const ibChar x = ibToLower(a[i]), y = ibToLower(b[i]);
		if (x != y) return x < y ? -1 : 1;
	}
	return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

bool ibString::IsAscii() const noexcept
{
	for (ibChar c : View()) if (c > 0x7F) return false;
	return true;
}

// --- ibString::Format -------------------------------------------------------------------------------------

namespace ibFStringFormat {

namespace {
bool IsDigit(wchar_t c) noexcept { return c >= L'0' && c <= L'9'; }
bool IsOneOf(wchar_t c, const wchar_t* set) noexcept {
	for (; *set != 0; ++set) if (*set == c) return true;
	return false;
}
} // namespace

// The format as swprintf reads it — wide, as the C library has it. Each conversion is re-spelled for the
// argument that is actually there — `ls` / `lc` for text and characters (the one wide spelling every C
// library agrees on: MSVC's bare `%s` is wide, glibc's is narrow), `ll` for a 64-bit number, nothing for an
// int — so what is written decides only HOW a value prints: flags, width, precision, the conversion letter.
// A conversion its argument cannot answer, `%n`, a positional `%1$s`, or a count that does not agree is
// refused.
bool Prepare(const ibString& formatText, const Kind* given, size_t count, std::wstring& spec)
{
	const std::wstring format = formatText.wc_str();
	const size_t n = format.size();
	spec.clear();
	spec.reserve(n + 8);
	size_t next = 0;   // the argument the next conversion reads
	const auto take = [&](Kind& kind) {
		if (next >= count) return false;
		kind = given[next++];
		return true;
	};
	for (size_t i = 0; i < n; ++i) {
		spec.push_back(format[i]);
		if (format[i] != L'%')
			continue;
		if (i + 1 < n && format[i + 1] == L'%') { spec.push_back(L'%'); ++i; continue; }

		size_t j = i + 1;
		Kind kind;
		while (j < n && IsOneOf(format[j], L"-+ #0")) spec.push_back(format[j++]);
		if (j < n && format[j] == L'*') { if (!take(kind) || kind != Kind::Int) return false; spec.push_back(format[j++]); }
		else while (j < n && IsDigit(format[j])) spec.push_back(format[j++]);
		if (j < n && format[j] == L'.') {
			spec.push_back(format[j++]);
			if (j < n && format[j] == L'*') { if (!take(kind) || kind != Kind::Int) return false; spec.push_back(format[j++]); }
			else while (j < n && IsDigit(format[j])) spec.push_back(format[j++]);
		}
		// The length as WRITTEN is dropped — the argument decides it (MSVC's `I64` included).
		while (j < n && IsOneOf(format[j], L"hlLqjzt")) ++j;
		if (j < n && format[j] == L'I') { ++j; while (j < n && IsDigit(format[j])) ++j; }
		if (j >= n || !take(kind))
			return false;

		switch (const wchar_t conversion = format[j]) {
		case L's':
			if (kind != Kind::Text) return false;
			spec += L"ls";
			break;
		case L'c':
			if (kind != Kind::Int) return false;
			spec += L"lc";
			break;
		case L'd': case L'i': case L'u': case L'o': case L'x': case L'X':
			if (kind == Kind::Int64) spec += L"ll";
			else if (kind != Kind::Int) return false;
			spec.push_back(conversion);
			break;
		case L'f': case L'F': case L'e': case L'E': case L'g': case L'G': case L'a': case L'A':
			if (kind != Kind::Real) return false;
			spec.push_back(conversion);
			break;
		case L'p':
			if (kind != Kind::Pointer) return false;
			spec.push_back(conversion);
			break;
		default:
			return false;
		}
		i = j;
	}
	return next == count;
}

// 🛑 ON APPLE'S LIBC THE WIDE PRINTF IS NOT WIDE ALL THE WAY: it passes every character through the
// thread's multibyte locale, and in the "C" locale a character past ASCII cannot be converted — a Cyrillic
// argument, a Cyrillic word in the format — so the call failed, and a failure reads the same as "does not
// fit": the buffer grew to the limit and Format gave up (IbString.FormatKeepsCyrillic, macOS CI,
// 2026-09-26; glibc and MSVC copy wide characters as they are). So there it runs under a UTF-8 locale of
// its own, for this call only; the process's locale is not touched.
bool Print(std::wstring& out, const wchar_t* spec, ...)
{
#if defined(__IBOSX__)
	static const locale_t s_utf8 = newlocale(LC_CTYPE_MASK, "UTF-8", static_cast<locale_t>(0));
	const locale_t previous = s_utf8 != static_cast<locale_t>(0) ? uselocale(s_utf8) : static_cast<locale_t>(0);
#endif

	// vswprintf reports "does not fit" and nothing more, so the buffer grows until it does.
	bool fits = false;
	for (size_t capacity = std::wcslen(spec) + 64; capacity <= kFormatMaxLength && !fits; capacity *= 2) {
		out.resize(capacity);
		va_list args;
		va_start(args, spec);
		const int written = std::vswprintf(&out[0], capacity, spec, args);
		va_end(args);
		if (written >= 0 && static_cast<size_t>(written) < capacity) {
			out.resize(static_cast<size_t>(written));
			fits = true;
		}
	}

#if defined(__IBOSX__)
	if (previous != static_cast<locale_t>(0))
		uselocale(previous);
#endif
	return fits;
}

ibString Plain(const ibString& format)
{
	if (format.find(ibT('%')) == ibString::npos)
		return format;   // nothing to read — the same text, shared
	const size_t n = format.Len();
	ibString text;
	text.reserve(n);
	for (size_t i = 0; i < n; ++i) {
		text.push_back(format[i]);
		if (format[i] == ibT('%') && i + 1 < n && format[i + 1] == ibT('%'))
			++i;
	}
	return text;
}

} // namespace ibFStringFormat
