// FORKED from wxWidgets: the choice dialogs (src/generic/choicdgg.cpp) - see choiceDialog.h.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/choicdgg.cpp
// Purpose:     Choice dialogs
// Author:      Julian Smart
// Modified by: 03.11.00: VZ to add wxArrayString and multiple sel functions
// Created:     04/01/98
// Copyright:   (c) wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/choiceDialog.h"

#include <wx/arrstr.h>

#include "deskgui/univ/ctrl/listBox.h"
#include "deskgui/univ/ctrl/checkListBox.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

#define wxID_LISTBOX 3000

// ----------------------------------------------------------------------------
// private functions
// ----------------------------------------------------------------------------

namespace
{

// Helper used by the wrapper functions below to handle their x, y, centre,
// width and height parameters.
class ChoiceDialogGeometry
{
public:
    ChoiceDialogGeometry(int x, int y, bool centre, int width, int height)
        : m_pos(x, y),
          m_size(width, height),
          m_style(wxCHOICEDLG_STYLE)
    {
        if ( m_pos != wxDefaultPosition && !m_pos.IsFullySpecified() )
        {
            wxFAIL_MSG("Either both x and y must be valid or none of them.");
            m_pos = wxDefaultPosition;
        }

        // Explicitly specified position takes precedence over centring.
        if ( !centre || m_pos != wxDefaultPosition )
            m_style &= ~wxCENTRE;
    }

    // Style and position to create the dialog with.
    long GetStyle() const { return m_style; }
    const wxPoint& GetPos() const { return m_pos; }

    // Grow, but never shrink, the dialog to the requested size: this must be
    // called after creating it as its initial size is determined by its
    // contents.
    void ApplySize(wxDialog& dialog) const
    {
        wxSize size = dialog.GetSize();
        size.IncTo(m_size);
        if ( size == dialog.GetSize() )
            return;

        dialog.SetSize(size);

        // It was centred using its old size, so do it again.
        if ( m_style & wxCENTRE )
            dialog.Centre();
    }

private:
    wxPoint m_pos;
    const wxSize m_size;
    long m_style;
};

} // anonymous namespace

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wrapper functions
// ----------------------------------------------------------------------------

wxString ibGetSingleChoice( const wxString& message,
                            const wxString& caption,
                            int n, const wxString *choices,
                            wxWindow *parent,
                            int x, int y,
                            bool centre,
                            int width, int height,
                            int initialSelection)
{
    const ChoiceDialogGeometry geom(x, y, centre, width, height);
    ibSingleChoiceDialog dialog(parent, message, caption, n, choices,
                                nullptr, geom.GetStyle(), geom.GetPos());
    geom.ApplySize(dialog);

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetStringSelection() : wxString();
}

wxString ibGetSingleChoice( const wxString& message,
                            const wxString& caption,
                            const wxArrayString& choices,
                            wxWindow *parent,
                            int x, int y,
                            bool centre,
                            int width, int height,
                            int initialSelection)
{
    const ChoiceDialogGeometry geom(x, y, centre, width, height);
    ibSingleChoiceDialog dialog(parent, message, caption, choices,
                                nullptr, geom.GetStyle(), geom.GetPos());
    geom.ApplySize(dialog);

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetStringSelection() : wxString();
}

wxString ibGetSingleChoice( const wxString& message,
                            const wxString& caption,
                            const wxArrayString& choices,
                            int initialSelection,
                            wxWindow *parent)
{
    return ibGetSingleChoice(message, caption, choices, parent,
                             wxDefaultCoord, wxDefaultCoord,
                             true, wxDefaultCoord, wxDefaultCoord,
                             initialSelection);
}

wxString ibGetSingleChoice( const wxString& message,
                            const wxString& caption,
                            int n, const wxString *choices,
                            int initialSelection,
                            wxWindow *parent)
{
    return ibGetSingleChoice(message, caption, n, choices, parent,
                             wxDefaultCoord, wxDefaultCoord,
                             true, wxDefaultCoord, wxDefaultCoord,
                             initialSelection);
}

int ibGetSingleChoiceIndex( const wxString& message,
                            const wxString& caption,
                            int n, const wxString *choices,
                            wxWindow *parent,
                            int x, int y,
                            bool centre,
                            int width, int height,
                            int initialSelection)
{
    const ChoiceDialogGeometry geom(x, y, centre, width, height);
    ibSingleChoiceDialog dialog(parent, message, caption, n, choices,
                                nullptr, geom.GetStyle(), geom.GetPos());
    geom.ApplySize(dialog);

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetSelection() : -1;
}

