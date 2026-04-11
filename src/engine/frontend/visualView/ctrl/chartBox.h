#ifndef _CHARTBOX_H__
#define _CHARTBOX_H__

#include "window.h"

class ibValueChartBox : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueChartBox);
public:

	ibValueChartBox();

	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());
};

#endif