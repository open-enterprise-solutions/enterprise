// OWN uikit control (replaces the FORKED univ version) — see comboBox.h.

#include <wx/wxprec.h>

#include "frontend/uikit/ctrl/comboBox.h"

#if wxUSE_COMBOBOX

#ifndef WX_PRECOMP
    #include <wx/log.h>
    #include <wx/dcclient.h>
    #include <wx/validate.h>
    #include <wx/arrstr.h>
#endif

#include <wx/popupwin.h>

#include "frontend/uikit/theme.h"
#include "frontend/uikit/renderer.h"
#include "frontend/uikit/colourScheme.h"
#include "frontend/uikit/ctrl/listBox.h"
#include "frontend/uikit/ctrl/textCtrl.h"

// ----------------------------------------------------------------------------
// ibComboPopupWindow: the transient window hosting the items listbox
// ----------------------------------------------------------------------------

class ibComboPopupWindow : public wxPopupTransientWindow
{
public:
    ibComboPopupWindow(ibComboBox *combo)
        : m_combo(combo)
    {
        (void)Create(combo, wxBORDER_NONE);

        SetBackgroundColour(wxSCHEME_COLOUR(
            ibThemeEngine::Get()->GetColourScheme(), WINDOW));

        m_lbox = new ibListBox(this, wxID_ANY);
    }

    ibListBox *GetLBox() const { return m_lbox; }

    // show under the combo sized for the current items
    void Popup()
    {
        const wxSize sizeCombo = m_combo->GetSize();

        // the listbox fills the whole popup — its own flat border frames
        // the popup, no extra chrome (an own border strip left dark glitchy
        // edges and dead space around the list)
        const int count = (int)m_lbox->GetCount();
        int hLine = m_lbox->GetLineHeight();
        if ( hLine <= 0 )
            hLine = m_lbox->GetCharHeight();
        if ( hLine <= 0 )
            hLine = 16;

        const int rows = wxMax(1, wxMin(count, 8));
        const wxSize size(sizeCombo.x, rows*hLine + 4 /* border + breathing */);

        SetSize(size);
        m_lbox->SetSize(0, 0, size.x, size.y);

        Position(m_combo->ClientToScreen(wxPoint(0, 0)),
                 wxSize(0, sizeCombo.y));

        wxPopupTransientWindow::Popup();
    }

protected:
    virtual void OnDismiss() override
    {
        m_combo->OnPopupDismiss();
    }

private:
    ibComboBox *m_combo;
    ibListBox *m_lbox;
};

// ----------------------------------------------------------------------------
// ibComboBox creation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibComboBox, ibControl);

wxBEGIN_EVENT_TABLE(ibComboBox, ibControl)
    EVT_LEFT_DOWN(ibComboBox::OnLeftDown)
    EVT_SIZE(ibComboBox::OnSize)
wxEND_EVENT_TABLE()

void ibComboBox::Init()
{
    m_popup = nullptr;
    m_lbox = nullptr;
    m_text = nullptr;
}

ibComboBox::ibComboBox(wxWindow *parent,
                       wxWindowID id,
                       const wxString& value,
                       const wxPoint& pos,
                       const wxSize& size,
                       const wxArrayString& choices,
                       long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    Init();

    (void)Create(parent, id, value, pos, size, choices,
                 style, validator, name);
}

bool ibComboBox::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxString& value,
                        const wxPoint& pos,
                        const wxSize& size,
                        const wxArrayString& choices,
                        long style,
                        const wxValidator& validator,
                        const wxString& name)
{
    return Create(parent, id, value, pos, size,
                  (int)choices.GetCount(),
                  choices.GetCount() ? &choices[0] : nullptr,
                  style, validator, name);
}

