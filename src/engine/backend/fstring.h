#ifndef __FSTRING_H__
#define __FSTRING_H__

// =============================================================================
// ibString (fstring.h) — the engine's own string, as a FACADE.
//
// Outside, an ibString is ONE POINTER (sizeof(void*)) — the handle the rest of
// the engine passes, returns and keeps in a value's union. Inside, in the module
// (fstring.cpp), lives the text itself: a wchar_t string (exactly wxString's
// wxChar width) on a per-thread pooled allocator, together with the count of the
// ibStrings holding it — ibNumber's scheme for its heap tier (fnumber.h). A copy
// of a string is one more owner of the text, not a copy of the characters; a
// write goes to a text of its own (copy on write); an empty string holds nothing
// and allocates nothing. Nothing outside the module can reach the text but
// through this facade.
//
// The ONLY thing it does with wxWidgets is convert itself to/from a wxString at
// the boundary (operator wxString / ctor). Every operation is its own — ported
// from the wx sources where relevant — and works directly on the wchar buffer,
// constructing no wxString.
//
// operator wxString is IMPLICIT on purpose: ibString is the value-storage type
// and wxString is the boundary, so an ibString flows transparently into the
// wxString-shaped call sites (serialization, metadata lookups, wx APIs) without
// scattering .ToWxString() everywhere. The reverse (wxString -> ibString) is the
// implicit ctor. All other ops stay native.
//
// Why wchar_t (and the file named fstring.h, not string.h): see
// docs/private/value-audit.md. The runtime indexes strings by wxChar position, so wchar
// storage keeps Mid/Find/StrLen O(1) and exact; string.h would shadow the C
// <string.h>.
// =============================================================================

#include <atomic>        // the owners' count the facade knows (copy / let go inline)
#include <cstddef>
#include <cstdint>
#include <cwchar>        // std::swprintf — ibString::Format
#include <string>
#include <string_view>   // std::hash<ibString> hashes a view of the buffer
#include <tuple>         // std::apply — ibString::Format holds its arguments for the call
#include <type_traits>
#include <wx/string.h>   // ONLY for the boundary conversion (operator wxString / ctor)

#ifndef BACKEND_API      // the same definition backend.h makes — this header stays light
#ifdef BACKEND_EXPORTS
#define BACKEND_API WXEXPORT
#else
#define BACKEND_API WXIMPORT
#endif
#endif

class ibStringStore;     // the text itself — fstring.cpp

// --- ibString ---------------------------------------------------------------

class BACKEND_API ibString
{
public:
	static constexpr size_t npos = static_cast<size_t>(-1);

	// --- construction / copy / move ---
	// An empty string holds no text at all; anything else makes the text once, and a COPY of an
	// ibString is one more owner of that text — no characters are copied until somebody writes.
	ibString() noexcept = default;
	ibString(const wchar_t* ws);
	ibString(const wchar_t* ws, size_t n);
	explicit ibString(wchar_t c, size_t count = 1);   // wxString(ch, count), explicit: a character is not a string
	ibString(const std::wstring& ws);                 // std::wstring interchange
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

	// --- other conversions (native) ---
	std::wstring   ToStdWString() const;
	const wchar_t* wc_str() const noexcept;

	// UTF-8 form for DB / serialization / wire — native codec, no wxString.
	std::string ToUtf8() const;
	void SetUtf8(const char* p, size_t n);