int ibGetSingleChoiceIndex( const wxString& message,
                            const wxString& caption,
                            const wxArrayString& choices,
                            wxWindow *parent,
                            int x, int y,
                            bool centre,
                            int width, int height,
                            int initialSelection)
{
    const ChoiceDialogGeometry geom(x, y, centre, width, height);
    ibSingleChoiceDialog dialog(parent, message, caption, choices,
                                nullptr, geom.GetStyle(), geom.GetPos());
    geom.ApplySize(dialog);

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetSelection() : -1;
}

int ibGetSingleChoiceIndex( const wxString& message,
                            const wxString& caption,
                            const wxArrayString& choices,
                            int initialSelection,
                            wxWindow *parent)
{
    return ibGetSingleChoiceIndex(message, caption, choices, parent,
                                  wxDefaultCoord, wxDefaultCoord,
                                  true, wxDefaultCoord, wxDefaultCoord,
                                  initialSelection);
}


int ibGetSingleChoiceIndex( const wxString& message,
                            const wxString& caption,
                            int n, const wxString *choices,
                            int initialSelection,
                            wxWindow *parent)
{
    return ibGetSingleChoiceIndex(message, caption, n, choices, parent,
                                  wxDefaultCoord, wxDefaultCoord,
                                  true, wxDefaultCoord, wxDefaultCoord,
                                  initialSelection);
}


void *ibGetSingleChoiceData( const wxString& message,
                             const wxString& caption,
                             int n, const wxString *choices,
                             void **client_data,
                             wxWindow *parent,
                             int x, int y,
                             bool centre,
                             int width, int height,
                             int initialSelection)
{
    const ChoiceDialogGeometry geom(x, y, centre, width, height);
    ibSingleChoiceDialog dialog(parent, message, caption, n, choices,
                                client_data, geom.GetStyle(), geom.GetPos());
    geom.ApplySize(dialog);

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetSelectionData() : nullptr;
}

void *ibGetSingleChoiceData( const wxString& message,
                             const wxString& caption,
                             const wxArrayString& choices,
                             void **client_data,
                             wxWindow *parent,
                             int x, int y,
                             bool centre,
                             int width, int height,
                             int initialSelection)
{
    const ChoiceDialogGeometry geom(x, y, centre, width, height);
    ibSingleChoiceDialog dialog(parent, message, caption, choices,
                                client_data, geom.GetStyle(), geom.GetPos());
    geom.ApplySize(dialog);

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetSelectionData() : nullptr;
}

void* ibGetSingleChoiceData( const wxString& message,
                             const wxString& caption,
                             const wxArrayString& choices,
                             void **client_data,
                             int initialSelection,
                             wxWindow *parent)
{
    return ibGetSingleChoiceData(message, caption, choices,
                                 client_data, parent,
                                 wxDefaultCoord, wxDefaultCoord,
                                 true, wxDefaultCoord, wxDefaultCoord,
                                 initialSelection);
}

void* ibGetSingleChoiceData( const wxString& message,
                             const wxString& caption,
                             int n, const wxString *choices,
                             void **client_data,
                             int initialSelection,
                             wxWindow *parent)
{
    return ibGetSingleChoiceData(message, caption, n, choices,
                                 client_data, parent,
                                 wxDefaultCoord, wxDefaultCoord,
                                 true, wxDefaultCoord, wxDefaultCoord,
                                 initialSelection);
}


int ibGetSelectedChoices(wxArrayInt& selections,
                         const wxString& message,
                         const wxString& caption,
                         int n, const wxString *choices,
                         wxWindow *parent,
                         int x, int y,
                         bool centre,
                         int width, int height)
{
    const ChoiceDialogGeometry geom(x, y, centre, width, height);
    ibMultiChoiceDialog dialog(parent, message, caption, n, choices,
                               geom.GetStyle(), geom.GetPos());
    geom.ApplySize(dialog);

    // call this even if selections array is empty and this then (correctly)
    // deselects the first item which is selected by default
    dialog.SetSelections(selections);

    if ( dialog.ShowModal() != wxID_OK )
    {
        // NB: intentionally do not clear the selections array here, the caller
        //     might want to preserve its original contents if the dialog was
        //     cancelled
        return -1;
    }

    selections = dialog.GetSelections();
    return static_cast<int>(selections.GetCount());
}

