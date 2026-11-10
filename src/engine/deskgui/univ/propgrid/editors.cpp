// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, src/propgrid/editors.cpp): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/propgrid/editors.cpp
// Purpose:     ibPropertyGrid editors
// Author:      Jaakko Salli
// Created:     2007-04-14
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"


#ifndef WX_PRECOMP
    #include "wx/settings.h"
    #include "wx/textctrl.h"
#endif

#include "wx/dcbuffer.h"

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/editors.h"
#include "deskgui/univ/propgrid/props.h"
#include "deskgui/univ/propgrid/private.h"
#include "deskgui/univ/theme.h"          // ibThemeEngine — the check box: the theme's renderer
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/colourScheme.h"   // the check box's cell: the theme's content
#include "deskgui/univ/ctrl/control.h"   // ibControl — the simple check box's base
#include "deskgui/univ/ctrl/button.h"    // ibButton — the editor's "…" and the buttons' strip
#include "deskgui/univ/ctrl/bmpButton.h" // ibBitmapButton
#include "deskgui/univ/ctrl/textCtrl.h"  // ibTextCtrl — the text editor
#include "deskgui/univ/ctrl/comboBox.h"  // ibComboBox — the choice editor, drawn by the grid

// How many pixels between textctrl and button
#ifdef __WXMAC__
    #define ibPG_TEXTCTRL_AND_BUTTON_SPACING        4
#else
    #define ibPG_TEXTCTRL_AND_BUTTON_SPACING        2
#endif

#define ibPG_BUTTON_SIZEDEC                         0

// -----------------------------------------------------------------------

#if defined(__WXMSW__)
    // tested
    #define ibPG_NAT_BUTTON_BORDER_Y            1

    #define ibPG_TEXTCTRLYADJUST                (m_spacingy+0)

#elif defined(__WXGTK__)
    // tested
    #define ibPG_NAT_BUTTON_BORDER_Y        1

    #define ibPG_TEXTCTRLYADJUST            0

#elif defined(__WXMAC__)
    // partially tested
    #define ibPG_NAT_BUTTON_BORDER_Y        0

    #define ibPG_TEXTCTRLYADJUST            2

#else
    // defaults
    #define ibPG_NAT_BUTTON_BORDER_Y        0

    #define ibPG_TEXTCTRLYADJUST            0

#endif

#define ibPG_BUTTON_BORDER_WIDTH (-ibPG_BUTTON_SIZEDEC + ibPG_NAT_BUTTON_BORDER_Y)

// for odcombo
#ifdef __WXMAC__
#define ibPG_CHOICEXADJUST           -3 // required because wxComboCtrl reserves 3pixels for wxTextCtrl's focus ring
#define ibPG_CHOICEYADJUST           -3
#else
#define ibPG_CHOICEXADJUST           0
#define ibPG_CHOICEYADJUST           0
#endif

// Number added to image width for SetCustomPaintWidth
#define ODCB_CUST_PAINT_MARGIN               6

// Milliseconds to wait for two mouse-ups after focus in order
// to trigger a double-click.
#define DOUBLE_CLICK_CONVERSION_TRESHOLD        500

// -----------------------------------------------------------------------
// ibPGEditor
// -----------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibPGEditor, wxObject);

wxString ibPGEditor::GetName() const
{
    return GetClassInfo()->GetClassName();
}

void ibPGEditor::DrawValue( wxDC& dc, const wxRect& rect,
                            ibPGProperty* WXUNUSED(property),
                            const wxString& text ) const
{
    dc.DrawText( text, rect.x+ibPG_XBEFORETEXT, rect.y );
}

bool ibPGEditor::GetValueFromControl( ibVariant&, ibPGProperty*, wxWindow* ) const
{
    return false;
}

void ibPGEditor::SetControlStringValue( ibPGProperty* WXUNUSED(property), wxWindow*, const wxString& ) const
{
}


void ibPGEditor::SetControlIntValue( ibPGProperty* WXUNUSED(property), wxWindow*, int ) const
{
}


int ibPGEditor::InsertItem( wxWindow*, const wxString&, int ) const
{
    return -1;
}


void ibPGEditor::DeleteItem( wxWindow*, int ) const
{
}

void ibPGEditor::SetItems(wxWindow* WXUNUSED(ctrl), const wxArrayString& WXUNUSED(labels)) const
{
}

void ibPGEditor::OnFocus( ibPGProperty*, wxWindow* ) const
{
}

void ibPGEditor::SetControlAppearance( ibPropertyGrid* pg,
                                       ibPGProperty* property,
                                       wxWindow* ctrl,
                                       const ibPGCell& cell,
                                       const ibPGCell& oCell,
                                       bool unspecified ) const
{
    // Get old editor appearance
    ibTextCtrl* tc = wxDynamicCast(ctrl, ibTextCtrl);
    ibComboCtrl* cb = nullptr;
    if ( !tc )
    {
        cb = wxDynamicCast(ctrl, ibComboCtrl);
        if ( cb )
        {
            tc = cb->GetTextCtrl();
        }
    }

    if ( tc || cb )
    {
        wxString tcText;
        bool changeText = false;

        if ( cell.HasText() && !pg->IsEditorFocused() )
        {
            tcText = cell.GetText();
            changeText = true;
        }
        else if ( oCell.HasText() )
        {
            tcText = property->GetValueAsString(
                property->HasFlag(ibPGFlags::ReadOnly)?ibPGPropValFormatFlags::Null:ibPGPropValFormatFlags::EditableValue);
            changeText = true;
        }

        if ( changeText )
        {
            // This prevents value from being modified
            if ( tc )
            {
                pg->SetupTextCtrlValue(tcText);
                tc->SetValue(tcText);
            }
            else
            {
                // SEAM vs wx: wxComboCtrl::SetText - the field's text, the popup's choice left alone
                cb->ChangeValue(tcText);
            }
        }
    }

    // Do not make the mistake of calling GetClassDefaultAttributes()
    // here. It is static, while GetDefaultAttributes() is virtual
    // and the correct one to use.
    wxVisualAttributes vattrs = ctrl->GetDefaultAttributes();

    // Foreground colour
    const wxColour& fgCol = cell.GetFgCol();
    if ( fgCol.IsOk() )
    {
        ctrl->SetForegroundColour(fgCol);
    }
    else if ( oCell.GetFgCol().IsOk() )
    {
        ctrl->SetForegroundColour(vattrs.colFg);
    }

    // Background colour
    const wxColour& bgCol = cell.GetBgCol();
    if ( bgCol.IsOk() )
    {
        ctrl->SetBackgroundColour(bgCol);
    }
    else if ( oCell.GetBgCol().IsOk() )
    {
        ctrl->SetBackgroundColour(vattrs.colBg);
    }

    // Font
    const wxFont& font = cell.GetFont();
    if ( font.IsOk() )
    {
        ctrl->SetFont(font);
    }
    else if ( oCell.GetFont().IsOk() )
    {
        ctrl->SetFont(vattrs.font);
    }

    // Also call the old SetValueToUnspecified()
    if ( unspecified )
        SetValueToUnspecified(property, ctrl);
}

void ibPGEditor::SetValueToUnspecified( ibPGProperty* WXUNUSED(property),
                                        wxWindow* WXUNUSED(ctrl) ) const
{
}

bool ibPGEditor::CanContainCustomImage() const
{
    return false;
}

// -----------------------------------------------------------------------
// ibPGTextCtrlEditor
// -----------------------------------------------------------------------

IB_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(TextCtrl,ibPGTextCtrlEditor,ibPGEditor)


ibPGWindowList ibPGTextCtrlEditor::CreateControls( ibPropertyGrid* propGrid,
                                                   ibPGProperty* property,
                                                   const wxPoint& pos,
                                                   const wxSize& sz ) const
{
    wxString text;

    //
    // If has children, and limited editing is specified, then don't create.
    if ( property->HasFlag(ibPGFlags::NoEditor) &&
         property->HasAnyChild() )
        return nullptr;

    ibPGPropValFormatFlags fmtFlags = ibPGPropValFormatFlags::Null;
    if ( !property->HasFlag(ibPGFlags::ReadOnly) &&
         !property->IsValueUnspecified() )
        fmtFlags |= ibPGPropValFormatFlags::EditableValue;
    text = property->GetValueAsString(fmtFlags);

    int flags = 0;
    if ( property->HasFlag(ibPGPropertyFlags_Password) &&
         wxDynamicCast(property, ibStringProperty) )
        flags |= wxTE_PASSWORD;

    wxWindow* wnd = propGrid->GenerateEditorTextCtrl(pos,sz,text,nullptr,flags,
                                                     property->GetMaxLength());

    return wnd;
}

