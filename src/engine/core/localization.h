#ifndef _CORE_LOCALIZATION_H__
#define _CORE_LOCALIZATION_H__

#include <vector>

#include <wx/string.h>

#include "core/core.h"

// ONE LANGUAGE'S TEXT of a translated text — its code (ru/en/uk, the configuration's short form) and the text.
struct ibLocalizationEntry {
	wxString m_code;
	wxString m_data;
};

typedef std::vector<ibLocalizationEntry> ibLocalizationEntryArray;

// THE TRANSLATION ENGINE — a text in its stored form (`en = 'Goods'; ru = 'Товары';`) and a language in, that
// language's text out; and the form read and written. Which language is asked is always the caller's to say: the
// server's session knows its person's, the client its own (ibBackendLocalization::GetUserLanguage).
class CORE_API ibLocalization {
	ibLocalization() = delete;
public:

	static bool CreateLocalizationArray(const wxString& strRawTranslate, ibLocalizationEntryArray& array);

	// The stored form of a text — a plain text written as the given language's; one already in the form kept as it is.
	static wxString CreateLocalizationRawLocText(const wxString& strLangCode, const wxString& strLocale);
	static bool IsLocalizationString(const wxString& strRawLocale);
	static wxString GetRawLocText(const ibLocalizationEntryArray& array);
	static bool GetRawLocText(const ibLocalizationEntryArray& array, wxString& strResult);

	static bool IsEmptyLocalizationString(const wxString& strRawLocale);

	static void SetArrayTranslate(const wxString& strLangCode, ibLocalizationEntryArray& array, const wxString& strResult);

	// The language asked for, else the first written. False only when there is nothing to say.
	static bool GetTranslateFromArray(const wxString& strLangCode,
		const ibLocalizationEntryArray& array, wxString& strResult);
	static wxString GetTranslateFromArray(const wxString& strLangCode,
		const ibLocalizationEntryArray& array);

	// ⭐ THE READING OF A TEXT — its stored form and a language, that language's text; a text in no form is itself.
	static bool GetTranslateGetRawLocText(
		const wxString& strLangCode, const wxString& strRawLocale, wxString& strResult);
	static wxString GetTranslateGetRawLocText(const wxString& strLangCode, const wxString& strRawLocale);
};

#endif
