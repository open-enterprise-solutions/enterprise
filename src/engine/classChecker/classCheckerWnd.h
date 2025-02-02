#ifndef __CLASS_CHEKER_WND_H__
#define __CLASS_CHEKER_WND_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/textctrl.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/frame.h>

class CFrameClassChecker : public wxFrame {
	wxStaticText* m_staticText_clsid;
	wxTextCtrl* m_textCtrlCode;
	wxStaticText* m_staticText_output;
	wxTextCtrl* m_textCtrlResult;
public:

	CFrameClassChecker(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Class identifier checker"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(500, 130), long style = wxDEFAULT_FRAME_STYLE | wxTAB_TRAVERSAL);
	virtual~CFrameClassChecker();

protected:

	// Virtual event handlers, override them in your derived class
	virtual void ClsidOnText(wxCommandEvent& event);
	virtual void OutputOnText(wxCommandEvent& event);

};
#endif