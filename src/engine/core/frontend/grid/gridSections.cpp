////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2C-team
//	Description : grid sections
////////////////////////////////////////////////////////////////////////////

#include "gridSections.h"
#include "gridCommon.h"
#include "gridUtils.h"
#include "core/compiler/translateError.h"
#include "utils/stringUtils.h"

CSectionCtrl::CSectionCtrl(CGrid* grid, eSectionMode setMode) :
	m_grid(grid),
	m_sectionMode(setMode) {
}

wxPoint CSectionCtrl::GetRange(const wxString& sectionName) const
{
	for (unsigned int n = 0; n < m_aSections.size(); n++) {
		const section_t& currSection = m_aSections[n];
		if (StringUtils::CompareString(sectionName, currSection.m_sectionName)) {
			return wxPoint(currSection.m_rangeFrom, currSection.m_rangeTo);
		}
	}

	CTranslateError::Error("Wrong section named \"%s\"", sectionName);
	return wxPoint();
}

int CSectionCtrl::FindInSection(unsigned int currNumber, wxString& secName) const
{
	for (unsigned int n = 0; n < m_aSections.size(); n++)
	{
		int result = 0;

		const section_t& currSection = m_aSections[n];

		if (currNumber >= currSection.m_rangeFrom && currNumber <= currSection.m_rangeTo)
			result += 1;
		if (currNumber == currSection.m_rangeFrom)
			result += 2;
		if (currNumber == currSection.m_rangeTo)
			result += 4;

		if (result > 0) {
			secName = currSection.m_sectionName;
			return result;
		}
	}

	return 0;
}

section_t *CSectionCtrl::FindSectionByPos(unsigned int pos)
{
	for (unsigned int n = 0; n < m_aSections.size(); n++) {
		int result = 0;
		section_t& currSection = m_aSections[n];
		if (pos >= currSection.m_rangeFrom &&
			pos <= currSection.m_rangeTo)
			result += 1;
		if (pos == currSection.m_rangeFrom)
			result += 2;
		if (pos == currSection.m_rangeTo)
			result += 4;
		if (result > 0) {
			return &currSection;
		}
	}
	return NULL;
}

int CSectionCtrl::GetNSectionFromPoint(const wxPoint& point) const
{
	wxGridCellCoords idCurrentCell = m_grid->GetCellFromPoint(point);

	unsigned int currCol = idCurrentCell.GetCol();
	unsigned int currRow = idCurrentCell.GetRow();

	for (unsigned int n = 0; n < m_aSections.size(); n++) {
		const section_t& currSection = m_aSections[n];
		if (m_sectionMode == LEFT_MODE) {
			if (currRow >= currSection.m_rangeFrom &&
				currRow <= currSection.m_rangeTo) {
				return n;
			}
		}
		else if (m_sectionMode == UPPER_MODE) {
			if (currCol >= currSection.m_rangeFrom &&
				currCol <= currSection.m_rangeTo) {
				return n;
			}
		}
	}

	return wxNOT_FOUND;
}

bool CSectionCtrl::Add(unsigned int rangeFrom, unsigned int rangeTo)
{
	for (unsigned int n = 0; n < m_aSections.size(); n++) {
		section_t& currSection = m_aSections[n];
		if (rangeFrom >= currSection.m_rangeFrom &&
			rangeFrom <= currSection.m_rangeTo) {
			if (rangeTo > currSection.m_rangeTo) {//расширение нижней границы
				currSection.m_rangeTo = rangeTo;
				m_aSections[n] = currSection;
				return false;
			}
		}
		if (rangeTo >= currSection.m_rangeFrom &&
			rangeTo <= currSection.m_rangeTo) {
			if (rangeFrom < currSection.m_rangeFrom) { //расширение верхней границы
				currSection.m_rangeFrom = rangeFrom;
				m_aSections[n] = currSection;
				return false;
			}
		}
		if (rangeFrom >= currSection.m_rangeFrom &&
			rangeFrom <= currSection.m_rangeTo)
			return false;
		if (rangeTo >= currSection.m_rangeFrom &&
			rangeTo <= currSection.m_rangeTo)
			return false;
	}

	wxString num;
	num << m_aSections.size() + 1;

	//добавление новой секции
	section_t currSection;
	currSection.m_sectionName = _("Section_") + num;
	currSection.m_rangeFrom = rangeFrom;
	currSection.m_rangeTo = rangeTo;

	if (CSectionCtrl::EditName(currSection)) {
		m_aSections.push_back(currSection);
		return true;
	}

	return false;
}

bool CSectionCtrl::Remove(unsigned int rangeFrom, unsigned int rangeTo)
{
	for (unsigned int n = 0; n < m_aSections.size(); n++) {
		section_t& currSection = m_aSections[n];
		if (rangeFrom == currSection.m_rangeFrom &&
			rangeTo == currSection.m_rangeTo) { //удаление секции
			m_aSections.erase(m_aSections.begin() + n);
			return true;
		}
		if (rangeFrom >= currSection.m_rangeFrom &&
			rangeFrom <= currSection.m_rangeTo) {
			if (rangeTo >= currSection.m_rangeTo) { //удаление нижней границы
				currSection.m_rangeTo = rangeFrom - 1;
				m_aSections[n] = currSection;
				return true;
			}
		}
		if (rangeTo >= currSection.m_rangeFrom &&
			rangeTo <= currSection.m_rangeTo) {
			if (rangeFrom <= currSection.m_rangeFrom) { //удаление верхней границы
				currSection.m_rangeFrom = rangeTo + 1;
				m_aSections[n] = currSection;
				return true;
			}
		}
	}

	return false;
}

void CSectionCtrl::InsertRow(unsigned int nRow)
{
	if (nRow < 0)
		return;

	for (unsigned int n = 0; n < m_aSections.size(); n++) {
		section_t& currSection = m_aSections[n];
		if (nRow < currSection.m_rangeFrom)
			currSection.m_rangeFrom++;
		if (nRow <= currSection.m_rangeTo)
			currSection.m_rangeTo++;
		m_aSections[n] = currSection;
	}
}

void CSectionCtrl::RemoveRow(unsigned int nRow)
{
	if (nRow < 0)
		return;

	for (unsigned int n = 0; n < m_aSections.size(); n++) {
		section_t& currSection = m_aSections[n];
		if (nRow < currSection.m_rangeFrom)
			currSection.m_rangeFrom--;
		if (nRow <= currSection.m_rangeTo)
			currSection.m_rangeTo--;
		if (currSection.m_rangeTo < currSection.m_rangeFrom) {
			m_aSections.erase(m_aSections.begin() + n); n--;
		}
		else {
			m_aSections[n] = currSection;
		}
	}
}

bool CSectionCtrl::EditName(int pos)
{
	if (pos >= 0 && pos < (int)CSectionCtrl::Count()) {
		section_t& currSection =
			CSectionCtrl::GetSection(pos);
		return EditName(currSection);
	}

	return false;
}

#include "utils/stringUtils.h"

bool CSectionCtrl::EditName(section_t& section)
{
	CInputSectionWnd* dlg = new CInputSectionWnd(m_grid, wxID_ANY);

	dlg->SetTitle(_("Identifier section"));
	dlg->SetSection(section.m_sectionName);

	if (dlg->ShowModal() == wxID_OK) {
		wxString sectionName = dlg->GetSection();
		if (StringUtils::CheckCorrectName(sectionName) < 0) {
			section.m_sectionName = sectionName;
			return true;
		}
	}

	return false;
}