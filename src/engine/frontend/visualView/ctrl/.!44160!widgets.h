#ifndef _COMMON_H_
#define _COMMON_H_

#include "window.h"
#include "typeControl.h"

/////////////////////////////////////////////////////////////////////////////////////
//                                 COMMON ELEMENTS                                 //
/////////////////////////////////////////////////////////////////////////////////////
#include <wx/button.h>

class ibValueButton : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueButton);
public:

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueButton();

	//get title
	virtual wxString GetControlTitle() const { return GetCaption(); }

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