int ibGetSelectedChoices(wxArrayInt& selections,
                         const wxString& message,
                         const wxString& caption,
                         const wxArrayString& choices,
                         wxWindow *parent,
                         int x, int y,
                         bool centre,
                         int width, int height)
{
    const ChoiceDialogGeometry geom(x, y, centre, width, height);
    ibMultiChoiceDialog dialog(parent, message, caption, choices,
                               geom.GetStyle(), geom.GetPos());
    geom.ApplySize(dialog);

    // call this even if selections array is empty and this then (correctly)
    // deselects the first item which is selected by default
    dialog.SetSelections(selections);

    if ( dialog.ShowModal() != wxID_OK )
    {
        // NB: intentionally do not clear the selections array here, the caller
        //     might want to preserve its original contents if the dialog was
        //     cancelled
        return -1;
    }

    selections = dialog.GetSelections();
    return static_cast<int>(selections.GetCount());
}

// ----------------------------------------------------------------------------
// ibAnyChoiceDialog
// ----------------------------------------------------------------------------

bool ibAnyChoiceDialog::Create(wxWindow *parent,
                               const wxString& message,
                               const wxString& caption,
                               int n, const wxString *choices,
                               long styleDlg,
                               const wxPoint& pos,
                               long styleLbox)
{
    // extract the buttons styles from the dialog one and remove them from it
    const long styleBtns = styleDlg & (wxOK | wxCANCEL);
    styleDlg &= ~styleBtns;

    if ( !ibDialog::Create(GetParentForModalDialog(parent, styleDlg), wxID_ANY, caption, pos, wxDefaultSize, styleDlg) )
        return false;

    wxBoxSizer *topsizer = new wxBoxSizer( wxVERTICAL );

    // 1) text message
    topsizer->
        Add(CreateTextSizer(message), wxSizerFlags().Expand().TripleBorder());

    // 2) list box
    m_listbox = CreateList(n, choices, styleLbox);

    if ( n > 0 )
        m_listbox->SetSelection(0);

    topsizer->
        Add(m_listbox, wxSizerFlags().Expand().Proportion(1).TripleBorder(wxLEFT | wxRIGHT));

    // 3) buttons if any
    wxSizer *
        buttonSizer = CreateSeparatedButtonSizer(styleBtns);
    if ( buttonSizer )
    {
        topsizer->Add(buttonSizer, wxSizerFlags().Expand().DoubleBorder());
    }

    SetSizer( topsizer );

    topsizer->SetSizeHints( this );

    if ( styleDlg & wxCENTRE )
        Centre(wxBOTH);

    m_listbox->SetFocus();

    return true;
}

bool ibAnyChoiceDialog::Create(wxWindow *parent,
                               const wxString& message,
                               const wxString& caption,
                               const wxArrayString& choices,
                               long styleDlg,
                               const wxPoint& pos,
                               long styleLbox)
{
    wxCArrayString chs(choices);
    return Create(parent, message, caption, chs.GetCount(), chs.GetStrings(),
                  styleDlg, pos, styleLbox);
}

ibListBox *ibAnyChoiceDialog::CreateList(int n, const wxString *choices, long styleLbox)
{
    return new ibListBox( this, wxID_LISTBOX,
                          wxDefaultPosition, wxDefaultSize,
                          n, choices,
                          styleLbox );
}

// ----------------------------------------------------------------------------
// ibSingleChoiceDialog
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibSingleChoiceDialog, ibDialog)
    EVT_BUTTON(wxID_OK, ibSingleChoiceDialog::OnOK)
    EVT_LISTBOX_DCLICK(wxID_LISTBOX, ibSingleChoiceDialog::OnListBoxDClick)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(ibSingleChoiceDialog, ibDialog);

bool ibSingleChoiceDialog::Create( wxWindow *parent,
                                   const wxString& message,
                                   const wxString& caption,
                                   int n,
                                   const wxString *choices,
                                   void **clientData,
                                   long style,
                                   const wxPoint& pos )
{
    if ( !ibAnyChoiceDialog::Create(parent, message, caption,
                                    n, choices,
                                    style, pos) )
        return false;

    m_selection = n > 0 ? 0 : -1;

    // SEAM vs wx: the item's data named by its container - the library's list box is a window and an item container
    // both, each with client data of its own, and wx's wxWindowWithItems that forwarded both (ctrlsub.h) is not under
    // it (comboBox.cpp's way)
    if (clientData)
    {
        for (int i = 0; i < n; i++)
            m_listbox->wxItemContainer::SetClientData(i, clientData[i]);
    }

    return true;
}

