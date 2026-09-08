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

#ifdef OES_USE_WEB
private:
	// What SetPage was last given. The render shim is re-read on every
	// refresh, so the text has to live on the side that outlives one.
	wxString m_page;
#endif
};

#endif // ! _HTMLBOX_H__
