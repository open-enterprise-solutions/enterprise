// OWN deskgui control — see comboCtrl.h.

#include <wx/wxprec.h>

#include "deskgui/univ/ctrl/comboCtrl.h"

#if wxUSE_COMBOBOX

#ifndef WX_PRECOMP
    #include <wx/dcclient.h>
    #include <wx/validate.h>
#endif

#include <wx/popupwin.h>
#include <wx/display.h>

#include "deskgui/univ/theme.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/ctrl/textCtrl.h"

// ----------------------------------------------------------------------------
// ibComboPopup
// ----------------------------------------------------------------------------

void ibComboPopup::SetStringValue(const wxString& WXUNUSED(value))
{
}

void ibComboPopup::OnPopup()
{
}

void ibComboPopup::PaintComboControl(wxDC& WXUNUSED(dc), const wxRect& WXUNUSED(rect))
{
}

void ibComboPopup::OnComboDoubleClick()
{
}

// ----------------------------------------------------------------------------
// ibComboPopupWindow: the transient window hosting the popup's control
// ----------------------------------------------------------------------------

class ibComboPopupWindow : public wxPopupTransientWindow
{
public:
    explicit ibComboPopupWindow(ibComboCtrl *combo)
        : m_combo(combo)
    {
        (void)Create(combo, wxBORDER_NONE);

        SetBackgroundColour(wxTHEME_COLOUR(WINDOW));
    }

    // show under the combo, the popup's control filling the window - its own border frames it, no extra chrome (an
    // own border strip left dark glitchy edges and dead space around the list)
    void Popup()
    {
        ibComboPopup *popup = m_combo->GetPopupControl();
        const wxSize sizeCombo = m_combo->GetSize();

        // the popup's room: half the screen the combo is on
        const wxRect rectDisplay = wxDisplay(m_combo).GetClientArea();
        const wxSize size = popup->GetAdjustedSize(sizeCombo.x, wxDefaultCoord, rectDisplay.height / 2);

        SetSize(size);
        popup->GetControl()->SetSize(0, 0, size.x, size.y);
        popup->OnPopup();

        // SEAM vs wxPopupWindow::Position: placed as wx's wxComboCtrlBase::ShowPopup places it - its left edge on the
        // combo's, its right on the combo's when wider than the room to the right (Position put a window overflowing
        // the screen wholly to the left of the point: a list as wide as its items opened beside a grid's combo at the
        // screen's edge), above the combo when there is no room below
        const wxRect rectCombo = m_combo->GetScreenRect();
        int x = rectCombo.x;
        if ( x + size.x > rectDisplay.GetRight() + 1 )
            x = wxMax(rectCombo.GetRight() + 1 - size.x, rectDisplay.x);
        int y = rectCombo.GetBottom() + 1;
        if ( y + size.y > rectDisplay.GetBottom() + 1 && rectCombo.y - size.y >= rectDisplay.y )
            y = rectCombo.y - size.y;
        Move(x, y, wxSIZE_NO_ADJUSTMENTS);

        // the keys go to the popup's control: a list walks its items, Enter chooses one
        wxPopupTransientWindow::Popup(popup->GetControl());
    }

protected:
    virtual void OnDismiss() override
    {
        m_combo->OnPopupDismiss();
    }

private:
    ibComboCtrl *m_combo;
};

// ----------------------------------------------------------------------------
// ibComboCtrl creation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibComboCtrl, ibControl);

wxBEGIN_EVENT_TABLE(ibComboCtrl, ibControl)
    EVT_PAINT(ibComboCtrl::OnPaint)
    EVT_LEFT_DOWN(ibComboCtrl::OnLeftDown)
    EVT_LEFT_DCLICK(ibComboCtrl::OnLeftDClick)
    EVT_SET_FOCUS(ibComboCtrl::OnSetFocus)
    EVT_SIZE(ibComboCtrl::OnSize)
wxEND_EVENT_TABLE()

void ibComboCtrl::Init()
{
    m_winPopup = nullptr;
    m_popup = nullptr;
    m_text = nullptr;
    m_widthCustomPaint = 0;
}

