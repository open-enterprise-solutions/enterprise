///////////////////////////////////////////////////////////////////////////
// Name:        core/localization/translations.h
// Purpose:     the engine's own message catalogs - gettext .mo files, one set per language, chosen per thread
// Licence:     the .mo reader and the plural-forms calculator in translations.cpp are wxWidgets' (wxWindows
//              licence, NOTICE.md); this interface and the choice per thread are the engine's own
///////////////////////////////////////////////////////////////////////////

#ifndef __CORE_TRANSLATIONS_H__
#define __CORE_TRANSLATIONS_H__

#include <wx/translation.h>   // wx's own gettext macros: taken off below, and WXINTL_NO_GETTEXT_MACRO keeps _() off
#include <wx/string.h>

#include "core/core.h"        // CORE_API

#include <cstddef>
#include <memory>
#include <vector>

// ⭐⭐ THE ENGINE TRANSLATES ITS OWN WORDS - one set of catalogs per language, chosen per thread.
//
// wxTranslations is one object for the whole process, and wx's _() asks it: a process that holds several
// bases could speak one language only, the first base's (ibApplicationHost::InitLocale). The engine's words -
// refusals, captions, the lines of its windows - now come through ibTranslations: ONE language is ONE set of
// catalogs (ibMsgCatalog, a gettext .mo each), loaded once and kept for the process, and the language a call
// speaks in is the thread's - the set the SOURCE installed by the backend answers (that of the base the
// thread works for, session/regionalSettings.h), else the process's (SetProcessLanguage: InitLocale's word,
// or a client's --locale). Core knows no base: the source is a callback, the way ibDateTime::Now() takes its
// distance - and it answers a set, not a name, so a translation copies no string on its way.
//
// A set does not change once made: the domains are declared (AddDomain) before any language is asked for,
// and what the readers hold - a pointer to a set, the strings in it - stays valid for the process.
//
// Where a catalog is looked for: <prefix>/<language>/LC_MESSAGES/<domain>.mo and <prefix>/<language>/<domain>.mo,
// for the language as named ("uk_UA") and then its language alone ("uk"); the prefixes are what
// AddCatalogLookupPathPrefix adds, first, then the working folder's `lang`, the program folder's `lang`, and
// the first `lang` found walking up from the executable (a run from the build tree).
//
// What wx keeps: wxLocale and its wxTranslations stay the GUI's, for wx's own windows and the C locale; a
// catalog the host adds to wxLocale is for them. The engine's _() never reaches them.
class ibMsgCatalog;

class CORE_API ibTranslations {
public:
	~ibTranslations();
	ibTranslations(const ibTranslations&) = delete;
	ibTranslations& operator=(const ibTranslations&) = delete;

	const wxString& GetLanguage() const { return m_language; }

	bool IsLoaded(const wxString& domain) const;

	// The translation, or null when no catalog of this language has it: the first catalog that has the text
	// answers. `n` chooses the plural form by the catalog's rule; `context` is gettext's msgctxt.
	const wxString* GetTranslatedString(const wxString& text, const wxString& context = wxString()) const;
	const wxString* GetTranslatedString(const wxString& text, unsigned n, const wxString& context) const;

	// ---- the process: where catalogs are, which domains every language loads, which language a thread speaks ----

	static void AddCatalogLookupPathPrefix(const wxString& prefix);
	// A domain every language loads when it is made - "open_es", the engine's own, declared by the backend
	// with the host. Declared BEFORE any language is asked for: a set is not changed under its readers, so a
	// language made earlier does not take a domain declared later, and the journal says so.
	static void AddDomain(const wxString& domain);
	// The language of a thread whose source answers nothing: the process's own. Empty - nothing is translated.
	static void SetProcessLanguage(const wxString& language);
	static wxString GetProcessLanguage();
	// The canonical name with its region of a language asked for by any of its names - "uk", "uk_UA", "uk-UA",
	// "uk_UA.UTF-8", "Ukrainian" are all uk_UA; empty asks for the machine's own language. Empty when wx knows
	// no language by that name.
	static wxString CanonicalLanguage(const wxString& asked);
	// The set of the thread's own language, asked on every translation; null = the process's. Installed by the
	// backend (the base the thread works for), taken out with null. Answers the source that was in place.
	using Source = const ibTranslations* (*)();
	static Source SetSource(Source source);
	// The language's set, made and loaded on the first ask and kept for the process; null for an empty name.
	static const ibTranslations* ForLanguage(const wxString& language);
	static const ibTranslations* ForThisThread();

