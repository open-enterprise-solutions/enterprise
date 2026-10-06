// =============================================================================
// ibString — the module: the text behind the facade (fstring.h).
//
// How a text is MADE, written into and freed lives here and nowhere else: the per-thread
// pool the blocks come from, and Impl — one block holding the count of the ibStrings that
// share it, the length, the room and the characters. The header shows one pointer and the
// block's header (Shared), so copying and reading need no call into this module.
// =============================================================================

#include "backend/fstring.h"

#include <atomic>
#include <cerrno>        // ToLong & co read ERANGE, as wx does
#include <climits>       // ToInt's range
#include <cstdarg>       // Print — the arguments Format hands on
#include <cstdlib>       // std::atexit — the last drain
#include <cwchar>        // vswprintf; wmemcpy / wmemmove / wmemset — the characters in their block
#include <functional>    // std::less — is an appended piece inside this very text
#include <locale>
#if defined(__APPLE__)
#include <xlocale.h>     // newlocale / uselocale — Print's own UTF-8 locale
#endif
#if defined(_MSC_VER)
#include <intrin.h>      // _BitScanReverse — the pool's class, counted
#endif
#include <new>
#include <sstream>       // ToCDouble — the classic locale, whatever the process has set
#include <stdexcept>     // std::out_of_range — insert past the end, as basic_string refused it
#include <wx/wxcrt.h>    // wxTolower / wxToupper (per-char case primitives)

// --- fast pooled allocator ----------------------------------------------------

namespace ibFStringPool {
namespace detail {

