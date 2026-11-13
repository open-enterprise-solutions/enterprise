#include "core/localization/locale.h"

#include <cctype>    // std::tolower - a name compared without regard to case, as wxStricmp compares it
#include <clocale>
#include <cstdlib>   // getenv - the system's locale, as its variables name it
#include <string>

#ifdef __IBMSW__
#include <windows.h>   // GetUserDefaultLocaleName, GetLocaleInfoEx
#endif

#include "core/localization/languageTable.h"   // kLanguages - wx's database

namespace {

// wxStricmp over the database's names, which are ASCII.
bool SameNoCase(const std::string& a, const char* b)
{
	size_t i = 0;
	for (; i < a.size() && b[i] != '\0'; ++i) {
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	}
	return i == a.size() && b[i] == '\0';
}

std::string BeforeFirst(const char* text, char c)
{
	const std::string s(text);
	return s.substr(0, s.find(c));
}

bool IsDefaultCLocale(const std::string& name)
{
	return SameNoCase(name, "C") || SameNoCase(name, "POSIX");
}

// wxUILocale::FindLanguageInfo(const wxLocaleIdent&): by the BCP 47 tag, exactly, else by its language.
const ibLanguageInfo* FindByTag(const std::string& asked)
{
	const std::string tag = IsDefaultCLocale(asked) ? std::string("en-US") : asked;
	const std::string language = tag.substr(0, tag.find('-'));
	const ibLanguageInfo* found = nullptr;
	for (const ibLanguageInfo& info : kLanguages) {
		if (SameNoCase(tag, info.m_tag))
			return &info;   // exact match, stop searching
		// a match - but an exact one may come later; the entry for a language comes before its regions'
		if (found == nullptr && SameNoCase(language, BeforeFirst(info.m_tag, '-').c_str()))
			found = &info;
	}
	return found;
}

// wxUILocale::FindLanguageInfo(const wxString&): "language[_region][.codeset][@modifier]" by the canonical name, or by
// the description in English ("Russian", "Russian (Russia)"). The codeset is no part of a name the database holds.
const ibLanguageInfo* FindByName(const std::string& asked)
{
	std::string locale = IsDefaultCLocale(asked) ? std::string("en_US") : asked;
	const size_t codeset = locale.find('.');
	if (codeset != std::string::npos) {
		const size_t modifier = locale.find('@', codeset);
		locale = locale.substr(0, codeset) + (modifier != std::string::npos ? locale.substr(modifier) : std::string());
	}
	const std::string plain = locale.substr(0, locale.find('@'));
	const size_t cut = plain.find('_');
	const std::string languageOnly = plain.substr(0, cut);
	const std::string language = cut != std::string::npos
		? languageOnly + " (" + plain.substr(cut + 1) + ")"   // the description of a language and its region
		: languageOnly;

	const ibLanguageInfo* found = nullptr;
	for (const ibLanguageInfo& info : kLanguages) {
		if (SameNoCase(locale, info.m_canonical) || SameNoCase(language, info.m_description))
			return &info;   // exact match, stop searching
		if (found == nullptr
			&& (SameNoCase(locale, BeforeFirst(info.m_canonical, '_').c_str()) || SameNoCase(languageOnly, info.m_description)))
			found = &info;
	}
	return found;
}

#ifdef __IBMSW__
// wxLanguageInfo::TrySetLocale on Windows: the locale name first, as the CRT takes one; then the language's and its
// country's names in English with the ANSI code page ("Russian_Russia.1251") - asked of Windows by the tag, where wx
// asked by the LANGID.
bool TrySetLocale(const ibLanguageInfo& info)
{
	if (std::setlocale(LC_ALL, info.CanonicalWithRegion().ToUtf8().c_str()) != nullptr)
		return true;

	const ibString tag = ibString::FromUTF8(info.m_tag);
	wchar_t buffer[256] = {};
	if (::GetLocaleInfoEx(tag.wc_str(), LOCALE_SENGLISHLANGUAGENAME, buffer, 256) <= 0)
		return false;
	std::wstring locale = buffer;
	if (::GetLocaleInfoEx(tag.wc_str(), LOCALE_SENGLISHCOUNTRYNAME, buffer, 256) > 0)
		locale += L'_' + std::wstring(buffer);
	if (::GetLocaleInfoEx(tag.wc_str(), LOCALE_IDEFAULTANSICODEPAGE, buffer, 256) > 0 && std::wstring(buffer) != L"0")
		locale += L'.' + std::wstring(buffer);
	return ::_wsetlocale(LC_ALL, locale.c_str()) != nullptr;
}

// A language written only in Unicode has no ANSI code page, and the CRT cannot be set to it; wxLocale::Init did not
// count that a failure - the catalogs are still read, and so are ours.
bool IsUnicodeOnly(const ibLanguageInfo& info)
{
	wchar_t buffer[16] = {};
	return ::GetLocaleInfoEx(ibString::FromUTF8(info.m_tag).wc_str(), LOCALE_IDEFAULTANSICODEPAGE, buffer, 16) > 0
		&& std::wstring(buffer) == L"0";
}
#else
// wxSetlocaleTryUTF8: the UTF-8 forms first - the charset before a modifier, "sr_RS.UTF-8@latin" - then the name as it is.
bool TrySetUtf8(const std::string& name)
{
	const size_t at = name.find('@');
	const std::string base = name.substr(0, at);
	const std::string modifier = at != std::string::npos ? name.substr(at) : std::string();
	if (base.find('.') == std::string::npos) {
		for (const char* charset : { ".UTF-8", ".utf-8", ".UTF8", ".utf8" }) {
			if (std::setlocale(LC_ALL, (base + charset + modifier).c_str()) != nullptr)
				return true;
		}
	}
	return std::setlocale(LC_ALL, name.c_str()) != nullptr;
}

// wxSetlocaleTryAll: with and without the UTF-8 form, and with and without the region.
bool TrySetAll(const std::string& name)
{
	if (TrySetUtf8(name))
		return true;
	const size_t region = name.find('_');
	if (region == std::string::npos)
		return false;
	const size_t rest = name.find_first_of(".@", region);
	return TrySetUtf8(name.substr(0, region) + (rest != std::string::npos ? name.substr(rest) : std::string()));
}

// wxUILocaleImplUnix::Use: and, since some C libraries (glibc) still use the old ISO 639 codes, by those.
bool TrySetLocale(const ibLanguageInfo& info)
{
	const std::string name = info.CanonicalWithRegion().ToUtf8();
	if (TrySetAll(name))
		return true;

	const size_t end = name.find_first_of("_.@");
	const std::string language = name.substr(0, end);
	const std::string rest = end != std::string::npos ? name.substr(end) : std::string();
	if (language == "he")
		return TrySetAll("iw" + rest);
	if (language == "id")
		return TrySetAll("in" + rest);
	if (language == "yi")
		return TrySetAll("ji" + rest);
	if (language == "nb" || language == "nn")
		return TrySetAll(std::string("no_") + (language == "nb" ? "NO" : "NY"));
	return false;   // nothing else to try
}

// wx's GetLocaleFromEnvVar: "lang[_LANG][.encoding][@modifier]", the encoding dropped; "C" and "POSIX" name none; a
// language spelled in full ("german", as SuSE sets LANG) read by its description; and the old ISO 639 codes made the
// new ones.
std::string LocaleFromEnvVar(const char* variable)
{
	const char* const value = std::getenv(variable);
	if (value == nullptr || *value == '\0')
		return std::string();
	std::string named(value);
	const size_t at = named.find('@');
	const std::string modifier = at != std::string::npos ? named.substr(at) : std::string();
	std::string full = named.substr(0, named.find_first_of("@."));
	if (full == "C" || full == "POSIX")
		return std::string();

	if (full.find('_') == std::string::npos && full.size() > 2) {
		for (const ibLanguageInfo& info : kLanguages) {
			if (SameNoCase(full, info.m_description)) {
				full = info.m_canonical;
				break;
			}
		}
	}

	const size_t cut = full.find('_');
	const std::string original = full.substr(0, cut);
	std::string region = cut != std::string::npos ? full.substr(cut) : std::string();
	std::string language = original;
	if (original == "iw")
		language = "he";
	else if (original == "in")
		language = "id";
	else if (original == "ji")
		language = "yi";
	else if (original == "no" && region == "_NO")
		language = "nb";
	else if (original == "no" && region == "_NY") {
		language = "nn";
		region = "_NO";
	}
	else if (original == "no")
		language = "nb";
	return language + region + modifier;
}
#endif

} // namespace

