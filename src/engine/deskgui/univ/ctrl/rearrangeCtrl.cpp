// FORKED from wxWidgets: the rearrange list, control and dialog (src/common/rearrangectrl.cpp) - see rearrangeCtrl.h.

///////////////////////////////////////////////////////////////////////////////
// Name:        src/common/rearrangectrl.cpp
// Purpose:     implementation of classes in wx/rearrangectrl.h
// Author:      Vadim Zeitlin
// Created:     2008-12-15
// Copyright:   (c) 2008 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/ctrl/rearrangeCtrl.h"

#include <wx/sizer.h>

#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/staticText.h"

// ============================================================================
// ibRearrangeList implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibRearrangeList, ibCheckListBox)
    EVT_CHECKLISTBOX(wxID_ANY, ibRearrangeList::OnCheck)
wxEND_EVENT_TABLE()

bool ibRearrangeList::Create(wxWindow *parent,
                             wxWindowID id,
                             const wxPoint& pos,
                             const wxSize& size,
                             const wxArrayInt& order,
                             const wxArrayString& items,
                             long style,
                             const wxValidator& validator,
                             const wxString& name)
{
    // construct the array of items in the order in which they should appear in
    // the control
    const size_t count = items.size();
    wxCHECK_MSG( order.size() == count, false, "arrays not in sync" );

    wxArrayString itemsInOrder;
    itemsInOrder.reserve(count);
    size_t n;
    for ( n = 0; n < count; n++ )
    {
        int idx = order[n];
        if ( idx < 0 )
            idx = -idx - 1;
        itemsInOrder.push_back(items[idx]);
    }

    // do create the real control
    if ( !ibCheckListBox::Create(parent, id, pos, size, itemsInOrder,
                                 style, validator, name) )
        return false;

    // and now check all the items which should be initially checked
    for ( n = 0; n < count; n++ )
    {
        if ( order[n] >= 0 )
        {
            // Be careful to call the base class version here and not our own
            // which would also update m_order itself.
            ibCheckListBox::Check(n);
        }
    }

    m_order = order;

    return true;
}

bool ibRearrangeList::CanMoveCurrentUp() const
{
    const int sel = GetSelection();
    return sel != wxNOT_FOUND && sel != 0;
}

bool ibRearrangeList::CanMoveCurrentDown() const
{
    const int sel = GetSelection();
    return sel != wxNOT_FOUND && static_cast<unsigned>(sel) != GetCount() - 1;
}

bool ibRearrangeList::MoveCurrentUp()
{
    const int sel = GetSelection();
    if ( sel == wxNOT_FOUND || sel == 0 )
        return false;

    Swap(sel, sel - 1);
    SetSelection(sel - 1);

    return true;
}

bool ibRearrangeList::MoveCurrentDown()
{
    const int sel = GetSelection();
    if ( sel == wxNOT_FOUND || static_cast<unsigned>(sel) == GetCount() - 1 )
        return false;

    Swap(sel, sel + 1);
    SetSelection(sel + 1);

    return true;
}

void ibRearrangeList::Swap(int pos1, int pos2)
{
    // update the internally stored order
    wxSwap(m_order[pos1], m_order[pos2]);


    // and now also swap all the attributes of the items

    // first the label
    const wxString stringTmp = GetString(pos1);
    SetString(pos1, GetString(pos2));
    SetString(pos2, stringTmp);

    // then the checked state
    const bool checkedTmp = IsChecked(pos1);
    ibCheckListBox::Check(pos1, IsChecked(pos2));
    ibCheckListBox::Check(pos2, checkedTmp);

    // and finally the client data, if necessary - SEAM vs wx: the item container's, named, the window has its own
    // client data and object (wxControlWithItems brought the container's into scope, ibListBox does not)
    switch ( GetClientDataType() )
    {
        case wxClientData_None:
            // nothing to do
            break;

        case wxClientData_Object:
            {
                wxClientData * const dataTmp = DetachClientObject(pos1);
                wxItemContainer::SetClientObject(pos1, DetachClientObject(pos2));
                wxItemContainer::SetClientObject(pos2, dataTmp);
            }
            break;

        case wxClientData_Void:
            {
                void * const dataTmp = wxItemContainer::GetClientData(pos1);
                wxItemContainer::SetClientData(pos1, wxItemContainer::GetClientData(pos2));
                wxItemContainer::SetClientData(pos2, dataTmp);
            }
            break;
    }
}

void ibRearrangeList::Check(unsigned int item, bool check)
{
    if ( check == IsChecked(item) )
        return;

    ibCheckListBox::Check(item, check);

    m_order[item] = ~m_order[item];
}

void ibRearrangeList::OnCheck(wxCommandEvent& event)
{
    // update the internal state to match the new item state
    const int n = event.GetInt();

    if ( (m_order[n] >= 0) != IsChecked(n) )
        m_order[n] = ~m_order[n];
}

int ibRearrangeList::DoInsertItems(const wxArrayStringsAdapter& items, unsigned int pos,
                                   void **clientData, wxClientDataType type)
{
    int ret = ibCheckListBox::DoInsertItems(items, pos, clientData, type);
    const size_t numItems = items.GetCount();
    for ( size_t i = 0; i < numItems; i++ )
    {
        // Item is not checked initially.
        const int idx = ~m_order.size();
        m_order.Insert(idx, pos+i);
    }
    return ret;
}