	constexpr std::size_t kClasses[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
	constexpr int         kNum       = static_cast<int>(sizeof(kClasses) / sizeof(kClasses[0]));
	constexpr std::size_t kCap       = 128;   // max cached blocks per class per thread

	static_assert(kClasses[0] == 16, "ClassOf counts classes as powers of two from 16");

	// The width of a number in bits — std::bit_width, which this C++17 tree does not have.
	inline int BitWidth(std::size_t x) noexcept {
		if (x == 0) return 0;
#if defined(_MSC_VER)
		unsigned long top;
#  if defined(_WIN64)
		_BitScanReverse64(&top, x);
#  else
		_BitScanReverse(&top, x);
#  endif
		return static_cast<int>(top) + 1;
#else
		return static_cast<int>(sizeof(unsigned long long) * 8) - __builtin_clzll(static_cast<unsigned long long>(x));
#endif
	}

	// The class a block of this many bytes comes from (16 << class holds it), counted rather than searched:
	// a string's making and its going both ask.
	inline int ClassOf(std::size_t bytes) noexcept {
		if (bytes <= kClasses[0]) return 0;
		if (bytes > kClasses[kNum - 1]) return -1;   // larger than the biggest class → straight to ::operator new
		return BitWidth(bytes - 1) - 4;               // 17..32 → 1, 33..64 → 2, … 2049..4096 → 8
	}

	struct Node { Node* next; };

	struct ThreadPool {
		// ⚠ NO INITIALIZERS: a thread_local of a trivial type is zeroed STATICALLY. `= {}` here gave the pool a
		// constructor, and MSVC then asked a lazy-init guard at EVERY access — four of them in one free, seen in
		// the disassembly (`cmp byte ptr [tls+0A0h],0 … call __dyn_tls_on_demand_init`, 2026-09-28).
		Node*         head[kNum];
		std::size_t   count[kNum];

		// Hand every cached block back to the CRT. Shared by Drain() and, off Windows, by the
		// destructor below.
		void Release() noexcept {
			for (int c = 0; c < kNum; ++c) {
				Node* node = head[c];
				while (node != nullptr) {
					Node* const next = node->next;
					::operator delete(node);
					node = next;
				}
				head[c] = nullptr;
				count[c] = 0;
			}
		}

#ifndef __WXMSW__
		// POSIX has no DLL_THREAD_DETACH, so the drain that covers worker threads on Windows has
		// no place to live here — and without it every thread that ends keeps its cache forever,
		// which on a long-running server (wenterprise-server spawns per session) accumulates
		// thread after thread. So off Windows the pool destroys itself on thread exit, which is
		// exactly what thread_local destructors are for.
		//
		// NOT done on Windows, deliberately: there a thread_local with a destructor is built by
		// __dyn_tls_init for EVERY thread and costs an 8-byte registration node that is lost when
		// a thread is killed at process exit — the trade is measured in
		// docs/private/engineering-playbook/25-memory-leaks.md. DllMain covers those threads for free, so
		// the pool stays trivially destructible there.
		~ThreadPool() noexcept { Release(); }
#endif
	};

	// One per thread — and, the text living in this module only, one pool for every string.
	//
	// ⚠ ON LINUX, AT A FIXED OFFSET (initial-exec). The pool is read at every string's making and going, and from
	// inside a shared library the default model reaches a thread_local through a __tls_get_addr call each time —
	// the likeliest part of making a string going from 20 to 104 ns on the CI's Linux when the pool moved in here
	// (IbStringBench, 2026-09-27), while it costs 46 on Windows, whose DLL thread storage is a fixed slot anyway.
	// The library is loaded with the process, never opened later, which is what the model asks.
#if defined(__linux__)
	thread_local ThreadPool t_pool __attribute__((tls_model("initial-exec")));
#else
	thread_local ThreadPool t_pool;
#endif

} // namespace detail

// A block of class `c`: a cached one, or a fresh one of the class's full size. The caller has counted the class
// already — the string's block asks it once, and knows then how much room it gets.
inline void* AllocateClass(int c) {
	detail::ThreadPool& pool = detail::t_pool;       // the thread's pool reached once
	if (detail::Node* n = pool.head[c]) {            // reuse a cached block
		pool.head[c] = n->next;
		--pool.count[c];
		return n;
	}
	return ::operator new(detail::kClasses[c]);       // fresh block, full class size
}

inline void Deallocate(void* p, std::size_t bytes) noexcept {
	if (p == nullptr) return;
	const int c = detail::ClassOf(bytes == 0 ? 1 : bytes);
	if (c < 0) { ::operator delete(p); return; }      // oversized → return to the OS
	detail::ThreadPool& pool = detail::t_pool;
	if (pool.count[c] >= detail::kCap) { ::operator delete(p); return; }   // cache full
	detail::Node* n = static_cast<detail::Node*>(p);   // cache for reuse
	n->next = pool.head[c];
	pool.head[c] = n;
	++pool.count[c];
}

// Hands this thread's cached blocks back to the CRT. The cache exists to make string churn
// cheap, not to outlive the process: at exit the free list is indistinguishable from a leak in
// the CRT dump — the block still holds its old contents with only the first word overwritten by
// `next`, which is why the dump used to show fragments of metadata names. 279 blocks of that
// hide the next real leak.
//
// An explicit call at a chosen point, because the one thread that needs it most cannot be reached
// any other way: the thread that calls exit() detaches the PROCESS, never itself, so no
// thread-exit hook fires for it on any platform. Worker threads are covered without this — by
// DLL_THREAD_DETACH on Windows, by ~ThreadPool elsewhere (see the note there).
void Drain() noexcept { detail::t_pool.Release(); }

} // namespace ibFStringPool

// ⚠ AND ONCE MORE, LAST OF ALL. A store of strings as long-lived as the process — the pictures a server sends, made
// once and kept in statics (ibServerPicture) — hands its blocks back during static destruction, after every Drain
// above; there they would sit in the pool past the end and read as leaks. So a drain is registered before every
// ordinary static of this module and runs after all of them: the runtime keeps static destructors and atexit
// handlers in one LIFO list (docs/private/engineering-playbook/25-memory-leaks.md, "Ordering: running a cleanup last").
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4073)   // init_seg(lib) is reserved for library code — this IS the library
#pragma init_seg(lib)            // construct before every ordinary static in this DLL
#pragma warning(pop)
#endif