	// --- query / element access / iteration (no conversion) ---
	bool   IsEmpty() const noexcept;
	// True when the string is empty or contains only whitespace. No
	// allocation — scans the wchar buffer directly (cf. IsBlankString).
	bool   IsBlank() const noexcept;
	size_t Len()     const noexcept;                   // wxChar units (== wxString::Length)
	size_t Length()  const noexcept { return Len(); }  // wxString-name alias (migration convenience)
	// A text this string owns alone is emptied where it is, and keeps its capacity for what comes next;
	// a shared one is let go.
	void   Clear() noexcept;
	ibString& Empty()      { Clear(); return *this; }   // wx's Empty() CLEARS; IsEmpty() asks
	wchar_t operator[](size_t i) const;
	// ⚠ A WRITABLE CHARACTER makes the text this string's own first; the reference is good until the
	// string is next copied or changed — held across a copy, it would write into both.
	wchar_t& operator[](size_t i);
	wchar_t GetChar(size_t i) const;
	void    SetChar(size_t i, wchar_t c);
	wchar_t Last() const;
	const wchar_t* begin() const noexcept { return wc_str(); }
	const wchar_t* end()   const noexcept { return wc_str() + Len(); }
	wchar_t*       begin();   // ⚠ as operator[] above
	wchar_t*       end();

	// --- the std::wstring spelling: positions, npos = not found ---
	bool   empty()  const noexcept { return IsEmpty(); }
	void   clear()        noexcept { Clear(); }
	size_t length() const noexcept { return Len(); }
	size_t size()   const noexcept { return Len(); }
	const wchar_t* c_str() const noexcept { return wc_str(); }
	size_t find(const ibString& sub, size_t start = 0) const;
	size_t find(wchar_t c, size_t start = 0) const;
	size_t rfind(const ibString& sub, size_t start = npos) const;
	size_t rfind(wchar_t c, size_t start = npos) const;
	size_t find_first_of(const ibString& set, size_t start = 0) const;
	size_t find_last_of(const ibString& set, size_t start = npos) const;
	size_t find_first_not_of(const ibString& set, size_t start = 0) const;
	ibString substr(size_t pos = 0, size_t count = npos) const;
	ibString& erase(size_t pos = 0, size_t count = npos);
	ibString& insert(size_t pos, const ibString& s);
	ibString& append(const ibString& s) { return *this += s; }
	ibString& append(const wchar_t* s, size_t n);
	void push_back(wchar_t c);
	void reserve(size_t n);
	void resize(size_t n, wchar_t c = wxT('\0'));
	void swap(ibString& o) noexcept { Shared* const held = m_impl; m_impl = o.m_impl; o.m_impl = held; }

	// --- slicing / search (wxChar-indexed — exact wxString parity) ---
	ibString Mid(size_t first, size_t count = npos) const;
	ibString Left(size_t count) const;
	ibString Right(size_t count) const;
	// Characters from..to INCLUSIVE, as wx has it.
	ibString SubString(size_t from, size_t to) const { return Mid(from, to - from + 1); }
	// wx's Find: an int, wxNOT_FOUND (-1) when absent, and a CHARACTER may be sought from the end.
	// A search from a position is `find` above — std's name for std's meaning.
	int    Find(const ibString& sub) const;
	int    Find(wchar_t c, bool fromEnd = false) const;
	bool   Contains(const ibString& sub) const { return find(sub) != npos; }
	size_t Freq(wchar_t c) const;
	bool   StartsWith(const ibString& p, ibString* rest = nullptr) const;
	bool   EndsWith(const ibString& s, ibString* rest = nullptr) const;
	// wx's four, with wx's answers when the character is absent: BeforeFirst / AfterLast give the whole
	// string, AfterFirst / BeforeLast give nothing.
	ibString BeforeFirst(wchar_t c, ibString* rest = nullptr) const;
	ibString AfterFirst(wchar_t c) const;
	ibString BeforeLast(wchar_t c, ibString* rest = nullptr) const;
	ibString AfterLast(wchar_t c) const;
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
	ibString& Pad(size_t count, wchar_t c = wxT(' '), bool fromRight = true);
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
	bool ToDouble(double* val) const;    // the current locale's decimal point, as wx
	bool ToCDouble(double* val) const;   // always '.', whatever the locale

