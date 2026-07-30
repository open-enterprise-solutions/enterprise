#ifndef __FSTRING_H__
#define __FSTRING_H__

// =============================================================================
// ibString (fstring.h) — self-contained engine string.
//
// Storage is std::wstring (wchar_t — exactly wxString's internal wxChar width).
// The ONLY thing it does with wxWidgets is convert itself to/from a wxString at
// the boundary (operator wxString / ctor). Every operation is its own — ported
// from the wx sources where relevant — and works directly on the wchar buffer,
// constructing no wxString:
//   - case:  per-char loop ported from wxString::MakeLower/MakeUpper.
//   - UTF-8: a standard UTF-8 <-> wchar codec, native, no wxString.
//   - slicing / search / compare / trim / iteration: std::wstring directly.
//
// operator wxString is IMPLICIT on purpose: ibString is the value-storage type
// and wxString is the boundary, so an ibString flows transparently into the
// wxString-shaped call sites (serialization, metadata lookups, wx APIs) without
// scattering .ToWxString() everywhere. The reverse (wxString -> ibString) is the
// implicit ctor. All other ops stay native.
//
// Allocator: ibString's wchar buffers come from a per-thread, size-classed
// free-list (ibFStringPool below) — short strings stay in std::wstring's SSO
// and never allocate; longer ones reuse a cached block instead of hitting the
// general allocator. Header-only and stateless, so sizeof(ibString) ==
// sizeof(std::wstring) (the allocator folds away via EBO).
//
// Why std::wstring (and the file named fstring.h, not string.h): see
// docs/value-audit.md. The runtime indexes strings by wxChar position, so wchar
// storage keeps Mid/Find/StrLen O(1) and exact; string.h would shadow the C
// <string.h>.
// =============================================================================

#include <string>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <wx/string.h>   // ONLY for the boundary conversion (operator wxString / ctor)
#include <wx/wxcrt.h>    // wxTolower / wxToupper (per-char case primitives)

class ibReaderMemory;
class ibWriterMemory;

// --- fast pooled allocator (header-only, stateless) -------------------------

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
		// docs/engineering-playbook/25-memory-leaks.md. DllMain covers those threads for free, so
		// the pool stays trivially destructible there.
		~ThreadPool() noexcept { Release(); }
#endif
	};

	// inline (C++17) ⇒ ONE instance merged across all TUs; thread_local ⇒ one
	// per thread. (Must NOT be in an anonymous namespace.)
	inline thread_local ThreadPool t_pool;

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
//
// Note the pool is thread_local AND per-module — an inline variable is one instance per TU set, so
// each DLL carries its own. Draining has to happen inside the module whose strings churn.
inline void Drain() noexcept { detail::t_pool.Release(); }

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

using ibStringStore = std::basic_string<wchar_t, std::char_traits<wchar_t>, ibStringAllocator<wchar_t>>;

// --- ibString ---------------------------------------------------------------

class ibString
{
public:
	static constexpr size_t npos = ibStringStore::npos;

	// --- construction / copy / move ---
	ibString() = default;
	ibString(const wchar_t* ws) : m_data(ws ? ws : L"") {}
	ibString(const ibStringStore& ws) : m_data(ws) {}
	ibString(ibStringStore&& ws) noexcept : m_data(std::move(ws)) {}
	ibString(const std::wstring& ws) : m_data(ws.c_str(), ws.size()) {}   // std::wstring interchange
	ibString(const char* utf8) { if (utf8) SetUtf8(utf8, std::char_traits<char>::length(utf8)); }

	ibString(const ibString&)                = default;
	ibString(ibString&&) noexcept            = default;
	ibString& operator=(const ibString&)     = default;
	ibString& operator=(ibString&&) noexcept = default;

	// --- the wx boundary (the only place wxString is touched) ---
	// Both directions implicit: ibString IS the value type, wxString the
	// boundary — call sites that expect a wxString get one transparently.
	ibString(const wxString& s) : m_data(s.wc_str(), s.length()) {}
	wxString ToWxString() const { return wxString(m_data.c_str(), m_data.size()); }
	operator wxString() const { return ToWxString(); }

	// --- other conversions (native) ---
	std::wstring   ToStdWString() const { return std::wstring(m_data.begin(), m_data.end()); }
	const wchar_t* wc_str()       const { return m_data.c_str(); }
	const ibStringStore& raw()    const { return m_data; }