namespace {
struct ibStringPoolDrainAtExit {
	ibStringPoolDrainAtExit() { std::atexit([] { ibFStringPool::Drain(); }); }
};
#if defined(__GNUC__)
ibStringPoolDrainAtExit s_stringPoolDrainAtExit __attribute__((init_priority(101)));
#else
ibStringPoolDrainAtExit s_stringPoolDrainAtExit;
#endif
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
	explicit Impl(size_t cap) noexcept : Shared(1, cap) { Chars()[0] = wxT('\0'); }

	static size_t BytesFor(size_t cap) noexcept { return sizeof(Shared) + (cap + 1) * sizeof(wchar_t); }

	// Room for `cap` characters at least — and for as many more as the pool's block holds anyway. The class is
	// counted once: it says which block and how big it is.
	static Impl* Make(size_t cap) {
		const size_t asked = BytesFor(cap);
		const int c = ibFStringPool::detail::ClassOf(asked);
		const size_t bytes = c < 0 ? asked : ibFStringPool::detail::kClasses[c];
		void* const place = c < 0 ? ::operator new(bytes) : ibFStringPool::AllocateClass(c);
		return new (place) Impl((bytes - sizeof(Shared)) / sizeof(wchar_t) - 1);
	}
	static Impl* Copy(const wchar_t* text, size_t len, size_t cap) {
		Impl* const impl = Make(cap < len ? len : cap);
		if (len != 0) std::wmemcpy(impl->Chars(), text, len);
		impl->SetLength(len);
		return impl;
	}
	void SetLength(size_t len) noexcept { m_len = len; Chars()[len] = wxT('\0'); }

	static Impl* Of(Shared* shared) noexcept { return static_cast<Impl*>(shared); }
	bool Alone() const noexcept { return m_refCount.load(std::memory_order_acquire) == 1; }
};

void ibString::Free(Shared* shared) noexcept
{
	Impl* const impl = Impl::Of(shared);
	const size_t bytes = Impl::BytesFor(impl->m_cap);
	impl->~Impl();
	ibFStringPool::Deallocate(impl, bytes);
}

// A text somebody else holds is copied first, with exactly the room asked for — which is what a concatenation
// asks, so `a + b` is one block; one of its own without room grows by half again, so a string appended to in a
// loop is copied a logarithmic number of times.
wchar_t* ibString::Own(size_t room)
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

bool IsSpace(wchar_t c) noexcept {
	return c == wxT(' ') || c == wxT('\t') || c == wxT('\n') || c == wxT('\r') || c == wxT('\f') || c == wxT('\v');
}

// A piece of a text, made a text of its own — one block, the characters copied once.
ibString Slice(std::wstring_view piece) { return ibString(piece.data(), piece.size()); }

// Two texts the same, a case-insensitive pair folded only where the characters differ — matching
// characters need no folding, and names that are the same usually match exactly.
bool SameText(const wchar_t* a, size_t an, const wchar_t* b, size_t bn, bool caseSensitive) noexcept {
	if (an != bn) return false;
	for (size_t i = 0; i < an; ++i) {
		if (a[i] == b[i]) continue;
		if (caseSensitive || wxTolower(a[i]) != wxTolower(b[i])) return false;
	}
	return true;
}

void EncodeUtf8(uint32_t cp, std::string& out) {
	if (cp < 0x80) {
		out.push_back(static_cast<char>(cp));
	} else if (cp < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

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
	return *end == wxT('\0');
}

} // namespace

// --- construction / copy / move ---------------------------------------------------

