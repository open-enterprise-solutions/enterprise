#ifndef _GRID_SECTION_H__
#define _GRID_SECTION_H__

#include <wx/window.h>
#include <wx/file.h>

enum eSectionMode
{
	NONE_MODE,
	LEFT_MODE,
	UPPER_MODE,
};

struct CSection
{
	wxString sSectionName;
	int nRangeFrom;
	int nRangeTo;
};

class CSectionArray : public std::vector<CSection>
{
public:

	void Load(wxFile &f);
	void Save(wxFile &f);
};

class CGrid;

class CSectionCtrl
{
	CGrid *m_grid; 
	
private:

	//section array 
	std::vector<CSection> m_aSections; 
	//section mode 
	eSectionMode m_nMode;

public:

	CSectionCtrl(CGrid *grid, eSectionMode nSetMode);
	~CSectionCtrl();

	wxPoint GetRange(const wxString &sSectionName);
	int FindInSection(int nCurRow, wxString &csStr);
	CSection FindSectionByPos(int pos);
	int GetNSectionFromPoint(wxPoint point);

	CSection GetSection(int index) { return m_aSections[index]; }

	void Add(int nRangeFrom, int nRangeTo);
	void Remove(int nRangeFrom, int nRangeTo);

	void InsertRow(int row);
	void RemoveRow(int row);

	bool EditName(int row);
	unsigned int GetSize();

	void ClearSections() { m_aSections.clear(); }
};

#endif 