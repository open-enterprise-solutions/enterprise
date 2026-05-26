#ifndef _AUDIT_LOG_WND_H__
#define _AUDIT_LOG_WND_H__

// Журнал регистрации — admin viewer over the .olg files written by
// ibLogger. Mirrors the ibDialogUserList shape: Luna AuiToolBar +
// ibDataViewCtrl + custom IndexListModel with PrepareData refresh.
// Read-only — the sink owns writes. Filter strip on top is
// journal-specific (no equivalent in userList because that one shows
// all rows unconditionally).

#include <wx/dialog.h>
#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/datectrl.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/timer.h>
#include <wx/sizer.h>

#include <memory>

#include "frontend/frontend.h"
#include "frontend/win/ctrls/dataview/dataview.h"
#include "backend/logger/loggerReader.h"

class ibAuditLogModel;

class FRONTEND_API ibDialogAuditLog : public wxDialog {
public:
    ibDialogAuditLog(wxWindow* parent,
                     wxWindowID id = wxID_ANY,
                     const wxString& title = _("Registration journal"),
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxSize(960, 560),
                     long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    virtual ~ibDialogAuditLog();

private:
    void BuildLayout();
    void Reload();
    ibLogFilter ReadFilter() const;

    // Toolbar handlers — single command dispatcher per Luna pattern.
    void OnCommandMenu(wxCommandEvent& event);
    void OnItemActivated(ibDataViewEvent& event);
    void OnTailToggle(wxCommandEvent& event);
    void OnTimer(wxTimerEvent& event);

    // Filter strip
    wxDatePickerCtrl* m_fromDate     = nullptr;
    wxDatePickerCtrl* m_toDate       = nullptr;
    wxChoice*         m_levelChoice  = nullptr;
    wxChoice*         m_sourceChoice = nullptr;
    wxTextCtrl*       m_userText     = nullptr;
    wxTextCtrl*       m_searchText   = nullptr;
    wxCheckBox*       m_tailCheck    = nullptr;

    // AuiToolBar + DataView shell (mirrors ibDialogUserList).
    wxAuiToolBar*     m_toolbarMain  = nullptr;
    ibDataViewCtrl*   m_dataEditor   = nullptr;
    std::shared_ptr<wxTimer> m_tailTimer;

    // Backend reader bound to appData's logger directory.
    std::unique_ptr<ibLoggerReader> m_reader;
};

#endif