	// --- mutation / concat ---
	// Appending to nothing is taking the other text as it is — one more owner, no characters copied.
	ibString& operator+=(const ibString& o);
	ibString& operator+=(const wchar_t* s);
	ibString& operator+=(wchar_t c);
	ibString& Append(const ibString& o)     { return *this += o; }
	ibString& Append(wchar_t c, size_t count = 1);
	ibString& Prepend(const ibString& o)    { return insert(0, o); }
	friend ibString operator+(ibString a, const ibString& b) { a += b; return a; }
	// Exact matches for a literal or a character on either side, so `name + wxT(".")` is OURS — without
	// them wx's operator+(const wxString&, const wchar_t*) is an equal candidate and the call is ambiguous.
	friend ibString operator+(ibString a, const wchar_t* b) { a += b; return a; }
	friend ibString operator+(const wchar_t* a, const ibString& b) { ibString r(a); r += b; return r; }
	friend ibString operator+(ibString a, wchar_t b) { a += b; return a; }
	friend ibString operator+(wchar_t a, const ibString& b) { ibString r; r += a; r += b; return r; }

	// …and wx's own string where it still meets ours — a translation `_("…")`, a wx call's answer. Exact,
	// so the mix is not ambiguous (the two convert into each other), and what comes out is OURS.
	ibString& operator+=(const wxString& s);
	friend ibString operator+(ibString a, const wxString& b) { a += b; return a; }
	friend ibString operator+(const wxString& a, const ibString& b) { ibString r(a); r += b; return r; }

	// wx's stream-style append. Numbers are written as wxString writes them.
	ibString& operator<<(const ibString& s) { return *this += s; }
	ibString& operator<<(const wchar_t* s)  { return *this += s; }
	ibString& operator<<(const wxString& s) { return *this += s; }
	ibString& operator<<(wchar_t c)         { return *this += c; }
	ibString& operator<<(int n)                { return *this += Format(wxT("%d"), n); }
	ibString& operator<<(unsigned int n)       { return *this += Format(wxT("%u"), n); }
	ibString& operator<<(long n)               { return *this += Format(wxT("%ld"), n); }
	ibString& operator<<(unsigned long n)      { return *this += Format(wxT("%lu"), n); }
	ibString& operator<<(long long n)          { return *this += Format(wxT("%lld"), n); }
	ibString& operator<<(unsigned long long n) { return *this += Format(wxT("%llu"), n); }
	ibString& operator<<(double d)             { return *this += Format(wxT("%g"), d); }

	// Replace `from` with `to`, in place over the buffer — every occurrence, or the first only
	// (wxString::Replace semantics). Returns the count. Empty `from` is a no-op (as wxString — it
	// would otherwise loop forever). Nothing found leaves a shared text shared.
	size_t Replace(const ibString& from, const ibString& to, bool replaceAll = true);

	// --- comparison (native; case-insensitive folds per-char via wxTolower) ---
	bool operator==(const ibString& o) const noexcept;
	bool operator!=(const ibString& o) const noexcept { return !(*this == o); }
	bool operator<(const ibString& o)  const noexcept;
	// …and against a literal, exact — see operator+ above for why.
	bool operator==(const wchar_t* s) const noexcept;
	bool operator!=(const wchar_t* s) const noexcept { return !(*this == s); }
	friend bool operator==(const wchar_t* a, const ibString& b) { return b == a; }
	friend bool operator!=(const wchar_t* a, const ibString& b) { return !(b == a); }
	bool operator==(const wxString& s) const;
	bool operator!=(const wxString& s) const { return !(*this == s); }
	friend bool operator==(const wxString& a, const ibString& b) { return b == a; }
	friend bool operator!=(const wxString& a, const ibString& b) { return !(b == a); }
	bool IsSameAs(const ibString& o, bool caseSensitive = true) const;
	// …and exact for a literal and for wx's own string (a bytecode's names), read where they lie —
	// the trio operator== has, and for the same reason.
	bool IsSameAs(const wchar_t* s, bool caseSensitive = true) const;
	bool IsSameAs(const wxString& s, bool caseSensitive = true) const;
	bool IsSameAs(wchar_t c, bool caseSensitive = true) const;
	int  Cmp(const ibString& o) const noexcept;
	int  CmpNoCase(const ibString& o) const;
	bool IsAscii() const noexcept;

