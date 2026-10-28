#ifndef __FSTRING_H__
#define __FSTRING_H__

// =============================================================================
// ibString (fstring.h) — the engine's own string, as a FACADE.
//
// Outside, an ibString is ONE POINTER (sizeof(void*)) — the handle the rest of
// the engine passes, returns and keeps in a value's union — on ONE BLOCK: the count
// of the ibStrings holding it, the length, the room, and the char16_t characters
// (UTF-16, on every OS) right after them — ibNumber's scheme for its heap tier
// (fnumber.h). The block's header is visible here, so a string is copied and READ
// inline; it is made, written into and freed in the module (fstring.cpp), from a
// per-thread pool. A copy of a string is one more owner of the text, not a copy of
// the characters; a write goes to a text of its own (copy on write); an empty
// string holds nothing and allocates nothing.
//
// The ONLY thing it does with wxWidgets is convert itself to/from a wxString at
// the boundary (operator wxString / ctor). Every operation is its own — ported
// from the wx sources where relevant — and works directly on the char16_t buffer,
// constructing no wxString.
//
// operator wxString is IMPLICIT on purpose: ibString is the value-storage type
// and wxString is the boundary, so an ibString flows transparently into the
// wxString-shaped call sites (serialization, metadata lookups, wx APIs) without
// scattering .ToWxString() everywhere. The reverse (wxString -> ibString) is the
// implicit ctor. All other ops stay native.
//
// ⭐ WHY char16_t: ONE WIDTH ON EVERY OS. wchar_t is two bytes on Windows and four on
// Linux and macOS, so a text's length, an index into it and its bytes on the wire
// (OES-RPC writes UTF-16LE) were one thing on one OS and another on the next. The
// wide text stays the BOUNDARY — a wxT("…") literal, std::wstring, wxString, the C
// library's wide calls: read as it lies where wchar_t is two bytes, converted where
// it is four. The runtime indexes strings by unit, so Mid/Find/StrLen stay O(1)
// (docs/private/value-audit.md); the file is fstring.h because string.h would
// shadow the C <string.h>.
// =============================================================================

#include <atomic>        // the owners' count the facade knows (copy / let go inline)
#include <cstddef>
#include <cstdint>
#include <cwchar>        // std::swprintf — ibString::Format; WCHAR_MAX — how wide the boundary's character is
#include <cwctype>       // std::iswdigit, std::towlower… — a character's class and case
#include <iosfwd>        // std::ostream — operator<<
#include <string>
#include <string_view>   // std::hash<ibString> hashes a view of the buffer
#include <tuple>         // std::apply — ibString::Format holds its arguments for the call
#include <type_traits>
#include <utility>       // std::declval — what Format holds a text as
#include <wx/string.h>   // ONLY for the boundary conversion (operator wxString / ctor)

#include "core/coreApi.h"   // CORE_API, ibChar, ibT

// --- a character: its class and its case ------------------------------------
// The C library's wide answers — what wxIsdigit, wxTolower and the rest gave, without wx — for a character of
// any width: ours, the boundary's wchar_t, and wx's own (a wxString read by index gives a wxUniChar, which
// converts to every integer type but char16_t alike). The case comes back as the character it was given.

template<class C> inline bool ibIsDigit(C c) { return std::iswdigit(static_cast<wint_t>(c)) != 0; }
template<class C> inline bool ibIsSpace(C c) { return std::iswspace(static_cast<wint_t>(c)) != 0; }
template<class C> inline bool ibIsAlpha(C c) { return std::iswalpha(static_cast<wint_t>(c)) != 0; }
template<class C> inline bool ibIsAlnum(C c) { return std::iswalnum(static_cast<wint_t>(c)) != 0; }
template<class C> inline bool ibIsUpper(C c) { return std::iswupper(static_cast<wint_t>(c)) != 0; }
template<class C> inline bool ibIsLower(C c) { return std::iswlower(static_cast<wint_t>(c)) != 0; }

template<class C> inline C ibToLower(C c) { return static_cast<C>(std::towlower(static_cast<wint_t>(c))); }
template<class C> inline C ibToUpper(C c) { return static_cast<C>(std::towupper(static_cast<wint_t>(c))); }

#if WCHAR_MAX > 0xFFFF
// ibString::wc_str()'s text where wchar_t is four bytes: our characters converted, a std::wstring the wide call
// reads as its pointer — held to the end of the expression it is asked in.
struct ibWideBuffer : std::wstring {
	using std::wstring::wstring;
	operator const wchar_t*() const noexcept { return c_str(); }
};
#endif

// --- ibString ---------------------------------------------------------------

class CORE_API ibString
{
public:
	static constexpr size_t npos = static_cast<size_t>(-1);

