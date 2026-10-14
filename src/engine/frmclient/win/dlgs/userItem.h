#ifndef _FRMCLIENT_USER_WND_H__
#define _FRMCLIENT_USER_WND_H__

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
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/dialog.h>
#include <wx/button.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/choice.h>

#include "frmclient/backend/backend_core.h"
#include "frmclient/backend/roleHelper.h"   // ibRoleCompositionMode — the membership carries how the role combines

#include "frmclient/win/ctrls/checktree.h"
#include "frmclient/win/theme/luna_tabart.h"

// A USER — the desktop's dialog (frontend/win/dlgs/userItem) as it is, on ibUserInfo: its roles and languages offered
// as the server lists them, where the desktop's took the active configuration. What its OK checked before it saved — that
// some user keeps a role with the administration rights — and the password's hash are the server's: a refusal comes
// back with the desktop's text.
class ibDialogUserItem : public wxDialog {

	struct ibDataUserRole {
		wxString m_strRoleGuid;
		wxString m_strRoleName;
		ibRoleID m_miRoleId;
		// Read off the role metaobject beside its name, and written into the membership: the stored
		// row then says both WHICH role and HOW it combines (ibRoleCompositionMode).
		ibRoleCompositionMode m_mode = ibRoleCompositionMode_Union;
	};

	struct ibDataUserLanguageItem {
		wxString m_strLanguageGuid;
		wxString m_strLanguageName;
		wxString m_strLanguageCode;
	};

public:

	bool ReadUserData(const ibGuid& userGuid, bool copy = false);

	ibDialogUserItem(wxWindow* parent, wxWindowID id = wxID_ANY,
		const wxString& title = _("User"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(400, 264), long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

private:

	bool m_bInitialized;
	// Set when the password box changes after the record has been shown.
	// Untouched, OK sends no password and the server keeps the one stored.
	bool m_bPasswordTouched;

	std::map<wxTreeItemId, ibDataUserRole> m_roleArray;
	std::map<int, ibDataUserLanguageItem> m_languageArray;

	wxAuiNotebook* m_mainNotebook;
	wxPanel* m_main;
	wxStaticText* m_staticName;
	wxStaticText* m_staticFullName;
	wxTextCtrl* m_textName;
	wxTextCtrl* m_textFullName;
	wxStaticLine* m_staticline;
	wxStaticText* m_staticPassword;
	wxTextCtrl* m_textPassword;
	wxPanel* m_other;
	wxStdDialogButtonSizer* m_bottom;
	wxButton* m_bottomOK;
	wxButton* m_bottomCancel;

	wxStaticText* m_staticRole;
	wxStaticText* m_staticLanguage;
	ibCheckTree* m_choiceRole;
	wxChoice* m_choiceLanguage;

	ibGuid m_userGuid;
};

#endif
