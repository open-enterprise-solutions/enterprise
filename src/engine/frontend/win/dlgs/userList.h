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
#include <wx/sizer.h>
#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/itemattr.h>

#include "frontend/frontend.h"
#include "frontend/win/ctrls/dataview/dataview.h"

class FRONTEND_API ibDialogUserList : public wxDialog
{
	wxAuiToolBar* m_toolbarMain;

	wxAuiToolBarItem* m_toolAdd;
	wxAuiToolBarItem* m_toolCopy;
	wxAuiToolBarItem* m_toolEdit;
	wxAuiToolBarItem* m_toolDelete;

	ibDataViewCtrl* m_dataEditor;
	wxStaticText* m_conflictNote = nullptr;

	void ShowSharedNames();

public:

	ibDialogUserList(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("User list"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(614, 317), long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	virtual ~ibDialogUserList();

	void OnCommandMenu(wxCommandEvent &event);
	void OnContextMenu(ibDataViewEvent &event);

	void OnItemActivated(ibDataViewEvent &event);
};

#endif 