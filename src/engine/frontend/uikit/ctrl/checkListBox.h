// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/checklst.h
// Purpose:     ibCheckListBox class for wxUniversal
// Author:      Vadim Zeitlin
// Created:     12.09.00
// Copyright:   (c) Vadim Zeitlin
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_CHECKLST_H_
#define _WX_UNIV_CHECKLST_H_

#include "frontend/frontend.h"

#include "frontend/uikit/ctrl/listBox.h"

// ----------------------------------------------------------------------------
// actions
// ----------------------------------------------------------------------------

#define ibACTION_CHECKLISTBOX_TOGGLE wxT("toggle")

// ----------------------------------------------------------------------------
// ibCheckListBox
// ----------------------------------------------------------------------------

// SEAM vs univ: wxCheckListBoxBase rides the native wxListBox chain — derive
// from our ibListBox directly
class FRONTEND_API ibCheckListBox : public ibListBox
{
public:
    // ctors
    ibCheckListBox() { Init(); }

    ibCheckListBox(wxWindow *parent,
                   wxWindowID id,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize,
                   int nStrings = 0,
                   const wxString choices[] = nullptr,
                   long style = 0,
                   const wxValidator& validator = wxDefaultValidator,
                   const wxString& name = wxASCII_STR(wxListBoxNameStr))
    {
        Init();

        Create(parent, id, pos, size, nStrings, choices, style, validator, name);
    }
    ibCheckListBox(wxWindow *parent,
                   wxWindowID id,
                   const wxPoint& pos,
                   const wxSize& size,
                   const wxArrayString& choices,
                   long style = 0,
                   const wxValidator& validator = wxDefaultValidator,
                   const wxString& name = wxASCII_STR(wxListBoxNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                int nStrings = 0,
                const wxString choices[] = nullptr,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxListBoxNameStr));
    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayString& choices,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxListBoxNameStr));

    // implement check list box methods (the virtuals lived in the lost
    // wxCheckListBoxBase)
    virtual bool IsChecked(unsigned int item) const;
    virtual void Check(unsigned int item, bool check = true);

    // and input handling
    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = -1l,
                               const wxString& strArg = wxEmptyString) override;

    static ibInputHandler *GetStdInputHandler(ibInputHandler *handlerDef);
    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef) override
    {
        return GetStdInputHandler(handlerDef);
    }

protected:
    // override all methods which add/delete items to update m_checks array as
    // well
    virtual void OnItemInserted(unsigned int pos) override;
    virtual void DoDeleteOneItem(unsigned int n) override;
    virtual void DoClear() override;

    // draw the check items instead of the usual ones
    virtual void DoDrawRange(ibControlRenderer *renderer,
                             int itemFirst, int itemLast) override;

    // take them also into account for size calculation
    virtual wxSize DoGetBestClientSize() const override;

    // common part of all ctors
    void Init();

private:
    // the array containing the checked status of the items
    wxArrayInt m_checks;

    wxDECLARE_DYNAMIC_CLASS(ibCheckListBox);
};

#endif // _WX_UNIV_CHECKLST_H_
