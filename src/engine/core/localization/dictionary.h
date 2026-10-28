///////////////////////////////////////////////////////////////////////////
// Name:        core/localization/dictionary.h
// Purpose:     the engine's own message catalogs - gettext .mo files, one set per language, chosen per thread
// Licence:     the .mo reader and the plural-forms calculator in dictionary.cpp are wxWidgets' (wxWindows
//              licence, NOTICE.md); this interface and the choice per thread are the engine's own
///////////////////////////////////////////////////////////////////////////

#ifndef __CORE_DICTIONARY_H__
#define __CORE_DICTIONARY_H__

#include "core/core.h"        // CORE_API
#include "core/fstring.h"     // ibString — the catalogs' words are the engine's strings

#include <cstddef>
#include <memory>
#include <vector>

// ⭐⭐ THE ENGINE TRANSLATES ITS OWN WORDS - one dictionary per language, chosen per thread.
//
// wxTranslations is one object for the whole process, and wx's _() asks it: a process that holds several
// bases could speak one language only, the first base's (ibApplicationHost::InitLocale). The engine's words -
// refusals, captions, the lines of its windows - now come through ibDictionary: ONE language is ONE set of
// catalogs (ibMsgCatalog, a gettext .mo each), loaded once and kept for the process, and the language a call
// speaks in is the thread's - the set the SOURCE installed by the backend answers (that of the base the
// thread works for, ibApplicationInstance::GetDictionary), else the process's (SetProcessLanguage: InitLocale's word,
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

// The engine's own gettext domain - lang/<language>/open_es.mo. Every language loads it, with no one declaring it.
inline const ibChar* const kEngineDomain = ibT("open_es");

class CORE_API ibDictionary {
public:
	~ibDictionary();
	ibDictionary(const ibDictionary&) = delete;
	ibDictionary& operator=(const ibDictionary&) = delete;

	const ibString& GetLanguage() const { return m_language; }

	bool IsLoaded(const ibString& domain) const;

	// The translation, or null when no catalog of this language has it: the first catalog that has the text
	// answers. `n` chooses the plural form by the catalog's rule; `context` is gettext's msgctxt.
	const ibString* GetTranslatedString(const ibString& text, const ibString& context = ibString()) const;
	const ibString* GetTranslatedString(const ibString& text, unsigned n, const ibString& context) const;

	// ---- the process: where catalogs are, which domains every language loads, which language a thread speaks ----

	static void AddCatalogLookupPathPrefix(const ibString& prefix);
	// A domain every language loads when it is made, beside the engine's own (kEngineDomain, there from the
	// start). Declared BEFORE any language is asked for: a set is not changed under its readers, so a language
	// made earlier does not take a domain declared later, and the journal says so.
	static void AddDomain(const ibString& domain);
	// The language of a thread whose source answers nothing: the process's own, kept as CanonicalLanguage spells
	// it. Empty, or what names no language - nothing is translated.
	static void SetProcessLanguage(const ibString& language);
	static ibString GetProcessLanguage();
	// The name a language is kept and looked for under, however it was spelled - "uk_UA", "uk-UA", "UK_ua" and
	// "uk_UA.UTF-8" are all uk_UA, "uk" is uk. Empty for what is not a language's name (two or three letters,
	// and two of a region); the machine's own language is asked of the system by whoever starts the process.
	static ibString CanonicalLanguage(const ibString& asked);
	// The set of the thread's own language, asked on every translation; null = the process's. Installed by the
	// backend (the base the thread works for), taken out with null. Answers the source that was in place.
	using Source = const ibDictionary* (*)();
	static Source SetSource(Source source);
	// The language's set, made and loaded on the first ask and kept for the process; null for what names no
	// language. Asked by any spelling of the name (CanonicalLanguage): one language is one set.
	static const ibDictionary* ForLanguage(const ibString& language);
	static const ibDictionary* ForThisThread();

private:
	// Made by the registry alone (ForLanguage), which loads it under its lock and hands it out only when whole.
	explicit ibDictionary(const ibString& language);
	// A catalog of this language found under one of `prefixes` - the language as named, then its language
	// alone. False when none is there, or the one there is not readable (the journal says).
	bool LoadCatalogFrom(const std::vector<ibString>& prefixes, const ibString& domain);