	// --- construction / copy / move ---
	// An empty string holds no text at all; anything else makes the text once, and a COPY of an
	// ibString is one more owner of that text — no characters are copied until somebody writes.
	ibString() noexcept = default;
	ibString(const ibChar* s);
	ibString(const ibChar* s, size_t n);
	explicit ibString(ibChar c, size_t count = 1);  // wxString(ch, count), explicit: a character is not a string
	// The wide boundary — a wxT("…") literal, std::wstring: its characters as they are where wchar_t is two
	// bytes, a character past the basic plane made a surrogate pair where it is four.
	ibString(const wchar_t* ws);
	ibString(const wchar_t* ws, size_t n);
	ibString(const std::wstring& ws);
	ibString(const char* utf8);

	// Copy, assign and let go INLINE: a copy is one more owner (an atomic step on the count the facade
	// knows), and only the last owner's going reaches into the module (Free).
	ibString(const ibString& o) noexcept : m_impl(o.m_impl) { Hold(m_impl); }
	ibString(ibString&& o) noexcept : m_impl(o.m_impl) { o.m_impl = nullptr; }
	ibString& operator=(const ibString& o) noexcept {
		if (m_impl == o.m_impl) return *this;   // the same text again (a loop writing one value into one slot): no count touched
		Hold(o.m_impl);   // first — `s = s` must not let go of the text it is about to keep
		Release(m_impl);
		m_impl = o.m_impl;
		return *this;
	}
	ibString& operator=(ibString&& o) noexcept {
		if (this != &o) { Release(m_impl); m_impl = o.m_impl; o.m_impl = nullptr; }
		return *this;
	}
	~ibString() { Release(m_impl); }

	// --- the wx boundary (the only place wxString is touched) ---
	// Both directions implicit: ibString IS the value type, wxString the
	// boundary — call sites that expect a wxString get one transparently.
	ibString(const wxString& s);
	wxString ToWxString() const;
	operator wxString() const { return ToWxString(); }

	// --- the wide boundary: the OS's wide calls, the C library's, wx's ---
	// Where wchar_t is two bytes (Windows) it is UTF-16 as ours is, and the characters are read where they lie;
	// where it is four they are converted, for the expression that reads them.
#if WCHAR_MAX <= 0xFFFF
	const wchar_t* wc_str() const noexcept { return reinterpret_cast<const wchar_t*>(c_str()); }
#else
	ibWideBuffer   wc_str() const;
#endif

	// --- other conversions (native) ---
	// READING IS INLINE — the length and the characters are in the block's header (Shared, below), so asking a
	// string for them is no call into the module, as it was not before the text went behind the facade.
	const ibChar* c_str() const noexcept { return m_impl != nullptr ? m_impl->Chars() : ibT(""); }

	// UTF-8 form for DB / serialization / wire — native codec, no wxString.
	std::string ToUtf8() const;
	void SetUtf8(const char* p, size_t n);

	// --- query / element access / iteration (no conversion) ---
	bool   IsEmpty() const noexcept { return m_impl == nullptr || m_impl->m_len == 0; }
	// True when the string is empty or contains only whitespace. No
	// allocation — scans the buffer directly (cf. IsBlankString).
	bool   IsBlank() const noexcept;
	size_t Len()     const noexcept { return m_impl != nullptr ? m_impl->m_len : 0; }   // UTF-16 units
	size_t Length()  const noexcept { return Len(); }  // wxString-name alias (migration convenience)
	// A text this string owns alone is emptied where it is, and keeps its capacity for what comes next;
	// a shared one is let go.
	void   Clear() noexcept;
	ibString& Empty()      { Clear(); return *this; }   // wx's Empty() CLEARS; IsEmpty() asks
	// Read at the terminator too: s[Len()] is '\0'.
	ibChar operator[](size_t i) const { return c_str()[i]; }
	// ⚠ A WRITABLE CHARACTER makes the text this string's own first; the reference is good until the
	// string is next copied or changed — held across a copy, it would write into both.
	ibChar& operator[](size_t i);
	ibChar GetChar(size_t i) const { return c_str()[i]; }
	void     SetChar(size_t i, ibChar c);
	ibChar Last() const { return View().back(); }
	using const_iterator = const ibChar*;
	using iterator       = ibChar*;
	const ibChar* begin() const noexcept { return c_str(); }
	const ibChar* end()   const noexcept { return c_str() + Len(); }
	ibChar*       begin();   // ⚠ as operator[] above
	ibChar*       end();

