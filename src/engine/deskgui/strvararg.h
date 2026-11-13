#ifndef _DESKGUI_STRVARARG_H__
#define _DESKGUI_STRVARARG_H__

// THE ENGINE'S TEXT IN wx's OWN VARARGS (wxString::Format, wxLogXxx, wxMessageBox's caption built by Format) -
// an ibString is a `%s`, held as ibString::Format holds it (wx/strvararg.h). The core declares wxString and
// includes none of wx, so these live on the side that does: a program's code with wx in it.
//
// ...and so is the engine's character pointer (an ibT literal, a table's `const ibChar*`): without its own
// normalizer wx takes it for a POINTER - it compiles, and a `%s` given one asserts in a Debug build and
// prints whatever the wide reader makes of the bytes elsewhere.
//
// Without this header an ibString handed to one of wx's varargs does not compile (wx's primary
// wxFormatStringSpecifier has no `value` for a class); a `const ibChar*` would compile and be wrong, which is
// why frmclient's backend_core.h and deskgui.h include it rather than each call site.

#include <wx/strvararg.h>

#include <utility>

#include "core/fstring.h"   // ibString, ibChar, ibFStringFormat::Hold / Pass

namespace ibFStringFormat {

// The text held for one of wx's calls, as Format holds it - a base of the normalizer below, so it is made
// before the normalizer reads it.
struct Held {
	explicit Held(const ibString& s) : m_text(Hold(s)) {}
	explicit Held(const ibChar* s) : m_text(Hold(s)) {}
	decltype(Hold(std::declval<const ibString&>())) m_text;
};

} // namespace ibFStringFormat

// Written out: wx #undefs wxFORMAT_STRING_SPECIFIER and WX_ARG_NORMALIZER_FORWARD at the end of
// strvararg.h, so a header of ours cannot use them.
template<> struct wxFormatStringSpecifier<ibString> { enum { value = wxFormatString::Arg_String }; };
template<> struct wxFormatStringSpecifier<const ibChar*> { enum { value = wxFormatString::Arg_String }; };
template<> struct wxFormatStringSpecifier<ibChar*> { enum { value = wxFormatString::Arg_String }; };

// ...and one character of it is a `%c`, as wx's own wchar_t is - handed over as one (the same for both of wx's builds).
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

#endif // _DESKGUI_STRVARARG_H__
