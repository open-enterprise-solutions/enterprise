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

class CGrid;

struct section_t {
	wxString m_sectionName;
	unsigned int m_rangeFrom;
	unsigned int m_rangeTo;
};

class CSectionCtrl {
	CGrid* m_grid;
private:
	//section array 
	std::vector<section_t> m_aSections;
	//section mode 
	eSectionMode m_sectionMode;
public:

	CSectionCtrl(CGrid* grid, eSectionMode setMode);

	wxPoint GetRange(const wxString& sectionName) const;
	int FindInSection(unsigned int currRow, wxString& sectionName) const;
	section_t *FindSectionByPos(unsigned int pos);
	int GetNSectionFromPoint(const wxPoint& point) const;

	section_t& GetSection(unsigned int index) {
		return m_aSections[index];
	}

	eSectionMode GetSectionMode() const {
		return m_sectionMode; 
	}

	bool Add(unsigned int rangeFrom, unsigned int rangeTo);
	bool Remove(unsigned int rangeFrom, unsigned int rangeTo);

	void InsertRow(unsigned int row);
	void RemoveRow(unsigned int row);

	bool EditName(int pos);
	bool EditName(section_t& section);

	unsigned int CellSize() const {
		if (m_sectionMode == eSectionMode::LEFT_MODE) {
			unsigned int countSection = m_aSections.size();
			return countSection > 0 ? 80 : 0;
		}
		else {
			unsigned int countSection = m_aSections.size();
			return countSection > 0 ? 20 : 0;
		}
		return 0;
	}

	unsigned int Count() const {
		return m_aSections.size();
	}

	void Clear() {
		m_aSections.clear();
	}
};

#endif 