	// UTF-8 form for DB / serialization / wire — native codec, no wxString.
	std::string ToUtf8() const {
		std::string out;
		out.reserve(m_data.size());
		for (size_t i = 0; i < m_data.size(); ++i) {
			uint32_t cp = static_cast<uint32_t>(static_cast<std::make_unsigned<wchar_t>::type>(m_data[i]));
			if constexpr (sizeof(wchar_t) == 2) {           // UTF-16: combine surrogate pairs
				if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < m_data.size()) {
					const uint32_t lo = static_cast<uint16_t>(m_data[i + 1]);
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
	void SetUtf8(const char* p, size_t n) {
		m_data.clear();
		m_data.reserve(n);
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

	// --- query / element access / iteration (no conversion) ---
	bool   IsEmpty() const { return m_data.empty(); }
	// True when the string is empty or contains only whitespace. No
	// allocation — scans the wchar buffer directly (cf. IsBlankString).
	bool   IsBlank() const {
		for (wchar_t c : m_data) if (!IsSpace(c)) return false;
		return true;
	}
	size_t Len()     const { return m_data.size(); }   // wxChar units (== wxString::Length)
	size_t Length()  const { return m_data.size(); }   // wxString-name alias (migration convenience)
	void   Clear()         { m_data.clear(); }
	wchar_t operator[](size_t i) const { return m_data[i]; }
	ibStringStore::const_iterator begin() const { return m_data.begin(); }
	ibStringStore::const_iterator end()   const { return m_data.end(); }

	// --- slicing / search (wxChar-indexed — exact wxString parity) ---
	ibString Mid(size_t first, size_t count = npos) const {
		if (first >= m_data.size()) return ibString();
		return ibString(m_data.substr(first, count));
	}
	ibString Left(size_t count)  const { return ibString(m_data.substr(0, count)); }
	ibString Right(size_t count) const {
		return count >= m_data.size() ? *this : ibString(m_data.substr(m_data.size() - count));
	}
	size_t Find(const ibString& sub) const { return m_data.find(sub.m_data); }
	size_t Find(const ibString& sub, size_t start) const { return m_data.find(sub.m_data, start); }
	size_t Find(wchar_t c) const { return m_data.find(c); }
	size_t Find(wchar_t c, size_t start) const { return m_data.find(c, start); }
	bool   Contains(const ibString& sub) const { return Find(sub) != npos; }
	bool   StartsWith(const ibString& p) const { return m_data.rfind(p.m_data, 0) == 0; }
	bool   EndsWith(const ibString& s) const {
		return m_data.size() >= s.m_data.size() &&
		       m_data.compare(m_data.size() - s.m_data.size(), s.m_data.size(), s.m_data) == 0;
	}

	// --- case: per-char loop ported from wxString::MakeLower/MakeUpper. ---
	ibString Lower() const {
		ibStringStore r(m_data);
		for (wchar_t& c : r) c = static_cast<wchar_t>(wxTolower(c));
		return ibString(std::move(r));
	}
	ibString Upper() const {
		ibStringStore r(m_data);
		for (wchar_t& c : r) c = static_cast<wchar_t>(wxToupper(c));
		return ibString(std::move(r));
	}

	// --- trim: over the wchar buffer (1 alloc, native). ---
	ibString Trim(bool fromRight = true) const {
		size_t b = 0, e = m_data.size();
		if (fromRight) { while (e > b && IsSpace(m_data[e - 1])) --e; }
		else           { while (b < e && IsSpace(m_data[b]))     ++b; }
		return ibString(m_data.substr(b, e - b));
	}
	// Trim both ends in one pass / one allocation.
	ibString TrimAll() const {
		size_t b = 0, e = m_data.size();
		while (b < e && IsSpace(m_data[b]))     ++b;
		while (e > b && IsSpace(m_data[e - 1])) --e;
		return ibString(m_data.substr(b, e - b));
	}

	// --- mutation / concat ---
	ibString& operator+=(const ibString& o) { m_data += o.m_data; return *this; }
	friend ibString operator+(ibString a, const ibString& b) { a += b; return a; }

	// Replace every occurrence of `from` with `to`, in place over the
	// buffer (wxString::Replace replaceAll semantics). Returns the count.
	// Empty `from` is a no-op (matches wxString — avoids an infinite loop).
	size_t Replace(const ibString& from, const ibString& to) {
		if (from.m_data.empty()) return 0;
		size_t count = 0, pos = 0;
		while ((pos = m_data.find(from.m_data, pos)) != ibStringStore::npos) {
			m_data.replace(pos, from.m_data.size(), to.m_data);
			pos += to.m_data.size();
			++count;
		}
		return count;
	}

	// --- comparison (native; case-insensitive folds per-char via wxTolower) ---
	bool operator==(const ibString& o) const { return m_data == o.m_data; }
	bool operator!=(const ibString& o) const { return m_data != o.m_data; }
	bool operator<(const ibString& o)  const { return m_data < o.m_data; }
	bool IsSameAs(const ibString& o, bool caseSensitive = true) const {
		if (caseSensitive) return m_data == o.m_data;
		if (m_data.size() != o.m_data.size()) return false;
		for (size_t i = 0; i < m_data.size(); ++i)
			if (wxTolower(m_data[i]) != wxTolower(o.m_data[i])) return false;
		return true;
	}

private:
	static bool IsSpace(wchar_t c) {
		return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\f' || c == L'\v';
	}
	static void EncodeUtf8(uint32_t cp, std::string& out) {
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
	void AppendCodepoint(uint32_t cp) {
		if constexpr (sizeof(wchar_t) == 2) {            // UTF-16: split astral to surrogate pair
			if (cp > 0xFFFF) {
				cp -= 0x10000;
				m_data.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
				m_data.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
				return;
			}
		}
		m_data.push_back(static_cast<wchar_t>(cp));
	}

	ibStringStore m_data;   // wchar_t — wxChar-width
};

// Transparent wrapper: must not grow beyond its std::wstring member, or the
// footprint rationale (ibValue 96 → smaller) is lost.
static_assert(sizeof(ibString) == sizeof(ibStringStore),
              "ibString must stay a zero-overhead wrapper over its store");

#endif // __FSTRING_H__
