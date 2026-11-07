// FORKED from wxWidgets: wxPropertySheetDialog (src/generic/propdlg.cpp) - see propertySheetDialog.h.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/propdlg.cpp
// Purpose:     wxPropertySheetDialog
// Author:      Julian Smart
// Created:     2005-03-12
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "gui/univ/propertySheetDialog.h"

#include "gui/univ/ctrl/notebook.h"

//-----------------------------------------------------------------------------
// ibPropertySheetDialog
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibPropertySheetDialog, ibDialog);

bool ibPropertySheetDialog::Create(wxWindow* parent, wxWindowID id, const wxString& title,
                                   const wxPoint& pos, const wxSize& sz, long style,
                                   const wxString& name)
{
    parent = GetParentForModalDialog(parent, style);

    if (!ibDialog::Create(parent, id, title, pos, sz, style|wxCLIP_CHILDREN, name))
        return false;

    wxBoxSizer *topSizer = new wxBoxSizer( wxVERTICAL );
    SetSizer(topSizer);

    // This gives more space around the edges
    m_innerSizer = new wxBoxSizer( wxVERTICAL );

    topSizer->Add(m_innerSizer, 1, wxGROW|wxALL, m_sheetOuterBorder);

    m_bookCtrl = CreateBookCtrl();
    AddBookCtrl(m_innerSizer);

    return true;
}

void ibPropertySheetDialog::Init()
{
    m_innerSizer = nullptr;
    m_bookCtrl = nullptr;
    m_sheetOuterBorder = 2;
    m_sheetInnerBorder = 5;
}

// Layout the dialog, to be called after pages have been created
void ibPropertySheetDialog::LayoutDialog(int centreFlags)
{
    GetSizer()->SetSizeHints(this);
    if (centreFlags)
        Centre(centreFlags);
}

// Creates the buttons, if any
void ibPropertySheetDialog::CreateButtons(int flags)
{
    // SEAM vs wx: ibDialog's row - the library's buttons
    wxSizer *buttonSizer = CreateButtonSizer(flags);
    if( buttonSizer )
    {
        m_innerSizer->Add( buttonSizer, wxSizerFlags().Expand().Border(wxALL, FromDIP(2)) );
        m_innerSizer->AddSpacer(2);
    }
}

// Creates the book control
ibNotebook* ibPropertySheetDialog::CreateBookCtrl()
{
    // SEAM vs wx: the notebook alone (see the class)
    return new ibNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxCLIP_CHILDREN | wxBK_DEFAULT);
}

// Adds the book control to the inner sizer.
void ibPropertySheetDialog::AddBookCtrl(wxSizer* sizer)
{
    sizer->Add( m_bookCtrl, wxSizerFlags(1).Expand().Border(wxALL, m_sheetInnerBorder) );
}

// Override function in base
wxWindow* ibPropertySheetDialog::GetContentWindow() const
{
    return GetBookCtrl();
}
