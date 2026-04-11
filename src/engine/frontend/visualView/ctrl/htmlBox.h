#ifndef  _HTMLBOX_H__

#include "window.h"
#include <wx/html/htmlwin.h>

class ibValueHTMLBox : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueHTMLBox);
public:

	ibValueHTMLBox(); 

	//methods 
	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //method call

	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost *visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost *visualHost, bool firstСreated) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, ibVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost *visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory &reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());
};

#endif // ! _HTMLBOX_H__