#if 0
void ibPGTextCtrlEditor::DrawValue( wxDC& dc, ibPGProperty* property, const wxRect& rect ) const
{
    if ( !property->IsValueUnspecified() )
    {
        wxString drawStr = property->GetDisplayedString();

        // Code below should no longer be needed, as the obfuscation
        // is now done in GetValueAsString.
        /*if ( property->HasFlag(ibPGPropertyFlags_Password) &&
             wxDynamicCast(property, ibStringProperty) )
        {
            size_t a = drawStr.length();
            drawStr.Empty();
            drawStr.Append(wxS('*'),a);
        }*/
        dc.DrawText( drawStr, rect.x+ibPG_XBEFORETEXT, rect.y );
    }
}
#endif

void ibPGTextCtrlEditor::UpdateControl( ibPGProperty* property, wxWindow* ctrl ) const
{
    ibTextCtrl* tc = wxDynamicCast(ctrl, ibTextCtrl);
    if (!tc) return;

    wxString s;

    if ( tc->HasFlag(wxTE_PASSWORD) )
        s = property->GetValueAsString(ibPGPropValFormatFlags::FullValue);
    else
        s = property->GetDisplayedString();

    ibPropertyGrid* pg = property->GetGrid();

    pg->SetupTextCtrlValue(s);
    tc->SetValue(s);

    //
    // Fix indentation, just in case (change in font boldness is one good
    // reason).
    tc->SetMargins(0);
}

// Provided so that, for example, ComboBox editor can use the same code
// (multiple inheritance would get way too messy).
bool ibPGTextCtrlEditor::OnTextCtrlEvent( ibPropertyGrid* propGrid,
                                          ibPGProperty* WXUNUSED(property),
                                          wxWindow* ctrl,
                                          wxEvent& event )
{
    if ( !ctrl )
        return false;

    if ( event.GetEventType() == wxEVT_TEXT_ENTER )
    {
        if ( propGrid->IsEditorsValueModified() )
        {
            return true;
        }
    }
    else if ( event.GetEventType() == wxEVT_TEXT )
    {
        // Pass this event (with PG id) outside ibPropertyGrid
        // with so that, if necessary, program can tell when user
        // is editing a textctrl.
        wxEvent *evt = event.Clone();
        evt->SetId(propGrid->GetId());
        propGrid->GetEventHandler()->QueueEvent(evt);

        propGrid->EditorsValueWasModified();
    }
    return false;
}


bool ibPGTextCtrlEditor::OnEvent( ibPropertyGrid* propGrid,
                                  ibPGProperty* property,
                                  wxWindow* ctrl,
                                  wxEvent& event ) const
{
    return ibPGTextCtrlEditor::OnTextCtrlEvent(propGrid,property,ctrl,event);
}


bool ibPGTextCtrlEditor::GetTextCtrlValueFromControl( ibVariant& variant, ibPGProperty* property, wxWindow* ctrl )
{
    ibTextCtrl* tc = wxStaticCast(ctrl, ibTextCtrl);
    wxString textVal = tc->GetValue();

    if ( property->UsesAutoUnspecified() && textVal.empty() )
    {
        variant.Reset();
        return true;
    }

    bool res = property->StringToValue(variant, textVal, ibPGPropValFormatFlags::EditableValue);

    // Changing unspecified always causes event (returning
    // true here should be enough to trigger it).
    // TODO: Move to propgrid.cpp
    if ( !res && variant.GetType() == ibValueTypes::TYPE_EMPTY )
        res = true;

    return res;
}


bool ibPGTextCtrlEditor::GetValueFromControl( ibVariant& variant, ibPGProperty* property, wxWindow* ctrl ) const
{
    return ibPGTextCtrlEditor::GetTextCtrlValueFromControl(variant, property, ctrl);
}


void ibPGTextCtrlEditor::SetControlStringValue( ibPGProperty* property, wxWindow* ctrl, const wxString& txt ) const
{
    ibTextCtrl* tc = wxStaticCast(ctrl, ibTextCtrl);

    ibPropertyGrid* pg = property->GetGrid();
    wxASSERT(pg);  // Really, property grid should exist if editor does
    if ( pg )
    {
        pg->SetupTextCtrlValue(txt);
        tc->SetValue(txt);
    }
}


static
void ibPGTextCtrlEditor_OnFocus( ibPGProperty* property,
                                 ibTextCtrl* tc )
{
    // Make sure there is correct text (instead of unspecified value
    // indicator or hint text)
    ibPGPropValFormatFlags fmtFlags = property->HasFlag(ibPGFlags::ReadOnly) ?
        ibPGPropValFormatFlags::Null : ibPGPropValFormatFlags::EditableValue;
    wxString correctText = property->GetValueAsString(fmtFlags);

    if ( tc->GetValue() != correctText )
    {
        property->GetGrid()->SetupTextCtrlValue(correctText);
        tc->SetValue(correctText);
    }

    tc->SelectAll();
}

void ibPGTextCtrlEditor::OnFocus( ibPGProperty* property,
                                  wxWindow* wnd ) const
{
    ibTextCtrl* tc = wxStaticCast(wnd, ibTextCtrl);
    ibPGTextCtrlEditor_OnFocus(property, tc);
}

ibPGTextCtrlEditor::~ibPGTextCtrlEditor()
{
    // Reset the global pointer. Useful when ibPropertyGrid is accessed
    // from an external main loop.
    ibPG_EDITOR(TextCtrl) = nullptr;
}


// -----------------------------------------------------------------------
// ibPGChoiceEditor
// -----------------------------------------------------------------------


IB_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(Choice,ibPGChoiceEditor,ibPGEditor)


// This is a special enhanced double-click processor class.
// In essence, it allows for double-clicks for which the
// first click "created" the control.
// SEAM vs wx: it sits on the combo's text child - the value area the mouse lands on (wx's combo drew its read-only
// value itself, the processor checked the click was in GetTextRect)
class ibPGDoubleClickProcessor : public wxEvtHandler
{
public:

    ibPGDoubleClickProcessor( ibComboBox* combo, ibBoolProperty* property )
        : wxEvtHandler()
        , m_timeLastMouseUp(0)
        , m_combo(combo)
        , m_property(property)
        , m_downReceived(false)
    {
    }

protected:

    void OnMouseEvent( wxMouseEvent& event )
    {
        wxMilliClock_t t = ::wxGetLocalTimeMillis();
        wxEventType evtType = event.GetEventType();

        if ( m_property->HasFlag(ibPGPropertyFlags_UseDCC) &&
             !m_combo->IsPopupShown() )
        {
            if ( evtType == wxEVT_LEFT_DOWN )
            {
                // Set value to avoid up-events without corresponding downs
                m_downReceived = true;
            }
            else if ( evtType == wxEVT_LEFT_DCLICK )
            {
                // We'll make our own double-clicks
                event.SetEventType(0);
                return;
            }
            else if ( evtType == wxEVT_LEFT_UP )
            {
                if ( m_downReceived || m_timeLastMouseUp == 1 )
                {
                    wxMilliClock_t timeFromLastUp = (t-m_timeLastMouseUp);

                    if ( timeFromLastUp < DOUBLE_CLICK_CONVERSION_TRESHOLD )
                    {
                        event.SetEventType(wxEVT_LEFT_DCLICK);
                        m_timeLastMouseUp = 1;
                    }
                    else
                    {
                        m_timeLastMouseUp = t;
                    }
                }
            }
        }

        event.Skip();
    }

    void OnSetFocus( wxFocusEvent& event )
    {
        m_timeLastMouseUp = ::wxGetLocalTimeMillis();
        event.Skip();
    }

private:
    wxMilliClock_t              m_timeLastMouseUp;
    ibComboBox*                 m_combo;
    ibBoolProperty*             m_property;  // Selected property
    bool                        m_downReceived;

    wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(ibPGDoubleClickProcessor, wxEvtHandler)
    EVT_MOUSE_EVENTS(ibPGDoubleClickProcessor::OnMouseEvent)
    EVT_SET_FOCUS(ibPGDoubleClickProcessor::OnSetFocus)
wxEND_EVENT_TABLE()



class ibPGComboBox : public ibComboBox
{
public:

    ibPGComboBox()
        : ibComboBox()
        , m_dclickProcessor(nullptr)
        , m_selProp(nullptr)
    {
    }

