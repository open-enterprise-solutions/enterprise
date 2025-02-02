#ifndef _GRID_H__
#define _GRID_H__

#include "window.h"
#include "mainFrame/grid/gridCommon.h"

class CValueGridBox : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueGridBox);
public:

	CValueGridBox();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost *visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, IVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost *visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());
};

#endif