// FORKED from wxWidgets: wxAnyChoiceDialog, wxSingleChoiceDialog, wxMultiChoiceDialog and the functions over them
// (include/wx/generic/choicdgg.h, src/generic/choicdgg.cpp), of the library's dialog, static text, list box and check
// list box (wx -> ib prefixes applied).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/choicdgg.h
// Purpose:     Generic choice dialogs
// Author:      Julian Smart
// Modified by: 03.11.00: VZ to add wxArrayString and multiple sel functions
// Created:     01/02/97
// Copyright:   (c) wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_CHOICEDIALOG_H_
#define _DESKGUI_UNIV_CHOICEDIALOG_H_

#include "deskgui/univ/dialog.h"

#include <wx/choicdlg.h>   // wxCHOICEDLG_STYLE - wx's default
#include <wx/dynarray.h>
#include <wx/listbox.h>    // wxLB_* styles

class ibListBox;

// ----------------------------------------------------------------------------
// ibAnyChoiceDialog: a base class for dialogs containing a listbox
// ----------------------------------------------------------------------------

class DESKGUI_API ibAnyChoiceDialog : public ibDialog
{
public:
    ibAnyChoiceDialog() : m_listbox(nullptr) { }

    ibAnyChoiceDialog(wxWindow *parent,
                      const wxString& message,
                      const wxString& caption,
                      int n, const wxString *choices,
                      long styleDlg = wxCHOICEDLG_STYLE,
                      const wxPoint& pos = wxDefaultPosition,
                      long styleLbox = wxLB_ALWAYS_SB)
    {
        (void)Create(parent, message, caption, n, choices,
                     styleDlg, pos, styleLbox);
    }
    ibAnyChoiceDialog(wxWindow *parent,
                      const wxString& message,
                      const wxString& caption,
                      const wxArrayString& choices,
                      long styleDlg = wxCHOICEDLG_STYLE,
                      const wxPoint& pos = wxDefaultPosition,
                      long styleLbox = wxLB_ALWAYS_SB)
    {
        (void)Create(parent, message, caption, choices,
                     styleDlg, pos, styleLbox);
    }

    bool Create(wxWindow *parent,
                const wxString& message,
                const wxString& caption,
                int n, const wxString *choices,
                long styleDlg = wxCHOICEDLG_STYLE,
                const wxPoint& pos = wxDefaultPosition,
                long styleLbox = wxLB_ALWAYS_SB);
    bool Create(wxWindow *parent,
                const wxString& message,
                const wxString& caption,
                const wxArrayString& choices,
                long styleDlg = wxCHOICEDLG_STYLE,
                const wxPoint& pos = wxDefaultPosition,
                long styleLbox = wxLB_ALWAYS_SB);

protected:
    ibListBox *m_listbox;

    virtual ibListBox *CreateList(int n,
                                  const wxString *choices,
                                  long styleLbox);

    wxDECLARE_NO_COPY_CLASS(ibAnyChoiceDialog);
};

// ----------------------------------------------------------------------------
// ibSingleChoiceDialog: a dialog with single selection listbox
// ----------------------------------------------------------------------------

class DESKGUI_API ibSingleChoiceDialog : public ibAnyChoiceDialog
{
public:
    ibSingleChoiceDialog()
    {
        m_selection = -1;
    }

    ibSingleChoiceDialog(wxWindow *parent,
                         const wxString& message,
                         const wxString& caption,
                         int n,
                         const wxString *choices,
                         void **clientData = nullptr,
                         long style = wxCHOICEDLG_STYLE,
                         const wxPoint& pos = wxDefaultPosition)
    {
        Create(parent, message, caption, n, choices, clientData, style, pos);
    }

    ibSingleChoiceDialog(wxWindow *parent,
                         const wxString& message,
                         const wxString& caption,
                         const wxArrayString& choices,
                         void **clientData = nullptr,
                         long style = wxCHOICEDLG_STYLE,
                         const wxPoint& pos = wxDefaultPosition)
    {
        Create(parent, message, caption, choices, clientData, style, pos);
    }

