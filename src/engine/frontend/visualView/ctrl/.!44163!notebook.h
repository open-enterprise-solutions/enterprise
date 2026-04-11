#ifndef _NOTEBOOKS_H__
#define _NOTEBOOKS_H__

#include "window.h"
#include <wx/aui/auibook.h>

class ibValueNotebookPage;

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON FORM
const ibClassID g_controlNotebookCLSID = string_to_clsid("CT_NTBK");
const ibClassID g_controlNotebookPageCLSID = string_to_clsid("CT_NTPG");

//********************************************************************************************
//*                                 Value Notebook                                           *
//********************************************************************************************

class ibValueNotebook : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueNotebook);

public:

	ibValueNotebook();

	//get title
	virtual wxString GetControlTitle() const {
		return _("Notebook");
	}

	//control factory 
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
