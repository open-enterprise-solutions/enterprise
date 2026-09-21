#include "backend_localization.h"
#include "appData.h"
#include "session/session.h"

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

	// One language done — kept, or replacing an earlier one of the same code.
	const auto commit = [&array]() {
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
		entry.m_code.Clear();
		entry.m_data.Clear();
	};

	const std::wstring raw = strRawLocale.ToStdWstring();
	for (size_t i = 0; i < raw.size(); ++i) {
		const wchar_t c = raw[i];

		// ⭐ A DOUBLED QUOTE INSIDE THE TEXT IS AN APOSTROPHE — `en = 'the document''s movements';`.
		// Read as two toggles it closed the text and opened it again, and the apostrophe vanished from
		// every caption that had one (the payroll demo's English texts, 2026-09-10). GetRawLocText
		// writes it back doubled.
		if (open_text && open_symbol && c == wxT('\'') && i + 1 < raw.size() && raw[i + 1] == wxT('\'')) {
			entry.m_data += c;
			++i;
			continue;
		}

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
			commit();
			open_text = open_symbol = false;
			continue;
		}

		if (open_text && open_symbol)
			entry.m_data += c;
		else if (!open_text && !open_symbol)
			entry.m_code += c;
	}

	// …AND THE LAST LANGUAGE MAY END WITH THE STRING, without its `;` (see IsLocalizationString).
	if (open_text && !open_symbol)
		commit();

	return array.size() > 0;
}

// 🛑 A STORED TEXT WAS TRANSLATED, A PLAIN ONE WRAPPED — two answers under one name, so what a cell held
// depended on what it said, and a filled template landed in either form. It makes the stored form now:
// one already in it is kept as it is (2026-09-21).
wxString ibBackendLocalization::CreateLocalizationRawLocText(const wxString& strLocale)
{
	if (IsLocalizationString(strLocale))
		return strLocale;

	wxString text = strLocale;
	text.Replace(wxT("'"), wxT("''"));   // an apostrophe is written doubled (CreateLocalizationArray)
	return wxString::Format(wxT("%s = '%s';"), GetUserLanguage(), text);
}

bool ibBackendLocalization::IsLocalizationString(const wxString& strRawLocale)
{
	if (strRawLocale.IsEmpty())
		return false;

	// ⭐ NO `=`, NO LANGUAGE — and nothing to copy or walk. Every cell of a composed report is plain text
	// read as it lands (PutArea), so this answer is asked hundreds of thousands of times on a large sheet,
	// and almost always "no".
	if (strRawLocale.find(wxT('=')) == wxString::npos)
		return false;

	bool open_text = false,
		open_symbol = false;

	bool success = false;
	bool closed  = false;   // the current language's text has been closed by its quote

	for (const auto& c : strRawLocale.ToStdWstring()) {

		if ((!open_text && (c == wxT(' ') || c == wxT('\n'))) || c == wxT('\'')) {
			if (open_text && c == wxT('\'')) {
				open_symbol = !open_symbol;
				if (!open_symbol)
					closed = true;
			}
			success = false;
			continue;
		}
		else if (!open_text && !open_symbol && c == wxT('=')) {
			open_text = true;
			closed = false;
			success = false;
			continue;
		}
		else if (open_text && !open_symbol && c == wxT(';')) {
			open_text = open_symbol = false;
			success = true;
			continue;
		}
	}

	// ⭐ THE LAST LANGUAGE MAY END WITH THE STRING. `en = 'June'; ru = 'Iyun'` is how a person writes it
	// — the separator is BETWEEN languages — and it was not recognised at all: Tstr handed the whole
	// text back and a heading read "en = 'June'; ru = ..." (the payroll demo's month names, 2026-09-10).
	// A closed quote at the end is the end of that language, exactly as `;` would have been.
	if (!success && open_text && !open_symbol && closed)
		success = true;

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
		wxString text = pair.m_data;
		text.Replace(wxT("'"), wxT("''"));   // an apostrophe is written doubled (CreateLocalizationArray)
		strResult += wxString::Format(
			wxT("%s = '%s';"), pair.m_code, text);
	}
	return array.size() > 0;
}

// 🛑 IT ASKED ONLY THE LANGUAGE IN FORCE, with a parser of its own, so a caption written in another was
// empty here while the screen showed it (GetTranslateFromArray falls back). Empty now means empty in
// every language — the same array the reading uses.
bool ibBackendLocalization::IsEmptyLocalizationString(const wxString& strRawLocale)
{
	thread_local ibBackendLocalizationEntryArray array;
	if (!CreateLocalizationArray(strRawLocale, array))
		return strRawLocale.IsEmpty();

	for (const ibBackendLocalizationEntry& entry : array) {
		if (!entry.m_data.IsEmpty())
			return false;
	}

	return true;
}

bool ibBackendLocalization::GetTranslateGetRawLocText(const wxString& strRawLocale, wxString& strResult)
{
	return GetTranslateGetRawLocText(GetUserLanguage(), strRawLocale, strResult);
}

