// =============================================================================
// ibString — the module: the text behind the facade (fstring.h).
//
// Everything that knows how a string is stored lives here and nowhere else: the
// per-thread pool the characters come from, the store (a wchar_t basic_string on
// that pool), and Impl — the store together with the count of the ibStrings
// holding it. The header shows one pointer.
// =============================================================================

#include "backend/fstring.h"

#include <atomic>
#include <cerrno>        // ToLong & co read ERANGE, as wx does
#include <climits>       // ToInt's range
#include <locale>
#include <new>
#include <sstream>       // ToCDouble — the classic locale, whatever the process has set
#include <wx/wxcrt.h>    // wxTolower / wxToupper (per-char case primitives)

// --- fast pooled allocator ----------------------------------------------------

namespace ibFStringPool {
namespace detail {

	constexpr std::size_t kClasses[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
	constexpr int         kNum       = static_cast<int>(sizeof(kClasses) / sizeof(kClasses[0]));
	constexpr std::size_t kCap       = 128;   // max cached blocks per class per thread

	inline int ClassOf(std::size_t bytes) noexcept {
		for (int i = 0; i < kNum; ++i)
			if (bytes <= kClasses[i]) return i;
		return -1;   // larger than the biggest class → straight to ::operator new
	}

	struct Node { Node* next; };

	struct ThreadPool {
		Node*         head[kNum]  = {};
		std::size_t   count[kNum] = {};

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
	thread_local ThreadPool t_pool;

} // namespace detail

inline void* Allocate(std::size_t bytes) {
	if (bytes == 0) bytes = 1;
	const int c = detail::ClassOf(bytes);
	if (c < 0)
		return ::operator new(bytes);
	if (detail::Node* n = detail::t_pool.head[c]) {   // reuse a cached block
		detail::t_pool.head[c] = n->next;
		--detail::t_pool.count[c];
		return n;
	}
	return ::operator new(detail::kClasses[c]);       // fresh block, full class size
}

inline void Deallocate(void* p, std::size_t bytes) noexcept {
	if (p == nullptr) return;
	if (bytes == 0) bytes = 1;
	const int c = detail::ClassOf(bytes);
	if (c < 0 || detail::t_pool.count[c] >= detail::kCap) {
		::operator delete(p);                          // oversized, or cache full → return to OS
		return;
	}
	detail::Node* n = static_cast<detail::Node*>(p);   // cache for reuse
	n->next = detail::t_pool.head[c];
	detail::t_pool.head[c] = n;
	++detail::t_pool.count[c];
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

template <class T>
struct ibStringAllocator {
	using value_type = T;

	ibStringAllocator() noexcept = default;
	template <class U> ibStringAllocator(const ibStringAllocator<U>&) noexcept {}

	T* allocate(std::size_t n) {
		if (n > static_cast<std::size_t>(-1) / sizeof(T)) throw std::bad_alloc();
		return static_cast<T*>(ibFStringPool::Allocate(n * sizeof(T)));
	}
	void deallocate(T* p, std::size_t n) noexcept {
		ibFStringPool::Deallocate(p, n * sizeof(T));
	}

	template <class U> bool operator==(const ibStringAllocator<U>&) const noexcept { return true; }
	template <class U> bool operator!=(const ibStringAllocator<U>&) const noexcept { return false; }
};

// --- the store: the characters themselves ---------------------------------------
//
// A class of its own rather than an alias, so the header can name it without showing it.

class ibStringStore : public std::basic_string<wchar_t, std::char_traits<wchar_t>, ibStringAllocator<wchar_t>>
{
	using Base = std::basic_string<wchar_t, std::char_traits<wchar_t>, ibStringAllocator<wchar_t>>;
public:
	using Base::Base;
	ibStringStore() = default;
	ibStringStore(const Base& text) : Base(text) {}
	ibStringStore(Base&& text) noexcept : Base(std::move(text)) {}
};

// --- the text and its owners ------------------------------------------------------

// The owners' count (Shared) is the facade's; the characters are this module's.
struct ibString::Impl : ibString::Shared
{
	ibStringStore m_text;   // wchar_t — wxChar-width

	template <class... Args>
	explicit Impl(Args&&... args) : Shared(1), m_text(std::forward<Args>(args)...) {}

	template <class... Args>
	static Impl* Make(Args&&... args) {
		void* const place = ibFStringPool::Allocate(sizeof(Impl));
		try { return new (place) Impl(std::forward<Args>(args)...); }
		catch (...) { ibFStringPool::Deallocate(place, sizeof(Impl)); throw; }
	}
	static Impl* Of(Shared* shared) noexcept { return static_cast<Impl*>(shared); }
	bool Alone() const noexcept { return m_refCount.load(std::memory_order_acquire) == 1; }
};

void ibString::Free(Shared* shared) noexcept
{
	Impl* const impl = Impl::Of(shared);
	impl->~Impl();
	ibFStringPool::Deallocate(impl, sizeof(Impl));
}

const ibStringStore& ibString::Text() const noexcept
{
	static const ibStringStore s_empty;
	return m_impl != nullptr ? Impl::Of(m_impl)->m_text : s_empty;
}

ibStringStore& ibString::Own()
{
	if (m_impl == nullptr)
		m_impl = Impl::Make();
	else if (!Impl::Of(m_impl)->Alone()) {
		Impl* const own = Impl::Make(Impl::Of(m_impl)->m_text);
		Release(m_impl);
		m_impl = own;
	}
	return Impl::Of(m_impl)->m_text;
}

ibString ibString::Adopt(ibStringStore&& text)
{
	ibString result;
	if (!text.empty())
		result.m_impl = Impl::Make(std::move(text));
	return result;
}

namespace {

bool IsSpace(wchar_t c) noexcept {
	return c == wxT(' ') || c == wxT('\t') || c == wxT('\n') || c == wxT('\r') || c == wxT('\f') || c == wxT('\v');
}

int AsFound(size_t at) noexcept { return at == ibString::npos ? -1 : static_cast<int>(at); }

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

ibString::ibString(const wchar_t* ws) { if (ws != nullptr && *ws != wxT('\0')) m_impl = Impl::Make(ws); }
ibString::ibString(const wchar_t* ws, size_t n) { if (ws != nullptr && n != 0) m_impl = Impl::Make(ws, n); }
ibString::ibString(wchar_t c, size_t count) { if (count != 0) m_impl = Impl::Make(count, c); }
ibString::ibString(const std::wstring& ws) { if (!ws.empty()) m_impl = Impl::Make(ws.c_str(), ws.size()); }
ibString::ibString(const char* utf8) { if (utf8) SetUtf8(utf8, std::char_traits<char>::length(utf8)); }
ibString::ibString(const wxString& s) { if (!s.empty()) m_impl = Impl::Make(s.wc_str(), s.length()); }

// --- conversions -----------------------------------------------------------------

wxString ibString::ToWxString() const { return wxString(Text().c_str(), Text().size()); }
std::wstring ibString::ToStdWString() const { return std::wstring(Text().begin(), Text().end()); }
const wchar_t* ibString::wc_str() const noexcept { return Text().c_str(); }

std::string ibString::ToUtf8() const
{
	const ibStringStore& text = Text();
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

void ibString::SetUtf8(const char* p, size_t n)
{
	Clear();
	if (n == 0) return;
	Own().reserve(n);
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
		AppendCodepoint(cp);
	}
}

ibString ibString::FromUTF8(const char* s, size_t n)
{
	ibString r;
	if (s != nullptr) r.SetUtf8(s, n == npos ? std::char_traits<char>::length(s) : n);
	return r;
}

void ibString::AppendCodepoint(uint32_t cp)
{
	ibStringStore& text = Own();
	if constexpr (sizeof(wchar_t) == 2) {            // UTF-16: split astral to surrogate pair
		if (cp > 0xFFFF) {
			cp -= 0x10000;
			text.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
			text.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
			return;
		}
	}
	text.push_back(static_cast<wchar_t>(cp));
}

// --- query / element access ---------------------------------------------------------

bool ibString::IsEmpty() const noexcept { return Text().empty(); }

bool ibString::IsBlank() const noexcept
{
	for (wchar_t c : Text()) if (!IsSpace(c)) return false;
	return true;
}

size_t ibString::Len() const noexcept { return Text().size(); }

void ibString::Clear() noexcept
{
	if (m_impl != nullptr && Impl::Of(m_impl)->Alone()) Impl::Of(m_impl)->m_text.clear();
	else { Release(m_impl); m_impl = nullptr; }
}

wchar_t ibString::operator[](size_t i) const { return Text()[i]; }
wchar_t& ibString::operator[](size_t i)      { return Own()[i]; }
wchar_t ibString::GetChar(size_t i) const    { return Text()[i]; }
void    ibString::SetChar(size_t i, wchar_t c) { Own()[i] = c; }
wchar_t ibString::Last() const               { return Text().back(); }
wchar_t* ibString::begin() { return IsEmpty() ? nullptr : &Own()[0]; }
wchar_t* ibString::end()   { return IsEmpty() ? nullptr : &Own()[0] + Len(); }

// --- the std::wstring spelling ----------------------------------------------------------

size_t ibString::find(const ibString& sub, size_t start) const { return Text().find(sub.Text(), start); }
size_t ibString::find(wchar_t c, size_t start) const           { return Text().find(c, start); }
size_t ibString::rfind(const ibString& sub, size_t start) const { return Text().rfind(sub.Text(), start); }
size_t ibString::rfind(wchar_t c, size_t start) const          { return Text().rfind(c, start); }
size_t ibString::find_first_of(const ibString& set, size_t start) const { return Text().find_first_of(set.Text(), start); }
size_t ibString::find_last_of(const ibString& set, size_t start) const  { return Text().find_last_of(set.Text(), start); }
size_t ibString::find_first_not_of(const ibString& set, size_t start) const { return Text().find_first_not_of(set.Text(), start); }
ibString ibString::substr(size_t pos, size_t count) const { return Adopt(Text().substr(pos, count)); }
ibString& ibString::erase(size_t pos, size_t count) { if (pos < Len()) Own().erase(pos, count); return *this; }
ibString& ibString::insert(size_t pos, const ibString& s) { if (!s.IsEmpty()) Own().insert(pos, s.Text()); return *this; }
ibString& ibString::append(const wchar_t* s, size_t n) { if (n != 0) Own().append(s, n); return *this; }
void ibString::push_back(wchar_t c)             { Own().push_back(c); }
void ibString::reserve(size_t n)                { if (n > Len()) Own().reserve(n); }
void ibString::resize(size_t n, wchar_t c)      { if (n != Len()) Own().resize(n, c); }

// --- slicing / search ---------------------------------------------------------------------

ibString ibString::Mid(size_t first, size_t count) const
{
	if (first >= Text().size()) return ibString();
	return Adopt(Text().substr(first, count));
}
ibString ibString::Left(size_t count) const { return Adopt(Text().substr(0, count)); }
ibString ibString::Right(size_t count) const
{
	return count >= Text().size() ? *this : Adopt(Text().substr(Text().size() - count));
}
int    ibString::Find(const ibString& sub) const { return AsFound(Text().find(sub.Text())); }
int    ibString::Find(wchar_t c, bool fromEnd) const { return AsFound(fromEnd ? Text().rfind(c) : Text().find(c)); }
size_t ibString::Freq(wchar_t c) const { size_t n = 0; for (wchar_t x : Text()) if (x == c) ++n; return n; }

bool ibString::StartsWith(const ibString& p, ibString* rest) const
{
	if (Text().size() < p.Text().size() || Text().compare(0, p.Text().size(), p.Text()) != 0) return false;
	if (rest != nullptr) *rest = Adopt(Text().substr(p.Text().size()));
	return true;
}

bool ibString::EndsWith(const ibString& s, ibString* rest) const
{
	if (Text().size() < s.Text().size() ||
		Text().compare(Text().size() - s.Text().size(), s.Text().size(), s.Text()) != 0) return false;
	if (rest != nullptr) *rest = Adopt(Text().substr(0, Text().size() - s.Text().size()));
	return true;
}

ibString ibString::BeforeFirst(wchar_t c, ibString* rest) const
{
	const size_t at = Text().find(c);
	if (at == npos) { if (rest != nullptr) rest->Clear(); return *this; }
	if (rest != nullptr) *rest = Adopt(Text().substr(at + 1));
	return Adopt(Text().substr(0, at));
}

ibString ibString::AfterFirst(wchar_t c) const
{
	const size_t at = Text().find(c);
	return at == npos ? ibString() : Adopt(Text().substr(at + 1));
}

ibString ibString::BeforeLast(wchar_t c, ibString* rest) const
{
	const size_t at = Text().rfind(c);
	if (at == npos) { if (rest != nullptr) *rest = *this; return ibString(); }
	if (rest != nullptr) *rest = Adopt(Text().substr(at + 1));
	return Adopt(Text().substr(0, at));
}

ibString ibString::AfterLast(wchar_t c) const
{
	const size_t at = Text().rfind(c);
	return at == npos ? *this : Adopt(Text().substr(at + 1));
}

bool ibString::Matches(const ibString& maskText) const
{
	const wchar_t* mask = maskText.Text().c_str();
	const wchar_t* text = Text().c_str();
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

ibString ibString::Lower() const
{
	ibStringStore r(Text());
	for (wchar_t& c : r) c = static_cast<wchar_t>(wxTolower(c));
	return Adopt(std::move(r));
}

ibString ibString::Upper() const
{
	ibStringStore r(Text());
	for (wchar_t& c : r) c = static_cast<wchar_t>(wxToupper(c));
	return Adopt(std::move(r));
}

ibString& ibString::MakeLower() { if (!IsEmpty()) for (wchar_t& c : Own()) c = static_cast<wchar_t>(wxTolower(c)); return *this; }
ibString& ibString::MakeUpper() { if (!IsEmpty()) for (wchar_t& c : Own()) c = static_cast<wchar_t>(wxToupper(c)); return *this; }

// --- trim / pad -------------------------------------------------------------------------------

ibString& ibString::Trim(bool fromRight)
{
	const ibStringStore& text = Text();
	size_t b = 0, e = text.size();
	if (fromRight) { while (e > b && IsSpace(text[e - 1])) --e; }
	else           { while (b < e && IsSpace(text[b]))     ++b; }
	if (e - b == text.size()) return *this;
	if (fromRight) Own().erase(e); else Own().erase(0, b);
	return *this;
}

ibString ibString::TrimAll() const
{
	const ibStringStore& text = Text();
	size_t b = 0, e = text.size();
	while (b < e && IsSpace(text[b]))     ++b;
	while (e > b && IsSpace(text[e - 1])) --e;
	return b == 0 && e == text.size() ? *this : Adopt(text.substr(b, e - b));
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
	if (fromRight) Own().append(count, c); else Own().insert(0, count, c);
	return *this;
}

ibString& ibString::Truncate(size_t len)     { if (len < Text().size()) Own().erase(len); return *this; }
ibString& ibString::RemoveLast(size_t n)     { if (n != 0 && !IsEmpty()) Own().erase(n < Text().size() ? Text().size() - n : 0); return *this; }

// --- numbers (ported from wxString) -------------------------------------------------------------

bool ibString::IsNumber() const
{
	const ibStringStore& text = Text();
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
	Own() += o.Text();
	return *this;
}

ibString& ibString::operator+=(const wchar_t* s)  { if (s != nullptr && *s != wxT('\0')) Own() += s; return *this; }
ibString& ibString::operator+=(wchar_t c)         { Own() += c; return *this; }
ibString& ibString::operator+=(const wxString& s) { if (!s.empty()) Own().append(s.wc_str(), s.length()); return *this; }
ibString& ibString::Append(wchar_t c, size_t count) { if (count != 0) Own().append(count, c); return *this; }

// `from` / `to` are held for the call, so either may be this very string: the text is made its own
// first, and they keep reading the one they were given.
size_t ibString::Replace(const ibString& fromText, const ibString& toText, bool replaceAll)
{
	const ibString from(fromText), to(toText);
	if (from.Text().empty() || Text().find(from.Text()) == npos) return 0;
	ibStringStore& text = Own();
	size_t count = 0, pos = 0;
	while ((pos = text.find(from.Text(), pos)) != npos) {
		text.replace(pos, from.Text().size(), to.Text());
		pos += to.Text().size();
		++count;
		if (!replaceAll) break;
	}
	return count;
}

// --- comparison ----------------------------------------------------------------------------------------

bool ibString::operator==(const ibString& o) const noexcept { return m_impl == o.m_impl || Text() == o.Text(); }
bool ibString::operator<(const ibString& o)  const noexcept { return Text() < o.Text(); }
bool ibString::operator==(const wchar_t* s) const noexcept { return Text().compare(s != nullptr ? s : wxT("")) == 0; }
bool ibString::operator==(const wxString& s) const { return Text().compare(0, npos, s.wc_str(), s.length()) == 0; }

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
	const ibStringStore& text = Text();
	return text.size() == 1 && (caseSensitive ? text[0] == c : wxTolower(text[0]) == wxTolower(c));
}

int ibString::Cmp(const ibString& o) const noexcept
{
	if (m_impl == o.m_impl) return 0;
	const int r = Text().compare(o.Text());
	return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

// The order of a case-folded index (stringUtils' ibStringCaseFoldLess): folded only where the characters
// differ, so names that share a long prefix compare at the price of the one character where they part.
int ibString::CmpNoCase(const ibString& o) const
{
	if (m_impl == o.m_impl) return 0;   // one text
	const ibStringStore& a = Text();
	const ibStringStore& b = o.Text();
	const wchar_t* const pa = a.c_str();
	const wchar_t* const pb = b.c_str();
	const size_t n = a.size() < b.size() ? a.size() : b.size();
	for (size_t i = 0; i < n; ++i) {
		if (pa[i] == pb[i]) continue;
		const wint_t x = wxTolower(pa[i]), y = wxTolower(pb[i]);
		if (x != y) return x < y ? -1 : 1;
	}
	return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

bool ibString::IsAscii() const noexcept
{
	for (wchar_t c : Text()) if (static_cast<unsigned>(c) > 0x7F) return false;
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
