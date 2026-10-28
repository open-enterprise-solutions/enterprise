#ifndef _FRMCLIENT_PAGE_WINDOW_H__
#define _FRMCLIENT_PAGE_WINDOW_H__

#include <wx/panel.h>
#include <wx/sizer.h>

// A NOTEBOOK'S PAGE — the old desktop's window:a panel laying out what is placed on it.
class ibPanelPage : public wxPanel
{
	wxDECLARE_DYNAMIC_CLASS(ibPanelPage);

public:

	ibPanelPage();
	ibPanelPage(wxWindow *parent,
		wxWindowID winid = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxTAB_TRAVERSAL | wxNO_BORDER,
		const wxString& name = wxPanelNameStr);

	~ibPanelPage();

	void SetOrientation(int orient) { m_boxSizer->SetOrientation(orient); }
	wxBoxSizer *GetSizer() { return m_boxSizer; }

protected:

	wxBoxSizer *m_boxSizer;
};

#endif // !_FRMCLIENT_PAGE_WINDOW_H__
