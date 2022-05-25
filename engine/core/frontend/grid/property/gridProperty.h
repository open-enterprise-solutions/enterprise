#ifndef _gridProperty_H__
#define _gridProperty_H__

#include <wx/grid.h>
#include "common/objectbase.h"

class CPropertyCell : public IObjectBase
{
	wxGrid *m_ownerGrid;

	wxGridCellCoords m_topLeftCoords;
	wxGridCellCoords m_bottomRightCoords;

private:

	OptionList *GetOptionsAlignment(Property *property)
	{
		OptionList *optList = new OptionList();
		optList->AddOption("Left", wxALIGN_LEFT);
		optList->AddOption("Right", wxALIGN_RIGHT);
		optList->AddOption("Top", wxALIGN_TOP);
		optList->AddOption("Bottom", wxALIGN_BOTTOM);
		optList->AddOption("Center", wxALIGN_CENTER);
		return optList;
	}

public:

	CPropertyCell(wxGrid *grid);

	void SetCellCoords(const wxGridCellCoords &coords);
	void SetCellCoords(const wxGridCellCoords &topLeftCoords, const wxGridCellCoords &bottomRightCoords);

	//system override 
	virtual int GetComponentType() const override { 
		return COMPONENT_TYPE_ABSTRACT; 
	}
	
	virtual wxString GetObjectTypeName() const override { return wxT("cells"); }
	virtual wxString GetClassName() const override { return wxT("cells";) }

	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

#endif 