	// --- the std::basic_string spelling: positions, npos = not found ---
	bool   empty()  const noexcept { return IsEmpty(); }
	void   clear()        noexcept { Clear(); }
	size_t length() const noexcept { return Len(); }
	size_t size()   const noexcept { return Len(); }
	size_t find(const ibString& sub, size_t start = 0) const { return View().find(sub.View(), start); }
	size_t find(ibChar c, size_t start = 0) const          { return View().find(c, start); }
	size_t rfind(const ibString& sub, size_t start = npos) const { return View().rfind(sub.View(), start); }
	size_t rfind(ibChar c, size_t start = npos) const         { return View().rfind(c, start); }
	size_t find_first_of(const ibString& set, size_t start = 0) const;
	size_t find_first_of(ibChar c, size_t start = 0) const             { return View().find(c, start); }
	size_t find_last_of(const ibString& set, size_t start = npos) const;
	size_t find_first_not_of(const ibString& set, size_t start = 0) const;
	size_t find_first_not_of(ibChar c, size_t start = 0) const         { return View().find_first_not_of(c, start); }
	size_t find_last_not_of(ibChar c, size_t start = npos) const       { return View().find_last_not_of(c, start); }
	ibString substr(size_t pos = 0, size_t count = npos) const;
	ibString& erase(size_t pos = 0, size_t count = npos);
	ibString& insert(size_t pos, const ibString& s);
	ibString& insert(size_t pos, size_t count, ibChar c) { return insert(pos, ibString(c, count)); }
	ibString& replace(size_t pos, size_t count, const ibString& s) { erase(pos, count); return insert(pos, s); }
	ibString& append(const ibString& s) { return *this += s; }
	ibString& append(const ibChar* s, size_t n);
	ibString& append(size_t count, ibChar c) { return Append(c, count); }
	void push_back(ibChar c);
	void reserve(size_t n);
	void resize(size_t n, ibChar c = ibT('\0'));
	void swap(ibString& o) noexcept { Shared* const held = m_impl; m_impl = o.m_impl; o.m_impl = held; }

	// --- slicing / search (unit-indexed — wxString's names and answers) ---
	ibString Mid(size_t first, size_t count = npos) const;
	ibString Left(size_t count) const;
	ibString Right(size_t count) const;
	// Characters from..to INCLUSIVE, as wx has it.
	ibString SubString(size_t from, size_t to) const { return Mid(from, to - from + 1); }
	// wx's Find: an int, wxNOT_FOUND (-1) when absent, and a CHARACTER may be sought from the end.
	// A search from a position is `find` above — std's name for std's meaning.
	int    Find(const ibString& sub) const { return AsFound(View().find(sub.View())); }
	int    Find(ibChar c, bool fromEnd = false) const { return AsFound(fromEnd ? View().rfind(c) : View().find(c)); }
	bool   Contains(const ibString& sub) const { return find(sub) != npos; }
	size_t Freq(ibChar c) const;
	bool   StartsWith(const ibString& p, ibString* rest = nullptr) const;
	bool   EndsWith(const ibString& s, ibString* rest = nullptr) const;
	// wx's four, with wx's answers when the character is absent: BeforeFirst / AfterLast give the whole
	// string, AfterFirst / BeforeLast give nothing.
	ibString BeforeFirst(ibChar c, ibString* rest = nullptr) const;
	ibString AfterFirst(ibChar c) const;
	ibString BeforeLast(ibChar c, ibString* rest = nullptr) const;
	ibString AfterLast(ibChar c) const;
	// Shell-style mask — `*` any run, `?` one character — ported from wxString::Matches.
	bool Matches(const ibString& mask) const;

	// --- case: per-char loop ported from wxString::MakeLower/MakeUpper. ---
	ibString Lower() const;
	ibString Upper() const;
	ibString& MakeLower();
	ibString& MakeUpper();

	// --- trim / pad: IN PLACE, as wxString has them ---
	// Nothing to cut leaves a shared text shared.
	ibString& Trim(bool fromRight = true);
	// Both ends, as a COPY — the runtime's TrimAll, for a string that is not to be changed.
	ibString TrimAll() const;
	enum stripType { leading = 0x1, trailing = 0x2, both = 0x3 };
	ibString Strip(int how = trailing) const;
	ibString& Pad(size_t count, ibChar c = ibT(' '), bool fromRight = true);
	ibString& Truncate(size_t len);
	ibString& RemoveLast(size_t n = 1);
	ibString& Remove(size_t pos, size_t len = npos) { return erase(pos, len); }

	// --- numbers: wxString::ToLong & co — the WHOLE string must be the number, as wx requires ---
	bool IsNumber() const;
	bool ToLong(long* val, int base = 10) const;
	bool ToULong(unsigned long* val, int base = 10) const;
	bool ToLongLong(long long* val, int base = 10) const;
	bool ToULongLong(unsigned long long* val, int base = 10) const;
	bool ToInt(int* val, int base = 10) const;
	bool ToUInt(unsigned int* val, int base = 10) const;
	bool ToDouble(double* val) const;    // the current locale's decimal point, as wx
	bool ToCDouble(double* val) const;   // always '.', whatever the locale

