#include "backend_localization.h"

// What is answered when nothing has said a language yet.
static const wxString ms_strUserLanguage = wxT("en");

// ⚠ THE CLIENT'S — one person's, the session's language as the server says it: the client is one session's window,
// where the server's translate state is each session's own (ibSession::GetTranslateState).
static wxString s_userLanguage;

////////////////////////////////////////////////////////////////////////////////

void ibBackendLocalization::SetUserLanguage(const wxString& strUserLanguage)
{
	s_userLanguage = strUserLanguage.IsEmpty() ? ms_strUserLanguage : strUserLanguage;
}

const wxString& ibBackendLocalization::GetUserLanguage()
{
	if (!s_userLanguage.IsEmpty())
		return s_userLanguage;
	return ms_strUserLanguage;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

void ibTranslateString::SetRawText(const wxString& strRawTranslate)
{
	m_translations.clear();

	if (ibLocalization::CreateLocalizationArray(strRawTranslate, m_translations))
		return;

	// Not in the format: it IS the text, in the language in force.
	if (!strRawTranslate.IsEmpty())
		SetTranslate(strRawTranslate);
}

// ⭐ EXACTLY THIS LANGUAGE — the Find half. GetTranslate falls back to the language in force, which
// is what a reader wants and what an editor must not have: one box per language, and a substitute in
// the box is stored AS that language's translation the moment OK is pressed.
bool ibTranslateString::FindTranslate(const wxString& strLangCode, wxString& strResult) const
{
	const auto iterator = std::find_if(m_translations.begin(), m_translations.end(),
		[&strLangCode](const ibLocalizationEntry& entry) {
			return stringUtils::CompareString(entry.m_code, strLangCode); });

	if (iterator == m_translations.end()) {
		strResult.Clear();
		return false;
	}

	strResult = iterator->m_data;
	return true;
}

wxString ibTranslateString::FindTranslate(const wxString& strLangCode) const
{
	wxString strResult;
	FindTranslate(strLangCode, strResult);
	return strResult;
}

void ibTranslateString::RemoveTranslate(const wxString& strLangCode)
{
	m_translations.erase(std::remove_if(m_translations.begin(), m_translations.end(),
		[&strLangCode](const ibLocalizationEntry& entry) {
			return stringUtils::CompareString(entry.m_code, strLangCode); }),
		m_translations.end());
}

bool ibTranslateString::IsEmpty() const
{
	for (const ibLocalizationEntry& entry : m_translations) {
		if (!entry.m_data.IsEmpty())
			return false;
	}

	return true;
}

// EQUAL WHEN THEY SAY THE SAME THING IN THE SAME LANGUAGES — order is how they were written, not
// what they mean, so it is not compared.
bool ibTranslateString::operator == (const ibTranslateString& src) const
{
	if (src.m_translations.size() != m_translations.size())
		return false;

	for (const ibLocalizationEntry& entry : m_translations) {
		// EXACTLY this language — the same reason the editor asks that way.
		wxString strResult;
		if (!src.FindTranslate(entry.m_code, strResult) || strResult != entry.m_data)
			return false;
	}

	return true;
}
