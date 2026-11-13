#ifndef __CORE_LOCALE_H__
#define __CORE_LOCALE_H__

// THE PROCESS'S LOCALE - what wxLocale / wxUILocale did for the engine, carried over without wx: wx's database of
// languages (languageTable.h), a language found in it by any of its names, the language the system speaks, and the C
// library's locale set to a language. The engine's own words are not this: they are ibDictionary's (dictionary.h), one
// set per language, chosen per thread. The C library's locale is the process's - the character classes (in "C" glibc
// knows no Cyrillic letter: iswalpha of one is false), the collation, the separator printf writes (fnumber.cpp).

#include "core/core.h"

// ONE LANGUAGE - wxLanguageInfo, a row of wx's database.
struct ibLanguageInfo {
	const char* m_tag;                 // BCP 47: "ru", "ru-RU", "az-Cyrl-AZ"
	const char* m_canonical;           // POSIX: "ru", "ru_RU", "az_AZ@cyrillic"
	const char* m_canonicalRef;        // the region a language alone stands for: "ru_RU" for "ru"; "" for the rest
	const char* m_description;         // in English: "Russian (Russia)"
	const char* m_nativeDescription;   // in the language itself, UTF-8

	// The name a locale is set by - the region's when the language alone stands for one (wxLanguageInfo::
	// GetCanonicalWithRegion): "ru" is set as ru_RU, "en" as en_GB.
	ibString CanonicalWithRegion() const { return ibString::FromUTF8(*m_canonicalRef != '\0' ? m_canonicalRef : m_canonical); }
};

// A language by any of its names - wxUILocale::FindLanguageInfo: POSIX ("ru_RU", "ru_RU.UTF-8", "ru"), BCP 47 ("ru-RU"),
// in English ("Russian", "Russian (Russia)"); "C" and "POSIX" are en_US. The exact entry, else the language's own (the
// entry for a language comes before its regions'); null for a name the database does not know.
CORE_API const ibLanguageInfo* ibFindLanguageInfo(const ibString& name);

// The language the user wants an interface in, by its canonical name ("ru_RU") - wxUILocale::GetSystemLanguage: the
// first of the languages the user prefers that the database knows (on Windows the settings' list, then the system's
// interface languages; elsewhere LC_ALL, else LC_MESSAGES, else LANG, then LANGUAGE's list, and en_US when none of them
// names one - "C" and "POSIX" name none); on Windows, none known - the user's default locale. Empty when that is unknown too.
CORE_API ibString ibSystemLanguage();

// The C library's locale for the process set to a language found as ibFindLanguageInfo finds it - what wxLocale::Init
// did: by its canonical name with its region, then (wxLanguageInfo::TrySetLocale) by its English names and code page on
// Windows, and elsewhere as wxSetlocaleTryAll tries it - the UTF-8 forms first, then as named, then without the region,
// then glibc's old ISO 639 codes. Answers the name it was set by ("en_GB" for "en"); empty when the database does not
// know the language or the machine has none of its forms, and the locale is then as it was. An empty language is the
// machine's own (setlocale ""), answered as ibSystemLanguage names it, "C" when the database does not know it.
CORE_API ibString ibSetLocale(const ibString& language);

#endif // __CORE_LOCALE_H__