ibString::ibString(const wchar_t* ws)
{
	if (ws != nullptr && *ws != wxT('\0')) { const size_t n = std::wcslen(ws); m_impl = Impl::Copy(ws, n, n); }
}
ibString::ibString(const wchar_t* ws, size_t n) { if (ws != nullptr && n != 0) m_impl = Impl::Copy(ws, n, n); }
ibString::ibString(wchar_t c, size_t count)
{
	if (count == 0) return;
	Impl* const impl = Impl::Make(count);
	std::wmemset(impl->Chars(), c, count);
	impl->SetLength(count);
	m_impl = impl;
}
ibString::ibString(const std::wstring& ws) { if (!ws.empty()) m_impl = Impl::Copy(ws.c_str(), ws.size(), ws.size()); }
ibString::ibString(const char* utf8) { if (utf8) SetUtf8(utf8, std::char_traits<char>::length(utf8)); }
ibString::ibString(const wxString& s) { if (!s.empty()) m_impl = Impl::Copy(s.wc_str(), s.length(), s.length()); }

// --- conversions -----------------------------------------------------------------

wxString ibString::ToWxString() const { return wxString(wc_str(), Len()); }
std::wstring ibString::ToStdWString() const { return std::wstring(wc_str(), Len()); }

std::string ibString::ToUtf8() const
{
	const std::wstring_view text = View();
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i) {
		uint32_t cp = static_cast<uint32_t>(static_cast<std::make_unsigned<wchar_t>::type>(text[i]));
		if constexpr (sizeof(wchar_t) == 2) {           // UTF-16: combine surrogate pairs
			if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
				const uint32_t lo = static_cast<uint16_t>(text[i + 1]);
				if (lo >= 0xDC00 && lo <= 0xDFFF) {
					cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
					++i;
				}
			}
		}
		EncodeUtf8(cp, out);
	}
	return out;
}

// Decoded straight into the block: a UTF-8 sequence is never fewer bytes than the characters it makes (four
// bytes are two UTF-16 units at most), so `n` characters of room are enough and nothing grows on the way.
void ibString::SetUtf8(const char* p, size_t n)
{
	Clear();
	if (n == 0) return;
	wchar_t* const chars = Own(n);
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
		if constexpr (sizeof(wchar_t) == 2) {            // UTF-16: split astral to surrogate pair
			if (cp > 0xFFFF) {
				cp -= 0x10000;
				chars[len++] = static_cast<wchar_t>(0xD800 + (cp >> 10));
				chars[len++] = static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
				continue;
			}
		}
		chars[len++] = static_cast<wchar_t>(cp);
	}
	SetLength(len);
	// …and a text of characters longer than a byte has room for its BYTES: Cyrillic twice what it takes, CJK
	// three times, for as long as the string lives - and a string a parser made lives in the value it built.
	// A LONG one moves to a block of its own size, once: its block is past the pool (a plain allocation of
	// exactly what was asked), so the room is real memory. A short one stays: inside the pool the slack is one
	// size class at most and the block goes back to be reused, while the move is a second allocation and a
	// copy on every decode - "Privet" x4 on a 4-byte wchar_t asks 256 bytes by its bytes and 128 by its
	// characters, and moving it cost the decode about 30% (2026-09-29). An ASCII text never moves either.
	if (len < n && Impl::BytesFor(Impl::Of(m_impl)->m_cap) > ibFStringPool::detail::kClasses[ibFStringPool::detail::kNum - 1])
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
	if constexpr (sizeof(wchar_t) == 2) {            // UTF-16: split astral to surrogate pair
		if (cp > 0xFFFF) {
			cp -= 0x10000;
			push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
			push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
			return;
		}
	}
	push_back(static_cast<wchar_t>(cp));
}

// --- query / element access ---------------------------------------------------------

bool ibString::IsBlank() const noexcept
{
	for (wchar_t c : View()) if (!IsSpace(c)) return false;
	return true;
}

void ibString::Clear() noexcept
{
	if (m_impl != nullptr && Impl::Of(m_impl)->Alone()) Impl::Of(m_impl)->SetLength(0);
	else { Release(m_impl); m_impl = nullptr; }
}