bool ibComboCtrl::Create(wxWindow *parent,
                         wxWindowID id,
                         const wxString& value,
                         const wxPoint& pos,
                         const wxSize& size,
                         long style,
                         const wxValidator& validator,
                         const wxString& name)
{
    // the combo looks like a text field — same default border
    if ( (style & wxBORDER_MASK) == 0 )
        style |= wxBORDER_SUNKEN;

    if ( !ibControl::Create(parent, id, pos, size, style, validator, name) )
        return false;

    // the combo IS a text box + a drop-down: the value always lives in
    // an ibTextCtrl child (read-only one for wxCB_READONLY) - born in its
    // value area: a combo created at its final size (a cell's editor) gets no
    // EVT_SIZE after this, and the child's own best width covered the button
    const wxRect rectText = GetTextRect();
    m_text = new ibTextCtrl(this, wxID_ANY, value,
                            rectText.GetPosition(), rectText.GetSize(),
                            wxBORDER_NONE |
                            ((style & wxCB_READONLY) ? wxTE_READONLY : 0) |
                            (style & wxTE_PROCESS_ENTER));

    // forward the text events under our id
    const auto forwardText = [this](wxCommandEvent& event) {
        wxCommandEvent evt(event.GetEventType(), GetId());
        evt.SetEventObject(this);
        evt.SetString(event.GetString());
        ProcessWindowEvent(evt);
    };
    m_text->Bind(wxEVT_TEXT, forwardText);
    m_text->Bind(wxEVT_TEXT_ENTER, forwardText);

    // the keys pressed in the text are the combo's first (wx's wxComboBoxExtraInputHandler): its input handler walks
    // the items, a cell's grid moves on with Tab and Enter - what it leaves goes to the text; Tab never does
    m_text->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        wxKeyEvent redirected(event);
        redirected.SetId(GetId());
        redirected.SetEventObject(this);
        if ( !GetEventHandler()->ProcessEvent(redirected) && event.GetKeyCode() != WXK_TAB )
            event.Skip();
    });

    if ( style & wxCB_READONLY )
    {
        // a click on the read-only value opens the drop-down - unless a double click steps the value
        // (wxCC_SPECIAL_DCLICK): then the button opens it
        m_text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
            if ( HasFlag(wxCC_SPECIAL_DCLICK) )
            {
                event.Skip();
                return;
            }
            if ( IsPopupShown() )
                HidePopup();
            else
                ShowPopup();
        });
        m_text->Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& event) {
            if ( HasFlag(wxCC_SPECIAL_DCLICK) && m_popup )
                m_popup->OnComboDoubleClick();
            else
                event.Skip();
        });
    }

    SetInitialSize(size);
    CreateInputHandler(ibINP_HANDLER_COMBOBOX);

    return true;
}

ibComboCtrl::~ibComboCtrl()
{
    // m_winPopup/m_text are our children — destroyed by wxWindow, the popup's control with m_winPopup
}

void ibComboCtrl::SetPopupControl(ibComboPopup* popup)
{
    wxCHECK_RET( !m_popup, wxT("ibComboCtrl: the popup is set once") );

    m_popup = popup;
    m_popup->m_combo = this;

    m_winPopup = new ibComboPopupWindow(this);
    m_popup->Create(m_winPopup);
}

// ----------------------------------------------------------------------------
// value
// ----------------------------------------------------------------------------

wxString ibComboCtrl::GetValue() const
{
    return m_text->GetValue();
}

// the popup follows the value set - a list's chosen item is the value's, the strip shows its picture (wx's
// wxComboCtrlBase::OnSetValue)
void ibComboCtrl::SetValue(const wxString& value)
{
    m_text->SetValue(value);
    if ( m_popup )
        m_popup->SetStringValue(value);
    Refresh();
}

void ibComboCtrl::ChangeValue(const wxString& value)
{
    m_text->ChangeValue(value);
    if ( m_popup )
        m_popup->SetStringValue(value);
    Refresh();
}

void ibComboCtrl::SetInsertionPointEnd()
{
    m_text->SetInsertionPointEnd();
}

// ----------------------------------------------------------------------------
// popup
// ----------------------------------------------------------------------------

void ibComboCtrl::ShowPopup()
{
    if ( !m_popup || IsPopupShown() )
        return;

    m_popup->SetStringValue(GetValue());
    m_winPopup->Popup();

    // the drop button is drawn pressed while shown
    Refresh();
}

void ibComboCtrl::HidePopup()
{
    if ( !IsPopupShown() )
        return;

    m_winPopup->Dismiss();
    Refresh();
}

bool ibComboCtrl::IsPopupShown() const
{
    return m_winPopup && m_winPopup->IsShown();
}

void ibComboCtrl::OnPopupDismiss()
{
    // refresh the drop button which was drawn pressed while shown
    Refresh();
}

// ----------------------------------------------------------------------------
// geometry and drawing
// ----------------------------------------------------------------------------

wxRect ibComboCtrl::GetButtonRect() const
{
    const wxSize size = GetClientSize();
    const int w = wxMin(size.y, size.x);
    return wxRect(size.x - w, 0, w, size.y);
}

wxRect ibComboCtrl::GetTextRect() const
{
    // the text child pads its own content — give it the full area left of
    // the drop button (a vertical deflate here clipped the value line)
    const wxSize size = GetClientSize();
    return wxRect(0, 0, size.x - GetButtonRect().width - 1, size.y);
}

void ibComboCtrl::SetCustomPaintWidth(int width)
{
    m_widthCustomPaint = width;
    PositionTextCtrl();
    Refresh();
}