    ~ibPGComboBox()
    {
        if ( m_dclickProcessor )
        {
            GetTextCtrl()->RemoveEventHandler(m_dclickProcessor);
            delete m_dclickProcessor;
        }
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& value,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayString& choices,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxS("ibPGComboBox"))
    {
        if ( !ibComboBox::Create( parent,
                                  id,
                                  value,
                                  pos,
                                  size,
                                  choices,
                                  style,
                                  validator,
                                  name ) )
            return false;

        // Enabling double-click processor makes sense
        // only for ibBoolProperty.
        m_selProp = GetGrid()->GetSelection();
        wxASSERT(m_selProp);
        ibBoolProperty* boolProp = wxDynamicCast(m_selProp, ibBoolProperty);
        if ( boolProp )
        {
            m_dclickProcessor = new ibPGDoubleClickProcessor(this, boolProp);
            GetTextCtrl()->PushEventHandler(m_dclickProcessor);
        }

        return true;
    }

    // SEAM vs wx: the hint is the text child's (SetHint), not drawn in the value - every item is the grid's to paint
    virtual void OnDrawItem( wxDC& dc,
                             const wxRect& rect,
                             int item,
                             int flags ) const override
    {
        wxRect r(rect);
        GetGrid()->OnComboItemPaint(this, item, &dc, r, flags);
    }

    virtual wxCoord OnMeasureItem( size_t item ) const override
    {
        ibPropertyGrid* pg = GetGrid();
        wxRect rect;
        rect.x = -1;
        rect.width = 0;
        pg->OnComboItemPaint( this, item, nullptr, rect, 0 );
        return rect.height;
    }

    ibPropertyGrid* GetGrid() const
    {
        ibPropertyGrid* pg = wxDynamicCast(GetParent(),
                                           ibPropertyGrid);
        wxASSERT(pg);
        return pg;
    }

    virtual wxCoord OnMeasureItemWidth( size_t item ) const override
    {
        ibPropertyGrid* pg = GetGrid();
        wxRect rect;
        rect.x = -1;
        rect.width = -1;
        pg->OnComboItemPaint( this, item, nullptr, rect, 0 );
        return rect.width;
    }

    // SEAM vs wx: no PositionTextCtrl override (the platforms' x adjustments) - ibComboCtrl places its text child
    // past the custom paint strip itself