	// --- wx spellings of the conversions above ---
	std::wstring ToStdWstring() const { return ToStdWString(); }
	std::string  ToStdString()  const { return ToUtf8(); }   // UTF-8: the core has no other narrow encoding
	std::string  ToUTF8()       const { return ToUtf8(); }
	std::string  utf8_str()     const { return ToUtf8(); }
	static ibString FromUTF8(const char* s, size_t n = npos);
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

	// One Unicode code point, as one wchar or (UTF-16, astral) a surrogate pair — a `\uXXXX` in JSON.
	void AppendCodepoint(uint32_t cp);

private:
	// ⭐ THE OWNERS' COUNT is the one thing the facade knows of its text — enough to copy a string and
	// let it go INLINE, with no call into the module: a string value's copy is the runtime's most
	// frequent string operation (measured 2026-09-26: 5.5 → 10.3 ns while it was a call). The
	// characters stay behind the facade, in Impl (fstring.cpp), which derives from this.
	struct Shared {
		explicit Shared(long owners) noexcept : m_refCount(owners) {}
		std::atomic<long> m_refCount;
	};
	struct Impl;                                  // : Shared — the text itself, fstring.cpp

	static void Hold(Shared* shared) noexcept {
		if (shared != nullptr) shared->m_refCount.fetch_add(1, std::memory_order_relaxed);
	}
	static void Release(Shared* shared) noexcept {
		if (shared != nullptr && shared->m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
			Free(shared);
	}
	static void Free(Shared* shared) noexcept;    // the last owner went: the text is destroyed — fstring.cpp

	// A write goes to a text this string owns alone (Own copies it first when anybody else holds it).
	const ibStringStore& Text() const noexcept;   // to READ: the text held, or the one empty text
	ibStringStore& Own();                         // to WRITE: the text made this string's own
	static ibString Adopt(ibStringStore&& text);  // a finished text, handed to a new string

	Shared* m_impl = nullptr;   // nullptr = the empty string: no text, nothing allocated
};

// One pointer: a string is a handle on its text, copied as a pointer and a count.
static_assert(sizeof(ibString) == sizeof(void*),
              "ibString must stay a single handle on its shared text");

// A key of an unordered container, as wxString is one — by its characters.
namespace std {
template <> struct hash<ibString> {
	size_t operator()(const ibString& s) const noexcept {
		return std::hash<std::wstring_view>()(std::wstring_view(s.wc_str(), s.Len()));
	}
};
}

// --- wx's own varargs (wxString::Format, wxLogXxx, the exception doors): an ibString is a `%s`
// read where it lies, exactly as wx reads a std::wstring (wx/strvararg.h). ----------------------

// Written out: wx #undefs wxFORMAT_STRING_SPECIFIER and WX_ARG_NORMALIZER_FORWARD at the end of
// strvararg.h, so a header of ours cannot use them.
template<> struct wxFormatStringSpecifier<ibString> { enum { value = wxFormatString::Arg_String }; };

#if !wxUSE_UTF8_LOCALE_ONLY
template<>
struct wxArgNormalizerWchar<const ibString&> : public wxArgNormalizerWchar<const wchar_t*>
{
	wxArgNormalizerWchar(const ibString& s, const wxFormatString* fmt, unsigned index)
		: wxArgNormalizerWchar<const wchar_t*>(s.wc_str(), fmt, index) {}
};
template<>
struct wxArgNormalizerWchar<ibString> : public wxArgNormalizerWchar<const ibString&>
{
	wxArgNormalizerWchar(const ibString& s, const wxFormatString* fmt, unsigned index)
		: wxArgNormalizerWchar<const ibString&>(s, fmt, index) {}
};
#endif // !wxUSE_UTF8_LOCALE_ONLY

#if wxUSE_UNICODE_UTF8
template<>
struct wxArgNormalizerUtf8<const ibString&> : public wxArgNormalizerUtf8<const wchar_t*>
{
	wxArgNormalizerUtf8(const ibString& s, const wxFormatString* fmt, unsigned index)
		: wxArgNormalizerUtf8<const wchar_t*>(s.wc_str(), fmt, index) {}
};
template<>
struct wxArgNormalizerUtf8<ibString> : public wxArgNormalizerUtf8<const ibString&>
{
	wxArgNormalizerUtf8(const ibString& s, const wxFormatString* fmt, unsigned index)
		: wxArgNormalizerUtf8<const ibString&>(s, fmt, index) {}
};
#endif // wxUSE_UNICODE_UTF8

// --- the string pool (fstring.cpp) --------------------------------------------

namespace ibFStringPool {
// Hands this thread's cached blocks back to the CRT — see the note at the definition.
void Drain() noexcept;
} // namespace ibFStringPool

// --- ibString::Format -------------------------------------------------------

namespace ibFStringFormat {

// What an argument is, as far as a format is concerned — the one fact each specifier is re-spelled for.
enum class Kind : unsigned char { Text, Int, Int64, Real, Pointer };

// What an argument is HELD as for the length of the call. Wide text is read WHERE IT LIES — an
// ibString's buffer, a wide literal, and (while callers are in transit) a wxString's, which on the wide
// builds this tree uses is a wchar buffer too; only NARROW text is decoded, from UTF-8, into a string
// this call owns. A number is widened to int or to 64 bits by its own size, a real to double, an enum
// to its number, a character to int.
inline const wchar_t* Hold(const ibString& s) noexcept { return s.wc_str(); }
inline const wchar_t* Hold(const wchar_t* s) noexcept { return s != nullptr ? s : wxT(""); }
inline const wchar_t* Hold(wchar_t* s) noexcept { return s != nullptr ? s : wxT(""); }
inline const wchar_t* Hold(const std::wstring& s) noexcept { return s.c_str(); }
inline const wchar_t* Hold(const wxString& s) noexcept { return s.wc_str(); }
inline const wchar_t* Hold(const wxCStrData& s) noexcept { return s.AsWChar(); }
inline ibString Hold(const char* s) { return ibString(s); }
inline ibString Hold(char* s) { return ibString(static_cast<const char*>(s)); }
inline ibString Hold(const std::string& s) { ibString text; text.SetUtf8(s.data(), s.size()); return text; }
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
inline const wchar_t* Pass(const ibString& s) noexcept { return s.wc_str(); }
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
BACKEND_API bool Prepare(const ibString& format, const Kind* given, size_t count, std::wstring& spec);
// A format with nothing to expand: the text is final, and only `%%` reads as one `%` (fstring.cpp).
BACKEND_API ibString Plain(const ibString& format);

// Past this the result is not a message any more, and a format that keeps failing is not going to fit.
constexpr size_t kFormatMaxLength = size_t(1) << 24;

} // namespace ibFStringFormat

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
		// swprintf reports "does not fit" and nothing more, so the buffer grows until it does.
		std::wstring out;
		for (size_t capacity = spec.size() + 64; capacity <= ibFStringFormat::kFormatMaxLength; capacity *= 2) {
			out.resize(capacity);
			const int written = std::apply([&](const auto&... value) {
				return std::swprintf(&out[0], capacity, spec.c_str(), ibFStringFormat::Pass(value)...);
			}, held);
			if (written >= 0 && static_cast<size_t>(written) < capacity)
				return ibString(out.c_str(), static_cast<size_t>(written));
		}
		wxFAIL_MSG(wxT("ibString::Format: the result does not fit"));
		return format;
	}
}

#endif // __FSTRING_H__