void ibRearrangeList::DoDeleteOneItem(unsigned int n)
{
    ibCheckListBox::DoDeleteOneItem(n);
    int idxDeleted = m_order[n];
    if ( idxDeleted < 0 )
        idxDeleted = ~idxDeleted;
    m_order.RemoveAt(n);
    // Remaining items have to be reindexed.
    for ( auto& idx : m_order )
    {
        if ( idx < 0 )
        {
            idx = ~idx;
            if ( idx > idxDeleted )
                idx = ~(idx-1);
        }
        else
        {
            if ( idx > idxDeleted )
                idx = idx-1;
        }
    }
}

void ibRearrangeList::DoClear()
{
    ibCheckListBox::DoClear();
    m_order.Clear();
}

// ============================================================================
// ibRearrangeCtrl implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibRearrangeCtrl, ibPanel)
    EVT_UPDATE_UI(wxID_UP, ibRearrangeCtrl::OnUpdateButtonUI)
    EVT_UPDATE_UI(wxID_DOWN, ibRearrangeCtrl::OnUpdateButtonUI)

    EVT_BUTTON(wxID_UP, ibRearrangeCtrl::OnButton)
    EVT_BUTTON(wxID_DOWN, ibRearrangeCtrl::OnButton)
wxEND_EVENT_TABLE()

void ibRearrangeCtrl::Init()
{
    m_list = nullptr;
}

bool
ibRearrangeCtrl::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        const wxArrayInt& order,
                        const wxArrayString& items,
                        long style,
                        const wxValidator& validator,
                        const wxString& name)
{
    // create all the windows
    if ( !ibPanel::Create(parent, id, pos, size, wxTAB_TRAVERSAL, name) )
        return false;

    m_list = new ibRearrangeList(this, wxID_ANY,
                                 wxDefaultPosition, wxDefaultSize,
                                 order, items,
                                 style, validator);
    ibButton * const btnUp = new ibButton(this, wxID_UP);
    ibButton * const btnDown = new ibButton(this, wxID_DOWN);

    // arrange them in a sizer
    wxSizer * const sizerBtns = new wxBoxSizer(wxVERTICAL);
    sizerBtns->Add(btnUp, wxSizerFlags().Centre().Border(wxBOTTOM));
    sizerBtns->Add(btnDown, wxSizerFlags().Centre().Border(wxTOP));

    wxSizer * const sizerTop = new wxBoxSizer(wxHORIZONTAL);
    sizerTop->Add(m_list, wxSizerFlags(1).Expand().Border(wxRIGHT));
    sizerTop->Add(sizerBtns, wxSizerFlags(0).Centre().Border(wxLEFT));
    SetSizer(sizerTop);

    m_list->SetFocus();

    return true;
}

void ibRearrangeCtrl::OnUpdateButtonUI(wxUpdateUIEvent& event)
{
    event.Enable( event.GetId() == wxID_UP ? m_list->CanMoveCurrentUp()
                                           : m_list->CanMoveCurrentDown() );
}

void ibRearrangeCtrl::OnButton(wxCommandEvent& event)
{
    if ( event.GetId() == wxID_UP )
        m_list->MoveCurrentUp();
    else
        m_list->MoveCurrentDown();
}

// ============================================================================
// ibRearrangeDialog implementation
// ============================================================================

namespace
{

enum ibRearrangeDialogSizerPositions
{
    Pos_Label,
    Pos_Ctrl,
    Pos_Buttons,
    Pos_Max
};

} // anonymous namespace

bool ibRearrangeDialog::Create(wxWindow *parent,
                               const wxString& message,
                               const wxString& title,
                               const wxArrayInt& order,
                               const wxArrayString& items,
                               const wxPoint& pos,
                               const wxString& name)
{
    if ( !ibDialog::Create(parent, wxID_ANY, title,
                           pos, wxDefaultSize,
                           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER,
                           name) )
        return false;

    m_ctrl = new ibRearrangeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 order, items);

    // notice that the items in this sizer should be inserted accordingly to
    // ibRearrangeDialogSizerPositions order
    wxSizer * const sizerTop = new wxBoxSizer(wxVERTICAL);

    if ( !message.empty() )
    {
        sizerTop->Add(new ibStaticText(this, wxID_ANY, message),
                      wxSizerFlags().Border());
    }
    else
    {
        // for convenience of other ibRearrangeDialog code that depends on
        // positions of sizer items, insert a dummy zero-sized item
        sizerTop->AddSpacer(0);
    }

    sizerTop->Add(m_ctrl,
                  wxSizerFlags(1).Expand().Border());
    sizerTop->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL),
                  wxSizerFlags().Expand().Border());
    SetSizerAndFit(sizerTop);

    return true;
}

void ibRearrangeDialog::AddExtraControls(wxWindow *win)
{
    wxSizer * const sizer = GetSizer();
    wxCHECK_RET( sizer, "the dialog must be created first" );

    wxASSERT_MSG( sizer->GetChildren().GetCount() == Pos_Max,
                  "calling AddExtraControls() twice?" );

    sizer->Insert(Pos_Buttons, win, wxSizerFlags().Expand().Border());

    win->MoveAfterInTabOrder(m_ctrl);

    // we need to update the initial/minimal window size
    sizer->SetSizeHints(this);
}

ibRearrangeList *ibRearrangeDialog::GetList() const
{
    wxCHECK_MSG( m_ctrl, nullptr, "the dialog must be created first" );

    return m_ctrl->GetList();
}

wxArrayInt ibRearrangeDialog::GetOrder() const
{
    wxCHECK_MSG( m_ctrl, wxArrayInt(), "the dialog must be created first" );

    return m_ctrl->GetList()->GetCurrentOrder();
}