	// What _() and its kin come to: the text in this thread's language, or the text itself.
	static wxString Translate(const wxString& text);
	static wxString Translate(const wxString& text, const wxString& context);
	static wxString TranslatePlural(const wxString& one, const wxString& many, int n, const wxString& context = wxString());

private:
	// Made by the registry alone (ForLanguage), which loads it under its lock and hands it out only when whole.
	explicit ibTranslations(const wxString& language);
	// A catalog of this language found under one of `prefixes` - the language as named, then its language
	// alone. False when none is there, or the one there is not readable (the journal says).
	bool LoadCatalogFrom(const std::vector<wxString>& prefixes, const wxString& domain);

	wxString                                    m_language;
	std::vector<std::unique_ptr<ibMsgCatalog>>  m_catalogs;   // in the order added
};

// ---- the gettext macros, over the engine's own catalogs ----
//
// A literal only, as wx insists: what xgettext does not see in the source is in no catalog, so a variable
// handed to _() would never be translated - it is refused at compile time instead. A wx header that spells
// _() in a default argument (wx/html/htmprint.h does) must come after this one; none the engine includes
// does today. Of these, wx keeps only _() off under WXINTL_NO_GETTEXT_MACRO: a unit that says wxPLURAL or
// wxGETTEXT_IN_CONTEXT before this header compiles against wx's own and translates through wx - every unit
// that says them today has this header first.
#undef _
#undef wxPLURAL
#undef wxGETTEXT_IN_CONTEXT
#undef wxGETTEXT_IN_CONTEXT_PLURAL

template<size_t N, typename T>
inline wxString ibTranslateLiteral(const T (&text)[N])
{
	return ibTranslations::Translate(wxString(text));
}
template<typename T>
inline wxString ibTranslateLiteral(T)
{
	static_assert(!sizeof(T), "_() takes a string literal: xgettext extracts what it sees, and a variable is in no catalog");
	return {};
}
template<size_t M, size_t N, typename T>
inline wxString ibTranslateInContextLiteral(const T (&context)[M], const T (&text)[N])
{
	return ibTranslations::Translate(wxString(text), wxString(context));
}
template<typename T, typename U>
inline wxString ibTranslateInContextLiteral(T, U)
{
	static_assert(!sizeof(T), "wxGETTEXT_IN_CONTEXT takes string literals");
	return {};
}
template<size_t M, size_t N, typename T>
inline wxString ibTranslatePluralLiteral(const T (&one)[M], const T (&many)[N], int n)
{
	return ibTranslations::TranslatePlural(wxString(one), wxString(many), n);
}
template<typename T, typename U>
inline wxString ibTranslatePluralLiteral(T, U, int)
{
	static_assert(!sizeof(T), "wxPLURAL takes string literals");
	return {};
}
template<size_t L, size_t M, size_t N, typename T>
inline wxString ibTranslateInContextPluralLiteral(const T (&context)[L], const T (&one)[M], const T (&many)[N], int n)
{
	return ibTranslations::TranslatePlural(wxString(one), wxString(many), n, wxString(context));
}
template<typename T, typename U, typename V>
inline wxString ibTranslateInContextPluralLiteral(T, U, V, int)
{
	static_assert(!sizeof(T), "wxGETTEXT_IN_CONTEXT_PLURAL takes string literals");
	return {};
}

// The markers wx defines only when its own _() is on: a string marked for extraction and left as it is -
// wx/defs.h spells wxALL_FILES with gettext_noop.
#ifndef gettext_noop
	#define gettext_noop(str) (str)
#endif
#ifndef N_
	#define N_(s) (s)
#endif

#define _(s)                                         ibTranslateLiteral((s))
#define wxPLURAL(sing, plur, n)                      ibTranslatePluralLiteral((sing), (plur), (n))
#define wxGETTEXT_IN_CONTEXT(c, s)                   ibTranslateInContextLiteral((c), (s))
#define wxGETTEXT_IN_CONTEXT_PLURAL(c, sing, plur, n) ibTranslateInContextPluralLiteral((c), (sing), (plur), (n))

#endif // __CORE_TRANSLATIONS_H__
