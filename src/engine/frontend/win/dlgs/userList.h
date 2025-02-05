#ifndef _USERLIST_WND_H__
#define _USERLIST_WND_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/gdicmn.h>
#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/dataview.h>
#include <wx/sizer.h>
#include <wx/dialog.h>

#include "frontend/frontend.h"

class FRONTEND_API CDialogUserList : public wxDialog
{
	wxAuiToolBar* m_auiToolBarUsers;

	wxAuiToolBarItem* m_toolAdd;
	wxAuiToolBarItem* m_toolCopy;
	wxAuiToolBarItem* m_toolEdit;
	wxAuiToolBarItem* m_toolDelete;

	wxDataViewCtrl* m_dataViewUsers;

public:

	CDialogUserList(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("User list"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(614, 317), long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	virtual ~CDialogUserList();

	void OnCommandMenu(wxCommandEvent &event);
	void OnContextMenu(wxDataViewEvent &event);

	void OnItemActivated(wxDataViewEvent &event);
};

#endif 