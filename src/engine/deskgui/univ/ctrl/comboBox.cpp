// OWN deskgui control (replaces the FORKED univ version) — see comboBox.h.

#include <wx/wxprec.h>

#include "deskgui/univ/ctrl/comboBox.h"

#if wxUSE_COMBOBOX

#ifndef WX_PRECOMP
    #include <wx/log.h>
    #include <wx/dcclient.h>
    #include <wx/validate.h>
    #include <wx/arrstr.h>
    #include <wx/settings.h>
#endif

#include "deskgui/univ/theme.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/ctrl/vListBox.h"
#include "deskgui/univ/ctrl/textCtrl.h"   // the chosen item's text, put straight into the field (SetSelection)

// ----------------------------------------------------------------------------
// ibVListBoxComboPopup: the combo box's drop-down (wx's wxVListBoxComboPopup)
// ----------------------------------------------------------------------------

// The list keeps no items: it is told their count and asks the combo to draw each (ibComboBox::OnDrawItem...). Its
// own selection is the item under the mouse or the keys while it is open; the combo's - the chosen one - changes by a
// choice (a click, Enter, a double click stepping the value) and by a value set that is an item's text. Closed
// without a choice, the chosen item stays.
class ibVListBoxComboPopup : public ibVListBox, public ibComboPopup
{
public:
    explicit ibVListBoxComboPopup(ibComboBox *combo)
        : m_comboBox(combo)
    {
    }

    // ibComboPopup
    virtual bool Create(wxWindow* parent) override
    {
        if ( !ibVListBox::Create(parent, wxID_ANY) )
            return false;

        SetFont(m_comboBox->GetFont());

        Bind(wxEVT_MOTION, &ibVListBoxComboPopup::OnMouseMove, this);
        Bind(wxEVT_LEFT_UP, &ibVListBoxComboPopup::OnLeftUp, this);
        Bind(wxEVT_KEY_DOWN, &ibVListBoxComboPopup::OnKey, this);
        return true;
    }

    virtual wxWindow* GetControl() override { return this; }

    virtual void SetStringValue(const wxString& value) override
    {
        // the chosen item is the value's (wx's wxVListBoxComboPopup::SetStringValue): kept when it shows the value
        // already - one of identical items -, else the item of the value; a text of no item keeps the chosen one
        const int selection = m_comboBox->m_selection;
        if ( selection != wxNOT_FOUND && m_comboBox->GetString(selection) == value )
            return;

        const int item = m_comboBox->FindString(value, true);
        if ( item != wxNOT_FOUND )
            m_comboBox->m_selection = item;
    }

    virtual wxSize GetAdjustedSize(int minWidth, int WXUNUSED(prefHeight), int maxHeight) override
    {
        // as many rows as there are, up to eight - the drop-down the combo had; one empty row for no items
        const size_t count = m_comboBox->GetCount();
        const size_t rows = wxMin(count, (size_t)8);

        int height = 0;
        for ( size_t n = 0; n < rows; n++ )
            height += m_comboBox->OnMeasureItem(n);
        if ( count == 0 )
            height = m_comboBox->GetRenderer()->GetListboxItemHeight(GetCharHeight());

        // as wide as the combo - wider, if an item needs it, with the scroll bar's room when the rows overflow
        int width = 0;
        for ( size_t n = 0; n < count; n++ )
            width = wxMax(width, (int)m_comboBox->OnMeasureItemWidth(n));
        if ( count > rows )
            width += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, this);

        const wxSize border = GetWindowBorderSize();
        return wxSize(wxMax(minWidth, width + border.x),
                      wxMin(height + border.y, maxHeight));
    }

    virtual void OnPopup() override
    {
        // the combo's items, counted as the list drops down: told the count, the list measures its rows - asks the
        // combo's OnMeasureItem, which a derived combo answers only once made (the grid's needs its property, set after
        // ibComboBox::Create); wx made the list at the first drop-down (wxVListBoxComboPopup::LazyCreate) and marked
        // the chosen item once sized
        SetItemCount(m_comboBox->GetCount());
        SetSelection(m_comboBox->GetSelection());
    }

    virtual void PaintComboControl(wxDC& dc, const wxRect& rect) override
    {
        const int item = m_comboBox->GetSelection();
        m_comboBox->OnDrawBackground(dc, rect, item, wxODCB_PAINTING_CONTROL);
        if ( item != wxNOT_FOUND )
            m_comboBox->OnDrawItem(dc, rect, item, wxODCB_PAINTING_CONTROL);
    }

    virtual void OnComboDoubleClick() override
    {
        // the value steps to the next item, the last to the first (wx's wxODCB_DCLICK_CYCLES)
        const int count = (int)m_comboBox->GetCount();
        if ( count > 0 )
            m_comboBox->OnPopupSelect((m_comboBox->GetSelection() + 1) % count);
    }

