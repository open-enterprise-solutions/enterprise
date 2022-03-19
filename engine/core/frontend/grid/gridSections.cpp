////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2C-team
//	Description : grid sections
////////////////////////////////////////////////////////////////////////////

#include "gridSections.h"
#include "gridCommon.h"
#include "gridUtils.h"
#include "compiler/functions.h"
#include "utils/stringUtils.h"

CSectionCtrl::CSectionCtrl(CGrid* grid, eSectionMode setMode) : m_grid(grid), m_nMode(setMode) {}

CSectionCtrl::~CSectionCtrl() {}

wxPoint CSectionCtrl::GetRange(const wxString& sectionName)
{
	wxString sSectionName = StringUtils::MakeUpper(sectionName);

	for (unsigned int n = 0; n < m_aSections.size(); n++)
	{
		CSection& currSection = m_aSections[n];
		if (sSectionName == StringUtils::MakeUpper(currSection.sSectionName)) {
			return wxPoint(currSection.nRangeFrom, currSection.nRangeTo);
		}
	}

	CTranslateError::Error("Неправильно задана секция с именем \"" + sSectionName + "\"");
	return wxPoint();
}

int CSectionCtrl::FindInSection(int nCurNumber, wxString& csStr)
{
	for (unsigned int n = 0; n < m_aSections.size(); n++)
	{
		int nRes = 0;

		CSection& currSection = m_aSections[n];

		if (nCurNumber >= currSection.nRangeFrom && nCurNumber <= currSection.nRangeTo) nRes += 1;
		if (nCurNumber == currSection.nRangeFrom) nRes += 2;
		if (nCurNumber == currSection.nRangeTo) nRes += 4;

		if (nRes) {
			csStr = currSection.sSectionName;
			return nRes;
		}
	}

	return 0;
}

CSection CSectionCtrl::FindSectionByPos(int pos)
{
	for (unsigned int n = 0; n < m_aSections.size(); n++)
	{
		int nRes = 0;

		CSection& currSection = m_aSections[n];

		if (pos >= currSection.nRangeFrom && pos <= currSection.nRangeTo) nRes += 1;
		if (pos == currSection.nRangeFrom) nRes += 2;
		if (pos == currSection.nRangeTo) nRes += 4;

		if (nRes & 4) return currSection;
	}

	return CSection();
}

int CSectionCtrl::GetNSectionFromPoint(wxPoint point)
{
	wxGridCellCoords idCurrentCell = m_grid->GetCellFromPoint(point);

	int nCurRow = NONE_MODE;
	if (LEFT_MODE == m_nMode)	nCurRow = idCurrentCell.GetRow();
	else if (UPPER_MODE == m_nMode) nCurRow = idCurrentCell.GetCol();

	for (unsigned int n = 0; n < m_aSections.size(); n++)
	{
		int nRes = 0;
		CSection& currSection = m_aSections[n];
		if (nCurRow >= currSection.nRangeFrom && nCurRow <= currSection.nRangeTo)
		{
			return n;
		}
	}

	return -1;
}

void CSectionCtrl::Add(int nRangeFrom, int nRangeTo)
{
	for (unsigned int n = 0; n < m_aSections.size(); n++)
	{
		CSection& currSection = m_aSections[n];

		if (nRangeFrom >= currSection.nRangeFrom && nRangeFrom <= currSection.nRangeTo)
		{
			if (nRangeTo > currSection.nRangeTo)//расширение нижней границы
			{
				currSection.nRangeTo = nRangeTo;
				m_aSections[n] = currSection;
				return;
			}
		}

		if (nRangeTo >= currSection.nRangeFrom && nRangeTo <= currSection.nRangeTo)
		{
			if (nRangeFrom < currSection.nRangeFrom)//расширение верхней границы
			{
				currSection.nRangeFrom = nRangeFrom;
				m_aSections[n] = currSection;
				return;
			}
		}

		if (nRangeFrom >= currSection.nRangeFrom && nRangeFrom <= currSection.nRangeTo)
			return;

		if (nRangeTo >= currSection.nRangeFrom && nRangeTo <= currSection.nRangeTo)
			return;
	}


	wxString num;
	num << m_aSections.size() + 1;

	//добавление новой секции
	CSection currSection;
	currSection.sSectionName = wxT("Section_") + num;
	currSection.nRangeFrom = nRangeFrom;
	currSection.nRangeTo = nRangeTo;
	m_aSections.push_back(currSection);

	if (!EditName(m_aSections.size() - 1)) {
		m_aSections.erase(m_aSections.begin() + m_aSections.size() - 1);
	}
}

void CSectionCtrl::Remove(int nRangeFrom, int nRangeTo)
{
	for (unsigned int n = 0; n < m_aSections.size(); n++)
	{
		CSection& currSection = m_aSections[n];

		if (nRangeFrom == currSection.nRangeFrom && nRangeTo == currSection.nRangeTo)
		{
			//удаление секции
			m_aSections.erase(m_aSections.begin() + n);
			return;
		}

		if (nRangeFrom >= currSection.nRangeFrom && nRangeFrom <= currSection.nRangeTo)
		{
			if (nRangeTo >= currSection.nRangeTo)//удаление нижней границы
			{
				currSection.nRangeTo = nRangeFrom - 1;
				m_aSections[n] = currSection;
				return;
			}
		}

		if (nRangeTo >= currSection.nRangeFrom && nRangeTo <= currSection.nRangeTo)
		{
			if (nRangeFrom <= currSection.nRangeFrom)//удаление верхней границы
			{
				currSection.nRangeFrom = nRangeTo + 1;
				m_aSections[n] = currSection;
				return;
			}
		}
	}
}

void CSectionCtrl::InsertRow(int nRow)
{
	if (nRow > 0)
	{
		for (unsigned int n = 0; n < m_aSections.size(); n++)
		{
			CSection& currSection = m_aSections[n];
			if (nRow < currSection.nRangeFrom)
				currSection.nRangeFrom++;
			if (nRow <= currSection.nRangeTo)
				currSection.nRangeTo++;
			m_aSections[n] = currSection;
		}
	}
}

void CSectionCtrl::RemoveRow(int nRow)
{
	if (nRow > 0)
	{
		for (unsigned int n = 0; n < m_aSections.size(); n++)
		{
			CSection& currSection = m_aSections[n];
			if (nRow < currSection.nRangeFrom)
				currSection.nRangeFrom--;
			if (nRow <= currSection.nRangeTo)
				currSection.nRangeTo--;
			if (currSection.nRangeTo < currSection.nRangeFrom)
			{
				m_aSections.erase(m_aSections.begin() + n);
				n--;
			}
			else
			{
				m_aSections[n] = currSection;
			}
		}
	}
}

bool CSectionCtrl::EditName(int row)
{
	if (row >= 0)
	{
		CSection& currSection = m_aSections[row];

		CInputSectionWnd* dlg = new CInputSectionWnd(m_grid, wxID_ANY);
		dlg->SetTitle("Identifier section");
		dlg->SetSection(currSection.sSectionName);

		if (dlg->ShowModal() == wxID_OK)
		{
			currSection.sSectionName = dlg->GetSection();
			m_aSections[row] = currSection;
			return true;
		}
	}

	return false;
}

unsigned int CSectionCtrl::GetSize()
{
	int nCount = 0;

	if (m_nMode == eSectionMode::LEFT_MODE)
	{
		nCount = m_aSections.size();
		return nCount > 0 ? 80 : 0;
	}
	else
	{
		nCount = m_aSections.size();
		return nCount > 0 ? 20 : 0;
	}

	return nCount;
}