    ibPGProperty* GetProperty() const { return m_selProp; }

private:
    ibPGDoubleClickProcessor*   m_dclickProcessor;
    ibPGProperty*               m_selProp;
};


void ibPropertyGrid::OnComboItemPaint( const ibPGComboBox* pCb,
                                       int item,
                                       wxDC* pDc,
                                       wxRect& rect,
                                       int flags )
{
    ibPGProperty* p = pCb->GetProperty();

    wxString text;

    const ibPGChoices& choices = p->GetChoices();
    int comValIndex = -1;

    const int choiceCount = choices.IsOk()? choices.GetCount(): 0;
    if ( item >= choiceCount && p->GetDisplayedCommonValueCount() > 0 )
    {
        comValIndex = item - choiceCount;
        if ( !p->IsValueUnspecified() || !(flags & wxODCB_PAINTING_CONTROL) )
        {
            text = GetCommonValueLabel(comValIndex);
        }
    }
    else
    {
        if ( !(flags & wxODCB_PAINTING_CONTROL) )
        {
            text = pCb->GetString(item);
        }
        else
        {
            if ( !p->IsValueUnspecified() )
                text = p->GetValueAsString(ibPGPropValFormatFlags::Null);
        }
    }

    if ( item < 0 )
        return;

    wxBitmap itemBitmap;

    if ( comValIndex == -1 && choices.IsOk() && choices.Item(item).GetBitmap().IsOk() )
        itemBitmap = choices.Item(item).GetBitmap().GetBitmapFor(this);

    //
    // Decide what custom image size to use
    // (Use item-specific bitmap only if not drawn in the control field.)
    wxSize cis;
    if ( itemBitmap.IsOk() && !(flags & wxODCB_PAINTING_CONTROL) )
    {
        cis = itemBitmap.GetSize();
    }
    else
    {
        cis = GetImageSize(p, item);
    }

    if ( rect.x + rect.width < 0 )
    {
        // Default measure behaviour (no flexible, custom paint image only)
        if ( rect.width < 0 )
        {
            wxCoord x, y;
            pCb->GetTextExtent(text, &x, &y);
            rect.width = cis.x + ibCC_CUSTOM_IMAGE_MARGIN1 + ibCC_CUSTOM_IMAGE_MARGIN2 + 9 + x;
        }

        rect.height = cis.y + 2;
        return;
    }

    ibPGPaintData paintdata;
    paintdata.m_parent = this;
    paintdata.m_choiceItem = item;

    // This is by the current (1.0.0b) spec - if painting control, item is -1
    if ( (flags & wxODCB_PAINTING_CONTROL) )
        paintdata.m_choiceItem = -1;

    wxCHECK_RET( pDc, wxS("Invalid DC") );

    wxDC& dc = *pDc;
    dc.SetBrush(*wxWHITE_BRUSH);

    //
    // DrawItem call
    ibPGCellRenderer* renderer = nullptr;
    const ibPGChoiceEntry* cell = nullptr;

    wxPoint pt(rect.x + ibPG_CONTROL_MARGIN - ibPG_CHOICEXADJUST - 1,
                rect.y + 1);

    int renderFlags = ibPGCellRenderer::DontUseCellColours;

    // If custom image had some size, we will start from the assumption
    // that custom paint procedure is required
    bool useCustomPaintProcedure =  cis.x > 0;

    if ( flags & wxODCB_PAINTING_SELECTED )
        renderFlags |= ibPGCellRenderer::Selected;

    if ( flags & wxODCB_PAINTING_CONTROL )
    {
        renderFlags |= ibPGCellRenderer::Control;

        // If ibPGFlags::CustomImage was set, then that means any custom
        // image will not appear on the control row (it may be too
        // large to fit, for instance). Also do not draw custom image
        // if no choice was selected.
        if ( !p->HasFlag(ibPGFlags::CustomImage) )
            useCustomPaintProcedure = false;
    }
    else
    {
        renderFlags |= ibPGCellRenderer::ChoicePopup;

        // For consistency, always use normal font when drawing drop down
        // items
        dc.SetFont(GetFont());
    }

    // If not drawing a selected popup item, then give property's
    // value image a chance.
    if ( p->GetValueImage() && item != pCb->GetSelection() )
        useCustomPaintProcedure = false;
    // If current choice had a bitmap set by the application, then
    // use it instead of any custom paint procedure
    // (only if not drawn in the control field).
    else if ( itemBitmap.IsOk() && !(flags & wxODCB_PAINTING_CONTROL) )
        useCustomPaintProcedure = false;

    if ( useCustomPaintProcedure )
    {
        pt.x += ibCC_CUSTOM_IMAGE_MARGIN1;
        wxRect r(pt, cis);

        if ( flags & wxODCB_PAINTING_CONTROL )
        {
            //r.width = cis.x;
            r.height = ibPG_STD_CUST_IMAGE_HEIGHT(m_lineHeight);
        }

        paintdata.m_drawnWidth = r.width;

        dc.SetPen(m_colPropFore);
        if ( comValIndex >= 0 )
        {
            const ibPGCommonValue* cv = GetCommonValue(comValIndex);
            renderer = cv->GetRenderer();
            r.width = rect.width;
            renderer->Render( dc, r, this, p, m_selColumn, comValIndex, renderFlags );
            return;
        }
        else
        {
            p->OnCustomPaint( dc, r, paintdata );
        }

        pt.x += paintdata.m_drawnWidth + ibCC_CUSTOM_IMAGE_MARGIN2 - 1;
    }
    else
    {
        // TODO: This aligns text so that it seems to be horizontally
        //       on the same line as property values. Not really
        //       sure if it is needed, but seems to not cause any harm.
        pt.x -= 1;

        if ( choices.IsOk() && comValIndex < 0 )
        {
            // This aligns bitmap horizontally so that it is
            // on the same position as bitmap drawn for static content
            // (without editor).
            wxRect r(rect);
            r.x -= 1;

            cell = &choices.Item(item);
            renderer = ibPGGlobalVars->m_defaultRenderer;
            int imageOffset = renderer->PreDrawCell(dc, r, this, *cell,
                                                    renderFlags );
            if ( imageOffset )
                imageOffset += ibCC_CUSTOM_IMAGE_MARGIN1 +
                                ibCC_CUSTOM_IMAGE_MARGIN2;
            pt.x += imageOffset;
        }
    }

    //
    // Draw text
    //

    pt.y += (rect.height-m_fontHeight)/2 - 1;

    pt.x += 1;

    dc.DrawText( text, pt.x + ibPG_XBEFORETEXT, pt.y );

    if ( renderer )
        renderer->PostDrawCell(dc, this, *cell, renderFlags);
}

static
bool ibPGChoiceEditor_SetCustomPaintWidth( ibPropertyGrid* propGrid, ibPGComboBox* cb, int cmnVal )
{
    ibPGProperty* property = propGrid->GetSelectedProperty();
    wxASSERT( property );

    wxSize imageSize;
    bool res;

    // TODO: Do this always when cell has custom text.
    if ( property->IsValueUnspecified() )
    {
        cb->SetCustomPaintWidth( 0 );
        return true;
    }

    if ( cmnVal >= 0 )
    {
        // Yes, a common value is being selected
        property->SetCommonValue( cmnVal );
        imageSize = propGrid->GetCommonValue(cmnVal)->
                            GetRenderer()->GetImageSize(property, 1, cmnVal);
        res = false;
    }
    else
    {
        imageSize = propGrid->GetImageSize(property, -1);
        res = true;
    }

    if ( imageSize.x )
        imageSize.x += ODCB_CUST_PAINT_MARGIN;
    cb->SetCustomPaintWidth( imageSize.x );

    return res;
}

// CreateControls calls this with CB_READONLY in extraStyle
wxWindow* ibPGChoiceEditor::CreateControlsBase( ibPropertyGrid* propGrid,
                                                ibPGProperty* property,
                                                const wxPoint& pos,
                                                const wxSize& sz,
                                                long extraStyle ) const
{
    // Since it is not possible (yet) to create a read-only combo box in
    // the same sense that ibTextCtrl is read-only, simply do not create
    // the control in this case.
    if ( property->HasFlag(ibPGFlags::ReadOnly) )
        return nullptr;

    const ibPGChoices& choices = property->GetChoices();
    wxString defString;
    int index = property->GetChoiceSelection();

    ibPGPropValFormatFlags fmtFlags = ibPGPropValFormatFlags::Null;
    if ( !property->HasFlag(ibPGFlags::ReadOnly) &&
         !property->IsValueUnspecified() )
        fmtFlags |= ibPGPropValFormatFlags::EditableValue;
    defString = property->GetValueAsString(fmtFlags);

    wxArrayString labels = choices.GetLabels();

    wxPoint po(pos);
    wxSize si(sz);
    po.y += ibPG_CHOICEYADJUST;
    si.y -= (ibPG_CHOICEYADJUST*2);

    po.x += ibPG_CHOICEXADJUST;
    si.x -= ibPG_CHOICEXADJUST;
    wxWindow* ctrlParent = propGrid->GetPanel();

    int odcbFlags = extraStyle | wxBORDER_NONE | wxTE_PROCESS_ENTER;

    if ( property->HasFlag(ibPGPropertyFlags_UseDCC) &&
         wxDynamicCast(property, ibBoolProperty) )
        odcbFlags |= wxODCB_DCLICK_CYCLES;

    //
    // If common value specified, use appropriate index
    unsigned int cmnVals = property->GetDisplayedCommonValueCount();
    if ( cmnVals )
    {
        if ( !property->IsValueUnspecified() )
        {
            int cmnVal = property->GetCommonValue();
            if ( cmnVal >= 0 )
            {
                index = labels.size() + cmnVal;
            }
        }

        for ( unsigned int i = 0; i < cmnVals; i++ )
            labels.Add(propGrid->GetCommonValueLabel(i));
    }

    ibPGComboBox* cb = new ibPGComboBox();
#ifdef __WXMSW__
    cb->Hide();
#endif
    cb->Create(ctrlParent,
               wxID_ANY,
               wxString(),
               po,
               si,
               labels,
               odcbFlags);

    // SEAM vs wx: no SetButtonPosition and SetMargins - ibComboCtrl's button is square by its height already, its text
    // child pads the text by the theme

    cb->SetBackgroundColour(propGrid->GetCellBackgroundColour());

    // Set hint text
    cb->GetTextCtrl()->SetHint(property->GetHintText());

    ibPGChoiceEditor_SetCustomPaintWidth( propGrid, cb,
                                          property->GetCommonValue() );

    if ( index >= 0 && index < (int)cb->GetCount() )
    {
        cb->SetSelection( index );
        if ( !defString.empty() )
            cb->ChangeValue( defString );   // SEAM vs wx: wxComboCtrl::SetText - the text, the choice left alone
    }
    else if ( !(extraStyle & wxCB_READONLY) && !defString.empty() )
    {
        propGrid->SetupTextCtrlValue(defString);
        cb->SetValue( defString );
    }
    else
    {
        cb->SetSelection( -1 );
    }

#ifdef __WXMSW__
    cb->Show();
#endif

    return (wxWindow*) cb;
}


void ibPGChoiceEditor::UpdateControl( ibPGProperty* property, wxWindow* ctrl ) const
{
    ibComboBox* cb = wxDynamicCast(ctrl, ibComboBox);
    wxCHECK_RET(cb, "Only ibComboBox editor can be updated");

    int ind = property->GetChoiceSelection();
    cb->SetSelection(ind);
}

ibPGWindowList ibPGChoiceEditor::CreateControls( ibPropertyGrid* propGrid, ibPGProperty* property,
        const wxPoint& pos, const wxSize& sz ) const
{
    return CreateControlsBase(propGrid,property,pos,sz,wxCB_READONLY);
}


int ibPGChoiceEditor::InsertItem( wxWindow* ctrl, const wxString& label, int index ) const
{
    ibComboBox* cb = wxDynamicCast(ctrl, ibComboBox);
    wxCHECK_MSG(cb, wxNOT_FOUND, "Only ibComboBox editor can be updated");

    if (index < 0)
        index = cb->GetCount();

    return cb->Insert(label,index);
}


void ibPGChoiceEditor::DeleteItem( wxWindow* ctrl, int index ) const
{
    ibComboBox* cb = wxDynamicCast(ctrl, ibComboBox);
    wxCHECK_RET(cb, "Only ibComboBox editor can be updated");

    cb->Delete(index);
}

void ibPGChoiceEditor::SetItems(wxWindow* ctrl, const wxArrayString& labels) const
{
    ibComboBox* cb = wxDynamicCast(ctrl, ibComboBox);
    wxCHECK_RET(cb, "Only ibComboBox editor can be updated");

    cb->Set(labels);
}

bool ibPGChoiceEditor::OnEvent( ibPropertyGrid* propGrid, ibPGProperty* property,
    wxWindow* ctrl, wxEvent& event ) const
{
    if ( event.GetEventType() == wxEVT_COMBOBOX )
    {
        ibPGComboBox* cb = (ibPGComboBox*)ctrl;
        int index = cb->GetSelection();
        int cmnValIndex = -1;
        int cmnVals = property->GetDisplayedCommonValueCount();
        int items = cb->GetCount();

        if ( index >= (items-cmnVals) )
        {
            // Yes, a common value is being selected
            cmnValIndex = index - (items-cmnVals);
            property->SetCommonValue( cmnValIndex );

            // Truly set value to unspecified?
            if ( propGrid->GetUnspecifiedCommonValue() == cmnValIndex )
            {
                if ( !property->IsValueUnspecified() )
                    propGrid->SetInternalFlag(ibPropertyGrid::ibPG_FL_VALUE_CHANGE_IN_EVENT);
                property->SetValueToUnspecified();
                if ( !cb->HasFlag(wxCB_READONLY) )
                {
                    wxString unspecValueText;
                    unspecValueText = propGrid->GetUnspecifiedValueText();
                    propGrid->SetupTextCtrlValue(unspecValueText);
                    cb->GetTextCtrl()->SetValue(unspecValueText);
                }
                return false;
            }
        }
        return ibPGChoiceEditor_SetCustomPaintWidth( propGrid, cb, cmnValIndex );
    }
    return false;
}


bool ibPGChoiceEditor::GetValueFromControl( ibVariant& variant, ibPGProperty* property, wxWindow* ctrl ) const
{
    ibComboBox* cb = (ibComboBox*)ctrl;

    int index = cb->GetSelection();

    if ( index != property->GetChoiceSelection() ||
        // Changing unspecified always causes event (returning
        // true here should be enough to trigger it).
         property->IsValueUnspecified()
       )
    {
        return property->IntToValue(variant, index, ibPGPropValFormatFlags::PropertySpecific);
    }
    return false;
}


void ibPGChoiceEditor::SetControlStringValue( ibPGProperty* property,
                                              wxWindow* ctrl,
                                              const wxString& txt ) const
{
    ibComboBox* cb = wxDynamicCast(ctrl, ibComboBox);
    wxCHECK_RET(cb, "Only ibComboBox editor can be updated");

    property->GetGrid()->SetupTextCtrlValue(txt);
    cb->SetValue(txt);
}


void ibPGChoiceEditor::SetControlIntValue( ibPGProperty* WXUNUSED(property), wxWindow* ctrl, int value ) const
{
    ibComboBox* cb = wxDynamicCast(ctrl, ibComboBox);
    wxCHECK_RET(cb, "Only ibComboBox editor can be updated");

    cb->SetSelection(value);
}


void ibPGChoiceEditor::SetValueToUnspecified( ibPGProperty* WXUNUSED(property),
                                              wxWindow* ctrl ) const
{
    ibComboBox* cb = wxDynamicCast(ctrl, ibComboBox);
    wxCHECK_RET(cb, "Only ibComboBox editor can be updated");

    if ( cb->HasFlag(wxCB_READONLY) )
        cb->SetSelection(-1);
}


bool ibPGChoiceEditor::CanContainCustomImage() const
{
    return true;
}


ibPGChoiceEditor::~ibPGChoiceEditor()
{
    ibPG_EDITOR(Choice) = nullptr;
}


// -----------------------------------------------------------------------
// ibPGComboBoxEditor
// -----------------------------------------------------------------------


IB_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(ComboBox,
                                      ibPGComboBoxEditor,
                                      ibPGChoiceEditor)


void ibPGComboBoxEditor::UpdateControl( ibPGProperty* property, wxWindow* ctrl ) const
{
    ibComboBox* cb = (ibComboBox*)ctrl;
    const int index = property->GetChoiceSelection();
    wxString s = property->GetValueAsString(ibPGPropValFormatFlags::EditableValue);
    cb->SetSelection(index);
    property->GetGrid()->SetupTextCtrlValue(s);
    cb->SetValue(s);
}


ibPGWindowList ibPGComboBoxEditor::CreateControls( ibPropertyGrid* propGrid,
                                                   ibPGProperty* property,
                                                   const wxPoint& pos,
                                                   const wxSize& sz ) const
{
    return CreateControlsBase(propGrid,property,pos,sz,0);
}


bool ibPGComboBoxEditor::OnEvent( ibPropertyGrid* propGrid,
                                  ibPGProperty* property,
                                  wxWindow* ctrl,
                                  wxEvent& event ) const
{
    wxWindow* textCtrl = nullptr;

    if ( ctrl )
    {
        if ( auto cb = wxDynamicCast(ctrl, ibComboBox) )
            textCtrl = cb->GetTextCtrl();
    }

    if ( ibPGTextCtrlEditor::OnTextCtrlEvent(propGrid,property,textCtrl,event) )
        return true;

    return ibPGChoiceEditor::OnEvent(propGrid,property,ctrl,event);
}


bool ibPGComboBoxEditor::GetValueFromControl( ibVariant& variant, ibPGProperty* property, wxWindow* ctrl ) const
{
    ibComboBox* cb = (ibComboBox*)ctrl;
    wxString textVal = cb->GetValue();

    if ( property->UsesAutoUnspecified() && textVal.empty() )
    {
        variant.Reset();
        return true;
    }

    bool res = property->StringToValue(variant, textVal, ibPGPropValFormatFlags::EditableValue | ibPGPropValFormatFlags::PropertySpecific);

    // Changing unspecified always causes event (returning
    // true here should be enough to trigger it).
    if ( !res && variant.GetType() == ibValueTypes::TYPE_EMPTY )
        res = true;

    return res;
}


void ibPGComboBoxEditor::OnFocus( ibPGProperty* property,
                                  wxWindow* ctrl ) const
{
    ibComboBox* cb = (ibComboBox*)ctrl;
    ibPGTextCtrlEditor_OnFocus(property, cb->GetTextCtrl());
}


ibPGComboBoxEditor::~ibPGComboBoxEditor()
{
    ibPG_EDITOR(ComboBox) = nullptr;
}



// -----------------------------------------------------------------------
// ibPGChoiceAndButtonEditor
// -----------------------------------------------------------------------


IB_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(ChoiceAndButton,
                                      ibPGChoiceAndButtonEditor,
                                      ibPGChoiceEditor)


ibPGWindowList ibPGChoiceAndButtonEditor::CreateControls( ibPropertyGrid* propGrid,
                                                          ibPGProperty* property,
                                                          const wxPoint& pos,
                                                          const wxSize& sz ) const
{
    // Use one two units smaller to match size of the combo's dropbutton.
    // (normally a bigger button is used because it looks better)
    int bt_wid = sz.y;
    bt_wid -= 2;
    wxSize bt_sz(bt_wid,bt_wid);

    // Position of button.
    wxPoint bt_pos(pos.x+sz.x-bt_sz.x,pos.y);
#ifdef __WXMAC__
    bt_pos.y -= 1;
#else
    bt_pos.y += 1;
#endif

    wxWindow* bt = propGrid->GenerateEditorButton( bt_pos, bt_sz );

    // Size of choice.
    wxSize ch_sz(sz.x-bt->GetSize().x,sz.y);

#ifdef __WXMAC__
    ch_sz.x -= ibPG_TEXTCTRL_AND_BUTTON_SPACING;
#endif

    wxWindow* ch = ibPGChoiceEditor::CreateControls(propGrid,property,
        pos,ch_sz).GetPrimary();

#ifdef __WXMSW__
    bt->Show();
#endif

    return ibPGWindowList(ch, bt);
}


ibPGChoiceAndButtonEditor::~ibPGChoiceAndButtonEditor()
{
    ibPG_EDITOR(ChoiceAndButton) = nullptr;
}

// -----------------------------------------------------------------------
// ibPGTextCtrlAndButtonEditor
// -----------------------------------------------------------------------

IB_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(TextCtrlAndButton,
                                      ibPGTextCtrlAndButtonEditor,
                                      ibPGTextCtrlEditor)


ibPGWindowList ibPGTextCtrlAndButtonEditor::CreateControls( ibPropertyGrid* propGrid,
                                                            ibPGProperty* property,
                                                            const wxPoint& pos,
                                                            const wxSize& sz ) const
{
    wxWindow* wnd2;
    wxWindow* wnd = propGrid->GenerateEditorTextCtrlAndButton( pos, sz, &wnd2,
        property->HasFlag(ibPGFlags::NoEditor), property);

    return ibPGWindowList(wnd, wnd2);
}


ibPGTextCtrlAndButtonEditor::~ibPGTextCtrlAndButtonEditor()
{
    ibPG_EDITOR(TextCtrlAndButton) = nullptr;
}

// -----------------------------------------------------------------------
// ibPGCheckBoxEditor
// -----------------------------------------------------------------------

#if ibPG_INCLUDE_CHECKBOX

IB_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(CheckBox,
                                      ibPGCheckBoxEditor,
                                      ibPGEditor)


// Check box state flags
enum class ibSimpleCheckBoxStates : int
{
    Unchecked   = 0,
    Checked     = 1,
    Bold        = 2,
    Unspecified = 4
};

constexpr ibSimpleCheckBoxStates operator&(ibSimpleCheckBoxStates a, ibSimpleCheckBoxStates b)
{
    return static_cast<ibSimpleCheckBoxStates>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr ibSimpleCheckBoxStates operator|(ibSimpleCheckBoxStates a, ibSimpleCheckBoxStates b)
{
    return static_cast<ibSimpleCheckBoxStates>(static_cast<int>(a) | static_cast<int>(b));
}

inline ibSimpleCheckBoxStates operator|=(ibSimpleCheckBoxStates& a, ibSimpleCheckBoxStates b)
{
    return a = a | b;
}

constexpr ibSimpleCheckBoxStates operator^(ibSimpleCheckBoxStates a, ibSimpleCheckBoxStates b)
{
    return static_cast<ibSimpleCheckBoxStates>(static_cast<int>(a) ^ static_cast<int>(b));
}

inline ibSimpleCheckBoxStates operator^=(ibSimpleCheckBoxStates& a, ibSimpleCheckBoxStates b)
{
    return a = a ^ b;
}

constexpr bool operator!(ibSimpleCheckBoxStates a)
{
    return static_cast<int>(a) == 0;
}

const int ibSCB_SETVALUE_CYCLE = 2;

static void DrawSimpleCheckBox(wxWindow* win, wxDC& dc, const wxRect& rect, ibSimpleCheckBoxStates state)
{
    int cbFlags = 0;
    if ( !!(state & ibSimpleCheckBoxStates::Unspecified) )
    {
        cbFlags |= wxCONTROL_UNDETERMINED;
    }
    else if ( !!(state & ibSimpleCheckBoxStates::Checked) )
    {
        cbFlags |= wxCONTROL_CHECKED;
    }

    if ( !!(state & ibSimpleCheckBoxStates::Bold) )
    {
        // wxCONTROL_CHECKED and wxCONTROL_PRESSED flags
        // are equivalent for wxOSX so we have to use
        // other flag to indicate "selected state".
#ifdef __WXOSX__
        cbFlags |= wxCONTROL_FOCUSED;
#else
        cbFlags |= wxCONTROL_PRESSED;
#endif
    }

    // the theme's box (SEAM vs wx: the native renderer's), at its own size, centred in the cell
    ibRenderer* const renderer = ibThemeEngine::Get()->GetRenderer();
    renderer->DrawCheckBox
    (
        win,
        dc,
        wxRect(renderer->GetCheckBoxSize(win)).CenterIn(rect),
        cbFlags
    );
}

//
// Real simple custom-drawn checkbox-without-label class.
//
class ibSimpleCheckBox : public ibControl
{
public:

    void SetValue( int value );

    ibSimpleCheckBox( wxWindow* parent,
                      wxWindowID id,
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize& size = wxDefaultSize )
        : ibControl(parent,id,pos,size,wxBORDER_NONE|wxWANTS_CHARS)
        , m_state(ibSimpleCheckBoxStates::Unchecked)
    {
        // Due to SetOwnFont stuff necessary for GTK+ 1.2, we need to have this
        ibControl::SetFont( parent->GetFont() );

        SetBoxHeight(12);
        ibControl::SetBackgroundStyle( wxBG_STYLE_PAINT );
    }

    virtual ~ibSimpleCheckBox() = default;

    void SetBoxHeight(int height)
    {
        m_boxHeight = height;
        m_boxRect = GetBoxRect(GetClientSize(), m_boxHeight);
    }

    static wxRect GetBoxRect(const wxRect& r, int box_h)
    {
        return wxRect(r.x + ibPG_XBEFORETEXT, r.y + ((r.height - box_h) / 2), box_h, box_h);
    }

    ibSimpleCheckBoxStates m_state;

private:
    void OnPaint( wxPaintEvent& event );
    void OnLeftClick( wxMouseEvent& event );
    void OnKeyDown( wxKeyEvent& event );

    void OnResize( wxSizeEvent& event )
    {
        SetBoxHeight(m_boxHeight); // Recalculate box rectangle
        Refresh();
        event.Skip();
    }
    void OnLeftClickActivate( wxCommandEvent& evt );

    int m_boxHeight;
    wxRect m_boxRect;

    wxDECLARE_EVENT_TABLE();
};

wxDEFINE_EVENT( ibEVT_CB_LEFT_CLICK_ACTIVATE, wxCommandEvent );

wxBEGIN_EVENT_TABLE(ibSimpleCheckBox, ibControl)
    EVT_PAINT(ibSimpleCheckBox::OnPaint)
    EVT_LEFT_DOWN(ibSimpleCheckBox::OnLeftClick)
    EVT_LEFT_DCLICK(ibSimpleCheckBox::OnLeftClick)
    EVT_KEY_DOWN(ibSimpleCheckBox::OnKeyDown)
    EVT_SIZE(ibSimpleCheckBox::OnResize)
    EVT_COMMAND(wxID_ANY, ibEVT_CB_LEFT_CLICK_ACTIVATE, ibSimpleCheckBox::OnLeftClickActivate)
wxEND_EVENT_TABLE()

void ibSimpleCheckBox::OnPaint( wxPaintEvent& WXUNUSED(event) )
{
    wxAutoBufferedPaintDC dc(this);

    wxColour bgcol = GetBackgroundColour();
    dc.SetBackground(wxBrush(bgcol));
    dc.Clear();
    dc.SetBrush( bgcol );
    dc.SetPen( bgcol );

    ibSimpleCheckBoxStates state = m_state;
    if ( !(state & ibSimpleCheckBoxStates::Unspecified) &&
         GetFont().GetWeight() == wxFONTWEIGHT_BOLD )
        state |= ibSimpleCheckBoxStates::Bold;

    DrawSimpleCheckBox(this, dc, m_boxRect, state);
}

void ibSimpleCheckBox::OnLeftClick( wxMouseEvent& event )
{
    if ( m_boxRect.Contains(event.GetPosition()) )
    {
        SetValue(ibSCB_SETVALUE_CYCLE);
    }
}

void ibSimpleCheckBox::OnKeyDown( wxKeyEvent& event )
{
    if ( event.GetKeyCode() == WXK_SPACE )
    {
        SetValue(ibSCB_SETVALUE_CYCLE);
    }
}

void ibSimpleCheckBox::SetValue( int value )
{
    if ( value == ibSCB_SETVALUE_CYCLE )
    {
        m_state ^= ibSimpleCheckBoxStates::Checked;
    }
    else
    {
        m_state = value == 0 ? ibSimpleCheckBoxStates::Unchecked : ibSimpleCheckBoxStates::Checked;
    }
    Refresh();

    wxCommandEvent evt(wxEVT_CHECKBOX,GetParent()->GetId());

    ibPropertyGrid* propGrid = (ibPropertyGrid*) GetParent();
    wxASSERT( wxDynamicCast(propGrid, ibPropertyGrid) );
    propGrid->HandleCustomEditorEvent(evt);
}

void ibSimpleCheckBox::OnLeftClickActivate( wxCommandEvent& evt )
{
    wxPoint pt(evt.GetInt(), evt.GetExtraLong());
    if ( m_boxRect.Contains(pt) )
    {
        SetValue(ibSCB_SETVALUE_CYCLE);
    }
}

ibPGWindowList ibPGCheckBoxEditor::CreateControls( ibPropertyGrid* propGrid,
                                                   ibPGProperty* property,
                                                   const wxPoint& pos,
                                                   const wxSize& size ) const
{
    if ( property->HasFlag(ibPGFlags::ReadOnly) )
        return nullptr;

    wxPoint pt = pos;
    pt.x -= ibPG_XBEFOREWIDGET;
    wxSize sz = size;
    sz.x = propGrid->GetFontHeight() + (ibPG_XBEFOREWIDGET*2) + 4;

    ibSimpleCheckBox* cb = new ibSimpleCheckBox(propGrid->GetPanel(),
                                                wxID_ANY, pt, sz);

    cb->SetBackgroundColour(wxTHEME_COLOUR(WINDOW));

    UpdateControl(property, cb);

    if ( !property->IsValueUnspecified() )
    {
        if ( propGrid->HasInternalFlag(ibPropertyGrid::ibPG_FL_ACTIVATION_BY_CLICK) )
        {
            // Send the event to toggle the value (if mouse cursor is on the item)
            wxPoint point = cb->ScreenToClient(::wxGetMousePosition());
            wxCommandEvent *evt = new wxCommandEvent(ibEVT_CB_LEFT_CLICK_ACTIVATE, cb->GetId());
            // Store mouse pointer position
            evt->SetInt(point.x);
            evt->SetExtraLong(point.y);
            wxQueueEvent(cb, evt);
        }
    }

    propGrid->SetInternalFlag(ibPropertyGrid::ibPG_FL_FIXED_WIDTH_EDITOR);

    return cb;
}

void ibPGCheckBoxEditor::DrawValue( wxDC& dc, const wxRect& rect,
                                    ibPGProperty* property,
                                    const wxString& WXUNUSED(text) ) const
{
    ibSimpleCheckBoxStates state = ibSimpleCheckBoxStates::Unchecked;

    if ( !property->IsValueUnspecified() )
    {
        state = property->GetChoiceSelection() == 0 ? ibSimpleCheckBoxStates::Unchecked : ibSimpleCheckBoxStates::Checked;
        if ( dc.GetFont().GetWeight() == wxFONTWEIGHT_BOLD )
            state |= ibSimpleCheckBoxStates::Bold;
    }
    else
    {
        state |= ibSimpleCheckBoxStates::Unspecified;
    }

    // Box rectangle
    wxRect r = ibSimpleCheckBox::GetBoxRect(rect, dc.GetCharHeight());
    DrawSimpleCheckBox(property->GetGrid(), dc, r, state);
}

void ibPGCheckBoxEditor::UpdateControl( ibPGProperty* property,
                                        wxWindow* ctrl ) const
{
    ibSimpleCheckBox* cb = wxDynamicCast(ctrl, ibSimpleCheckBox);
    wxCHECK_RET(cb, "Only ibSimpleCheckBox editor can be updated");

    if ( !property->IsValueUnspecified() )
        cb->m_state = property->GetChoiceSelection() == 0
                        ? ibSimpleCheckBoxStates::Unchecked : ibSimpleCheckBoxStates::Checked;
    else
        cb->m_state = ibSimpleCheckBoxStates::Unspecified;

    ibPropertyGrid* propGrid = property->GetGrid();
    cb->SetBoxHeight(propGrid->GetFontHeight());

    cb->Refresh();
}

bool ibPGCheckBoxEditor::OnEvent( ibPropertyGrid* WXUNUSED(propGrid), ibPGProperty* WXUNUSED(property),
    wxWindow* WXUNUSED(ctrl), wxEvent& event ) const
{
    if ( event.GetEventType() == wxEVT_CHECKBOX )
    {
        return true;
    }
    return false;
}


bool ibPGCheckBoxEditor::GetValueFromControl( ibVariant& variant, ibPGProperty* property, wxWindow* ctrl ) const
{
    ibSimpleCheckBox* cb = (ibSimpleCheckBox*)ctrl;

    int index = !!(cb->m_state & ibSimpleCheckBoxStates::Checked) ? 1 : 0;

    if ( index != property->GetChoiceSelection() ||
         // Changing unspecified always causes event (returning
         // true here should be enough to trigger it).
         property->IsValueUnspecified()
       )
    {
        return property->IntToValue(variant, index, ibPGPropValFormatFlags::PropertySpecific);
    }
    return false;
}


void ibPGCheckBoxEditor::SetControlIntValue( ibPGProperty* WXUNUSED(property), wxWindow* ctrl, int value ) const
{
    ((ibSimpleCheckBox*)ctrl)->m_state = value == 0 ? ibSimpleCheckBoxStates::Unchecked : ibSimpleCheckBoxStates::Checked;
    ctrl->Refresh();
}


void ibPGCheckBoxEditor::SetValueToUnspecified( ibPGProperty* WXUNUSED(property), wxWindow* ctrl ) const
{
    ((ibSimpleCheckBox*)ctrl)->m_state = ibSimpleCheckBoxStates::Unspecified;
    ctrl->Refresh();
}


ibPGCheckBoxEditor::~ibPGCheckBoxEditor()
{
    ibPG_EDITOR(CheckBox) = nullptr;
}

#endif // ibPG_INCLUDE_CHECKBOX

// -----------------------------------------------------------------------

wxWindow* ibPropertyGrid::GetEditorControl() const
{
    return  m_wndEditor;
}

// -----------------------------------------------------------------------

void ibPropertyGrid::CorrectEditorWidgetSizeX()
{
    int secWid = 0;

    // Use fixed selColumn 1 for main editor widgets
    int newSplitterx;
    CalcScrolledPosition(m_pState->DoGetSplitterPosition(0), 0, &newSplitterx, nullptr);
    int newWidth = newSplitterx + m_pState->GetColumnWidth(1);

    if ( m_wndEditor2 )
    {
        // if width change occurred, move secondary wnd by that amount
        wxRect r = m_wndEditor2->GetRect();
        secWid = r.width;
        r.x = newWidth - secWid;

        m_wndEditor2->SetSize( r );

        // if primary is textctrl, then we have to add some extra space
#ifdef __WXMAC__
        if ( m_wndEditor )
#else
        if ( wxDynamicCast(m_wndEditor, ibTextCtrl) )
#endif
            secWid += ibPG_TEXTCTRL_AND_BUTTON_SPACING;
    }

    if ( m_wndEditor )
    {
        wxRect r = m_wndEditor->GetRect();

        r.x = newSplitterx+m_ctrlXAdjust;

        if ( !(m_iFlags & ibPG_FL_FIXED_WIDTH_EDITOR) )
            r.width = newWidth - r.x - secWid;

        m_wndEditor->SetSize(r);
    }

    if ( m_wndEditor2 )
        m_wndEditor2->Refresh();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::CorrectEditorWidgetPosY()
{
    ibPGProperty* selected = GetSelection();

    if ( selected )
    {
        if ( m_labelEditor )
        {
            wxRect r = GetEditorWidgetRect(selected, m_selColumn);
            m_labelEditor->Move(r.GetPosition() + m_labelEditorPosRel);
        }

        if ( m_wndEditor || m_wndEditor2 )
        {
            wxRect r = GetEditorWidgetRect(selected, 1);

            if ( m_wndEditor )
            {
                m_wndEditor->Move(r.GetPosition() + m_wndEditorPosRel);
            }

            if ( m_wndEditor2 )
            {
                m_wndEditor2->Move(r.GetPosition() + m_wndEditor2PosRel);
            }
        }
    }
}

// -----------------------------------------------------------------------

// Fixes position of ibTextCtrl-like control (wxSpinCtrl usually
// fits into that category as well).
#ifndef ibPG_TEXTCTRLXADJUST
#if defined(__WXMSW__)
#define ibPG_TEXTCTRLXADJUST2 0
#elif defined(__WXGTK__)
  #if defined(__WXGTK3__)
  #define ibPG_TEXTCTRLXADJUST2 (-2)
  #else
  #define ibPG_TEXTCTRLXADJUST2 0
  #endif // wxGTK3/!wxGTK3
#else
#error "ibPG_TEXTCTRLXADJUST should be defined for this platform"
#endif
#endif // !ibPG_TEXTCTRLXADJUST

void ibPropertyGrid::FixPosForTextCtrl( wxWindow* ctrl,
                                        unsigned int WXUNUSED(forColumn),
                                        const wxPoint& offset )
{
    // Center the control vertically
    wxRect finalPos = ctrl->GetRect();
    int y_adj = (m_lineHeight - finalPos.height)/2 + ibPG_TEXTCTRLYADJUST;

    // Prevent over-sized control
    int sz_dec = (y_adj + finalPos.height) - m_lineHeight;
    if ( sz_dec < 0 ) sz_dec = 0;

    finalPos.y += y_adj;
    finalPos.height -= (y_adj+sz_dec);

#ifndef ibPG_TEXTCTRLXADJUST
    int textCtrlXAdjust = ibPG_XBEFORETEXT - 1 + ibPG_TEXTCTRLXADJUST2;

    ibTextCtrl* tc = static_cast<ibTextCtrl*>(ctrl);
    tc->SetMargins(0);
#else
    int textCtrlXAdjust = ibPG_TEXTCTRLXADJUST;
#endif

    finalPos.x += textCtrlXAdjust;
    finalPos.width -= textCtrlXAdjust;

    finalPos.Offset(offset);

    ctrl->SetSize(finalPos);
}

// -----------------------------------------------------------------------

wxWindow* ibPropertyGrid::GenerateEditorTextCtrl( const wxPoint& pos,
                                                  const wxSize& sz,
                                                  const wxString& value,
                                                  wxWindow* secondary,
                                                  int extraStyle,
                                                  int maxLen,
                                                  unsigned int forColumn )
{
    ibPGProperty* prop = GetSelection();
    wxASSERT(prop);

    int tcFlags = wxTE_PROCESS_ENTER | extraStyle;

    if ( prop->HasFlag(ibPGFlags::ReadOnly) && forColumn == 1 )
        tcFlags |= wxTE_READONLY;

    wxPoint p(pos);
    wxSize s(sz);

   // Need to reduce width of text control on Mac
#if defined(__WXMAC__)
    s.x -= 4;
#endif

    // For label editors, trim the size to allow better splitter grabbing
    if ( forColumn != 1 )
        s.x -= 2;

    // Take button into account
    if ( secondary )
    {
        s.x -= (secondary->GetSize().x + ibPG_TEXTCTRL_AND_BUTTON_SPACING);
        m_iFlags &= ~(ibPG_FL_PRIMARY_FILLS_ENTIRE);
    }

    // If the height is significantly higher, then use border, and fill the rect exactly.
    const bool hasSpecialSize = (sz.y - m_lineHeight) > 5;

    wxWindow* ctrlParent = GetPanel();

    if ( !hasSpecialSize )
        tcFlags |= wxBORDER_NONE;

    ibTextCtrl* tc = new ibTextCtrl();

#if defined(__WXMSW__)
    tc->Hide();
#endif
    SetupTextCtrlValue(value);
    tc->Create(ctrlParent,wxID_ANY,value, p, s,tcFlags);

#if defined(__WXMSW__)
    // On Windows, we need to override read-only text ctrl's background
    // colour to white. One problem with native 'grey' background is that
    // tc->GetBackgroundColour() doesn't seem to return correct value
    // for it.
    if ( tcFlags & wxTE_READONLY )
    {
        wxVisualAttributes vattrs = tc->GetDefaultAttributes();
        tc->SetBackgroundColour(vattrs.colBg);
    }
#endif

    // This code is repeated from DoSelectProperty(). However, font boldness
    // must be set before margin is set up below in FixPosForTextCtrl().
    if ( forColumn == 1 &&
         prop->HasFlag(ibPGFlags::Modified) &&
         HasFlag(ibPG_BOLD_MODIFIED) )
         tc->SetFont( m_captionFont );

    // Center the control vertically
    if ( !hasSpecialSize )
        FixPosForTextCtrl(tc, forColumn);

    if ( forColumn != 1 )
    {
        tc->SetBackgroundColour(m_colSelBack);
        tc->SetForegroundColour(m_colSelFore);
    }

#ifdef __WXMSW__
    tc->Show();
    if ( secondary )
        secondary->Show();
#endif

    // Set maximum length
    if ( maxLen > 0 )
        tc->SetMaxLength( maxLen );

    ibVariant attrVal = prop->GetAttribute(ibPG_ATTR_AUTOCOMPLETE);
    if ( attrVal.GetType() != ibValueTypes::TYPE_EMPTY )
    {
        wxASSERT(attrVal.GetClassName() == wxArrayStringVariantData::s_className);
        tc->AutoComplete(wxArrayStringRefFromVariant(attrVal));
    }

    // Set hint text
    tc->SetHint(prop->GetHintText());

    return tc;
}

// -----------------------------------------------------------------------

wxWindow* ibPropertyGrid::GenerateEditorButton( const wxPoint& pos, const wxSize& sz )
{
    ibPGProperty* selected = GetSelection();
    wxASSERT(selected);

    const wxString label(L"\u2026"); // "Horizontal ellipsis" character

    int dim = sz.y + 2*ibPG_BUTTON_BORDER_WIDTH;

    wxPoint p(pos.x + sz.x, pos.y - ibPG_BUTTON_BORDER_WIDTH);
    wxSize s(wxDefaultCoord, dim);

    ibButton* but = new ibButton();
  #ifdef __WXMSW__
    but->Hide();
  #endif
    but->Create(GetPanel(),wxID_ANY,label,p,s,wxWANTS_CHARS|wxBU_EXACTFIT);
    but->SetFont(GetFont().GetBaseFont().Smaller());
    // If button is narrow make it a square and move it to the correct position
    s = but->GetSize();
    if ( s.x < s.y )
    {
        s.x = s.y;
        but->SetSize(s);
    }
    p.x = pos.x + sz.x - s.x;
    but->Move(p);

    if ( selected->HasFlag(ibPGFlags::ReadOnly) && !selected->HasFlag(ibPGPropertyFlags_ActiveButton) )
        but->Disable();

    return but;
}

// -----------------------------------------------------------------------

wxWindow* ibPropertyGrid::GenerateEditorTextCtrlAndButton( const wxPoint& pos,
                                                           const wxSize& sz,
                                                           wxWindow** psecondary,
                                                           int limitedEditing,
                                                           ibPGProperty* property )
{
    wxWindow* but = GenerateEditorButton(pos,sz);
    *psecondary = but;

    if ( limitedEditing )
    {
    #ifdef __WXMSW__
        // There is button Show in GenerateEditorTextCtrl as well
        but->Show();
    #endif
        return nullptr;
    }

    wxString text;

    if ( !property->IsValueUnspecified() )
        text = property->GetValueAsString(property->HasFlag(ibPGFlags::ReadOnly) ? ibPGPropValFormatFlags::Null : ibPGPropValFormatFlags::EditableValue);

    return GenerateEditorTextCtrl(pos, sz, text, but, 0, property->GetMaxLength());
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetEditorAppearance( const ibPGCell& cell,
                                          bool unspecified )
{
    ibPGProperty* property = GetSelection();
    if ( !property )
        return;
    wxWindow* ctrl = GetEditorControl();
    if ( !ctrl )
        return;

    property->GetEditorClass()->SetControlAppearance( this,
                                                      property,
                                                      ctrl,
                                                      cell,
                                                      m_editorAppearance,
                                                      unspecified );

    m_editorAppearance = cell;
}

// -----------------------------------------------------------------------

ibTextCtrl* ibPropertyGrid::GetEditorTextCtrl() const
{
    wxWindow* wnd = GetEditorControl();

    if ( !wnd )
        return nullptr;

    ibTextCtrl* tc = wxDynamicCast(wnd, ibTextCtrl);
    if ( tc )
        return tc;

    ibComboBox* cb = wxDynamicCast(wnd, ibComboBox);
    if ( cb )
    {
        return cb->GetTextCtrl();
    }

    return nullptr;
}

// -----------------------------------------------------------------------

ibPGEditor* ibPropertyGridInterface::GetEditorByName( const wxString& editorName )
{
    auto it = ibPGGlobalVars->m_mapEditorClasses.find(editorName);
    return it == ibPGGlobalVars->m_mapEditorClasses.end() ? nullptr : it->second;
}

// -----------------------------------------------------------------------
// ibPGEditorDialogAdapter
// -----------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibPGEditorDialogAdapter, wxObject);

bool ibPGEditorDialogAdapter::ShowDialog( ibPropertyGrid* propGrid, ibPGProperty* property )
{
    if ( !propGrid->EditorValidate() )
        return false;

    bool res = DoShowDialog( propGrid, property );

    if ( res )
    {
        propGrid->ValueChangeInEvent( m_value );
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------
// ibPGMultiButton
// -----------------------------------------------------------------------

ibPGMultiButton::ibPGMultiButton( ibPropertyGrid* pg, const wxSize& sz )
    : ibWindow( pg->GetPanel(), wxID_ANY, wxPoint(-100,-100), wxSize(0, sz.y + 2*ibPG_BUTTON_BORDER_WIDTH) ),
      m_fullEditorSize(sz), m_buttonsWidth(0)
{
    SetBackgroundColour(pg->GetCellBackgroundColour());
    SetFont(pg->GetFont().GetBaseFont().Smaller());
}

void ibPGMultiButton::Finalize( ibPropertyGrid* WXUNUSED(propGrid),
                                const wxPoint& pos )
{
    Move( pos.x + m_fullEditorSize.x - m_buttonsWidth, pos.y - ibPG_BUTTON_BORDER_WIDTH, wxSIZE_ALLOW_MINUS_ONE);
}

#if wxUSE_BMPBUTTON

void ibPGMultiButton::Add( const wxBitmapBundle& bitmap, int itemid )
{
    wxSize sz = GetSize();
    ibBitmapButton* button = new ibBitmapButton(this, itemid, bitmap,
                           wxPoint(sz.x, 0), wxSize(wxDefaultCoord, sz.y),
                           wxBU_EXACTFIT);
    // If button is narrow make it a square
    wxSize szBtn = button->GetSize();
    if ( szBtn.x < szBtn.y )
        button->SetSize(wxSize(szBtn.y, szBtn.y));

    DoAddButton( button, sz );
}
#endif

void ibPGMultiButton::Add( const wxString& label, int itemid )
{
    wxSize sz = GetSize();
    ibButton* button = new ibButton(this, itemid, label,
                    wxPoint(sz.x, 0), wxSize(wxDefaultCoord, sz.y), wxBU_EXACTFIT);
    // If button is narrow make it a square
    wxSize szBtn = button->GetSize();
    if ( szBtn.x < szBtn.y )
        button->SetSize(wxSize(szBtn.y, szBtn.y));

    DoAddButton( button, sz );
}

void ibPGMultiButton::DoAddButton( wxWindow* button,
                                   const wxSize& sz )
{
    m_buttons.push_back(button);
    int bw = button->GetSize().x;
    SetSize(wxSize(sz.x+bw,sz.y));
    m_buttonsWidth += bw;
}

// -----------------------------------------------------------------------
