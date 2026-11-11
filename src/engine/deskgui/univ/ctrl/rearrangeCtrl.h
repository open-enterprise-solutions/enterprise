// FORKED from wxWidgets: wxRearrangeList, wxRearrangeCtrl and wxRearrangeDialog (include/wx/rearrangectrl.h,
// src/common/rearrangectrl.cpp), of the library's check list box, panel, buttons, static text and dialog (wx -> ib
// prefixes applied). The order array's convention is wx's: an item's index by its place, an unchecked one complemented.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/rearrangectrl.h
// Purpose:     various controls for rearranging the items interactively
// Author:      Vadim Zeitlin
// Created:     2008-12-15
// Copyright:   (c) 2008 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_REARRANGECTRL_H_
#define _DESKGUI_UNIV_REARRANGECTRL_H_

#include "deskgui/univ/ctrl/checkListBox.h"
#include "deskgui/univ/panel.h"
#include "deskgui/univ/dialog.h"

#include <wx/arrstr.h>
#include <wx/rearrangectrl.h>   // wxRearrangeListNameStr, wxRearrangeDialogNameStr - the windows' names, wx's

// ----------------------------------------------------------------------------
// ibRearrangeList: a (check) list box allowing to move items around
// ----------------------------------------------------------------------------

// This class works allows to change the order of the items shown in it as well
// as to check or uncheck them individually. The data structure used to allow
// this is the order array which contains the items indices indexed by their
// position with an added twist that the unchecked items are represented by the
// bitwise complement of the corresponding index (for any architecture using
// two's complement for negative numbers representation (i.e. just about any at
// all) this means that a checked item N is represented by -N-1 in unchecked
// state).
//
// So, for example, the array order [1 -3 0] used in conjunction with the items
// array ["first", "second", "third"] means that the items are displayed in the
// order "second", "third", "first" and the "third" item is unchecked while the
// other two are checked.
class DESKGUI_API ibRearrangeList : public ibCheckListBox
{
public:
    // ctors and such
    // --------------

    // default ctor, call Create() later
    ibRearrangeList() = default;

    // ctor creating the control, the arguments are the same as for
    // ibCheckListBox except for the extra order array which defines the
    // (initial) display order of the items as well as their statuses, see the
    // description above
    ibRearrangeList(wxWindow *parent,
                    wxWindowID id,
                    const wxPoint& pos,
                    const wxSize& size,
                    const wxArrayInt& order,
                    const wxArrayString& items,
                    long style = 0,
                    const wxValidator& validator = wxDefaultValidator,
                    const wxString& name = wxASCII_STR(wxRearrangeListNameStr))
    {
        Create(parent, id, pos, size, order, items, style, validator, name);
    }

    // Create() function takes the same parameters as the base class one and
    // the order array determining the initial display order
    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayInt& order,
                const wxArrayString& items,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxRearrangeListNameStr));


    // items order
    // -----------

    // get the current items order; the returned array uses the same convention
    // as the one passed to the ctor
    const wxArrayInt& GetCurrentOrder() const { return m_order; }

    // return true if the current item can be moved up or down (i.e. just that
    // it's not the first or the last one)
    bool CanMoveCurrentUp() const;
    bool CanMoveCurrentDown() const;

    // move the current item one position up or down, return true if it was moved
    // or false if the current item was the first/last one and so nothing was done
    bool MoveCurrentUp();
    bool MoveCurrentDown();


    // Override this to keep our m_order array in sync with the real item state.
    virtual void Check(unsigned int item, bool check = true) override;

protected:
    virtual int DoInsertItems(const wxArrayStringsAdapter& items, unsigned int pos,
                              void **clientData, wxClientDataType type) override;
    virtual void DoDeleteOneItem(unsigned int n) override;
    virtual void DoClear() override;

private:
    // swap two items at the given positions in the listbox
    void Swap(int pos1, int pos2);

    // event handler for item checking/unchecking
    void OnCheck(wxCommandEvent& event);


    // the current order array
    wxArrayInt m_order;


    wxDECLARE_EVENT_TABLE();
    wxDECLARE_NO_COPY_CLASS(ibRearrangeList);
};

// ----------------------------------------------------------------------------
// ibRearrangeCtrl: composite control containing a ibRearrangeList and buttons
// ----------------------------------------------------------------------------

class DESKGUI_API ibRearrangeCtrl : public ibPanel
{
public:
    // ctors/Create function are the same as for ibRearrangeList
    ibRearrangeCtrl()
    {
        Init();
    }

    ibRearrangeCtrl(wxWindow *parent,
                    wxWindowID id,
                    const wxPoint& pos,
                    const wxSize& size,
                    const wxArrayInt& order,
                    const wxArrayString& items,
                    long style = 0,
                    const wxValidator& validator = wxDefaultValidator,
                    const wxString& name = wxASCII_STR(wxRearrangeListNameStr))
    {
        Init();

        Create(parent, id, pos, size, order, items, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayInt& order,
                const wxArrayString& items,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxRearrangeListNameStr));

    // get the underlying listbox
    ibRearrangeList *GetList() const { return m_list; }

private:
    // common part of all ctors
    void Init();

    // event handlers for the buttons
    void OnUpdateButtonUI(wxUpdateUIEvent& event);
    void OnButton(wxCommandEvent& event);


    ibRearrangeList *m_list;


    wxDECLARE_EVENT_TABLE();
    wxDECLARE_NO_COPY_CLASS(ibRearrangeCtrl);
};

// ----------------------------------------------------------------------------
// ibRearrangeDialog: dialog containing a ibRearrangeCtrl
// ----------------------------------------------------------------------------

class DESKGUI_API ibRearrangeDialog : public ibDialog
{
public:
    // default ctor, use Create() later
    ibRearrangeDialog() { Init(); }

    // ctor for the dialog: message is shown inside the dialog itself, order
    // and items are passed to ibRearrangeList used internally
    ibRearrangeDialog(wxWindow *parent,
                      const wxString& message,
                      const wxString& title,
                      const wxArrayInt& order,
                      const wxArrayString& items,
                      const wxPoint& pos = wxDefaultPosition,
                      const wxString& name = wxASCII_STR(wxRearrangeDialogNameStr))
    {
        Init();

        Create(parent, message, title, order, items, pos, name);
    }

    bool Create(wxWindow *parent,
                const wxString& message,
                const wxString& title,
                const wxArrayInt& order,
                const wxArrayString& items,
                const wxPoint& pos = wxDefaultPosition,
                const wxString& name = wxASCII_STR(wxRearrangeDialogNameStr));


    // methods for the dialog customization

    // add extra contents to the dialog below the ibRearrangeCtrl part: the
    // given window (usually a panel containing more control inside it) must
    // have the dialog as its parent and will be inserted into it at the right
    // place by this method
    void AddExtraControls(wxWindow *win);

    // return the ibRearrangeList control used by the dialog
    ibRearrangeList *GetList() const;


    // get the order of items after it was modified by the user
    wxArrayInt GetOrder() const;

private:
    // common part of all ctors
    void Init() { m_ctrl = nullptr; }

    ibRearrangeCtrl *m_ctrl;

    wxDECLARE_NO_COPY_CLASS(ibRearrangeDialog);
};

#endif // _DESKGUI_UNIV_REARRANGECTRL_H_
