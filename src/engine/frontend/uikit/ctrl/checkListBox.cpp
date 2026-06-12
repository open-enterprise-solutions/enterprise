// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/checklst.cpp
// Purpose:     ibCheckListBox implementation
// Author:      Vadim Zeitlin
// Created:     12.09.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include <wx/wxprec.h>

#include "frontend/uikit/ctrl/checkListBox.h"


#if wxUSE_CHECKLISTBOX

#include <wx/checklst.h>

#ifndef WX_PRECOMP
    #include <wx/log.h>
    #include <wx/dcclient.h>
    #include <wx/validate.h>
#endif

#include "frontend/uikit/renderer.h"
#include "frontend/uikit/inputHandler.h"
#include "frontend/uikit/theme.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibCheckListBox, ibListBox);

// ----------------------------------------------------------------------------
// wxStdCheckListBoxInputHandler
// ----------------------------------------------------------------------------

class FRONTEND_API ibStdCheckListboxInputHandler : public ibStdInputHandler
{
public:
    ibStdCheckListboxInputHandler(ibInputHandler *inphand);

    virtual bool HandleKey(ibInputConsumer *consumer,
                           const wxKeyEvent& event,
                           bool pressed);
    virtual bool HandleMouse(ibInputConsumer *consumer,
                             const wxMouseEvent& event);
};

// ============================================================================
// implementation of ibCheckListBox
// ============================================================================

// ----------------------------------------------------------------------------
// creation
// ----------------------------------------------------------------------------

void ibCheckListBox::Init()
{
    m_inputHandlerType = ibINP_HANDLER_CHECKLISTBOX;
}

ibCheckListBox::ibCheckListBox(wxWindow *parent,
                               wxWindowID id,
                               const wxPoint &pos,
                               const wxSize &size,
                               const wxArrayString& choices,
                               long style,
                               const wxValidator& validator,
                               const wxString &name)
{
    Init();

    Create(parent, id, pos, size, choices, style, validator, name);
}

bool ibCheckListBox::Create(wxWindow *parent,
                            wxWindowID id,
                            const wxPoint &pos,
                            const wxSize &size,
                            const wxArrayString& choices,
                            long style,
                            const wxValidator& validator,
                            const wxString &name)
{
    wxCArrayString chs(choices);

    return Create(parent, id, pos, size, chs.GetCount(), chs.GetStrings(),
                  style, validator, name);
}

bool ibCheckListBox::Create(wxWindow *parent,
                            wxWindowID id,
                            const wxPoint &pos,
                            const wxSize &size,
                            int n,
                            const wxString choices[],
                            long style,
                            const wxValidator& validator,
                            const wxString &name)
{
    if ( !ibListBox::Create(parent, id, pos, size,
                            n, choices, style, validator, name) )
        return false;

    return true;
}

// ----------------------------------------------------------------------------
// ibCheckListBox functions
// ----------------------------------------------------------------------------

bool ibCheckListBox::IsChecked(unsigned int item) const
{
    wxCHECK_MSG( IsValid(item), false,
                 wxT("invalid index in ibCheckListBox::IsChecked") );

    return m_checks[item] != 0;
}

void ibCheckListBox::Check(unsigned int item, bool check)
{
    wxCHECK_RET( IsValid(item),
                 wxT("invalid index in ibCheckListBox::Check") );

    // intermediate var is needed to avoid compiler warning with VC++
    bool isChecked = m_checks[item] != 0;
    if ( check != isChecked )
    {
        m_checks[item] = check;

        RefreshItem(item);
    }
}

// ----------------------------------------------------------------------------
// methods forwarded to ibListBox
// ----------------------------------------------------------------------------

void ibCheckListBox::DoDeleteOneItem(unsigned int n)
{
    ibListBox::DoDeleteOneItem(n);

    m_checks.RemoveAt(n);
}

void ibCheckListBox::OnItemInserted(unsigned int pos)
{
    m_checks.Insert(false, pos);
}

void ibCheckListBox::DoClear()
{
    ibListBox::DoClear();
    m_checks.Empty();
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

wxSize ibCheckListBox::DoGetBestClientSize() const
{
    wxSize size = ibListBox::DoGetBestClientSize();
    size.x += GetRenderer()->GetCheckBitmapSize().x;

    return size;
}

void ibCheckListBox::DoDrawRange(ibControlRenderer *renderer,
                                 int itemFirst, int itemLast)
{
    renderer->DrawCheckItems(this, itemFirst, itemLast);
}

// ----------------------------------------------------------------------------
// actions
// ----------------------------------------------------------------------------

bool ibCheckListBox::PerformAction(const ibControlAction& action,
                                   long numArg,
                                   const wxString& strArg)
{
    if ( action == ibACTION_CHECKLISTBOX_TOGGLE )
    {
        int sel = (int)numArg;
        if ( sel == -1 )
        {
            sel = GetSelection();
        }

        if ( sel != -1 )
        {
            Check(sel, !IsChecked(sel));

            SendEvent(wxEVT_CHECKLISTBOX, sel);
        }
    }
    else
    {
        return ibListBox::PerformAction(action, numArg, strArg);
    }

    return true;
}

/* static */
ibInputHandler *ibCheckListBox::GetStdInputHandler(ibInputHandler *handlerDef)
{
    static ibStdCheckListboxInputHandler s_handler(handlerDef);

    return &s_handler;
}

// ----------------------------------------------------------------------------
// ibStdCheckListboxInputHandler
// ----------------------------------------------------------------------------

ibStdCheckListboxInputHandler::
ibStdCheckListboxInputHandler(ibInputHandler *inphand)
    : ibStdInputHandler(ibListBox::GetStdInputHandler(inphand))
{
}

bool ibStdCheckListboxInputHandler::HandleKey(ibInputConsumer *consumer,
                                              const wxKeyEvent& event,
                                              bool pressed)
{
    if ( pressed && (event.GetKeyCode() == WXK_SPACE) )
        consumer->PerformAction(ibACTION_CHECKLISTBOX_TOGGLE);

    return ibStdInputHandler::HandleKey(consumer, event, pressed);
}

bool ibStdCheckListboxInputHandler::HandleMouse(ibInputConsumer *consumer,
                                                const wxMouseEvent& event)
{
    if ( event.LeftDown() || event.LeftDClick() )
    {
        ibCheckListBox *lbox = wxStaticCast(consumer->GetInputWindow(), ibCheckListBox);
        int x, y;

        wxPoint pt = event.GetPosition();
        pt -= consumer->GetInputWindow()->GetClientAreaOrigin();
        lbox->CalcUnscrolledPosition(pt.x, pt.y, &x, &y);

        ibRenderer *renderer = lbox->GetRenderer();
        x -= renderer->GetCheckItemMargin();

        int item = y / lbox->GetLineHeight();
        if ( x >= 0 &&
             x < renderer->GetCheckBitmapSize().x &&
             item >= 0 &&
             (unsigned int)item < lbox->GetCount() )
        {
            lbox->PerformAction(ibACTION_CHECKLISTBOX_TOGGLE, item);

            return true;
        }
    }

    return ibStdInputHandler::HandleMouse(consumer, event);
}

#endif // wxUSE_CHECKLISTBOX