wchar_t& ibString::operator[](size_t i)      { return Own()[i]; }
void    ibString::SetChar(size_t i, wchar_t c) { Own()[i] = c; }
wchar_t* ibString::begin() { return IsEmpty() ? nullptr : Own(); }
wchar_t* ibString::end()   { return IsEmpty() ? nullptr : Own() + Len(); }

// --- the std::wstring spelling ----------------------------------------------------------

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
	wchar_t* const chars = Own();
	std::wmemmove(chars + pos, chars + pos + count, len - pos - count);
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
	wchar_t* const chars = Own(len + n);
	std::wmemmove(chars + pos + n, chars + pos, len - pos);
	std::wmemcpy(chars + pos, piece.wc_str(), n);
	SetLength(len + n);
	return *this;
}

// The characters may lie in this very text (`s.append(s.wc_str(), n)`), which may have to move: then they are
// taken out first.
ibString& ibString::append(const wchar_t* s, size_t n)
{
	if (n == 0) return *this;
	const size_t len = Len();
	if (m_impl != nullptr) {
		const wchar_t* const own = Impl::Of(m_impl)->Chars();
		if (std::less_equal<const wchar_t*>()(own, s) && std::less<const wchar_t*>()(s, own + len + 1)) {
			const std::wstring piece(s, n);
			return append(piece.data(), n);
		}
	}
	wchar_t* const chars = Own(len + n);
	std::wmemcpy(chars + len, s, n);
	SetLength(len + n);
	return *this;
}

void ibString::push_back(wchar_t c)
{
	const size_t len = Len();
	wchar_t* const chars = Own(len + 1);
	chars[len] = c;
	SetLength(len + 1);
}

void ibString::reserve(size_t n) { if (n > Len()) Own(n); }

void ibString::resize(size_t n, wchar_t c)
{
	const size_t len = Len();
	if (n == len) return;
	wchar_t* const chars = Own(n);
	if (n > len) std::wmemset(chars + len, c, n - len);
	SetLength(n);
}

// --- slicing / search ---------------------------------------------------------------------

ibString ibString::Mid(size_t first, size_t count) const
{
	const std::wstring_view text = View();
	if (first >= text.size()) return ibString();
	return Slice(text.substr(first, count));
}
ibString ibString::Left(size_t count) const { return Slice(View().substr(0, count)); }
ibString ibString::Right(size_t count) const
{
	const std::wstring_view text = View();
	return count >= text.size() ? *this : Slice(text.substr(text.size() - count));
}
size_t ibString::Freq(wchar_t c) const { size_t n = 0; for (wchar_t x : View()) if (x == c) ++n; return n; }

// `rest` may be this very string: what is returned is made before it is written.
bool ibString::StartsWith(const ibString& p, ibString* rest) const
{
	const std::wstring_view text = View(), prefix = p.View();
	if (text.size() < prefix.size() || text.compare(0, prefix.size(), prefix) != 0) return false;
	if (rest != nullptr) *rest = Slice(text.substr(prefix.size()));
	return true;
}

