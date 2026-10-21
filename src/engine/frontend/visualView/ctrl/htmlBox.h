#ifndef  _HTMLBOX_H__

#include "window.h"
#include <wx/html/htmlwin.h>

class ibValueHTMLBox : public ibValueWindow {
	public:

	ibValueHTMLBox(); 

	//methods
	void FillControlMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //method call

	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost *visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost *visualHost, bool firstCreated) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, ibVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost *visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:

	// Set from code only, never saved: the page lives as long as the form is open - the thin server's
	// copy keeps it the same way. Held here because a form's OnOpen calls SetPage BEFORE the window is
	// created, and a page handed only to the window was dropped on the floor.
	wxString m_page;
};

#endif // ! _HTMLBOX_H__
