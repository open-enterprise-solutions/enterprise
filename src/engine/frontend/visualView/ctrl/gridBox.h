#ifndef _GRID_H__
#define _GRID_H__

#include "window.h"
#include "frontend/win/editor/gridEditor/gridEditor.h"
#include "backend/system/value/valueSpreadsheet.h"

class ibValueGridBox : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueGridBox);
public:

	ibValueGridBox();

	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost *visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost *visualHost, bool firstСreated) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, ibVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost *visualHost) override;

	//support printing 
	virtual wxPrintout* CreatePrintout() const;

	//methods & attributes
	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory &reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

private:

	ibValuePtr<ibValueSpreadsheetDocument> m_valueSpreadsheet;
};

#endif