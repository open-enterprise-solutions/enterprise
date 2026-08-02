#include "backend_localization.h"
#include "appData.h"
#include "session/session.h"

static wxString ms_strEmptyLanguage;
static wxString ms_strUserLanguage = wxT("en");

////////////////////////////////////////////////////////////////////////////////

void ibBackendLocalization::SetUserLanguage(const wxString& strUserLanguage)
{
	if (strUserLanguage.IsEmpty()) {
		ms_strUserLanguage = wxT("en");
		return;
	}

	ms_strUserLanguage = strUserLanguage;
}

const wxString& ibBackendLocalization::GetUserLanguage()
{
	// Hot path — two slots only:
	//   1. ibSession::Current()->GetLanguageCode() — per-session,
	//      pre-computed m_resolvedLanguageCode (single field load),
	//      set in SetUserInfo on authentication.
	//   2. ms_strUserLanguage — process-wide default. Pinned by
	//      metadata OnInitialize to the configuration's main language
	//      code (metadata short-code form ru/en/uk).
	if (auto* s = ibSession::Current()) {
		const wxString& code = s->GetLanguageCode();
		if (!code.IsEmpty())
			return code;
	}
	return ms_strUserLanguage;
}

////////////////////////////////////////////////////////////////////////////////

bool ibBackendLocalization::CreateLocalizationArray(const wxString& strRawLocale, ibBackendLocalizationEntryArray& array)
{
	bool open_text = false,
		open_symbol = false;

	if (!IsLocalizationString(strRawLocale))
		return false;

#pragma region _calc_count_h_

	// POD scalar — no need for thread_local static; zero-init is free.
	size_t reserve_count = 0;

	for (const auto& c : strRawLocale.ToStdWstring()) {

		if ((!open_text && (c == wxT(' ') || c == wxT('\n'))) || c == wxT('\'')) {
			if (open_text && c == wxT('\''))
				open_symbol = !open_symbol;
			continue;
		}
		else if (!open_text && !open_symbol && c == wxT('=')) {
			open_text = true;
			continue;
		}
		else if (open_text && !open_symbol && c == wxT(';')) {
			reserve_count++;
			open_text = open_symbol = false;
			continue;
		}
	}

	open_text = open_symbol = false;

	array.clear();
	array.reserve(reserve_count);

#pragma endregion

	// thread_local so each worker / HTTP thread reuses its own buffer
	// capacity (wxString keeps its allocation across Clear) without
	// racing on a single shared static. Was plain `static` — crashed
	// /session on HTTP thread while session worker mutated the same
	// buffer (dump 2026-04-19 17:12).
	thread_local ibBackendLocalizationEntry entry;
	entry.m_code.Clear();
	entry.m_data.Clear();

	for (const auto& c : strRawLocale.ToStdWstring()) {

		if ((!open_text && (c == wxT(' ') || c == wxT('\n'))) || c == wxT('\'')) {
			if (open_text && c == wxT('\''))
				open_symbol = !open_symbol;
			continue;
		}
		else if (!open_text && !open_symbol && c == wxT('=')) {
			open_text = true;
			continue;
		}
		else if (open_text && !open_symbol && c == wxT(';')) {
			const wxString& code = entry.m_code;
			const auto iterator = std::find_if(array.begin(), array.end(),
				[code](const ibBackendLocalizationEntry& e) {
					return stringUtils::CompareString(code, e.m_code); });

			if (iterator == array.end()) {
				array.emplace_back(std::move(entry));
			}
			else {
				*iterator = std::move(entry);
			}

			open_text = open_symbol = false;
			continue;
		}

		if (open_text && open_symbol)
			entry.m_data += c;
		else if (!open_text && !open_symbol)
			entry.m_code += c;
	}

	return array.size() > 0;
}

wxString ibBackendLocalization::CreateLocalizationRawLocText(const wxString& strLocale)
{
	thread_local ibBackendLocalizationEntryArray array;
	const wxString& activeLang = GetUserLanguage();
	if (CreateLocalizationArray(strLocale, array))
		return GetTranslateFromArray(activeLang, array);

	return wxString::Format(wxT("%s = '%s';"), activeLang, strLocale);
}

bool ibBackendLocalization::IsLocalizationString(const wxString& strRawLocale)
{
	if (strRawLocale.IsEmpty())
		return false;

	bool open_text = false,
		open_symbol = false;

	bool success = false;

	for (const auto& c : strRawLocale.ToStdWstring()) {

		if ((!open_text && (c == wxT(' ') || c == wxT('\n'))) || c == wxT('\'')) {
			if (open_text && c == wxT('\''))
				open_symbol = !open_symbol;
			success = false;
			continue;
		}
		else if (!open_text && !open_symbol && c == wxT('=')) {
			open_text = true;
			success = false;
			continue;
		}
		else if (open_text && !open_symbol && c == wxT(';')) {
			open_text = open_symbol = false;
			success = true;
			continue;
		}
	}

	return success;
}

wxString ibBackendLocalization::GetRawLocText(const ibBackendLocalizationEntryArray& array)
{
	thread_local wxString strRawTranslate;
	GetRawLocText(array, strRawTranslate);
	return std::move(strRawTranslate);
}

