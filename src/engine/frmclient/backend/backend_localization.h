#ifndef __BACKEND_LOCALIZATION_H__
#define __BACKEND_LOCALIZATION_H__

#include <vector>

#include <wx/wx.h>

// What the engine's backend_core.h brought it: the string helpers.
#include "frmclient/frmclient.h"
#include "core/stringUtils.h"
#include "core/localization.h"   // ibLocalization — the translation engine; a language is always given to it

// WHICH LANGUAGE IS IN FORCE HERE — the engine's half of translation; the translating itself is the core's
// (ibLocalization), asked with this language.
class FRMCLIENT_API ibBackendLocalization {
	ibBackendLocalization() = delete;
public:

	// The configuration's main language code (the metadata short-code form
	// ru/en/uk that localization arrays are keyed on) for whoever translates
	// here — the current session's translate state, else this thread's
	// (ibSession::GetTranslateState). Pinned when the configuration loads and
	// when the designer changes it; a session takes its base's in CompileRoot.
	static void SetUserLanguage(const wxString& strUserLanguage);

	// Active configuration-language for the calling thread — the translate
	// state's answer: the script's override, else the user's, else the
	// configuration's; each session its own, a thread without one its own.
	// Every internal lookup
	// (synonym translate, raw-loc encode/decode) and the designer's
	// advprop string editor route through here.
	//
	// HOT PATH. A single report line / form synonym lookup hits this
	// once per translatable field, multiplied by row count — easily
	// millions of calls on a 10k-row report. The implementation must
	// stay at (const wxString&) return + cached session-side value +
	// no logic per call.
	static const wxString& GetUserLanguage();
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
class FRMCLIENT_API ibTranslateString {
	ibLocalizationEntryArray m_translations;
public:

	ibTranslateString() = default;
	ibTranslateString(const wxString& strRawTranslate) { SetRawText(strRawTranslate); }

	// A LITERAL IS A TEXT TOO — every property declares its default as one (wxT("Button")), and
	// wxString-then-translate is two conversions deep, which the language will not do by itself.
	ibTranslateString(const wxChar* strRawTranslate) { SetRawText(strRawTranslate); }

	// BY REFERENCE, both ways — a caller edits the cells in place.
	ibLocalizationEntryArray& GetTranslations() { return m_translations; }
	const ibLocalizationEntryArray& GetTranslations() const { return m_translations; }

	// WHAT A PERSON READS — the language in force, and a language with no cell of its own reads as
	// that one.
	//
	// ⭐ IT IS A STRING WHEREVER A STRING IS EXPECTED. A caption goes into a label, a tooltip, a log
	// line and a page header; every one of those asks for text, so it converts and there is nothing
	// to call. Which language is a question this type answers by itself.
	wxString GetString() const {
		return GetTranslate(ibBackendLocalization::GetUserLanguage());
	}

	// …AND THE SAME TEXT WRITTEN INTO `scratch`, which is what comes back — for a caller reading one text
	// after another: a scratch reused call after call grows once and then stops allocating.
	const wxString& GetString(wxString& scratch) const {
		ibLocalization::GetTranslateFromArray(ibBackendLocalization::GetUserLanguage(), m_translations, scratch);
		return scratch;
	}

	// …AND THE TEXT OF ONE NAMED LANGUAGE.
	wxString GetTranslate(const wxString& strLangCode) const {
		return ibLocalization::GetTranslateFromArray(strLangCode, m_translations);
	}

	// ⭐ AND EXACTLY THIS LANGUAGE, no substitute — the Find half of the pair. An editor showing one
	// box per language must not put the English text in the Russian box, because pressing OK would
	// then store it AS the Russian translation.
	bool FindTranslate(const wxString& strLangCode, wxString& strResult) const;
	wxString FindTranslate(const wxString& strLangCode) const;

	// A LANGUAGE THAT IS NOT HERE IS ADDED — SetArrayTranslate's own rule, kept where it was.
	void SetTranslate(const wxString& strLangCode, const wxString& strResult) {
		ibLocalization::SetArrayTranslate(strLangCode, m_translations, strResult);
	}
	void SetTranslate(const wxString& strResult) {
		SetTranslate(ibBackendLocalization::GetUserLanguage(), strResult);
	}

	// …AND ONE THAT IS TAKEN OUT. Not the same as writing it empty: a language with no cell reads as
	// another one (GetTranslate), a cell holding an empty text reads as nothing. The rest keep their order.
	void RemoveTranslate(const wxString& strLangCode);

	bool IsEmpty() const;

	// ⚠ THE STORED FORM, AND ONLY AT THE EDGE — serialisation, and a configuration written before
	// this. A text that is NOT in the format is the text itself, in the language in force: the same
	// reading the platform's own writer gives it (CreateLocalizationRawLocText).
	void SetRawText(const wxString& strRawTranslate);
	wxString GetRawText() const { return ibLocalization::GetRawLocText(m_translations); }

	bool operator == (const ibTranslateString& src) const;
	bool operator != (const ibTranslateString& src) const { return !(*this == src); }
};

#endif 