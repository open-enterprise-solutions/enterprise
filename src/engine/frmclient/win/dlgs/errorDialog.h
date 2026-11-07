#ifndef _ERROR_DLG_H__
#define _ERROR_DLG_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/stc/stc.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/checkbox.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include "deskgui/univ/ctrl/button.h"
#include <wx/sizer.h>
#include "deskgui/univ/dialog.h"

#include "frmclient/frmclient.h"
#include "frmclient/mainFrame/settings/editorsettings.h"
#include "frmclient/mainFrame/settings/fontcolorsettings.h"

// A SCRIPT'S FAILURE — the old desktop's dialog, asked by the server
// (ibProtocol::ibRequest::ibKind::Error). ShowModal answers the act chosen (ibProtocol::ibRequest::ibError::ibAct); closed — wxID_CANCEL.
class FRMCLIENT_API ibDialogError : public ibDialog {
public:

	ibDialogError(class ibFrontendMainFrame* parent, wxWindowID id = wxID_ANY,
		const wxString& title = _("Critical failure"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxCAPTION | wxSYSTEM_MENU | wxCLOSE_BOX | wxSTAY_ON_TOP);

	void SetErrorMessage(const wxString& strError) {
		m_errorOutput->SetReadOnly(false);
		m_errorOutput->SetText(strError);
		m_errorOutput->SetReadOnly(true);
	}

private:

	void OnButtonCloseProgramClick(wxCommandEvent& event);
	void OnButtonGotoDesignerClick(wxCommandEvent& event);
	void OnButtonCloseWindowClick(wxCommandEvent& event);

	wxStyledTextCtrl* m_errorOutput;

	ibButton* m_buttonCloseProgram;
	ibButton* m_buttonGotoDesigner;
	ibButton* m_buttonCloseWindow;
};

#endif