	ibString                                    m_language;
	std::vector<std::unique_ptr<ibMsgCatalog>>  m_catalogs;   // in the order added
};

// THE TRANSLATION — what wxGetTranslation was, and what _() and its kin come to: the text in this thread's
// language (ibDictionary::ForThisThread), or the text itself. A string made at run time — a caption read
// from a table, a format string handed over — is translated here; a literal says _(). `context` is gettext's
// msgctxt; `n` chooses the plural form by the catalog's rule.
CORE_API ibString ibGetTranslation(const ibString& text, const ibString& context = ibString());
CORE_API ibString ibGetTranslation(const ibString& one, const ibString& many, int n, const ibString& context = ibString());

// ---- the gettext macros, over the engine's own catalogs ----
//
// A literal only, as wx insists: what xgettext does not see in the source is in no catalog, so a variable
// handed to _() would never be translated - it is refused at compile time instead. A wx header that spells
// _() in a default argument (wx/html/htmprint.h does) must come after this one; none the engine includes
// does today. wx's own _() is off everywhere (WXINTL_NO_GETTEXT_MACRO, the build's); its plural and context
// macros are wx's GUI's, and the engine says ibPLURAL and ibGETTEXT_IN_CONTEXT.
#undef _

template<size_t N, typename T>
inline ibString ibTranslateLiteral(const T (&text)[N])
{
	return ibGetTranslation(ibString(text));
}
template<typename T>
inline ibString ibTranslateLiteral(T)
{
	static_assert(!sizeof(T), "_() takes a string literal: xgettext extracts what it sees, and a variable is in no catalog");
	return {};
}
template<size_t M, size_t N, typename T>
inline ibString ibTranslateInContextLiteral(const T (&context)[M], const T (&text)[N])
{
	return ibGetTranslation(ibString(text), ibString(context));
}
template<typename T, typename U>
inline ibString ibTranslateInContextLiteral(T, U)
{
	static_assert(!sizeof(T), "ibGETTEXT_IN_CONTEXT takes string literals");
	return {};
}
template<size_t M, size_t N, typename T>
inline ibString ibTranslatePluralLiteral(const T (&one)[M], const T (&many)[N], int n)
{
	return ibGetTranslation(ibString(one), ibString(many), n);
}
template<typename T, typename U>
inline ibString ibTranslatePluralLiteral(T, U, int)
{
	static_assert(!sizeof(T), "ibPLURAL takes string literals");
	return {};
}
template<size_t L, size_t M, size_t N, typename T>
inline ibString ibTranslateInContextPluralLiteral(const T (&context)[L], const T (&one)[M], const T (&many)[N], int n)
{
	return ibGetTranslation(ibString(one), ibString(many), n, ibString(context));
}
template<typename T, typename U, typename V>
inline ibString ibTranslateInContextPluralLiteral(T, U, V, int)
{
	static_assert(!sizeof(T), "ibGETTEXT_IN_CONTEXT_PLURAL takes string literals");
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

// A string marked for the catalog and left as it is - a table's caption, translated where it is shown (ibGetTranslation).
#define ibTRANSLATE(str)                             (str)
#define ibTRANSLATE_IN_CONTEXT(c, str)               (str)   // …marked with its msgctxt, translated with it
#define _(s)                                         ibTranslateLiteral((s))
#define ibPLURAL(sing, plur, n)                      ibTranslatePluralLiteral((sing), (plur), (n))
#define ibGETTEXT_IN_CONTEXT(c, s)                   ibTranslateInContextLiteral((c), (s))
#define ibGETTEXT_IN_CONTEXT_PLURAL(c, sing, plur, n) ibTranslateInContextPluralLiteral((c), (sing), (plur), (n))

#endif // __CORE_DICTIONARY_H__
