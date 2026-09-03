#ifndef __BACKEND_LOCALIZATION_H__
#define __BACKEND_LOCALIZATION_H__

#include "backend_core.h"

struct ibBackendLocalizationEntry {
	wxString m_code;
	wxString m_data;
};

typedef std::vector<ibBackendLocalizationEntry> ibBackendLocalizationEntryArray;

class BACKEND_API ibBackendLocalization {
	ibBackendLocalization() = delete;
public:

	// Process-wide configuration-language default. Pinned by metadata
	// OnInitialize to the configuration's main language code (the
	// metadata short-code form ru/en/uk that localization arrays are
	// keyed on); set once at boot from the platform locale before
	// metadata loads.
	static void SetUserLanguage(const wxString& strUserLanguage);

	// Active configuration-language for the calling thread — session's
	// GetLanguageCode() if a session is bound and it has a code,
	// otherwise the process-wide default above. Every internal lookup
	// (synonym translate, raw-loc encode/decode) and the designer's
	// advprop string editor route through here.
	//
	// HOT PATH. A single report line / form synonym lookup hits this
	// once per translatable field, multiplied by row count — easily
	// millions of calls on a 10k-row report. The implementation must
	// stay at (const wxString&) return + cached session-side value +
	// no logic per call.
	static const wxString& GetUserLanguage();

	static bool CreateLocalizationArray(const wxString& strRawTranslate,
		ibBackendLocalizationEntryArray& array);

	static wxString CreateLocalizationRawLocText(const wxString& strLocale);
	static bool IsLocalizationString(const wxString& strRawLocale);
	static wxString GetRawLocText(const ibBackendLocalizationEntryArray& array);
	static bool GetRawLocText(const ibBackendLocalizationEntryArray& array, wxString& strResult);

	static bool IsEmptyLocalizationString(const wxString& strRawLocale);

	static void SetArrayTranslate(ibBackendLocalizationEntryArray& array, const wxString &strResult);
	static void SetArrayTranslate(const wxString& strLangCode, ibBackendLocalizationEntryArray& array, const wxString& strResult);

	static bool GetTranslateFromArray(const wxString& strLangCode,
		const ibBackendLocalizationEntryArray& array, wxString& strResult);
	static wxString GetTranslateFromArray(const wxString& strLangCode,
		const ibBackendLocalizationEntryArray& array);
	
	static bool GetTranslateGetRawLocText(
		const wxString& strRawLocale, wxString& strResult);
	static bool GetTranslateGetRawLocText(
		const wxString& strLangCode, const wxString& strRawLocale, wxString& strResult);

	static wxString GetTranslateGetRawLocText(const wxString& strRawLocale);
	static wxString GetTranslateGetRawLocText(const wxString& strLangCode, const wxString& strRawLocale);
};

// ⭐ THE TRANSLATED TEXT ITSELF — what is passed, held and compared, the way ibNumber is what a
// number property holds. Above it are the verbs; this is the thing they act on, so a caller that has
// translations carries THEM and not a raw string plus the knowledge of how to take it apart.
//
// 🛑 THE FORMAT WAS STANDING IN FOR THE VALUE. `en = 'Goods'; ru = 'Товары';` is how a translated
// text is WRITTEN DOWN, and every reader parsed it, every writer reassembled it, and every question
// began with "is this text in the format at all?" — so a text nobody had translated yet answered no,
// and its first translation could not be written (measured over MCP, 2026-09-03).
//
// The format lives at the edge here: read once in SetRawText, written once in GetRawText.
class BACKEND_API ibTranslateString {
	ibBackendLocalizationEntryArray m_translations;
public:

	ibTranslateString() = default;
	ibTranslateString(const wxString& strRawTranslate) { SetRawText(strRawTranslate); }

	// A LITERAL IS A TEXT TOO — every property declares its default as one (wxT("Button")), and
	// wxString-then-translate is two conversions deep, which the language will not do by itself.
	ibTranslateString(const wxChar* strRawTranslate) { SetRawText(strRawTranslate); }

	// BY REFERENCE, both ways — a caller edits the cells in place.
	ibBackendLocalizationEntryArray& GetTranslations() { return m_translations; }
	const ibBackendLocalizationEntryArray& GetTranslations() const { return m_translations; }

	// WHAT A PERSON READS — the language in force, and a language with no cell of its own reads as
	// that one.
	//
	// ⭐ IT IS A STRING WHEREVER A STRING IS EXPECTED. A caption goes into a label, a tooltip, a log
	// line and a page header; every one of those asks for text, so it converts and there is nothing
	// to call. Which language is a question this type answers by itself.
	wxString GetString() const {
		return GetTranslate(ibBackendLocalization::GetUserLanguage());
	}
	operator wxString() const { return GetString(); }

	// …AND THE TEXT OF ONE NAMED LANGUAGE.
	wxString GetTranslate(const wxString& strLangCode) const {
		return ibBackendLocalization::GetTranslateFromArray(strLangCode, m_translations);
	}

	// ⭐ AND EXACTLY THIS LANGUAGE, no substitute — the Find half of the pair. An editor showing one
	// box per language must not put the English text in the Russian box, because pressing OK would
	// then store it AS the Russian translation.
	bool FindTranslate(const wxString& strLangCode, wxString& strResult) const;
	wxString FindTranslate(const wxString& strLangCode) const;

	// A LANGUAGE THAT IS NOT HERE IS ADDED — SetArrayTranslate's own rule, kept where it was.
	void SetTranslate(const wxString& strLangCode, const wxString& strResult) {
		ibBackendLocalization::SetArrayTranslate(strLangCode, m_translations, strResult);
	}
	void SetTranslate(const wxString& strResult) {
		ibBackendLocalization::SetArrayTranslate(m_translations, strResult);
	}

	bool IsEmpty() const;

	// ⚠ THE STORED FORM, AND ONLY AT THE EDGE — serialisation, and a configuration written before
	// this. A text that is NOT in the format is the text itself, in the language in force: the same
	// reading the platform's own writer gives it (CreateLocalizationRawLocText).
	void SetRawText(const wxString& strRawTranslate);
	wxString GetRawText() const { return ibBackendLocalization::GetRawLocText(m_translations); }

	bool operator == (const ibTranslateString& src) const;
	bool operator != (const ibTranslateString& src) const { return !(*this == src); }
};

#endif 