    bool Create(wxWindow *parent,
                const wxString& message,
                const wxString& caption,
                int n,
                const wxString *choices,
                void **clientData = nullptr,
                long style = wxCHOICEDLG_STYLE,
                const wxPoint& pos = wxDefaultPosition);
    bool Create(wxWindow *parent,
                const wxString& message,
                const wxString& caption,
                const wxArrayString& choices,
                void **clientData = nullptr,
                long style = wxCHOICEDLG_STYLE,
                const wxPoint& pos = wxDefaultPosition);

    void SetSelection(int sel);
    int GetSelection() const { return m_selection; }
    wxString GetStringSelection() const { return m_stringSelection; }
    void* GetSelectionData() const { return m_clientData; }

    // implementation from now on
    void OnOK(wxCommandEvent& event);
    void OnListBoxDClick(wxCommandEvent& event);

protected:
    int         m_selection;
    wxString    m_stringSelection;

    void DoChoice();

private:
    wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibSingleChoiceDialog);
    wxDECLARE_EVENT_TABLE();
};

// ----------------------------------------------------------------------------
// ibMultiChoiceDialog: a dialog with multi selection listbox
// ----------------------------------------------------------------------------

class DESKGUI_API ibMultiChoiceDialog : public ibAnyChoiceDialog
{
public:
    ibMultiChoiceDialog() = default;

    ibMultiChoiceDialog(wxWindow *parent,
                        const wxString& message,
                        const wxString& caption,
                        int n,
                        const wxString *choices,
                        long style = wxCHOICEDLG_STYLE,
                        const wxPoint& pos = wxDefaultPosition)
    {
        (void)Create(parent, message, caption, n, choices, style, pos);
    }
    ibMultiChoiceDialog(wxWindow *parent,
                        const wxString& message,
                        const wxString& caption,
                        const wxArrayString& choices,
                        long style = wxCHOICEDLG_STYLE,
                        const wxPoint& pos = wxDefaultPosition)
    {
        (void)Create(parent, message, caption, choices, style, pos);
    }

    bool Create(wxWindow *parent,
                const wxString& message,
                const wxString& caption,
                int n,
                const wxString *choices,
                long style = wxCHOICEDLG_STYLE,
                const wxPoint& pos = wxDefaultPosition);
    bool Create(wxWindow *parent,
                const wxString& message,
                const wxString& caption,
                const wxArrayString& choices,
                long style = wxCHOICEDLG_STYLE,
                const wxPoint& pos = wxDefaultPosition);

    void SetSelections(const wxArrayInt& selections);
    wxArrayInt GetSelections() const { return m_selections; }

    // implementation from now on
    virtual bool TransferDataFromWindow() override;

protected:
#if wxUSE_CHECKLISTBOX
    virtual ibListBox *CreateList(int n,
                                  const wxString *choices,
                                  long styleLbox) override;
#endif // wxUSE_CHECKLISTBOX

    wxArrayInt m_selections;

private:
    wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibMultiChoiceDialog);
};

// ----------------------------------------------------------------------------
// wrapper functions which can be used to get selection(s) from the user
// ----------------------------------------------------------------------------

// get the user selection as a string
DESKGUI_API wxString ibGetSingleChoice(const wxString& message,
                                   const wxString& caption,
                                   const wxArrayString& choices,
                                   wxWindow *parent = nullptr,
                                   int x = wxDefaultCoord,
                                   int y = wxDefaultCoord,
                                   bool centre = true,
                                   int width = wxDefaultCoord,
                                   int height = wxDefaultCoord,
                                   int initialSelection = 0);

DESKGUI_API wxString ibGetSingleChoice(const wxString& message,
                                   const wxString& caption,
                                   int n, const wxString *choices,
                                   wxWindow *parent = nullptr,
                                   int x = wxDefaultCoord,
                                   int y = wxDefaultCoord,
                                   bool centre = true,
                                   int width = wxDefaultCoord,
                                   int height = wxDefaultCoord,
                                   int initialSelection = 0);

DESKGUI_API wxString ibGetSingleChoice(const wxString& message,
                                   const wxString& caption,
                                   const wxArrayString& choices,
                                   int initialSelection,
                                   wxWindow *parent = nullptr);