	// --- mutation / concat ---
	// Appending to nothing is taking the other text as it is — one more owner, no characters copied.
	ibString& operator+=(const ibString& o);
	ibString& operator+=(const ibChar* s);
	ibString& operator+=(const wchar_t* s);
	ibString& operator+=(ibChar c);
	ibString& Append(const ibString& o)     { return *this += o; }
	ibString& Append(ibChar c, size_t count = 1);
	ibString& Prepend(const ibString& o)    { return insert(0, o); }
	// A text on the left is read, not taken: taken by value it was counted up only for the append to copy it out
	// of the shared block and count it down again — two atomic steps per `a + b`. One block of both lengths
	// instead; a temporary on the left (`a + b + c`) is appended to where it lies.
	friend ibString operator+(const ibString& a, const ibString& b)
	{
		if (b.IsEmpty()) return a;
		if (a.IsEmpty()) return b;
		ibString r;
		r.reserve(a.Len() + b.Len());
		r.append(a.c_str(), a.Len());
		r.append(b.c_str(), b.Len());
		return r;
	}
	friend ibString operator+(ibString&& a, const ibString& b) { a += b; return std::move(a); }
	// Exact matches for a literal or a character on either side, so `name + ibT(".")` is one append, not a text
	// made of the literal first…
	friend ibString operator+(const ibString& a, const ibChar* b)
	{
		const size_t n = b != nullptr ? std::char_traits<ibChar>::length(b) : 0;
		if (n == 0) return a;
		ibString r;
		r.reserve(a.Len() + n);
		r.append(a.c_str(), a.Len());
		r.append(b, n);
		return r;
	}
	friend ibString operator+(ibString&& a, const ibChar* b) { a += b; return std::move(a); }
	friend ibString operator+(const ibChar* a, const ibString& b) { ibString r(a); r += b; return r; }
	friend ibString operator+(ibString a, ibChar b) { a += b; return a; }
	friend ibString operator+(ibChar a, const ibString& b) { ibString r; r += a; r += b; return r; }
	// …and so `name + wxT(".")` is OURS — without them wx's operator+(const wxString&, const wchar_t*) is an
	// equal candidate and the call is ambiguous.
	friend ibString operator+(ibString a, const wchar_t* b) { a += b; return a; }
	friend ibString operator+(const wchar_t* a, const ibString& b) { ibString r(a); r += b; return r; }

	// …and wx's own string where it still meets ours — a translation `_("…")`, a wx call's answer. Exact,
	// so the mix is not ambiguous (the two convert into each other), and what comes out is OURS.
	ibString& operator+=(const wxString& s);
	friend ibString operator+(ibString a, const wxString& b) { a += b; return a; }
	friend ibString operator+(const wxString& a, const ibString& b) { ibString r(a); r += b; return r; }

	// wx's stream-style append. Numbers are written as wxString writes them; a character of either width is a
	// character, not the number `int` would take it for.
	ibString& operator<<(const ibString& s) { return *this += s; }
	ibString& operator<<(const ibChar* s) { return *this += s; }
	ibString& operator<<(const wchar_t* s)  { return *this += s; }
	ibString& operator<<(const wxString& s) { return *this += s; }
	ibString& operator<<(ibChar c)        { return *this += c; }
	ibString& operator<<(wchar_t c)         { return *this += static_cast<ibChar>(c); }
	ibString& operator<<(int n)                { return *this += Format(ibT("%d"), n); }
	ibString& operator<<(unsigned int n)       { return *this += Format(ibT("%u"), n); }
	ibString& operator<<(long n)               { return *this += Format(ibT("%ld"), n); }
	ibString& operator<<(unsigned long n)      { return *this += Format(ibT("%lu"), n); }
	ibString& operator<<(long long n)          { return *this += Format(ibT("%lld"), n); }
	ibString& operator<<(unsigned long long n) { return *this += Format(ibT("%llu"), n); }
	ibString& operator<<(double d)             { return *this += Format(ibT("%g"), d); }

	// Replace `from` with `to`, in place over the buffer — every occurrence, or the first only
	// (wxString::Replace semantics). Returns the count. Empty `from` is a no-op (as wxString — it
	// would otherwise loop forever). Nothing found leaves a shared text shared.
	size_t Replace(const ibString& from, const ibString& to, bool replaceAll = true);