bool ibSingleChoiceDialog::Create( wxWindow *parent,
                                   const wxString& message,
                                   const wxString& caption,
                                   const wxArrayString& choices,
                                   void **clientData,
                                   long style,
                                   const wxPoint& pos )
{
    wxCArrayString chs(choices);
    return Create( parent, message, caption, chs.GetCount(), chs.GetStrings(),
                   clientData, style, pos );
}

// Set the selection
void ibSingleChoiceDialog::SetSelection(int sel)
{
    wxCHECK_RET( sel >= 0 && (unsigned)sel < m_listbox->GetCount(),
                 "Invalid initial selection" );

    m_listbox->SetSelection(sel);
    m_selection = sel;
}

void ibSingleChoiceDialog::OnOK(wxCommandEvent& WXUNUSED(event))
{
    DoChoice();
}

void ibSingleChoiceDialog::OnListBoxDClick(wxCommandEvent& WXUNUSED(event))
{
    DoChoice();
}

void ibSingleChoiceDialog::DoChoice()
{
    m_selection = m_listbox->GetSelection();
    m_stringSelection = m_listbox->GetStringSelection();

    if ( m_listbox->HasClientUntypedData() )
        SetClientData(m_listbox->wxItemContainer::GetClientData(m_selection));

    EndModal(wxID_OK);
}

// ----------------------------------------------------------------------------
// ibMultiChoiceDialog
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibMultiChoiceDialog, ibDialog);

bool ibMultiChoiceDialog::Create( wxWindow *parent,
                                  const wxString& message,
                                  const wxString& caption,
                                  int n,
                                  const wxString *choices,
                                  long style,
                                  const wxPoint& pos )
{
    long styleLbox;
#if wxUSE_CHECKLISTBOX
    styleLbox = wxLB_ALWAYS_SB;
#else
    styleLbox = wxLB_ALWAYS_SB | wxLB_EXTENDED;
#endif

    if ( !ibAnyChoiceDialog::Create(parent, message, caption,
                                    n, choices,
                                    style, pos,
                                    styleLbox) )
        return false;

    return true;
}

bool ibMultiChoiceDialog::Create( wxWindow *parent,
                                  const wxString& message,
                                  const wxString& caption,
                                  const wxArrayString& choices,
                                  long style,
                                  const wxPoint& pos )
{
    wxCArrayString chs(choices);
    return Create( parent, message, caption, chs.GetCount(),
                   chs.GetStrings(), style, pos );
}

void ibMultiChoiceDialog::SetSelections(const wxArrayInt& selections)
{
#if wxUSE_CHECKLISTBOX
    ibCheckListBox* checkListBox = wxDynamicCast(m_listbox, ibCheckListBox);
    if (checkListBox)
    {
        // first clear all currently selected items
        size_t n,
            count = checkListBox->GetCount();
        for ( n = 0; n < count; ++n )
        {
            if (checkListBox->IsChecked(n))
                checkListBox->Check(n, false);
        }

        // now select the ones which should be selected
        count = selections.GetCount();
        for ( n = 0; n < count; n++ )
        {
            checkListBox->Check(selections[n]);
        }

        return;
    }
#endif

    // first clear all currently selected items
    size_t n,
           count = m_listbox->GetCount();
    for ( n = 0; n < count; ++n )
    {
        m_listbox->Deselect(n);
    }

    // now select the ones which should be selected
    count = selections.GetCount();
    for ( n = 0; n < count; n++ )
    {
        // SEAM vs wx: wxListBoxBase's Select(n), lost with the base (listBox.h) - its SetSelection(n, true)
        m_listbox->SetSelection(selections[n], true);
    }
}

bool ibMultiChoiceDialog::TransferDataFromWindow()
{
    m_selections.Empty();

#if wxUSE_CHECKLISTBOX
    ibCheckListBox* checkListBox = wxDynamicCast(m_listbox, ibCheckListBox);
    if (checkListBox)
    {
        size_t count = checkListBox->GetCount();
        for ( size_t n = 0; n < count; n++ )
        {
            if ( checkListBox->IsChecked(n) )
                m_selections.Add(n);
        }
        return true;
    }
#endif

    size_t count = m_listbox->GetCount();
    for ( size_t n = 0; n < count; n++ )
    {
        if ( m_listbox->IsSelected(n) )
            m_selections.Add(n);
    }

    return true;
}

#if wxUSE_CHECKLISTBOX

ibListBox *ibMultiChoiceDialog::CreateList(int n, const wxString *choices, long styleLbox)
{
    return new ibCheckListBox( this, wxID_LISTBOX,
                               wxDefaultPosition, wxDefaultSize,
                               n, choices,
                               styleLbox );
}

#endif // wxUSE_CHECKLISTBOX