DESKGUI_API wxString ibGetSingleChoice(const wxString& message,
                                   const wxString& caption,
                                   int n, const wxString *choices,
                                   int initialSelection,
                                   wxWindow *parent = nullptr);

// Same as above but gets position in list of strings, instead of string,
// or -1 if no selection
DESKGUI_API int ibGetSingleChoiceIndex(const wxString& message,
                                   const wxString& caption,
                                   const wxArrayString& choices,
                                   wxWindow *parent = nullptr,
                                   int x = wxDefaultCoord,
                                   int y = wxDefaultCoord,
                                   bool centre = true,
                                   int width = wxDefaultCoord,
                                   int height = wxDefaultCoord,
                                   int initialSelection = 0);

DESKGUI_API int ibGetSingleChoiceIndex(const wxString& message,
                                   const wxString& caption,
                                   int n, const wxString *choices,
                                   wxWindow *parent = nullptr,
                                   int x = wxDefaultCoord,
                                   int y = wxDefaultCoord,
                                   bool centre = true,
                                   int width = wxDefaultCoord,
                                   int height = wxDefaultCoord,
                                   int initialSelection = 0);

DESKGUI_API int ibGetSingleChoiceIndex(const wxString& message,
                                   const wxString& caption,
                                   const wxArrayString& choices,
                                   int initialSelection,
                                   wxWindow *parent = nullptr);

DESKGUI_API int ibGetSingleChoiceIndex(const wxString& message,
                                   const wxString& caption,
                                   int n, const wxString *choices,
                                   int initialSelection,
                                   wxWindow *parent = nullptr);

// Return client data instead or nullptr if canceled
DESKGUI_API void* ibGetSingleChoiceData(const wxString& message,
                                    const wxString& caption,
                                    const wxArrayString& choices,
                                    void **client_data,
                                    wxWindow *parent = nullptr,
                                    int x = wxDefaultCoord,
                                    int y = wxDefaultCoord,
                                    bool centre = true,
                                    int width = wxDefaultCoord,
                                    int height = wxDefaultCoord,
                                    int initialSelection = 0);

DESKGUI_API void* ibGetSingleChoiceData(const wxString& message,
                                    const wxString& caption,
                                    int n, const wxString *choices,
                                    void **client_data,
                                    wxWindow *parent = nullptr,
                                    int x = wxDefaultCoord,
                                    int y = wxDefaultCoord,
                                    bool centre = true,
                                    int width = wxDefaultCoord,
                                    int height = wxDefaultCoord,
                                    int initialSelection = 0);

DESKGUI_API void* ibGetSingleChoiceData(const wxString& message,
                                    const wxString& caption,
                                    const wxArrayString& choices,
                                    void **client_data,
                                    int initialSelection,
                                    wxWindow *parent = nullptr);


DESKGUI_API void* ibGetSingleChoiceData(const wxString& message,
                                    const wxString& caption,
                                    int n, const wxString *choices,
                                    void **client_data,
                                    int initialSelection,
                                    wxWindow *parent = nullptr);

// fill the array with the indices of the chosen items, it will be empty
// if no items were selected or Cancel was pressed - return the number of
// selections or -1 if cancelled
DESKGUI_API int ibGetSelectedChoices(wxArrayInt& selections,
                                 const wxString& message,
                                 const wxString& caption,
                                 int n, const wxString *choices,
                                 wxWindow *parent = nullptr,
                                 int x = wxDefaultCoord,
                                 int y = wxDefaultCoord,
                                 bool centre = true,
                                 int width = wxDefaultCoord,
                                 int height = wxDefaultCoord);

DESKGUI_API int ibGetSelectedChoices(wxArrayInt& selections,
                                 const wxString& message,
                                 const wxString& caption,
                                 const wxArrayString& choices,
                                 wxWindow *parent = nullptr,
                                 int x = wxDefaultCoord,
                                 int y = wxDefaultCoord,
                                 bool centre = true,
                                 int width = wxDefaultCoord,
                                 int height = wxDefaultCoord);

#endif // _DESKGUI_UNIV_CHOICEDIALOG_H_
