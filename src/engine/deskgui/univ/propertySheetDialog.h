// FORKED from wxWidgets: wxPropertySheetDialog (include/wx/generic/propdlg.h, src/generic/propdlg.cpp), of the
// library's dialog and notebook (wx -> ib prefixes applied).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/propdlg.h
// Purpose:     wxPropertySheetDialog
// Author:      Julian Smart
// Created:     2005-03-12
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_PROPERTYSHEETDIALOG_H_
#define _DESKGUI_UNIV_PROPERTYSHEETDIALOG_H_

#include "deskgui/univ/dialog.h"

class ibNotebook;

// SEAM vs wx: the book is the library's notebook, its one kind of book - wx chose the notebook, choicebook, toolbook,
// listbook or treebook by the sheet style (wxPROPSHEET_*) and fitted the dialog to the page on idle
// (wxPROPSHEET_SHRINKTOFIT); there is no sheet style here
class DESKGUI_API ibPropertySheetDialog : public ibDialog
{
public:
    ibPropertySheetDialog() { Init(); }

    ibPropertySheetDialog(wxWindow* parent, wxWindowID id,
                          const wxString& title,
                          const wxPoint& pos = wxDefaultPosition,
                          const wxSize& sz = wxDefaultSize,
                          long style = wxDEFAULT_DIALOG_STYLE,
                          const wxString& name = wxASCII_STR(wxDialogNameStr))
    {
        Init();
        Create(parent, id, title, pos, sz, style, name);
    }

    // Override function in base
    virtual wxWindow* GetContentWindow() const override;

    // Creates the buttons
    virtual void CreateButtons(int flags = wxOK|wxCANCEL);

    // Lay out the dialog, to be called after pages have been created
    virtual void LayoutDialog(int centreFlags = wxBOTH);

    // Creates the book control. If you want to use a different kind of
    // control, override.
    virtual ibNotebook* CreateBookCtrl();

    // Adds the book control to the inner sizer.
    virtual void AddBookCtrl(wxSizer* sizer);

    bool Create(wxWindow* parent, wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& sz = wxDefaultSize,
                long style = wxDEFAULT_DIALOG_STYLE,
                const wxString& name = wxASCII_STR(wxDialogNameStr));

    // Set and get the notebook
    void SetBookCtrl(ibNotebook* book) { m_bookCtrl = book; }
    ibNotebook* GetBookCtrl() const { return m_bookCtrl; }

    // Set and get the inner sizer
    void SetInnerSizer(wxSizer* sizer) { m_innerSizer = sizer; }
    wxSizer* GetInnerSizer() const { return m_innerSizer ; }

    // Set and get the border around the whole dialog
    void SetSheetOuterBorder(int border) { m_sheetOuterBorder = border; }
    int GetSheetOuterBorder() const { return m_sheetOuterBorder ; }

    // Set and get the border around the book control only
    void SetSheetInnerBorder(int border) { m_sheetInnerBorder = border; }
    int GetSheetInnerBorder() const { return m_sheetInnerBorder ; }

protected:
    void Init();

    ibNotebook*     m_bookCtrl;
    wxSizer*        m_innerSizer; // sizer for extra space
    int             m_sheetOuterBorder;
    int             m_sheetInnerBorder;

    wxDECLARE_DYNAMIC_CLASS(ibPropertySheetDialog);
};

#endif // _DESKGUI_UNIV_PROPERTYSHEETDIALOG_H_