	// --- comparison (native; case-insensitive folds per-char via ibToLower) ---
	bool operator==(const ibString& o) const noexcept { return m_impl == o.m_impl || View() == o.View(); }
	bool operator!=(const ibString& o) const noexcept { return !(*this == o); }
	bool operator<(const ibString& o)  const noexcept { return View() < o.View(); }
	bool operator>(const ibString& o)  const noexcept { return o < *this; }
	bool operator<=(const ibString& o) const noexcept { return !(o < *this); }
	bool operator>=(const ibString& o) const noexcept { return !(*this < o); }
	// …and against a literal, exact — see operator+ above for why.
	bool operator==(const ibChar* s) const noexcept { return View().compare(s != nullptr ? s : ibT("")) == 0; }
	bool operator!=(const ibChar* s) const noexcept { return !(*this == s); }
	friend bool operator==(const ibChar* a, const ibString& b) { return b == a; }
	friend bool operator!=(const ibChar* a, const ibString& b) { return !(b == a); }
	bool operator==(const wchar_t* s) const;
	bool operator!=(const wchar_t* s) const { return !(*this == s); }
	friend bool operator==(const wchar_t* a, const ibString& b) { return b == a; }
	friend bool operator!=(const wchar_t* a, const ibString& b) { return !(b == a); }
	bool operator==(const wxString& s) const;
	bool operator!=(const wxString& s) const { return !(*this == s); }
	friend bool operator==(const wxString& a, const ibString& b) { return b == a; }
	friend bool operator!=(const wxString& a, const ibString& b) { return !(b == a); }
	// …and against UTF-8 — a protocol's word, a narrow literal: the bytes decoded as they are compared, no text made of
	// them (either string would make one, and the two of them made the comparison ambiguous).
	bool operator==(const char* utf8) const noexcept;
	bool operator!=(const char* utf8) const noexcept { return !(*this == utf8); }
	friend bool operator==(const char* a, const ibString& b) { return b == a; }
	friend bool operator!=(const char* a, const ibString& b) { return !(b == a); }
	bool IsSameAs(const ibString& o, bool caseSensitive = true) const;
	// …and exact for a literal and for wx's own string (a bytecode's names), read where they lie —
	// the ones operator== has, and for the same reason.
	bool IsSameAs(const ibChar* s, bool caseSensitive = true) const;
	bool IsSameAs(const wchar_t* s, bool caseSensitive = true) const;
	bool IsSameAs(const wxString& s, bool caseSensitive = true) const;
	bool IsSameAs(ibChar c, bool caseSensitive = true) const;
	int  Cmp(const ibString& o) const noexcept {
		if (m_impl == o.m_impl) return 0;
		const int r = View().compare(o.View());
		return r < 0 ? -1 : (r > 0 ? 1 : 0);
	}
	int  CmpNoCase(const ibString& o) const;
	bool IsAscii() const noexcept;

	// --- wx spellings of the conversions above ---
	std::string  ToStdString()  const { return ToUtf8(); }   // UTF-8: the core has no other narrow encoding
	std::string  ToUTF8()       const { return ToUtf8(); }
	std::string  utf8_str()     const { return ToUtf8(); }
	static ibString FromUTF8(const char* s, size_t n = npos);
	static ibString FromUTF8(const std::string& s) { return FromUTF8(s.data(), s.size()); }
	template <class... Args>
	int Printf(const ibString& format, const Args&... args) { *this = Format(format, args...); return static_cast<int>(Len()); }

	// --- formatting: printf-style expansion (wxString::Format parity), native ---
	// The specifiers are wxString::Format's — `%s` for text, `%d`, `%ld`, `%zu`, `%.2f`, `%5s`, `%%` —
	// and HOW WIDE a number is read comes from the argument, not from the length written in the
	// format: `%d` given a 64-bit count prints the count, as wx's normalizer does. Arguments: ibString,
	// wide text, numbers, enums, pointers. A format its arguments do not answer is REFUSED — asserted,
	// and the text comes back as written — rather than read past what was passed.
	template <class... Args>
	static ibString Format(const ibString& format, const Args&... args);

	// One Unicode code point, as one unit or (past the basic plane) a surrogate pair — a `\uXXXX` in JSON.
	void AppendCodepoint(uint32_t cp);

private:
	// ⭐ THE BLOCK'S HEADER is what the facade knows of its text: the owners' count — enough to copy a string
	// and let it go INLINE, with no call into the module (a string value's copy is the runtime's most frequent
	// string operation: measured 2026-09-26, 5.5 → 10.3 ns while it was a call) — and the length and the room,
	// with the characters right after the header in the same block — enough to READ it inline. Making a text,
	// writing into it and freeing it stay behind the facade, in Impl (fstring.cpp), which derives from this.
	struct Shared {
		Shared(long owners, size_t room) noexcept : m_refCount(owners), m_len(0), m_cap(room) {}
		std::atomic<long> m_refCount;
		size_t            m_len;   // characters, the terminator not counted
		size_t            m_cap;   // characters the block has room for, the terminator not counted
		ibChar*       Chars() noexcept       { return reinterpret_cast<ibChar*>(this + 1); }
		const ibChar* Chars() const noexcept { return reinterpret_cast<const ibChar*>(this + 1); }
	};
	struct Impl;                                  // : Shared — making and freeing the block, fstring.cpp

