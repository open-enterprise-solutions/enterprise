////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2C-team
//	Description : grid sections
////////////////////////////////////////////////////////////////////////////

#include "gridSections.h"
#include "gridCommon.h"
#include "gridUtils.h"
#include "compiler/functions.h"
#include "utils/stringUtils.h"

CSectionCtrl::CSectionCtrl(CGrid *grid, eSectionMode setMode) : m_grid(grid), nMode(setMode) {}

CSectionCtrl::~CSectionCtrl() {}

wxPoint CSectionCtrl::GetRange(const wxString &sectionName)
{
	wxString sSectionName = StringUtils::MakeUpper(sectionName);

	for (unsigned int n = 0; n < aSections.size(); n++)
	{
		CSection Section = aSections[n];
		if (sSectionName == StringUtils::MakeUpper(Section.sSectionName))
		{
			return wxPoint(Section.nRangeFrom, Section.nRangeTo);
		}
	}

	CTranslateError::Error("Неправильно задана секция с именем \"" + sSectionName + "\"");
	return wxPoint();
}

int CSectionCtrl::FindInSection(int nCurNumber, wxString &csStr)
{
	for (unsigned int n = 0; n < aSections.size(); n++)
	{
		int nRes = 0;

		CSection Section = aSections[n];

		if (nCurNumber >= Section.nRangeFrom&&nCurNumber <= Section.nRangeTo) nRes += 1;
		if (nCurNumber == Section.nRangeFrom) nRes += 2;
		if (nCurNumber == Section.nRangeTo) nRes += 4;

		if (nRes)
		{
			csStr = Section.sSectionName;
			return nRes;
		}
	}

	return 0;
}

CSection CSectionCtrl::FindSectionByPos(int pos)
{
	for (unsigned int n = 0; n < aSections.size(); n++)
	{
		int nRes = 0;

		CSection Section = aSections[n];

		if (pos >= Section.nRangeFrom&&pos <= Section.nRangeTo) nRes += 1;
		if (pos == Section.nRangeFrom) nRes += 2;
		if (pos == Section.nRangeTo) nRes += 4;

		if (nRes & 4) return Section;
	}

	return CSection();
}

int CSectionCtrl::GetNSectionFromPoint(wxPoint point)
{
	wxGridCellCoords idCurrentCell = m_grid->GetCellFromPoint(point);

	int nCurRow = NONE_MODE;
	if (LEFT_MODE == nMode)	nCurRow = idCurrentCell.GetRow();
	else if (UPPER_MODE == nMode) nCurRow = idCurrentCell.GetCol();

	for (unsigned int n = 0; n < aSections.size(); n++)
	{
		int nRes = 0;
		CSection &Section = aSections[n];
		if (nCurRow >= Section.nRangeFrom&&nCurRow <= Section.nRangeTo)
		{
			return n;
		}
	}

	return -1;
}

void CSectionCtrl::Add(int nRangeFrom, int nRangeTo)
{
	for (unsigned int n = 0; n < aSections.size(); n++)
	{
		CSection Section = aSections[n];

		if (nRangeFrom >= Section.nRangeFrom && nRangeFrom <= Section.nRangeTo)
		{
			if (nRangeTo > Section.nRangeTo)//расширение нижней границы
			{
				Section.nRangeTo = nRangeTo;
				aSections[n] = Section;
				return;
			}
		}

		if (nRangeTo >= Section.nRangeFrom && nRangeTo <= Section.nRangeTo)
		{
			if (nRangeFrom < Section.nRangeFrom)//расширение верхней границы
			{
				Section.nRangeFrom = nRangeFrom;
				aSections[n] = Section;
				return;
			}
		}

		if (nRangeFrom >= Section.nRangeFrom && nRangeFrom <= Section.nRangeTo)
			return;

		if (nRangeTo >= Section.nRangeFrom && nRangeTo <= Section.nRangeTo)
			return;
	}


	wxString num;
	num << aSections.size() + 1;

	//добавление новой секции
	CSection Section;
	Section.sSectionName = wxT("Section_") + num;
	Section.nRangeFrom = nRangeFrom;
	Section.nRangeTo = nRangeTo;
	aSections.push_back(Section);

	if (!EditName(aSections.size() - 1)) aSections.erase(aSections.begin() + aSections.size() - 1);
}

void CSectionCtrl::Remove(int nRangeFrom, int nRangeTo)
{
	for (unsigned int n = 0; n < aSections.size(); n++)
	{
		CSection Section = aSections[n];

		if (nRangeFrom == Section.nRangeFrom && nRangeTo == Section.nRangeTo)
		{
			//удаление секции
			aSections.erase(aSections.begin() + n);
			return;
		}

		if (nRangeFrom >= Section.nRangeFrom && nRangeFrom <= Section.nRangeTo)
		{
			if (nRangeTo >= Section.nRangeTo)//удаление нижней границы
			{
				Section.nRangeTo = nRangeFrom - 1;
				aSections[n] = Section;
				return;
			}
		}

		if (nRangeTo >= Section.nRangeFrom&&nRangeTo <= Section.nRangeTo)
		{
			if (nRangeFrom <= Section.nRangeFrom)//удаление верхней границы
			{
				Section.nRangeFrom = nRangeTo + 1;
				aSections[n] = Section;
				return;
			}
		}
	}
}

void CSectionCtrl::InsertRow(int nRow)
{
	if (nRow > 0)
	{
		for (unsigned int n = 0; n < aSections.size(); n++)
		{
			CSection Section = aSections[n];
			if (nRow < Section.nRangeFrom)
				Section.nRangeFrom++;
			if (nRow <= Section.nRangeTo)
				Section.nRangeTo++;
			aSections[n] = Section;
		}
	}
}

void CSectionCtrl::RemoveRow(int nRow)
{
	if (nRow > 0)
	{
		for (unsigned int n = 0; n < aSections.size(); n++)
		{
			CSection Section = aSections[n];
			if (nRow < Section.nRangeFrom)
				Section.nRangeFrom--;
			if (nRow <= Section.nRangeTo)
				Section.nRangeTo--;
			if (Section.nRangeTo < Section.nRangeFrom)
			{
				aSections.erase(aSections.begin() + n);
				n--;
			}
			else
			{
				aSections[n] = Section;
			}
		}
	}
}

bool CSectionCtrl::EditName(int row)
{
	if (row >= 0)
	{
		CSection Section = aSections[row];

		CInputSectionDialog *dlg = new CInputSectionDialog(m_grid, wxID_ANY);
		dlg->SetTitle("Identifier section");
		dlg->SetSection(Section.sSectionName);

		if (dlg->ShowModal() == wxID_OK)
		{
			Section.sSectionName = dlg->GetSection();
			aSections[row] = Section;
			return true;
		}
	}

	return false;
}

unsigned int CSectionCtrl::GetSize()
{
	int nCount = 0;

	if (nMode == eSectionMode::LEFT_MODE)
	{
		nCount = aSections.size();
		return nCount > 0 ? 80 : 0;
	}
	else
	{
		nCount = aSections.size();
		return nCount > 0 ? 20 : 0;
	}

	return nCount;
}