protected:
    // ibVListBox
    virtual void OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const override
    {
        m_comboBox->OnDrawItem(dc, rect, (int)n, IsSelected(n) ? wxODCB_PAINTING_SELECTED : 0);
    }

    virtual wxCoord OnMeasureItem(size_t n) const override
    {
        return m_comboBox->OnMeasureItem(n);
    }

    virtual void OnDrawBackground(wxDC& dc, const wxRect& rect, size_t n) const override
    {
        m_comboBox->OnDrawBackground(dc, rect, (int)n, IsSelected(n) ? wxODCB_PAINTING_SELECTED : 0);
    }

private:
    int HitItem(const wxMouseEvent& event) const
    {
        return VirtualHitTest(event.GetPosition().y);
    }

    // the item under the mouse is the list's selection - the one Enter would choose
    void OnMouseMove(wxMouseEvent& event)
    {
        const int item = HitItem(event);
        if ( item != wxNOT_FOUND && item != GetSelection() )
            SetSelection(item);
        event.Skip();
    }

    void OnLeftUp(wxMouseEvent& event)
    {
        const int item = HitItem(event);
        if ( item != wxNOT_FOUND )
            m_comboBox->OnPopupSelect(item);
        else
            event.Skip();
    }

    // Enter chooses the list's item, Esc closes it as it was; the arrows walk it (ibVListBox::OnKeyDown)
    void OnKey(wxKeyEvent& event)
    {
        switch ( event.GetKeyCode() )
        {
            case WXK_RETURN:
            case WXK_NUMPAD_ENTER:
                if ( GetSelection() != wxNOT_FOUND )
                    m_comboBox->OnPopupSelect(GetSelection());
                else
                    m_comboBox->HidePopup();
                break;

            case WXK_ESCAPE:
                m_comboBox->HidePopup();
                break;

            default:
                event.Skip();
        }
    }

    ibComboBox *m_comboBox;
};

// ----------------------------------------------------------------------------
// ibComboBox creation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibComboBox, ibComboCtrl);

void ibComboBox::Init()
{
    m_list = nullptr;
    m_selection = wxNOT_FOUND;
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
    if ( !ibComboCtrl::Create(parent, id, value, pos, size, style, validator, name) )
        return false;

    // the list exists from the start: it shows the items kept here
    m_list = new ibVListBoxComboPopup(this);
    SetPopupControl(m_list);

    for ( int i = 0; i < n; i++ )
        Append(choices[i]);

    // the value given is the chosen item when it is one's text (wx's wxVListBoxComboPopup::Populate)
    if ( !value.empty() )
        m_list->SetStringValue(value);

    return true;
}

ibComboBox::~ibComboBox()
{
    // the list is the host's popup — destroyed with it; the client objects are wxItemContainer's
}

// ----------------------------------------------------------------------------
// the choice
// ----------------------------------------------------------------------------