void ibComboCtrl::PositionTextCtrl()
{
    if ( !m_text )
        return;

    wxRect rect = GetTextRect();
    rect.x += m_widthCustomPaint;
    rect.width -= m_widthCustomPaint;
    m_text->SetSize(rect.x, rect.y, rect.width, rect.height);
}

wxSize ibComboCtrl::DoGetBestClientSize() const
{
    // the height is dictated by the text child (its font + its paddings);
    // the width is uniform like the text field's, plus the square button
    const wxSize sizeText = m_text ? m_text->GetBestSize()
                                   : wxSize(100, GetCharHeight() + 8);

    return wxSize(m_widthCustomPaint + sizeText.x + sizeText.y, sizeText.y);
}

void ibComboCtrl::OnSize(wxSizeEvent& event)
{
    PositionTextCtrl();

    event.Skip();
}

void ibComboCtrl::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    ibThemePaintDC dc(this);

    ibRenderer *rend = GetRenderer();

    // the strip left of the text: the popup paints the value's picture there, clipped to it (wx's wxGenericComboCtrl)
    if ( m_popup && m_widthCustomPaint > 0 )
    {
        wxRect rectStrip = GetTextRect();
        rectStrip.width = m_widthCustomPaint;

        wxDCClipper clip(dc, rectStrip);
        dc.SetFont(GetFont());
        m_popup->PaintComboControl(dc, rectStrip);
    }

    const wxRect rectButton = GetButtonRect();

    // the value area is covered by the ibTextCtrl child — only the drop
    // button is ours: flat pad + separator + arrow
    int flags = GetStateFlags();
    if ( IsPopupShown() )
        flags |= wxCONTROL_PRESSED;

    ibColourScheme* scheme = ibThemeEngine::Get()->GetColourScheme();

    wxColour pad = scheme->Get(ibColourScheme::CONTROL);
    if ( flags & wxCONTROL_PRESSED )
        pad = scheme->Get(ibColourScheme::CONTROL_PRESSED);
    else if ( flags & wxCONTROL_CURRENT )
        pad = scheme->Get(ibColourScheme::CONTROL_CURRENT);

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(pad));
    dc.DrawRectangle(rectButton);

    // 1px separator between the value area and the button
    dc.SetPen(wxPen(scheme->Get(ibColourScheme::SHADOW_IN)));
    dc.DrawLine(rectButton.x, rectButton.y,
                rectButton.x, rectButton.y + rectButton.height);

    wxRect rectArrow = rectButton;
    rectArrow.Deflate(rectButton.width / 3, rectButton.height / 3);
    rend->DrawArrow(dc, wxDOWN, rectArrow, flags);
}

// ----------------------------------------------------------------------------
// input
// ----------------------------------------------------------------------------

void ibComboCtrl::OnLeftDown(wxMouseEvent& event)
{
    // a click on the drop button - or, read-only, on the strip: the parts of the host not covered by the text child -
    // toggles the popup
    const wxPoint pt = event.GetPosition();
    if ( IsPopupShown() )
        HidePopup();
    else if ( GetButtonRect().Contains(pt) ||
              (!IsEditable() && !HasFlag(wxCC_SPECIAL_DCLICK) && GetTextRect().Contains(pt)) )
        ShowPopup();
    else
        event.Skip();
}

void ibComboCtrl::OnLeftDClick(wxMouseEvent& event)
{
    const wxPoint pt = event.GetPosition();
    if ( m_popup && !IsEditable() && HasFlag(wxCC_SPECIAL_DCLICK) && GetTextRect().Contains(pt) )
        m_popup->OnComboDoubleClick();
    else
        event.Skip();
}

// the focus given to an editable host is its text's - the keys are typed there: a grid's editor, a date picker's part
// reached by Tab (wx's wxComboCtrlBase::OnFocusEvent, its main window); a read-only host keeps it - wx's has no text,
// its value drawn, and the read-only text's caret stood in a choice
void ibComboCtrl::OnSetFocus(wxFocusEvent& event)
{
    event.Skip();

    if ( IsEditable() && m_text && !m_text->HasFocus() )
        m_text->SetFocus();
}

bool ibComboCtrl::PerformAction(const ibControlAction& action,
                                long numArg,
                                const wxString& strArg)
{
    if ( action == ibACTION_COMBOBOX_POPUP )
        ShowPopup();
    else if ( action == ibACTION_COMBOBOX_DISMISS )
        HidePopup();
    else
        return ibControl::PerformAction(action, numArg, strArg);

    return true;
}

/* static */
ibInputHandler *ibComboCtrl::GetStdInputHandler(ibInputHandler *handlerDef)
{
    // mouse handling is done by our own event handlers; keyboard goes to the
    // editable child — the default control chain is enough here
    return handlerDef;
}

#endif // wxUSE_COMBOBOX
