#ifndef _TEXTBOX_H__
#define _TEXTBOX_H__

#include "window.h"
#include "frontend/win/editor/textEditor/textEditor.h"

class ibValueTextBox : public ibValueWindow {
	public:

	ibValueTextBox();

	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost *visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost *visualHost, bool firstCreated) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, ibVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost *visualHost) override;

	//support printing 
	virtual wxPrintout* CreatePrintout() const;

	//methods & attributes
	// No own name surface — the base ibValueFrame::FillMembers covers it.

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;
};

#endif