bool ibString::EndsWith(const ibString& s, ibString* rest) const
{
	const std::wstring_view text = View(), suffix = s.View();
	if (text.size() < suffix.size() || text.compare(text.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
	if (rest != nullptr) *rest = Slice(text.substr(0, text.size() - suffix.size()));
	return true;
}

ibString ibString::BeforeFirst(wchar_t c, ibString* rest) const
{
	const std::wstring_view text = View();
	const size_t at = text.find(c);
	if (at == npos) { if (rest != nullptr) rest->Clear(); return *this; }
	ibString before = Slice(text.substr(0, at));
	if (rest != nullptr) *rest = Slice(text.substr(at + 1));
	return before;
}

ibString ibString::AfterFirst(wchar_t c) const
{
	const std::wstring_view text = View();
	const size_t at = text.find(c);
	return at == npos ? ibString() : Slice(text.substr(at + 1));
}

ibString ibString::BeforeLast(wchar_t c, ibString* rest) const
{
	const std::wstring_view text = View();
	const size_t at = text.rfind(c);
	if (at == npos) { if (rest != nullptr) *rest = *this; return ibString(); }
	ibString before = Slice(text.substr(0, at));
	if (rest != nullptr) *rest = Slice(text.substr(at + 1));
	return before;
}

ibString ibString::AfterLast(wchar_t c) const
{
	const std::wstring_view text = View();
	const size_t at = text.rfind(c);
	return at == npos ? *this : Slice(text.substr(at + 1));
}

bool ibString::Matches(const ibString& maskText) const
{
	const wchar_t* mask = maskText.wc_str();
	const wchar_t* text = wc_str();
	const wchar_t* lastStarInText = nullptr;   // where '*' last matched, to backtrack to
	const wchar_t* lastStarInMask = nullptr;
match:
	for (; *mask != wxT('\0'); ++mask, ++text) {
		switch (*mask) {
		case wxT('?'):
			if (*text == wxT('\0')) return false;
			break;
		case wxT('*'): {
			lastStarInText = text;
			lastStarInMask = mask;
			while (*mask == wxT('*') || *mask == wxT('?')) ++mask;   // metacharacters right after it add nothing
			if (*mask == wxT('\0')) return true;
			const wchar_t* const nextMeta = std::wcspbrk(mask, wxT("*?"));
			const size_t run = nextMeta != nullptr ? size_t(nextMeta - mask) : std::wcslen(mask);
			const std::wstring piece(mask, run);
			const wchar_t* const found = std::wcsstr(text, piece.c_str());
			if (found == nullptr) return false;
			text = found + run - 1;   // -1: the loop steps past it
			mask += run - 1;
			break;
		}
		default:
			if (*mask != *text) return false;
			break;
		}
	}
	if (*text == wxT('\0')) return true;
	if (lastStarInText != nullptr) {   // failed: let the last '*' swallow one more character
		text = lastStarInText + 1;
		mask = lastStarInMask;
		lastStarInText = nullptr;
		goto match;
	}
	return false;
}

// --- case ------------------------------------------------------------------------------------

ibString ibString::Lower() const { ibString r(wc_str(), Len()); r.MakeLower(); return r; }
ibString ibString::Upper() const { ibString r(wc_str(), Len()); r.MakeUpper(); return r; }

ibString& ibString::MakeLower()
{
	if (IsEmpty()) return *this;
	wchar_t* const chars = Own();
	for (size_t i = 0, n = Len(); i < n; ++i) chars[i] = static_cast<wchar_t>(wxTolower(chars[i]));
	return *this;
}

ibString& ibString::MakeUpper()
{
	if (IsEmpty()) return *this;
	wchar_t* const chars = Own();
	for (size_t i = 0, n = Len(); i < n; ++i) chars[i] = static_cast<wchar_t>(wxToupper(chars[i]));
	return *this;
}

// --- trim / pad -------------------------------------------------------------------------------

ibString& ibString::Trim(bool fromRight)
{
	const std::wstring_view text = View();
	size_t b = 0, e = text.size();
	if (fromRight) { while (e > b && IsSpace(text[e - 1])) --e; }
	else           { while (b < e && IsSpace(text[b]))     ++b; }
	if (e - b == text.size()) return *this;
	return fromRight ? erase(e) : erase(0, b);
}

ibString ibString::TrimAll() const
{
	const std::wstring_view text = View();
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

ibString& ibString::Pad(size_t count, wchar_t c, bool fromRight)
{
	if (count == 0) return *this;
	if (fromRight) return Append(c, count);
	const size_t len = Len();
	wchar_t* const chars = Own(len + count);
	std::wmemmove(chars + count, chars, len);
	std::wmemset(chars, c, count);
	SetLength(len + count);
	return *this;
}

ibString& ibString::Truncate(size_t len)     { if (len < Len()) erase(len); return *this; }
ibString& ibString::RemoveLast(size_t n)     { if (n != 0 && !IsEmpty()) erase(n < Len() ? Len() - n : 0); return *this; }

// --- numbers (ported from wxString) -------------------------------------------------------------

bool ibString::IsNumber() const
{
	const std::wstring_view text = View();
	size_t i = (!text.empty() && (text[0] == wxT('-') || text[0] == wxT('+'))) ? 1 : 0;
	for (; i < text.size(); ++i) if (text[i] < wxT('0') || text[i] > wxT('9')) return false;
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
bool ibString::ToDouble(double* val) const {
	return ToNumeric(wc_str(), val, +[](const wchar_t* s, wchar_t** end, int) { return std::wcstod(s, end); }, 0);
}
bool ibString::ToCDouble(double* val) const {
	if (val == nullptr) return false;
	std::wistringstream in(ToStdWString());
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
	return append(o.wc_str(), o.Len());   // `o` may be this very string — append takes it out first
}

ibString& ibString::operator+=(const wchar_t* s)  { if (s != nullptr && *s != wxT('\0')) append(s, std::wcslen(s)); return *this; }
ibString& ibString::operator+=(wchar_t c)         { push_back(c); return *this; }
ibString& ibString::operator+=(const wxString& s) { if (!s.empty()) append(s.wc_str(), s.length()); return *this; }

ibString& ibString::Append(wchar_t c, size_t count)
{
	if (count == 0) return *this;
	const size_t len = Len();
	wchar_t* const chars = Own(len + count);
	std::wmemset(chars + len, c, count);
	SetLength(len + count);
	return *this;
}

// Written into a new text piece by piece and put in place at the end. `from` / `to` are held for the call, so
// either may be this very string.
size_t ibString::Replace(const ibString& fromText, const ibString& toText, bool replaceAll)
{
	const ibString from(fromText), to(toText);
	const std::wstring_view what = from.View(), with = to.View(), text = View();
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

bool ibString::operator==(const wxString& s) const { return View().compare(0, npos, s.wc_str(), s.length()) == 0; }

bool ibString::IsSameAs(const ibString& o, bool caseSensitive) const
{
	if (m_impl == o.m_impl) return true;   // one text
	return SameText(wc_str(), Len(), o.wc_str(), o.Len(), caseSensitive);
}

bool ibString::IsSameAs(const wchar_t* s, bool caseSensitive) const
{
	if (s == nullptr) s = wxT("");
	return SameText(wc_str(), Len(), s, std::wcslen(s), caseSensitive);
}

bool ibString::IsSameAs(const wxString& s, bool caseSensitive) const
{
	const auto& wide = s.ToStdWstring();   // wx's own wide storage, by reference where the build keeps one
	return SameText(wc_str(), Len(), wide.data(), wide.length(), caseSensitive);
}

bool ibString::IsSameAs(wchar_t c, bool caseSensitive) const
{
	const std::wstring_view text = View();
	return text.size() == 1 && (caseSensitive ? text[0] == c : wxTolower(text[0]) == wxTolower(c));
}

// The order of a case-folded index (stringUtils' ibStringCaseFoldLess): folded only where the characters
// differ, so names that share a long prefix compare at the price of the one character where they part.
int ibString::CmpNoCase(const ibString& o) const
{
	if (m_impl == o.m_impl) return 0;   // one text
	const std::wstring_view a = View(), b = o.View();
	const size_t n = a.size() < b.size() ? a.size() : b.size();
	for (size_t i = 0; i < n; ++i) {
		if (a[i] == b[i]) continue;
		const wint_t x = wxTolower(a[i]), y = wxTolower(b[i]);
		if (x != y) return x < y ? -1 : 1;
	}
	return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

bool ibString::IsAscii() const noexcept
{
	for (wchar_t c : View()) if (static_cast<unsigned>(c) > 0x7F) return false;
	return true;
}

// --- ibString::Format -------------------------------------------------------------------------------------

namespace ibFStringFormat {

namespace {
bool IsDigit(wchar_t c) noexcept { return c >= wxT('0') && c <= wxT('9'); }
bool IsOneOf(wchar_t c, const wchar_t* set) noexcept {
	for (; *set != 0; ++set) if (*set == c) return true;
	return false;
}
} // namespace

// The format as swprintf reads it. Each conversion is re-spelled for the argument that is actually
// there — `ls` / `lc` for text and characters (the one wide spelling every C library agrees on: MSVC's
// bare `%s` is wide, glibc's is narrow), `ll` for a 64-bit number, nothing for an int — so what is
// written decides only HOW a value prints: flags, width, precision, the conversion letter. A conversion
// its argument cannot answer, `%n`, a positional `%1$s`, or a count that does not agree is refused.
bool Prepare(const ibString& formatText, const Kind* given, size_t count, std::wstring& spec)
{
	const wchar_t* const format = formatText.wc_str();
	const size_t n = formatText.Len();
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
		if (format[i] != wxT('%'))
			continue;
		if (i + 1 < n && format[i + 1] == wxT('%')) { spec.push_back(wxT('%')); ++i; continue; }

		size_t j = i + 1;
		Kind kind;
		while (j < n && IsOneOf(format[j], wxT("-+ #0"))) spec.push_back(format[j++]);
		if (j < n && format[j] == wxT('*')) { if (!take(kind) || kind != Kind::Int) return false; spec.push_back(format[j++]); }
		else while (j < n && IsDigit(format[j])) spec.push_back(format[j++]);
		if (j < n && format[j] == wxT('.')) {
			spec.push_back(format[j++]);
			if (j < n && format[j] == wxT('*')) { if (!take(kind) || kind != Kind::Int) return false; spec.push_back(format[j++]); }
			else while (j < n && IsDigit(format[j])) spec.push_back(format[j++]);
		}
		// The length as WRITTEN is dropped — the argument decides it (MSVC's `I64` included).
		while (j < n && IsOneOf(format[j], wxT("hlLqjzt"))) ++j;
		if (j < n && format[j] == wxT('I')) { ++j; while (j < n && IsDigit(format[j])) ++j; }
		if (j >= n || !take(kind))
			return false;

		switch (const wchar_t conversion = format[j]) {
		case wxT('s'):
			if (kind != Kind::Text) return false;
			spec += wxT("ls");
			break;
		case wxT('c'):
			if (kind != Kind::Int) return false;
			spec += wxT("lc");
			break;
		case wxT('d'): case wxT('i'): case wxT('u'): case wxT('o'): case wxT('x'): case wxT('X'):
			if (kind == Kind::Int64) spec += wxT("ll");
			else if (kind != Kind::Int) return false;
			spec.push_back(conversion);
			break;
		case wxT('f'): case wxT('F'): case wxT('e'): case wxT('E'): case wxT('g'): case wxT('G'): case wxT('a'): case wxT('A'):
			if (kind != Kind::Real) return false;
			spec.push_back(conversion);
			break;
		case wxT('p'):
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
#if defined(__APPLE__)
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

#if defined(__APPLE__)
	if (previous != static_cast<locale_t>(0))
		uselocale(previous);
#endif
	return fits;
}

ibString Plain(const ibString& formatText)
{
	const wchar_t* const format = formatText.wc_str();
	const size_t n = formatText.Len();
	if (std::wcschr(format, wxT('%')) == nullptr)
		return formatText;   // nothing to read — the same text, shared
	std::wstring text;
	text.reserve(n);
	for (size_t i = 0; i < n; ++i) {
		text.push_back(format[i]);
		if (format[i] == wxT('%') && i + 1 < n && format[i + 1] == wxT('%'))
			++i;
	}
	return ibString(text);
}

} // namespace ibFStringFormat