void ibComboBox::OnPopupSelect(int selection)
{
    HidePopup();

    if ( selection == wxNOT_FOUND )
        return;

    SetSelection(selection);

    SendComboEvent();
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
// wxItemContainer — the items kept here, the list told their count
// ----------------------------------------------------------------------------

void ibComboBox::DoClear()
{
    m_strings.Clear();
    m_clientData.clear();
    m_selection = wxNOT_FOUND;
}

void ibComboBox::DoDeleteOneItem(unsigned int n)
{
    m_strings.RemoveAt(n);
    m_clientData.erase(m_clientData.begin() + n);

    if ( m_selection == (int)n )
        m_selection = wxNOT_FOUND;
    else if ( m_selection > (int)n )
        m_selection--;
}

unsigned int ibComboBox::GetCount() const
{
    return m_strings.GetCount();
}

wxString ibComboBox::GetString(unsigned int n) const
{
    wxCHECK_MSG( n < m_strings.GetCount(), wxString(), wxT("ibComboBox::GetString: invalid index") );
    return m_strings[n];
}

void ibComboBox::SetString(unsigned int n, const wxString& s)
{
    wxCHECK_RET( n < m_strings.GetCount(), wxT("ibComboBox::SetString: invalid index") );

    m_strings[n] = s;
    if ( m_selection == (int)n )
        ChangeValue(s);

    m_list->Refresh();
}

// the chosen item and the text it shows: nothing chosen, nothing shown (wx's wxOwnerDrawnComboBox::SetSelection)
void ibComboBox::SetSelection(int n)
{
    wxCHECK_RET( n == wxNOT_FOUND || (n >= 0 && n < (int)GetCount()),
                 wxT("ibComboBox::SetSelection: invalid index") );

    // the text straight into the field, past the popup - the item is chosen here, not looked up by its text
    m_selection = n;
    GetTextCtrl()->ChangeValue(n == wxNOT_FOUND ? wxString() : GetString(n));
    Refresh();
}

int ibComboBox::GetSelection() const
{
    return m_selection;
}

wxString ibComboBox::GetStringSelection() const
{
    const int sel = GetSelection();
    return sel == wxNOT_FOUND ? wxString() : GetString(sel);
}

int ibComboBox::DoInsertItems(const wxArrayStringsAdapter& items,
                              unsigned int pos,
                              void **clientData, wxClientDataType type)
{
    const unsigned int count = items.GetCount();
    for ( unsigned int i = 0; i < count; i++ )
    {
        m_strings.Insert(items[i], pos + i);
        m_clientData.insert(m_clientData.begin() + pos + i, nullptr);
        AssignNewItemClientData(pos + i, clientData, i, type);
    }

    if ( m_selection != wxNOT_FOUND && (int)pos <= m_selection )
        m_selection += count;

    return pos + count - 1;
}

void ibComboBox::DoSetItemClientData(unsigned int n, void* clientData)
{
    m_clientData[n] = clientData;
}

void* ibComboBox::DoGetItemClientData(unsigned int n) const
{
    return m_clientData[n];
}

// ----------------------------------------------------------------------------
// the items' drawing
// ----------------------------------------------------------------------------

void ibComboBox::OnDrawItem(wxDC& dc, const wxRect& rect, int item, int flags) const
{
    // the item's text as the list box draws its own (ibThemeRenderer::DrawItem) - the pad under the one marked is the
    // background's
    const wxColour colour = (flags & wxODCB_PAINTING_SELECTED) ? wxTHEME_COLOUR(HIGHLIGHT_TEXT)
                                                                : wxTHEME_COLOUR(CONTROL_TEXT);
    wxDCTextColourChanger colourChanger(dc, colour);

    wxRect rectText = rect;
    rectText.Deflate(2, 0);
    dc.DrawLabel(GetString(item), rectText, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
}

wxCoord ibComboBox::OnMeasureItem(size_t WXUNUSED(item)) const
{
    return GetRenderer()->GetListboxItemHeight(GetCharHeight());
}

wxCoord ibComboBox::OnMeasureItemWidth(size_t item) const
{
    return GetTextExtent(GetString(item)).x + 4;
}

void ibComboBox::OnDrawBackground(wxDC& dc, const wxRect& rect, int WXUNUSED(item), int flags) const
{
    // the item marked in the list - a flat pad of the theme's highlight, as the list box's (ibVListBox)
    if ( !(flags & wxODCB_PAINTING_SELECTED) )
        return;

    wxDCBrushChanger brushChanger(dc, wxBrush(wxTHEME_COLOUR(HIGHLIGHT)));
    wxDCPenChanger penChanger(dc, *wxTRANSPARENT_PEN);
    dc.DrawRectangle(rect);
}

// ----------------------------------------------------------------------------
// input
// ----------------------------------------------------------------------------

bool ibComboBox::PerformAction(const ibControlAction& action,
                               long numArg,
                               const wxString& strArg)
{
    if ( action == ibACTION_COMBOBOX_SELECT_NEXT )
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
        return ibComboCtrl::PerformAction(action, numArg, strArg);

    return true;
}

#endif // wxUSE_COMBOBOX
