#ifndef _CHARTBOX_H__
#define _CHARTBOX_H__

#include "window.h"

class CValueChartBox : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueChartBox);
public:

	CValueChartBox();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { 
		return wxT("chartbox");
	}
	
	virtual wxString GetObjectTypeName() const override { 
		return wxT("container"); 
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#endif