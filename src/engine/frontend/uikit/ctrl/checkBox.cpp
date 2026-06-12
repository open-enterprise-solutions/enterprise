// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/checkbox.cpp
// Purpose:     ibCheckBox implementation
// Author:      Vadim Zeitlin
// Created:     25.08.00
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

#include "frontend/uikit/ctrl/checkBox.h"
#include "frontend/uikit/ctrl/button.h"


#if wxUSE_CHECKBOX

#include <wx/checkbox.h>

#ifndef WX_PRECOMP
    #include <wx/dcclient.h>
    #include <wx/validate.h>

    #include <wx/button.h> // for ibACTION_BUTTON_XXX
#endif

#include "frontend/uikit/theme.h"
#include "frontend/uikit/renderer.h"
#include "frontend/uikit/inputHandler.h"
#include "frontend/uikit/colourScheme.h"

// ----------------------------------------------------------------------------
// ibStdCheckboxInputHandler: handles the mouse events for the check and radio
// boxes (handling the keyboard input is simple, but its handling differs a
// lot between GTK and MSW, so a new class should be derived for this)
// ----------------------------------------------------------------------------

class FRONTEND_API ibStdCheckboxInputHandler : public ibStdInputHandler
{
public:
    ibStdCheckboxInputHandler(ibInputHandler *inphand);

    // we have to override this one as ibStdButtonInputHandler version works
    // only with the buttons
    virtual bool HandleActivation(ibInputConsumer *consumer, bool activated);
};

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibCheckBox
// ----------------------------------------------------------------------------

void ibCheckBox::Init()
{
    m_isPressed = false;
    m_status = Status_Unchecked;
}

bool ibCheckBox::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxString &label,
                        const wxPoint &pos,
                        const wxSize &size,
                        long style,
                        const wxValidator& validator,
                        const wxString &name)
{
    WXValidateStyle( &style );
    if ( !ibControl::Create(parent, id, pos, size, style, validator, name) )
        return false;

    SetLabel(label);
    SetInitialSize(size);

    CreateInputHandler(ibINP_HANDLER_CHECKBOX);

    return true;
}

// ----------------------------------------------------------------------------
// checkbox interface
// ----------------------------------------------------------------------------

bool ibCheckBox::GetValue() const
{
    return (Get3StateValue() != wxCHK_UNCHECKED);
}

void ibCheckBox::SetValue(bool value)
{
    Set3StateValue( value ? wxCHK_CHECKED : wxCHK_UNCHECKED );
}

void ibCheckBox::OnCheck()
{
    // we do nothing here
}

// ----------------------------------------------------------------------------
// indicator bitmaps
// ----------------------------------------------------------------------------

wxBitmap ibCheckBox::GetBitmap(State state, Status status) const
{
    wxBitmap bmp = m_bitmaps[state][status];
    if ( !bmp.IsOk() )
        bmp = m_bitmaps[State_Normal][status];

    return bmp;
}