	static void Hold(Shared* shared) noexcept {
		if (shared != nullptr) shared->m_refCount.fetch_add(1, std::memory_order_relaxed);
	}
	static void Release(Shared* shared) noexcept {
		if (shared != nullptr && shared->m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
			Free(shared);
	}
	static void Free(Shared* shared) noexcept;    // the last owner went: the text is destroyed — fstring.cpp

	// A write goes to a text this string owns alone, with room for `room` characters at least: Own copies it
	// first when anybody else holds it, and grows it when it has no room. SetLength says how much was written.
	std::u16string_view View() const noexcept { return std::u16string_view(c_str(), Len()); }   // to READ
	ibChar* Own(size_t room = 0);               // to WRITE: the characters, made this string's own
	void SetLength(size_t length) noexcept;       // …and how many of them there are now

	// wx's found-or-not: an int, -1 when absent.
	static int AsFound(size_t at) noexcept { return at == npos ? -1 : static_cast<int>(at); }

	Shared* m_impl = nullptr;   // nullptr = the empty string: no text, nothing allocated
};

// One pointer: a string is a handle on its text, copied as a pointer and a count.
static_assert(sizeof(ibString) == sizeof(void*),
              "ibString must stay a single handle on its shared text");

// The empty string, named — what wx calls wxEmptyString. It holds no text, so it costs nothing to copy or to keep.
inline const ibString ibEmptyString;

// Written to a std stream (a test's message among them) as wxString writes itself: in UTF-8.
CORE_API std::ostream& operator<<(std::ostream& out, const ibString& s);

// --- wx's own string beside our literal (a name built as `GetName() + ibRegFigure::Balance`) --------
// What the mix makes is OURS, and the match is exact. Out here, not as friends: neither operand is an
// ibString, so a friend of it is found only by MSVC's own lookup and the same line fails on GCC / Clang.
inline ibString operator+(const wxString& a, const ibChar* b) { ibString r(a); r += b; return r; }
inline ibString operator+(const ibChar* a, const wxString& b) { ibString r(a); r += b; return r; }
CORE_API bool operator==(const wxString& a, const ibChar* b);
inline bool operator==(const ibChar* a, const wxString& b) { return b == a; }
inline bool operator!=(const wxString& a, const ibChar* b) { return !(a == b); }
inline bool operator!=(const ibChar* a, const wxString& b) { return !(b == a); }

// A key of an unordered container, as wxString is one — by its characters.
namespace std {
template <> struct hash<ibString> {
	size_t operator()(const ibString& s) const noexcept {
		return std::hash<std::u16string_view>()(std::u16string_view(s.c_str(), s.Len()));
	}
};
}

// --- ibString::Format -------------------------------------------------------

namespace ibFStringFormat {

// What an argument is, as far as a format is concerned — the one fact each specifier is re-spelled for.
enum class Kind : unsigned char { Text, Int, Int64, Real, Pointer };

// What an argument is HELD as for the length of the call — the C library's printf reads wide text. Wide text
// is read WHERE IT LIES: a wide literal, a std::wstring's, a wxString's (on the wide builds this tree uses, a
// wchar buffer too), and our own characters where wchar_t is two bytes, as both are UTF-16 there. Where it is
// four, our text is made into a wide string this call owns, and so is NARROW text, decoded from UTF-8, on
// every OS. A number is widened to int or to 64 bits by its own size, a real to double, an enum to its
// number, a character to int.
#if WCHAR_MAX <= 0xFFFF
inline const wchar_t* Hold(const ibString& s) noexcept { return s.wc_str(); }
inline const wchar_t* Hold(const ibChar* s) noexcept { return s != nullptr ? reinterpret_cast<const wchar_t*>(s) : L""; }
#else
inline std::wstring Hold(const ibString& s) { return s.wc_str(); }
inline std::wstring Hold(const ibChar* s) { return ibString(s).wc_str(); }
#endif
inline auto Hold(ibChar* s) { return Hold(static_cast<const ibChar*>(s)); }
inline const wchar_t* Hold(const wchar_t* s) noexcept { return s != nullptr ? s : L""; }
inline const wchar_t* Hold(wchar_t* s) noexcept { return s != nullptr ? s : L""; }
inline const wchar_t* Hold(const std::wstring& s) noexcept { return s.c_str(); }
inline const wchar_t* Hold(const wxString& s) noexcept { return s.wc_str(); }
inline const wchar_t* Hold(const wxCStrData& s) noexcept { return s.AsWChar(); }
inline std::wstring Hold(const char* s) { return ibString(s).wc_str(); }
inline std::wstring Hold(char* s) { return ibString(static_cast<const char*>(s)).wc_str(); }
inline std::wstring Hold(const std::string& s) { ibString text; text.SetUtf8(s.data(), s.size()); return text.wc_str(); }
inline int Hold(const wxUniChar& c) noexcept { return static_cast<int>(c.GetValue()); }
inline int Hold(const wxUniCharRef& c) noexcept { return static_cast<int>(wxUniChar(c).GetValue()); }

template <class T>
inline auto Hold(const T& v) noexcept {
	static_assert(std::is_arithmetic<T>::value || std::is_enum<T>::value || std::is_pointer<T>::value,
		"ibString::Format takes text, numbers, enums and pointers");
	if constexpr (std::is_enum<T>::value)
		return Hold(static_cast<typename std::underlying_type<T>::type>(v));
	else if constexpr (std::is_floating_point<T>::value)
		return static_cast<double>(v);
	else if constexpr (std::is_pointer<T>::value)
		return static_cast<const void*>(v);
	else if constexpr (sizeof(T) > sizeof(int))
		return static_cast<typename std::conditional<std::is_signed<T>::value, long long, unsigned long long>::type>(v);
	else
		return static_cast<typename std::conditional<std::is_signed<T>::value, int, unsigned int>::type>(v);
}

// …and what swprintf is handed: a held string by its buffer, anything else as it is.
inline const wchar_t* Pass(const std::wstring& s) noexcept { return s.c_str(); }
template <class T>
inline T Pass(T v) noexcept { return v; }

template <class P>
constexpr Kind KindOf() noexcept {
	if constexpr (std::is_same<P, const wchar_t*>::value) return Kind::Text;
	else if constexpr (std::is_same<P, double>::value)    return Kind::Real;
	else if constexpr (std::is_same<P, const void*>::value) return Kind::Pointer;
	else return sizeof(P) > sizeof(int) ? Kind::Int64 : Kind::Int;
}

// The format as swprintf reads it, each conversion re-spelled for the argument that is there — or
// false, the format refused (fstring.cpp).
CORE_API bool Prepare(const ibString& format, const Kind* given, size_t count, std::wstring& spec);
// A format with nothing to expand: the text is final, and only `%%` reads as one `%` (fstring.cpp).
CORE_API ibString Plain(const ibString& format);

// Past this the result is not a message any more, and a format that keeps failing is not going to fit.
constexpr size_t kFormatMaxLength = size_t(1) << 24;

// swprintf into `out`, grown until the result fits — false when it never does (fstring.cpp). The one
// place the C library formats for Format: on Apple's libc the wide printf passes through the thread's
// multibyte locale, so it is called under a UTF-8 one there.
CORE_API bool Print(std::wstring& out, const wchar_t* spec, ...);

// The text held for one of wx's calls, as Format holds it — a base of the normalizer below, so it is made
// before the normalizer reads it.
struct Held {
	explicit Held(const ibString& s) : m_text(Hold(s)) {}
	explicit Held(const ibChar* s) : m_text(Hold(s)) {}
	decltype(Hold(std::declval<const ibString&>())) m_text;
};

} // namespace ibFStringFormat

// --- wx's own varargs (wxString::Format, wxLogXxx, the exception doors): an ibString is a `%s`,
// held as Format holds it (wx/strvararg.h). --------------------------------------------------------
//
// …and so is the engine's character pointer (an ibT literal, a table's `const ibChar*`): without its own
// normalizer wx takes it for a POINTER — it compiles, and a `%s` given one asserts in a Debug build and
// prints whatever the wide reader makes of the bytes elsewhere.

// Written out: wx #undefs wxFORMAT_STRING_SPECIFIER and WX_ARG_NORMALIZER_FORWARD at the end of
// strvararg.h, so a header of ours cannot use them.
template<> struct wxFormatStringSpecifier<ibString> { enum { value = wxFormatString::Arg_String }; };
template<> struct wxFormatStringSpecifier<const ibChar*> { enum { value = wxFormatString::Arg_String }; };
template<> struct wxFormatStringSpecifier<ibChar*> { enum { value = wxFormatString::Arg_String }; };

// …and one character of it is a `%c`, as wx's own wchar_t is — handed over as one (the same for both of wx's builds).
template<> struct wxFormatStringSpecifier<ibChar> { enum { value = wxFormatString::Arg_Char | wxFormatString::Arg_Int }; };
template<>
struct wxArgNormalizer<ibChar> : public wxArgNormalizer<wchar_t>
{
	wxArgNormalizer(ibChar c, const wxFormatString* fmt, unsigned index)
		: wxArgNormalizer<wchar_t>(static_cast<wchar_t>(c), fmt, index) {}
};

#if !wxUSE_UTF8_LOCALE_ONLY
template<>
struct wxArgNormalizerWchar<const ibString&> : private ibFStringFormat::Held, public wxArgNormalizerWchar<const wchar_t*>
{
	wxArgNormalizerWchar(const ibString& s, const wxFormatString* fmt, unsigned index)
		: ibFStringFormat::Held(s), wxArgNormalizerWchar<const wchar_t*>(ibFStringFormat::Pass(m_text), fmt, index) {}
};
template<>
struct wxArgNormalizerWchar<ibString> : public wxArgNormalizerWchar<const ibString&>
{
	wxArgNormalizerWchar(const ibString& s, const wxFormatString* fmt, unsigned index)
		: wxArgNormalizerWchar<const ibString&>(s, fmt, index) {}
};
template<>
struct wxArgNormalizerWchar<const ibChar*> : private ibFStringFormat::Held, public wxArgNormalizerWchar<const wchar_t*>
{
	wxArgNormalizerWchar(const ibChar* s, const wxFormatString* fmt, unsigned index)
		: ibFStringFormat::Held(s), wxArgNormalizerWchar<const wchar_t*>(ibFStringFormat::Pass(m_text), fmt, index) {}
};
template<>
struct wxArgNormalizerWchar<ibChar*> : public wxArgNormalizerWchar<const ibChar*>
{
	wxArgNormalizerWchar(const ibChar* s, const wxFormatString* fmt, unsigned index)
		: wxArgNormalizerWchar<const ibChar*>(s, fmt, index) {}
};
#endif // !wxUSE_UTF8_LOCALE_ONLY

#if wxUSE_UNICODE_UTF8
template<>
struct wxArgNormalizerUtf8<const ibString&> : private ibFStringFormat::Held, public wxArgNormalizerUtf8<const wchar_t*>
{
	wxArgNormalizerUtf8(const ibString& s, const wxFormatString* fmt, unsigned index)
		: ibFStringFormat::Held(s), wxArgNormalizerUtf8<const wchar_t*>(ibFStringFormat::Pass(m_text), fmt, index) {}
};
template<>
struct wxArgNormalizerUtf8<ibString> : public wxArgNormalizerUtf8<const ibString&>
{
	wxArgNormalizerUtf8(const ibString& s, const wxFormatString* fmt, unsigned index)
		: wxArgNormalizerUtf8<const ibString&>(s, fmt, index) {}
};
template<>
struct wxArgNormalizerUtf8<const ibChar*> : private ibFStringFormat::Held, public wxArgNormalizerUtf8<const wchar_t*>
{
	wxArgNormalizerUtf8(const ibChar* s, const wxFormatString* fmt, unsigned index)
		: ibFStringFormat::Held(s), wxArgNormalizerUtf8<const wchar_t*>(ibFStringFormat::Pass(m_text), fmt, index) {}
};
template<>
struct wxArgNormalizerUtf8<ibChar*> : public wxArgNormalizerUtf8<const ibChar*>
{
	wxArgNormalizerUtf8(const ibChar* s, const wxFormatString* fmt, unsigned index)
		: wxArgNormalizerUtf8<const ibChar*>(s, fmt, index) {}
};
#endif // wxUSE_UNICODE_UTF8

// Every name spelled with its namespace, and no `using namespace`: this body is instantiated inside
// whatever file calls Format, and a KindOf / Hold / Pass of that file's own would meet ours (measured:
// queryLinkModel.cpp has a KindOf of its own).
template <class... Args>
inline ibString ibString::Format(const ibString& format, const Args&... args)
{
	if constexpr (sizeof...(Args) == 0) {
		// NOTHING TO EXPAND: the text is final, and a lone '%' in it ("50% off", a script's own
		// message) is a percent sign, not a specifier missing its argument. `%%` still reads as one.
		return ibFStringFormat::Plain(format);
	}
	else {
		const auto held = std::make_tuple(ibFStringFormat::Hold(args)...);
		const ibFStringFormat::Kind given[] = {
			ibFStringFormat::KindOf<decltype(ibFStringFormat::Pass(ibFStringFormat::Hold(args)))>()... };
		std::wstring spec;
		if (!ibFStringFormat::Prepare(format, given, sizeof...(Args), spec)) {
			wxFAIL_MSG(wxT("ibString::Format: the arguments do not answer the format"));
			return format;
		}
		std::wstring out;
		const bool fits = std::apply([&](const auto&... value) {
			return ibFStringFormat::Print(out, spec.c_str(), ibFStringFormat::Pass(value)...);
		}, held);
		if (fits)
			return ibString(out.c_str(), out.size());
		wxFAIL_MSG(wxT("ibString::Format: the result does not fit"));
		return format;
	}
}

#endif // __FSTRING_H__
