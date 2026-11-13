#ifndef _CORE_LOCALIZATION_H__
#define _CORE_LOCALIZATION_H__

#include <vector>


#include "core/core.h"

// ONE LANGUAGE'S TEXT of a translated text — its code (ru/en/uk, the configuration's short form) and the text.
struct ibLocalizationEntry {
	ibString m_code;
	ibString m_data;
};

typedef std::vector<ibLocalizationEntry> ibLocalizationEntryArray;

// THE TRANSLATION ENGINE — a text in its stored form (`en = 'Goods'; de = 'Waren';`) and a language in, that
// language's text out; and the form read and written. Which language is asked is always the caller's to say: the
// server's session knows its person's, the client its own (ibBackendLocalization::GetUserLanguage).
class CORE_API ibLocalization {
	ibLocalization() = delete;
public:

	static bool CreateLocalizationArray(const ibString& strRawTranslate, ibLocalizationEntryArray& array);

	// The stored form of a text — a plain text written as the given language's; one already in the form kept as it is.
	static ibString CreateLocalizationRawLocText(const ibString& strLangCode, const ibString& strLocale);
	static bool IsLocalizationString(const ibString& strRawLocale);
	static ibString GetRawLocText(const ibLocalizationEntryArray& array);
	static bool GetRawLocText(const ibLocalizationEntryArray& array, ibString& strResult);

	static bool IsEmptyLocalizationString(const ibString& strRawLocale);

	static void SetArrayTranslate(const ibString& strLangCode, ibLocalizationEntryArray& array, const ibString& strResult);

	// The language asked for, else the first written. False only when there is nothing to say.
	static bool GetTranslateFromArray(const ibString& strLangCode,
		const ibLocalizationEntryArray& array, ibString& strResult);
	static ibString GetTranslateFromArray(const ibString& strLangCode,
		const ibLocalizationEntryArray& array);

	// ⭐ THE READING OF A TEXT — its stored form and a language, that language's text; a text in no form is itself.
	static bool GetTranslateGetRawLocText(
		const ibString& strLangCode, const ibString& strRawLocale, ibString& strResult);
	static ibString GetTranslateGetRawLocText(const ibString& strLangCode, const ibString& strRawLocale);
};

#endif