bool ibComboBox::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxString& value,
                        const wxPoint& pos,
                        const wxSize& size,
                        int n,
                        const wxString choices[],
                        long style,
                        const wxValidator& validator,
                        const wxString& name)
{
    // the combo looks like a text field — same default border
    if ( (style & wxBORDER_MASK) == 0 )
        style |= wxBORDER_SUNKEN;

    if ( !ibControl::Create(parent, id, pos, size, style, validator, name) )
        return false;

    // the popup with the items listbox exists from the start: the item
    // storage lives in it and every wxItemContainer call delegates there
    m_popup = new ibComboPopupWindow(this);
    m_lbox = m_popup->GetLBox();

    m_lbox->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) {
        OnPopupSelect(event.GetInt());
    });

    for ( int i = 0; i < n; i++ )
        m_lbox->Append(choices[i]);

    // the combo IS a text box + a drop-down list: the value always lives in
    // an ibTextCtrl child (read-only one for wxCB_READONLY)
    m_text = new ibTextCtrl(this, wxID_ANY, value,
                            wxDefaultPosition, wxDefaultSize,
                            wxBORDER_NONE |
                            ((style & wxCB_READONLY) ? wxTE_READONLY : 0));

    // forward the text events under our id
    m_text->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        wxCommandEvent evt(wxEVT_TEXT, GetId());
        evt.SetEventObject(this);
        evt.SetString(event.GetString());
        ProcessWindowEvent(evt);
    });

    if ( style & wxCB_READONLY )
    {
        // a click on the read-only value area opens the list
        m_text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            if ( IsPopupShown() )
                HidePopup();
            else
                ShowPopup();
            Refresh();
        });
    }

    SetInitialSize(size);
    CreateInputHandler(ibINP_HANDLER_COMBOBOX);

    return true;
}

ibComboBox::~ibComboBox()
{
    // m_popup/m_lbox/m_text are our children — destroyed by wxWindow
}

// ----------------------------------------------------------------------------
// value
// ----------------------------------------------------------------------------

wxString ibComboBox::GetValue() const
{
    return m_text->GetValue();
}

void ibComboBox::SetValue(const wxString& value)
{
    m_text->SetValue(value);
}

void ibComboBox::ChangeValue(const wxString& value)
{
    m_text->ChangeValue(value);
}

// ----------------------------------------------------------------------------
// popup
// ----------------------------------------------------------------------------

void ibComboBox::ShowPopup()
{
    if ( GetCount() == 0 || IsPopupShown() )
        return;

    m_popup->Popup();
}

void ibComboBox::HidePopup()
{
    if ( IsPopupShown() )
        m_popup->Dismiss();
}

bool ibComboBox::IsPopupShown() const
{
    return m_popup && m_popup->IsShown();
}

void ibComboBox::OnPopupSelect(int selection)
{
    HidePopup();
    Refresh();

    if ( selection == wxNOT_FOUND )
        return;

    ChangeValue(GetString(selection));

    SendComboEvent();
}

void ibComboBox::OnPopupDismiss()
{
    // refresh the drop button which was drawn pressed while shown
    Refresh();
}

void ibComboBox::SendComboEvent()
{
    wxCommandEvent event(wxEVT_COMBOBOX, GetId());
    event.SetEventObject(this);
    event.SetInt(GetSelection());
    event.SetString(GetValue());
    ProcessWindowEvent(event);
}

// ----------------------------------------------------------------------------
// wxItemContainer — delegated to the popup listbox
// ----------------------------------------------------------------------------

void ibComboBox::DoClear()
{
    m_lbox->Clear();
}

void ibComboBox::DoDeleteOneItem(unsigned int n)
{
    m_lbox->Delete(n);
}

unsigned int ibComboBox::GetCount() const
{
    return m_lbox->GetCount();
}

wxString ibComboBox::GetString(unsigned int n) const
{
    return m_lbox->GetString(n);
}

void ibComboBox::SetString(unsigned int n, const wxString& s)
{
    m_lbox->SetString(n, s);
}

int ibComboBox::FindString(const wxString& s, bool bCase) const
{
    return m_lbox->FindString(s, bCase);
}

void ibComboBox::SetSelection(int n)
{
    m_lbox->SetSelection(n);

    if ( n >= 0 && n < (int)GetCount() )
        ChangeValue(GetString(n));
}

int ibComboBox::GetSelection() const
{
    return m_lbox->GetSelection();
}

wxString ibComboBox::GetStringSelection() const
{
    const int sel = GetSelection();
    return sel == wxNOT_FOUND ? wxString() : GetString(sel);
}

wxClientDataType ibComboBox::GetClientDataType() const
{
    return m_lbox->GetClientDataType();
}

void ibComboBox::SetClientDataType(wxClientDataType clientDataItemsType)
{
    m_lbox->SetClientDataType(clientDataItemsType);
}

