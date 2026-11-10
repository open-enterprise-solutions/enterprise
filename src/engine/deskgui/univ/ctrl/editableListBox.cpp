// FORKED from wxWidgets: wxEditableListBox (src/generic/editlbox.cpp) - see editableListBox.h.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/editlbox.cpp
// Purpose:     ListBox with editable items
// Author:      Vaclav Slavik
// Copyright:   (c) Vaclav Slavik
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/ctrl/editableListBox.h"

#include <wx/sizer.h>
#include <wx/artprov.h>

#include "deskgui/univ/ctrl/listCtrl.h"
#include "deskgui/univ/ctrl/bmpButton.h"
#include "deskgui/univ/ctrl/staticText.h"

// ============================================================================
// implementation
// ============================================================================

const char ibEditableListBoxNameStr[] = "editableListBox";

// list control with auto-resizable column:
class CleverListCtrl : public ibListCtrl
{
public:
   CleverListCtrl(wxWindow *parent,
                  wxWindowID id = wxID_ANY,
                  const wxPoint &pos = wxDefaultPosition,
                  const wxSize &size = wxDefaultSize,
                  long style = wxLC_ICON,
                  const wxValidator& validator = wxDefaultValidator,
                  const wxString &name = wxListCtrlNameStr)
         : ibListCtrl(parent, id, pos, size, style, validator, name)
    {
        CreateColumns();
    }

    void CreateColumns()
    {
        InsertColumn(0, wxT("item"));
        SizeColumns();
    }

    void SizeColumns()
    {
         int w = GetSize().x;
#ifdef __WXMSW__
         w -= GetScrollbarSize(wxVERTICAL) + 6;
#else
         w -= 2*GetScrollbarSize(wxVERTICAL);
#endif
         if (w < 0) w = 0;
         SetColumnWidth(0, w);
    }

private:
    wxDECLARE_EVENT_TABLE();
    void OnSize(wxSizeEvent& event)
    {
        SizeColumns();
        event.Skip();
    }
};

wxBEGIN_EVENT_TABLE(CleverListCtrl, ibListCtrl)
   EVT_SIZE(CleverListCtrl::OnSize)
wxEND_EVENT_TABLE()


// ----------------------------------------------------------------------------
// ibEditableListBox
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibEditableListBox, ibPanel);

// NB: generate the IDs at runtime to avoid conflict with XRCID values,
//     they could cause XRCCTRL() failures in XRC-based dialogs
const wxWindowIDRef ibID_ELB_DELETE = wxWindow::NewControlId();
const wxWindowIDRef ibID_ELB_EDIT = wxWindow::NewControlId();
const wxWindowIDRef ibID_ELB_NEW = wxWindow::NewControlId();
const wxWindowIDRef ibID_ELB_UP = wxWindow::NewControlId();
const wxWindowIDRef ibID_ELB_DOWN = wxWindow::NewControlId();
const wxWindowIDRef ibID_ELB_LISTCTRL = wxWindow::NewControlId();

wxBEGIN_EVENT_TABLE(ibEditableListBox, ibPanel)
    EVT_LIST_ITEM_SELECTED(ibID_ELB_LISTCTRL, ibEditableListBox::OnItemSelected)
    EVT_LIST_END_LABEL_EDIT(ibID_ELB_LISTCTRL, ibEditableListBox::OnEndLabelEdit)
    EVT_BUTTON(ibID_ELB_NEW, ibEditableListBox::OnNewItem)
    EVT_BUTTON(ibID_ELB_UP, ibEditableListBox::OnUpItem)
    EVT_BUTTON(ibID_ELB_DOWN, ibEditableListBox::OnDownItem)
    EVT_BUTTON(ibID_ELB_EDIT, ibEditableListBox::OnEditItem)
    EVT_BUTTON(ibID_ELB_DELETE, ibEditableListBox::OnDelItem)
wxEND_EVENT_TABLE()