// ⭐⭐ THE READING OF A TEXT — every caption, template, title and cell asks here: the language asked for,
// else the one in force, else the first written (GetTranslateFromArray, the rule ibTranslateString reads
// by too); and A TEXT IN NO FORMAT IS ITSELF. False only when there is nothing to say.
//
// 🛑 A PLAIN TEXT READ AS NOTHING. It answered EMPTY for anything not written in every language — a
// caption typed plainly, a value a template was filled with — so four callers wrote "…then the source"
// back by hand and two did not and showed nothing. With CreateLocalizationRawLocText translating a stored
// text and IsEmptyLocalizationString asking one language, a printed sheet carried every caption in its
// stored form, every language at once (2026-09-21). The rules are here, and the callers only ask.
//
// ⚠ HOT: every cell that lands (PutArea), every cell painted, every cell the content edge walks over — so
// the array is a per-thread scratch, and a plain text, almost every cell of a composed report, is
// answered without allocating anything but the answer (IsLocalizationString stops at the missing `=`).
bool ibBackendLocalization::GetTranslateGetRawLocText(const wxString& strLangCode, const wxString& strRawLocale, wxString& strResult)
{
	thread_local ibBackendLocalizationEntryArray array;
	if (CreateLocalizationArray(strRawLocale, array))
		GetTranslateFromArray(strLangCode, array, strResult);
	else
		strResult = strRawLocale;   // ⭐ a text in no format is itself

	return !strResult.IsEmpty();
}

wxString ibBackendLocalization::GetTranslateGetRawLocText(const wxString& strRawLocale)
{
	wxString strResult;
	GetTranslateGetRawLocText(strRawLocale, strResult);
	return strResult;
}

wxString ibBackendLocalization::GetTranslateGetRawLocText(const wxString& strLangCode, const wxString& strRawLocale)
{
	wxString strResult;
	GetTranslateGetRawLocText(strLangCode, strRawLocale, strResult);
	return strResult;
}

void ibBackendLocalization::SetArrayTranslate(ibBackendLocalizationEntryArray& array, const wxString& strResult)
{
	SetArrayTranslate(GetUserLanguage(), array, strResult);
}

// 🛑 IT ONLY UPDATED, AND SO COULD NOT WRITE A FIRST TRANSLATION. The loop looked for a cell with
// this code and did nothing when there was none — which is exactly the state of every caption that
// has never been filled in: the array is empty, the write folds to an empty string, and the caption
// is left blank while the caller is told the value was set.
//
// Measured over MCP on 2026-09-03: `metadata_set {property: "Synonym", value: "…", language: "en"}`
// answered with the value it had accepted, and reading the property back showed nothing. The gate
// above it had been fixed once for the very same case ("a property that has never been filled in is
// exactly when this is used") — the gate, not the write.
//
// ⭐ A language that is not in the array is APPENDED. Nobody asking to set a translation means "only
// if one is already there", and a no-op is the one answer that cannot be told from success.
//
// ⚠ THE CODE IS MATCHED THE WAY IT IS READ — without regard to case, as the parser folds a repeated
// language (CreateLocalizationArray) and FindTranslate finds one. An exact `==` here found `EN` for
// a reader and missed it for a writer, which appended a second English cell beside the first.
void ibBackendLocalization::SetArrayTranslate(const wxString& strLangCode, ibBackendLocalizationEntryArray& array, const wxString& strResult)
{
	for (auto& entry : array) {
		if (stringUtils::CompareString(entry.m_code, strLangCode)) {
			entry.m_data = strResult;
			return;
		}
	}

	array.push_back(ibBackendLocalizationEntry{ strLangCode, strResult });
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

	// ⭐ LAST RESORT: NEITHER THE REQUESTED LANGUAGE NOR THE ACTIVE ONE IS HERE, and a caption a person
	// can read beats an empty one. Returning blank made content authored in one set of languages
	// INVISIBLE under another UI language — every notebook tab, every decoration, every group heading
	// silently empty, with nothing on screen saying a translation was merely missing.
	//
	// ⚠ It matters here more than it looks: this configuration declares three languages, so a caption
	// written in two of them and read under the third hits this exact path. An object whose name a
	// person cannot see is, from where they sit, an object without a name.
	//
	// The FIRST entry rather than a guessed one: the array is in the order the author wrote it, so the
	// first is the one they started from (2026-08-19).
	if (!array.empty()) {
		strResult = array.front().m_data;
		return true;
	}

	strResult.Clear();
	return false;
}
////////////////////////////////////////////////////////////////////////

void ibTranslateString::SetRawText(const wxString& strRawTranslate)
{
	m_translations.clear();

	if (ibBackendLocalization::CreateLocalizationArray(strRawTranslate, m_translations))
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
		[&strLangCode](const ibBackendLocalizationEntry& entry) {
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
		[&strLangCode](const ibBackendLocalizationEntry& entry) {
			return stringUtils::CompareString(entry.m_code, strLangCode); }),
		m_translations.end());
}

bool ibTranslateString::IsEmpty() const
{
	for (const ibBackendLocalizationEntry& entry : m_translations) {
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

	for (const ibBackendLocalizationEntry& entry : m_translations) {
		// EXACTLY this language — the same reason the editor asks that way.
		wxString strResult;
		if (!src.FindTranslate(entry.m_code, strResult) || strResult != entry.m_data)
			return false;
	}

	return true;
}