void ibCheckBox::SetBitmap(const wxBitmap& bmp, State state, Status status)
{
    m_bitmaps[state][status] = bmp;
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

ibCheckBox::State ibCheckBox::GetState(int flags) const
{
    if ( flags & wxCONTROL_DISABLED )
        return State_Disabled;
    else if ( flags & wxCONTROL_PRESSED )
        return State_Pressed;
    else if ( flags & wxCONTROL_CURRENT )
        return State_Current;
    else
        return State_Normal;
}

void ibCheckBox::DoDraw(ibControlRenderer *renderer)
{
    int flags = GetStateFlags();

    wxDC& dc = renderer->GetDC();
    dc.SetFont(GetFont());
    dc.SetTextForeground(GetForegroundColour());

    switch ( Get3StateValue() )
    {
        case wxCHK_CHECKED:      flags |= wxCONTROL_CHECKED;      break;
        case wxCHK_UNDETERMINED: flags |= wxCONTROL_UNDETERMINED; break;
        default:                 /* do nothing */                 break;
    }

    wxBitmap bitmap(GetBitmap(GetState(flags), m_status));

    renderer->GetRenderer()->
        DrawCheckButton(dc,
                        GetLabelText(),
                        bitmap,
                        renderer->GetRect(),
                        flags,
                        GetWindowStyle() & wxALIGN_RIGHT ? wxALIGN_RIGHT
                                                         : wxALIGN_LEFT,
                        GetAccelIndex());
}

// ----------------------------------------------------------------------------
// geometry calculations
// ----------------------------------------------------------------------------

wxSize ibCheckBox::GetBitmapSize() const
{
    wxBitmap bmp = GetBitmap(State_Normal, Status_Checked);
    return bmp.IsOk() ? wxSize(bmp.GetWidth(), bmp.GetHeight())
                    : GetRenderer()->GetCheckBitmapSize();
}

wxSize ibCheckBox::DoGetBestClientSize() const
{
    wxInfoDC dc(wxConstCast(this, ibCheckBox));
    wxCoord width, height;
    dc.GetMultiLineTextExtent(GetLabel(), &width, &height);

    wxSize sizeBmp = GetBitmapSize();
    if ( height < sizeBmp.y )
        height = sizeBmp.y;

#if defined(wxUNIV_COMPATIBLE_MSW) && wxUNIV_COMPATIBLE_MSW
    // FIXME: flag nowhere defined so perhaps should be removed?

    // this looks better but is different from what wxMSW does
    height += GetCharHeight()/2;
#endif // wxUNIV_COMPATIBLE_MSW

    width += sizeBmp.x + 2*GetCharWidth();

    return wxSize(width, height);
}

// ----------------------------------------------------------------------------
// checkbox actions
// ----------------------------------------------------------------------------

void ibCheckBox::DoSet3StateValue(wxCheckBoxState state)
{
    Status status;
    switch ( state )
    {
        case wxCHK_UNCHECKED:    status = Status_Unchecked;   break;
        case wxCHK_CHECKED:      status = Status_Checked; break;
        default:                 wxFAIL_MSG(wxT("Unknown checkbox state"));
        wxFALLTHROUGH;
        case wxCHK_UNDETERMINED: status = Status_3rdState;  break;
    }
    if ( status != m_status )
    {
        m_status = status;

        if ( m_status == Status_Checked )
        {
            // invoke the hook
            OnCheck();
        }

        Refresh();
    }
}

wxCheckBoxState ibCheckBox::DoGet3StateValue() const
{
    switch ( m_status )
    {
        case Status_Checked:    return wxCHK_CHECKED;
        case Status_Unchecked:  return wxCHK_UNCHECKED;
        default:                /* go further */ break;
    }
    return wxCHK_UNDETERMINED;
}

void ibCheckBox::Press()
{
    if ( !m_isPressed )
    {
        m_isPressed = true;

        Refresh();
    }
}

void ibCheckBox::Release()
{
    if ( m_isPressed )
    {
        m_isPressed = false;

        Refresh();
    }
}

void ibCheckBox::Toggle()
{
    m_isPressed = false;

    Status status = m_status;

    switch ( Get3StateValue() )
    {
        case wxCHK_CHECKED:
            Set3StateValue(Is3rdStateAllowedForUser() ? wxCHK_UNDETERMINED : wxCHK_UNCHECKED);
            break;

        case wxCHK_UNCHECKED:
            Set3StateValue(wxCHK_CHECKED);
            break;

        case wxCHK_UNDETERMINED:
            Set3StateValue(wxCHK_UNCHECKED);
            break;
    }

    if( status != m_status )
        SendEvent();
}

void ibCheckBox::ChangeValue(bool value)
{
    SetValue(value);

    SendEvent();
}

void ibCheckBox::SendEvent()
{
    wxCommandEvent event(wxEVT_CHECKBOX, GetId());
    InitCommandEvent(event);
    wxCheckBoxState state = Get3StateValue();

    // If the style flag to allow the user setting the undetermined state
    // is not set, then skip the undetermined state and set it to unchecked.
    if ( state == wxCHK_UNDETERMINED && !Is3rdStateAllowedForUser() )
    {
        state = wxCHK_UNCHECKED;
        Set3StateValue(state);
    }

    event.SetInt(state);
    Command(event);
}

// ----------------------------------------------------------------------------
// input handling
// ----------------------------------------------------------------------------

bool ibCheckBox::PerformAction(const ibControlAction& action,
                               long numArg,
                               const wxString& strArg)
{
    if ( action == ibACTION_BUTTON_PRESS )
        Press();
    else if ( action == ibACTION_BUTTON_RELEASE )
        Release();
    if ( action == ibACTION_CHECKBOX_CHECK )
        ChangeValue(true);
    else if ( action == ibACTION_CHECKBOX_CLEAR )
        ChangeValue(false);
    else if ( action == ibACTION_CHECKBOX_TOGGLE )
        Toggle();
    else
        return ibControl::PerformAction(action, numArg, strArg);

    return true;
}

/* static */
ibInputHandler *ibCheckBox::CreateStdInputHandler(ibInputHandler *handlerDef)
{
    static ibStdCheckboxInputHandler s_handler(handlerDef);

    return &s_handler;
}

// ----------------------------------------------------------------------------
// ibStdCheckboxInputHandler
// ----------------------------------------------------------------------------

ibStdCheckboxInputHandler::ibStdCheckboxInputHandler(ibInputHandler *def)
                         : ibStdInputHandler(ibButton::GetStdInputHandler(def))
{
}

bool ibStdCheckboxInputHandler::HandleActivation(ibInputConsumer *consumer,
                                                 bool WXUNUSED(activated))
{
    // only the focused checkbox appearance changes when the app gains/loses
    // activation
    return consumer->GetInputWindow()->IsFocused();
}

wxIMPLEMENT_DYNAMIC_CLASS(ibCheckBox, ibControl);

#endif // wxUSE_CHECKBOX
