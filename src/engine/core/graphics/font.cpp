////////////////////////////////////////////////////////////////////////////
//	Description : A font as data — its text (font.h)
////////////////////////////////////////////////////////////////////////////

#include "font.h"

namespace {

// The weights before wxWidgets 3.1.2 — wxNORMAL, wxLIGHT, wxBOLD; texts written then still carry them.
enum ibLegacyFontWeight {
	ibLegacyFontWeight_Normal = 90,
	ibLegacyFontWeight_Light = 91,
	ibLegacyFontWeight_Bold = 92,
};

} // namespace

bool ibFont::FromString(const ibString& text)
{
	if (text.IsEmpty())
		return false;

	// The comma-separated pieces, empty ones kept — wxStringTokenizer's answer for a non-blank delimiter.
	ibString piece[6];
	ibString rest = text;
	for (size_t i = 0; i < 6 && !rest.IsEmpty(); ++i)
		piece[i] = rest.BeforeFirst(wxT(','), &rest);

	ibFont font;
	long number = 0;

	font.m_faceName = piece[0].TrimAll();

	if (piece[1].ToLong(&number))
		font.m_style = number >= ibFONTSTYLE_NORMAL && number <= ibFONTSTYLE_SLANT
			? static_cast<ibFontStyle>(number) : ibFONTSTYLE_NORMAL;

	if (piece[2].ToLong(&number)) {
		// The old weights are in range of nothing now, so read first; a numeric weight after them, from normal
		// to below the heaviest — the range that reader took.
		switch (number) {
		case ibLegacyFontWeight_Normal: font.m_weight = ibFONTWEIGHT_NORMAL; break;
		case ibLegacyFontWeight_Light: font.m_weight = ibFONTWEIGHT_LIGHT; break;
		case ibLegacyFontWeight_Bold: font.m_weight = ibFONTWEIGHT_BOLD; break;
		default: font.m_weight = number >= ibFONTWEIGHT_NORMAL && number < ibFONTWEIGHT_EXTRAHEAVY
			? static_cast<int>(number) : ibFONTWEIGHT_NORMAL; break;
		}
	}

	if (piece[3].ToLong(&number))
		font.m_pointSize = number;

	if (piece[4].ToLong(&number))
		font.m_family = number >= ibFONTFAMILY_DEFAULT && number <= ibFONTFAMILY_TELETYPE
			? static_cast<ibFontFamily>(number) : ibFONTFAMILY_DEFAULT;

	if (piece[5].ToLong(&number))
		font.m_underlined = number != 0;

	*this = font;
	return true;
}

ibString ibFont::ToString() const
{
	if (!IsOk())
		return ibString();
	return ibString::Format(wxT("%s,%d,%d,%d,%d,%d"), m_faceName, static_cast<int>(m_style),
		m_weight, GetPointSize(), static_cast<int>(m_family), m_underlined ? 1 : 0);
}