bool ibBackendLocalization::GetRawLocText(const ibBackendLocalizationEntryArray& array, wxString& strResult)
{
	strResult.Clear();
	for (const auto& pair : array) {
		strResult += wxString::Format(
			wxT("%s = '%s';"), pair.m_code, pair.m_data);
	}
	return array.size() > 0;
}

bool ibBackendLocalization::IsEmptyLocalizationString(const wxString& strRawLocale)
{
	if (strRawLocale.IsEmpty())
		return true;

	bool open_text = false,
		open_symbol = false;

	bool success = false;

	thread_local wxString code, data;
	code.Clear(); data.Clear();

	for (const auto& c : strRawLocale.ToStdWstring()) {

		if ((!open_text && (c == wxT(' ') || c == wxT('\n'))) || c == wxT('\'')) {
			if (open_text && c == wxT('\''))
				open_symbol = !open_symbol;
			success = false;
			continue;
		}
		else if (!open_text && !open_symbol && c == wxT('=')) {
			open_text = true;
			success = false;
			continue;
		}
		else if (open_text && !open_symbol && c == wxT(';')) {
			open_text = open_symbol = false;
			success = true;
			if (stringUtils::CompareString(GetUserLanguage(), code))
				break;
			continue;
		}

		if (open_text && open_symbol)
			data += c;
		else if (!open_text && !open_symbol)
			code += c;
	}

	if (success && stringUtils::CompareString(GetUserLanguage(), code))
		return data.IsEmpty();
	
	return true; 
}

bool ibBackendLocalization::GetTranslateGetRawLocText(const wxString& strRawLocale, wxString& strResult)
{
	return GetTranslateGetRawLocText(GetUserLanguage(), strRawLocale, strResult);
}

bool ibBackendLocalization::GetTranslateGetRawLocText(const wxString& strLangCode, const wxString& strRawLocale, wxString& strResult)
{
	thread_local ibBackendLocalizationEntryArray array;

	if (CreateLocalizationArray(strRawLocale, array) &&
		GetTranslateFromArray(strLangCode, array, strResult))
		return true;

	strResult.Clear();
	//strResult = strRawLocale;
	return false;
}

wxString ibBackendLocalization::GetTranslateGetRawLocText(const wxString& strRawLocale)
{
	thread_local wxString strResult;
	if (GetTranslateGetRawLocText(strRawLocale, strResult))
		return std::move(strResult);
	return ms_strEmptyLanguage;
}

wxString ibBackendLocalization::GetTranslateGetRawLocText(const wxString& strLangCode, const wxString& strRawLocale)
{
	thread_local wxString strResult;
	if (GetTranslateGetRawLocText(strLangCode, strRawLocale, strResult))
		return std::move(strResult);
	return ms_strEmptyLanguage;
}

void ibBackendLocalization::SetArrayTranslate(ibBackendLocalizationEntryArray& array, const wxString& strResult)
{
	SetArrayTranslate(GetUserLanguage(), array, strResult);
}

void ibBackendLocalization::SetArrayTranslate(const wxString& strLangCode, ibBackendLocalizationEntryArray& array, const wxString& strResult)
{
	for (auto& entry : array) {
		if (entry.m_code == strLangCode) {
			entry.m_data = strResult;
			break;
		}
	}
}

wxString ibBackendLocalization::GetTranslateFromArray(const wxString& strLangCode, const ibBackendLocalizationEntryArray& array)
{
	thread_local wxString result;
	GetTranslateFromArray(strLangCode, array, result);
	return std::move(result);
}

bool ibBackendLocalization::GetTranslateFromArray(const wxString& strLangCode, const ibBackendLocalizationEntryArray& array, wxString& strResult)
{
	if (!strLangCode.IsEmpty()) {
		auto iterator = std::find_if(array.begin(), array.end(),
			[strLangCode](const ibBackendLocalizationEntry& entry) {
				return stringUtils::CompareString(entry.m_code, strLangCode); });

		if (iterator != array.end()) {
			strResult = iterator->m_data;
			return true;
		}
		else {
			const wxString& strActiveLang = GetUserLanguage();
			if (!stringUtils::CompareString(strActiveLang, strLangCode)) {
				const auto iterator_by_default_lang = std::find_if(array.begin(), array.end(),
					[&strActiveLang](const ibBackendLocalizationEntry& entry) {
						return stringUtils::CompareString(entry.m_code, strActiveLang); });
				if (iterator_by_default_lang != array.end()) {
					strResult = iterator_by_default_lang->m_data;
					return true;
				}
			}
		}
	}
	else {
		const wxString& strActiveLang = GetUserLanguage();
		const auto iterator_by_default_lang = std::find_if(array.begin(), array.end(),
			[&strActiveLang](const ibBackendLocalizationEntry& entry) {
				return stringUtils::CompareString(entry.m_code, strActiveLang); });
		if (iterator_by_default_lang != array.end()) {
			strResult = iterator_by_default_lang->m_data;
			return true;
		}
	}

	strResult.Clear();
	return false;
}