int ibComboBox::DoInsertItems(const wxArrayStringsAdapter& items,
                              unsigned int pos,
                              void **clientData, wxClientDataType type)
{
    int index = wxNOT_FOUND;
    for ( size_t i = 0; i < items.GetCount(); i++ )
    {
        index = m_lbox->Insert(items[i], pos + i);
        if ( clientData )
        {
            // NB: qualified — wxEvtHandler has same-named methods (seam 17)
            if ( type == wxClientData_Object )
                m_lbox->wxItemContainer::SetClientObject(index,
                    (wxClientData *)clientData[i]);
            else if ( type == wxClientData_Void )
                m_lbox->wxItemContainer::SetClientData(index, clientData[i]);
        }
    }

    return index;
}

void ibComboBox::DoSetItemClientData(unsigned int n, void* clientData)
{
    m_lbox->wxItemContainer::SetClientData(n, clientData);
}

void* ibComboBox::DoGetItemClientData(unsigned int n) const
{
    return m_lbox->wxItemContainer::GetClientData(n);
}

// ----------------------------------------------------------------------------
// geometry and drawing
// ----------------------------------------------------------------------------

wxRect ibComboBox::GetButtonRect() const
{
    const wxSize size = GetClientSize();
    const int w = wxMin(size.y, size.x);
    return wxRect(size.x - w, 0, w, size.y);
}

wxRect ibComboBox::GetValueRect() const
{
    // the text child pads its own content — give it the full area left of
    // the drop button (a vertical deflate here clipped the value line)
    const wxSize size = GetClientSize();
    return wxRect(0, 0, size.x - GetButtonRect().width - 1, size.y);
}

wxSize ibComboBox::DoGetBestClientSize() const
{
    // the height is dictated by the text child (its font + its paddings);
    // the width is uniform like the text field's, plus the square button
    const wxSize sizeText = m_text ? m_text->GetBestSize()
                                   : wxSize(100, GetCharHeight() + 8);

    return wxSize(sizeText.x + sizeText.y, sizeText.y);
}

void ibComboBox::OnSize(wxSizeEvent& event)
{
    if ( m_text )
    {
        const wxRect rect = GetValueRect();
        m_text->SetSize(rect.x, rect.y, rect.width, rect.height);
    }

    event.Skip();
}

void ibComboBox::DoDraw(ibControlRenderer *renderer)
{
    wxDC& dc = renderer->GetDC();
    ibRenderer *rend = renderer->GetRenderer();

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

void ibComboBox::OnLeftDown(wxMouseEvent& event)
{
    // a click on the drop button (the only part of the host not covered by
    // the text child) toggles the popup
    if ( IsPopupShown() )
    {
        HidePopup();
        Refresh();
    }
    else if ( GetButtonRect().Contains(event.GetPosition()) )
    {
        ShowPopup();
        Refresh();
    }
    else
    {
        event.Skip();
    }
}

bool ibComboBox::PerformAction(const ibControlAction& action,
                               long numArg,
                               const wxString& strArg)
{
    if ( action == ibACTION_COMBOBOX_POPUP )
        ShowPopup();
    else if ( action == ibACTION_COMBOBOX_DISMISS )
        HidePopup();
    else if ( action == ibACTION_COMBOBOX_SELECT_NEXT )
    {
        const int sel = GetSelection();
        if ( sel + 1 < (int)GetCount() )
        {
            SetSelection(sel + 1);
            SendComboEvent();
        }
    }
    else if ( action == ibACTION_COMBOBOX_SELECT_PREV )
    {
        const int sel = GetSelection();
        if ( sel > 0 )
        {
            SetSelection(sel - 1);
            SendComboEvent();
        }
    }
    else if ( action == ibACTION_COMBOBOX_SELECT )
    {
        if ( numArg >= 0 && numArg < (long)GetCount() )
        {
            SetSelection((int)numArg);
            SendComboEvent();
        }
    }
    else
        return ibControl::PerformAction(action, numArg, strArg);

    return true;
}

/* static */
ibInputHandler *ibComboBox::GetStdInputHandler(ibInputHandler *handlerDef)
{
    // mouse handling is done by our own event handlers; keyboard goes to the
    // editable child — the default control chain is enough here
    return handlerDef;
}

#endif // wxUSE_COMBOBOX