const ibLanguageInfo* ibFindLanguageInfo(const ibString& name)
{
	if (name.IsEmpty())
		return nullptr;
	const std::string asked = name.ToUtf8();
	// A BCP 47 tag ("ru-RU") is read as one, a POSIX name or a description as those.
	return asked.find('-') != std::string::npos ? FindByTag(asked) : FindByName(asked);
}

ibString ibSystemLanguage()
{
#ifdef __IBMSW__
	wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
	if (::GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) <= 0)
		return ibString();
	const std::string tag = ibString(name).ToUtf8();
#else
	std::string tag = LocaleFromEnvVar("LC_ALL");
	if (tag.empty())
		tag = LocaleFromEnvVar("LANG");
	if (tag.empty())
		return ibString();
#endif
	// wxUILocale::GetSystemLocale: the entry that IS this locale - not a language's, found by falling back on it.
	const ibLanguageInfo* const info = ibFindLanguageInfo(ibString::FromUTF8(tag));
	if (info == nullptr)
		return ibString();
	const bool exact = tag.find('-') != std::string::npos ? SameNoCase(tag, info->m_tag) : SameNoCase(tag, info->m_canonical);
	return exact ? ibString::FromUTF8(info->m_canonical) : ibString();
}

ibString ibSetLocale(const ibString& language)
{
	if (language.IsEmpty()) {
		// wxLANGUAGE_DEFAULT: the C library does better on its own than a name given to it (wx).
		if (std::setlocale(LC_ALL, "") == nullptr)
			return ibString();
		const ibString own = ibSystemLanguage();
		return !own.IsEmpty() ? own : ibString(ibT("C"));
	}

	const ibLanguageInfo* const info = ibFindLanguageInfo(language);
	if (info == nullptr)
		return ibString();   // wx: "Unknown language" - nothing is set
	if (TrySetLocale(*info))
		return info->CanonicalWithRegion();
#ifdef __IBMSW__
	if (IsUnicodeOnly(*info))
		return info->CanonicalWithRegion();
#endif
	return ibString();
}