bool ibEditableListBox::Create(wxWindow *parent, wxWindowID id,
                          const wxString& label,
                          const wxPoint& pos, const wxSize& size,
                          long style,
                          const wxString& name)
{
    if (!ibPanel::Create(parent, id, pos, size, wxTAB_TRAVERSAL, name))
        return false;

    m_style = style;

    // ibEL_ALLOW_NEW requires ability to edit labels so implicitly add ibEL_ALLOW_EDIT
    if ( m_style & ibEL_ALLOW_NEW )
      m_style |= ibEL_ALLOW_EDIT;

    wxSizer *sizer = new wxBoxSizer(wxVERTICAL);

    ibPanel *subp = new ibPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxSUNKEN_BORDER | wxTAB_TRAVERSAL);
    wxSizer *subsizer = new wxBoxSizer(wxHORIZONTAL);

    subsizer->Add(new ibStaticText(subp, wxID_ANY, label),
                  wxSizerFlags(1).Center().Border(wxLEFT));

    const wxSizerFlags flagsCentered = wxSizerFlags().Center();

    if ( m_style & ibEL_ALLOW_EDIT )
    {
        m_bEdit = new ibBitmapButton(subp, ibID_ELB_EDIT,
                                     wxArtProvider::GetBitmap(wxART_EDIT, wxART_BUTTON));
        m_bEdit->SetToolTip(_("Edit item"));
        subsizer->Add(m_bEdit, flagsCentered);
    }

    if ( m_style & ibEL_ALLOW_NEW )
    {
        m_bNew = new ibBitmapButton(subp, ibID_ELB_NEW,
                                    wxArtProvider::GetBitmap(wxART_NEW, wxART_BUTTON));
        m_bNew->SetToolTip(_("New item"));
        subsizer->Add(m_bNew, flagsCentered);
    }

    if ( m_style & ibEL_ALLOW_DELETE )
    {
        m_bDel = new ibBitmapButton(subp, ibID_ELB_DELETE,
                                    wxArtProvider::GetBitmap(wxART_DELETE, wxART_BUTTON));
        m_bDel->SetToolTip(_("Delete item"));
        subsizer->Add(m_bDel, flagsCentered);
    }

    if (!(m_style & ibEL_NO_REORDER))
    {
        m_bUp = new ibBitmapButton(subp, ibID_ELB_UP,
                                   wxArtProvider::GetBitmap(wxART_GO_UP, wxART_BUTTON));
        m_bUp->SetToolTip(_("Move up"));
        subsizer->Add(m_bUp, flagsCentered);

        m_bDown = new ibBitmapButton(subp, ibID_ELB_DOWN,
                                     wxArtProvider::GetBitmap(wxART_GO_DOWN, wxART_BUTTON));
        m_bDown->SetToolTip(_("Move down"));
        subsizer->Add(m_bDown, flagsCentered);
    }

    subp->SetSizer(subsizer);
    subsizer->Fit(subp);

    sizer->Add(subp, wxSizerFlags().Expand());

    long st = wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL | wxSUNKEN_BORDER;
    if ( m_style & ibEL_ALLOW_EDIT )
         st |= wxLC_EDIT_LABELS;
    m_listCtrl = new CleverListCtrl(this, ibID_ELB_LISTCTRL,
                                    wxDefaultPosition, wxDefaultSize, st);
    wxArrayString empty_ar;
    SetStrings(empty_ar);

    sizer->Add(m_listCtrl, wxSizerFlags(1).Expand());

    SetSizer(sizer);
    Layout();

    return true;
}

void ibEditableListBox::SetStrings(const wxArrayString& strings)
{
    m_listCtrl->DeleteAllItems();
    size_t i;

    for (i = 0; i < strings.GetCount(); i++)
        m_listCtrl->InsertItem(i, strings[i]);

    m_listCtrl->InsertItem(strings.GetCount(), wxEmptyString);
    m_listCtrl->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
}

void ibEditableListBox::GetStrings(wxArrayString& strings) const
{
    strings.Clear();

    for (int i = 0; i < m_listCtrl->GetItemCount()-1; i++)
        strings.Add(m_listCtrl->GetItemText(i));
}

void ibEditableListBox::OnItemSelected(wxListEvent& event)
{
    m_selection = event.GetIndex();
    if (!(m_style & ibEL_NO_REORDER))
    {
        m_bUp->Enable(m_selection != 0 && m_selection < m_listCtrl->GetItemCount()-1);
        m_bDown->Enable(m_selection < m_listCtrl->GetItemCount()-2);
    }

    if (m_style & ibEL_ALLOW_EDIT)
        m_bEdit->Enable(m_selection < m_listCtrl->GetItemCount()-1);
    if (m_style & ibEL_ALLOW_DELETE)
        m_bDel->Enable(m_selection < m_listCtrl->GetItemCount()-1);
}

void ibEditableListBox::OnNewItem(wxCommandEvent& WXUNUSED(event))
{
    m_listCtrl->SetItemState(m_listCtrl->GetItemCount()-1,
                             wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    m_listCtrl->EditLabel(m_selection);
}

void ibEditableListBox::OnEndLabelEdit(wxListEvent& event)
{
    if ( event.GetIndex() == m_listCtrl->GetItemCount()-1 &&
         !event.GetText().empty() )
    {
        // The user edited last (empty) line, i.e. added new entry. We have to
        // add new empty line here so that adding one more line is still
        // possible:
        m_listCtrl->InsertItem(m_listCtrl->GetItemCount(), wxEmptyString);

        // Simulate a wxEVT_LIST_ITEM_SELECTED event for the new item,
        // so that the buttons are enabled/disabled properly
        wxListEvent selectionEvent(wxEVT_LIST_ITEM_SELECTED, m_listCtrl->GetId());
        selectionEvent.m_itemIndex = event.GetIndex();
        m_listCtrl->GetEventHandler()->ProcessEvent(selectionEvent);
    }
}

void ibEditableListBox::OnDelItem(wxCommandEvent& WXUNUSED(event))
{
    m_listCtrl->DeleteItem(m_selection);
    m_listCtrl->SetItemState(m_selection,
                             wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
}

void ibEditableListBox::OnEditItem(wxCommandEvent& WXUNUSED(event))
{
    m_listCtrl->EditLabel(m_selection);
}

void ibEditableListBox::SwapItems(long i1, long i2)
{
    // swap the text
    wxString t1 = m_listCtrl->GetItemText(i1);
    wxString t2 = m_listCtrl->GetItemText(i2);
    m_listCtrl->SetItemText(i1, t2);
    m_listCtrl->SetItemText(i2, t1);

    // swap the item data
    wxUIntPtr d1 = m_listCtrl->GetItemData(i1);
    wxUIntPtr d2 = m_listCtrl->GetItemData(i2);
    m_listCtrl->SetItemPtrData(i1, d2);
    m_listCtrl->SetItemPtrData(i2, d1);
}


void ibEditableListBox::OnUpItem(wxCommandEvent& WXUNUSED(event))
{
    SwapItems(m_selection - 1, m_selection);
    m_listCtrl->SetItemState(m_selection - 1,
                             wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
}

void ibEditableListBox::OnDownItem(wxCommandEvent& WXUNUSED(event))
{
    SwapItems(m_selection + 1, m_selection);
    m_listCtrl->SetItemState(m_selection + 1,
                             wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
}
