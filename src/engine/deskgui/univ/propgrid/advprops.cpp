// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, src/propgrid/advprops.cpp): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/propgrid/advprops.cpp
// Purpose:     ibPropertyGrid Advanced Properties (font, colour, etc.)
// Author:      Jaakko Salli
// Created:     2004-09-25
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"


#ifndef WX_PRECOMP
    #include "wx/dc.h"
    #include "wx/settings.h"
    #include "wx/textctrl.h"
    #include "wx/wxcrtvararg.h"
#endif

#include "deskgui/univ/propgrid/propgrid.h"

#if ibPG_INCLUDE_ADVPROPS

#include "deskgui/univ/propgrid/advprops.h"
#include "deskgui/univ/propgrid/private.h"

#include "deskgui/univ/ctrl/comboBox.h"     // ibComboBox — the choice editor
#include "wx/uilocale.h"

#include "deskgui/univ/ctrl/spinButton.h"   // ibSpinButton — the number's arrows
#include "deskgui/univ/theme.h"             // the font's and the cursor's sample: the theme's control surface
#include "deskgui/univ/colourScheme.h"

#include <limits>
#include <array>

// SEAM vs wx: a colour with alpha is drawn through a graphics context on every port - wx did it where the DC itself
// draws none (MSW), and the context draws it on any (2026-11-11)
#define ibPG_USE_GC_FOR_ALPHA  wxUSE_GRAPHICS_CONTEXT

#if ibPG_USE_GC_FOR_ALPHA
#include "wx/dcgraph.h"
#ifndef WX_PRECOMP
#include "wx/dcclient.h" // for wxDynamicCast purposes
#include "wx/dcmemory.h" // for wxDynamicCast purposes
#endif // WX_PRECOMP
#if wxUSE_METAFILE
    #include "wx/metafile.h"  // for wxDynamicCast purposes
#endif // wxUSE_METAFILE
#endif // ibPG_USE_GC_FOR_ALPHA

// -----------------------------------------------------------------------

#if defined(__WXMSW__)
    #define ibPG_CAN_DRAW_CURSOR           1
#elif defined(__WXGTK__)
    #define ibPG_CAN_DRAW_CURSOR           1
#elif defined(__WXMAC__)
    #define ibPG_CAN_DRAW_CURSOR           0
#else
    #define ibPG_CAN_DRAW_CURSOR           0
#endif

constexpr int ibPG_INT_MIN = std::numeric_limits<int>::min();
constexpr int ibPG_INT_MAX = std::numeric_limits<int>::max();

// -----------------------------------------------------------------------
// Value type related
// -----------------------------------------------------------------------


// Implement dynamic class for type value.
wxIMPLEMENT_DYNAMIC_CLASS(ibColourPropertyValue, wxObject);

bool operator == (const ibColourPropertyValue& a, const ibColourPropertyValue& b)
{
    return ( ( a.m_colour == b.m_colour ) && (a.m_type == b.m_type) );
}

bool operator == (const wxArrayInt& array1, const wxArrayInt& array2)
{
    if ( array1.size() != array2.size() )
        return false;
    for ( size_t i = 0; i < array1.size(); i++ )
    {
        if ( array1[i] != array2[i] )
            return false;
    }
    return true;
}

// -----------------------------------------------------------------------
// wxSpinCtrl-based property editor
// -----------------------------------------------------------------------

#if wxUSE_SPINBTN

// SEAM vs wx: on every port - the button is the library's and the class below reads only wx's mouse events (wx kept
// it to MSW)

//
// This class implements ability to rapidly change "spin" value
// by moving mouse when one of the spin buttons is depressed.
class ibPGSpinButton : public ibSpinButton
{
public:
    ibPGSpinButton() : ibSpinButton()
        , m_spins(1)
        , m_hasCapture(false)
    {
        Bind(wxEVT_LEFT_DOWN, &ibPGSpinButton::OnMouseLeftDown, this);
        Bind(wxEVT_LEFT_UP, &ibPGSpinButton::OnMouseLeftUp, this);
        Bind(wxEVT_MOTION, &ibPGSpinButton::OnMouseMove, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, &ibPGSpinButton::OnMouseCaptureLost, this);
    }

    int GetSpins() const
    {
        return m_spins;
    }

private:
    wxPoint m_ptPosition;

    // Having a separate spins variable allows us to handle validation etc. for
    // multiple spin events at once (with quick mouse movements there could be
    // hundreds of 'spins' being done at once). Technically things like this
    // should be stored in event (wxSpinEvent in this case), but there probably
    // isn't anything there that can be reliably reused.
    int     m_spins;

    // SpinButton seems to be a special for mouse capture, so we may need track
    // privately whether mouse is actually captured.
    bool    m_hasCapture;

    void Capture()
    {
        if ( !m_hasCapture )
        {
            CaptureMouse();
            m_hasCapture = true;
        }

        SetCursor(wxCURSOR_SIZENS);
    }
    void Release()
    {
        if ( m_hasCapture )
        {
            ReleaseMouse();
            m_hasCapture = false;
        }

        wxWindow *parent = GetParent();
        if ( parent )
            SetCursor(parent->GetCursor());
        else
            SetCursor(wxNullCursor);
    }

    void OnMouseLeftDown(wxMouseEvent& evt)
    {
        m_ptPosition = evt.GetPosition();
        evt.Skip();
    }

    void OnMouseLeftUp(wxMouseEvent& evt)
    {
        Release();
        evt.Skip();
    }

    void OnMouseMove(wxMouseEvent& evt)
    {
        if ( evt.LeftIsDown() )
        {
            int dy = m_ptPosition.y - evt.GetPosition().y;
            if ( dy )
            {
                Capture();
                m_ptPosition = evt.GetPosition();

                wxSpinEvent evtscroll( (dy >= 0) ? wxEVT_SCROLL_LINEUP :
                                                    wxEVT_SCROLL_LINEDOWN,
                                        GetId() );
                evtscroll.SetEventObject(this);

                wxASSERT( m_spins == 1 );

                m_spins = abs(dy);
                GetEventHandler()->ProcessEvent(evtscroll);
                m_spins = 1;
            }
        }

        evt.Skip();
    }

    void OnMouseCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
    {
        Release();
    }
};


IB_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(SpinCtrl,
                                      ibPGSpinCtrlEditor,
                                      ibPGEditor)


// Destructor. It is useful to reset the global pointer in it.
ibPGSpinCtrlEditor::~ibPGSpinCtrlEditor()
{
    ibPG_EDITOR(SpinCtrl) = nullptr;
}

// Create controls and initialize event handling.
ibPGWindowList ibPGSpinCtrlEditor::CreateControls( ibPropertyGrid* propgrid, ibPGProperty* property,
                                                   const wxPoint& pos, const wxSize& sz ) const
{
    ibSpinButton* wnd2;
    wxSize tcSz;

    ibNumericProperty* prop = wxDynamicCast(property, ibNumericProperty);
    if ( prop )
    {
        const int margin = 1;
        if ( prop->UseSpinMotion() )
        {
            wnd2 = new ibPGSpinButton();
        }
        else
        {
            wnd2 = new ibSpinButton();
        }

        wnd2->Hide();
        wnd2->Create(propgrid->GetPanel(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_VERTICAL);
        // Scale spin button to the required height (row height)
        wxSize butSz = wnd2->GetBestSize();
        double sc = (double)sz.y / butSz.y;
        int butWidth = wxMax(18, wxRound(sc*butSz.x));
        tcSz.Set(sz.x - butWidth - margin, sz.y);
        wnd2->SetSize(pos.x + tcSz.x + margin, pos.y, butWidth, sz.y);
        wnd2->SetRange(ibPG_INT_MIN, ibPG_INT_MAX);
        wnd2->SetValue(0);
    }
    else
    {
        wxFAIL_MSG( "SpinCtrl editor can be assigned only to numeric property" );
        tcSz.Set(sz.x, sz.y);
        wnd2 = nullptr;
    }

    wxWindow* wnd1 = ibPGTextCtrlEditor::CreateControls(propgrid, property, pos, tcSz).GetPrimary();
#if wxUSE_VALIDATORS
    // Let's add validator to make sure only numbers can be entered
    ibTextValidator validator(wxFILTER_NUMERIC, &m_tempString);
    wnd1->SetValidator(validator);
#endif

    return ibPGWindowList(wnd1, wnd2);
}

// Control's events are redirected here
bool ibPGSpinCtrlEditor::OnEvent(ibPropertyGrid* propgrid, ibPGProperty* property,
    wxWindow* wnd, wxEvent& event) const
{
    ibNumericProperty* prop = wxDynamicCast(property, ibNumericProperty);
    if ( prop )
    {
        wxEventType evtType = event.GetEventType();
        bool bigStep = false;

        if ( evtType == wxEVT_KEY_DOWN )
        {
            wxKeyEvent& keyEvent = (wxKeyEvent&)event;
            int keycode;
            keycode = keyEvent.GetKeyCode();

            if ( keycode == WXK_UP )
                evtType = wxEVT_SCROLL_LINEUP;
            else if ( keycode == WXK_DOWN )
                evtType = wxEVT_SCROLL_LINEDOWN;
            else if ( keycode == WXK_PAGEUP )
            {
                evtType = wxEVT_SCROLL_LINEUP;
                bigStep = true;
            }
            else if ( keycode == WXK_PAGEDOWN )
            {
                evtType = wxEVT_SCROLL_LINEDOWN;
                bigStep = true;
            }
        }

        if ( evtType == wxEVT_SCROLL_LINEUP || evtType == wxEVT_SCROLL_LINEDOWN )
        {
            int spins = 1;
            if ( prop->UseSpinMotion() )
            {
                ibPGSpinButton* spinButton =
                    (ibPGSpinButton*)propgrid->GetEditorControlSecondary();

                if ( spinButton )
                    spins = spinButton->GetSpins();
            }

            long stepScale = (evtType == wxEVT_SCROLL_LINEUP) ? 1L : -1L;
            if ( bigStep )
                stepScale *= 10L;
            stepScale *= spins;

            ibVariant v = prop->AddSpinStepValue(stepScale);
            SetControlStringValue(prop, propgrid->GetEditorControl(), prop->ValueToString(v));
            return true;
        }
    }
    return ibPGTextCtrlEditor::OnEvent(propgrid, property, wnd, event);
}
#endif // wxUSE_SPINBTN


// -----------------------------------------------------------------------
// ibDatePickerCtrl-based property editor
// -----------------------------------------------------------------------

#if wxUSE_DATEPICKCTRL


#include "deskgui/univ/ctrl/datePickerCtrl.h"
#include "wx/dateevt.h"

class ibPGDatePickerCtrlEditor : public ibPGEditor
{
    wxDECLARE_DYNAMIC_CLASS(ibPGDatePickerCtrlEditor);
public:
    virtual ~ibPGDatePickerCtrlEditor();

    wxString GetName() const override;
    virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid,
                                          ibPGProperty* property,
                                          const wxPoint& pos,
                                          const wxSize& size) const override;
    virtual void UpdateControl( ibPGProperty* property, wxWindow* wnd ) const override;
    virtual bool OnEvent( ibPropertyGrid* propgrid, ibPGProperty* property,
        wxWindow* wnd, wxEvent& event ) const override;
    virtual bool GetValueFromControl( ibVariant& variant, ibPGProperty* property, wxWindow* wnd ) const override;
    virtual void SetValueToUnspecified( ibPGProperty* WXUNUSED(property), wxWindow* wnd ) const override;
};


IB_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(DatePickerCtrl,
                                      ibPGDatePickerCtrlEditor,
                                      ibPGEditor)


ibPGDatePickerCtrlEditor::~ibPGDatePickerCtrlEditor()
{
    ibPG_EDITOR(DatePickerCtrl) = nullptr;
}

ibPGWindowList ibPGDatePickerCtrlEditor::CreateControls( ibPropertyGrid* propgrid,
                                                         ibPGProperty* property,
                                                         const wxPoint& pos,
                                                         const wxSize& sz ) const
{
    ibDateProperty* prop = wxDynamicCast(property, ibDateProperty);
    wxCHECK_MSG(prop, nullptr, "ibDatePickerCtrl editor can only be used with ibDateProperty or derivative.");

    // Use two stage creation to allow cleaner display (SEAM vs wx: on every port, the control's own height as on MSW)
    ibDatePickerCtrl* ctrl = new ibDatePickerCtrl();
    ctrl->Hide();
    wxSize useSz = wxDefaultSize;
    useSz.x = sz.x;

    wxDateTime dateValue(wxInvalidDateTime);

    ibVariant value = prop->GetValue();
    if ( value.GetType() == ibValueTypes::TYPE_DATE )
        dateValue = typeConv::DateTimeToWxDateTime(value.GetDate());

    ctrl->Create(propgrid->GetPanel(),
                 wxID_ANY,
                 dateValue,
                 pos,
                 useSz,
                 prop->GetDatePickerStyle() | wxNO_BORDER);

    ctrl->Show();

    return ctrl;
}

// Copies value from property to control
void ibPGDatePickerCtrlEditor::UpdateControl( ibPGProperty* property,
                                              wxWindow* wnd ) const
{
    ibDatePickerCtrl* ctrl = wxDynamicCast(wnd, ibDatePickerCtrl);
    wxCHECK_RET(ctrl, "Only ibDatePickerCtrl editor can be updated");

    wxDateTime dateValue(wxInvalidDateTime);
    ibVariant v(property->GetValue());
    if ( v.GetType() == ibValueTypes::TYPE_DATE )
        dateValue = typeConv::DateTimeToWxDateTime(v.GetDate());

    ctrl->SetValue( dateValue );
}

// Control's events are redirected here
bool ibPGDatePickerCtrlEditor::OnEvent( ibPropertyGrid* WXUNUSED(propgrid),
                                        ibPGProperty* WXUNUSED(property),
                                        wxWindow* WXUNUSED(wnd),
                                        wxEvent& event ) const
{
    if ( event.GetEventType() == wxEVT_DATE_CHANGED )
        return true;

    return false;
}

bool ibPGDatePickerCtrlEditor::GetValueFromControl( ibVariant& variant, ibPGProperty* WXUNUSED(property), wxWindow* wnd ) const
{
    ibDatePickerCtrl* ctrl = wxDynamicCast(wnd, ibDatePickerCtrl);
    wxCHECK_MSG(ctrl, false, "Value can be retrieved only from ibDatePickerCtrl editor");

    variant = IBVARIANT(ctrl->GetValue());

    return true;
}

void ibPGDatePickerCtrlEditor::SetValueToUnspecified( ibPGProperty* property,
                                                      wxWindow* wnd ) const
{
    ibDatePickerCtrl* ctrl = wxDynamicCast(wnd, ibDatePickerCtrl);
    wxCHECK_RET(ctrl, "Only ibDatePickerCtrl editor can be updated");

    ibDateProperty* prop = wxDynamicCast(property, ibDateProperty);
    wxCHECK_RET(prop, "ibDatePickerCtrl editor can only be used with ibDateProperty or derivative.");

    long datePickerStyle = prop->GetDatePickerStyle();
    if ( datePickerStyle & wxDP_ALLOWNONE )
        ctrl->SetValue(wxInvalidDateTime);
}

#endif // wxUSE_DATEPICKCTRL


// -----------------------------------------------------------------------
// ibFontProperty
// -----------------------------------------------------------------------

#include "wx/fontdlg.h"
#include "wx/fontenum.h"

//
// NB: Do not use wxS here since unlike wxT it doesn't translate to wxChar*
//

static constexpr std::array<const wxChar*, 8+1> gs_fp_es_family_labels
{
    wxT("Default"), wxT("Decorative"),
    wxT("Roman"), wxT("Script"),
    wxT("Swiss"), wxT("Modern"),
    wxT("Teletype"), wxT("Unknown"),
    nullptr
};

#if wxCHECK_CXX_STD(201402L) // [] is constexpr since C++14
static_assert(gs_fp_es_family_labels[gs_fp_es_family_labels.size() - 1] == nullptr,
    "nullptr has to mark the end of table");
#endif // >+ C++ 14

static constexpr std::array<long, 8> gs_fp_es_family_values
{
    wxFONTFAMILY_DEFAULT, wxFONTFAMILY_DECORATIVE,
    wxFONTFAMILY_ROMAN, wxFONTFAMILY_SCRIPT,
    wxFONTFAMILY_SWISS, wxFONTFAMILY_MODERN,
    wxFONTFAMILY_TELETYPE, wxFONTFAMILY_UNKNOWN
};

static_assert(gs_fp_es_family_values.size() == gs_fp_es_family_labels.size() - 1,
    "Values table has to have one item less than labels table");

static constexpr std::array<const wxChar*, 3+1> gs_fp_es_style_labels
{
    wxT("Normal"),
    wxT("Slant"),
    wxT("Italic"),
    nullptr
};

#if wxCHECK_CXX_STD(201402L) // [] is constexpr since C++14
static_assert(gs_fp_es_style_labels[gs_fp_es_style_labels.size() - 1] == nullptr,
    "nullptr has to mark the end of table");
#endif // >= C++ 14

static constexpr std::array<long,3> gs_fp_es_style_values
{
    wxFONTSTYLE_NORMAL,
    wxFONTSTYLE_SLANT,
    wxFONTSTYLE_ITALIC
};

static_assert(gs_fp_es_style_values.size() == gs_fp_es_style_labels.size() - 1,
    "Values table has to have one item less than labels table");

static constexpr std::array<const wxChar*, 10+1> gs_fp_es_weight_labels
{
    wxT("Thin"),
    wxT("ExtraLight"),
    wxT("Light"),
    wxT("Normal"),
    wxT("Medium"),
    wxT("SemiBold"),
    wxT("Bold"),
    wxT("ExtraBold"),
    wxT("Heavy"),
    wxT("ExtraHeavy"),
    nullptr
};

#if wxCHECK_CXX_STD(201402L) // [] is constexpr since C++14
static_assert(gs_fp_es_weight_labels[gs_fp_es_weight_labels.size() - 1] == nullptr,
    "nullptr has to mark the end of table");
#endif // >= C++ 14

static constexpr std::array<long, 10> gs_fp_es_weight_values
{
    wxFONTWEIGHT_THIN,
    wxFONTWEIGHT_EXTRALIGHT,
    wxFONTWEIGHT_LIGHT,
    wxFONTWEIGHT_NORMAL,
    wxFONTWEIGHT_MEDIUM,
    wxFONTWEIGHT_SEMIBOLD,
    wxFONTWEIGHT_BOLD,
    wxFONTWEIGHT_EXTRABOLD,
    wxFONTWEIGHT_HEAVY,
    wxFONTWEIGHT_EXTRAHEAVY
};

static_assert(gs_fp_es_weight_values.size() == gs_fp_es_weight_labels.size() - 1,
    "Values table has to have one item less than labels table");

// Class body is in advprops.h


ibPG_IMPLEMENT_PROPERTY_CLASS(ibFontProperty,ibEditorDialogProperty,TextCtrlAndButton)


ibFontProperty::ibFontProperty( const wxString& label, const wxString& name,
                                const wxFont& value )
    : ibEditorDialogProperty(label,name)
{
    SetValue(IBVARIANT(value));

    // Initialize font family choices list
    if ( !ibPGGlobalVars->m_fontFamilyChoices )
    {
        wxArrayString faceNames = wxFontEnumerator::GetFacenames();

        faceNames.Sort();

        ibPGGlobalVars->m_fontFamilyChoices = new ibPGChoices(faceNames);
    }

    wxFont font;
    font << m_value;

    /* TRANSLATORS: Label of font point size */
    AddPrivateChild( new ibIntProperty( _("Point Size"),
                     wxS("Point Size"),(long)font.GetPointSize() ) );

    wxString faceName = font.GetFaceName();
    // If font was not in there, add it now
    if ( !faceName.empty() &&
         ibPGGlobalVars->m_fontFamilyChoices->Index(faceName) == wxNOT_FOUND )
        ibPGGlobalVars->m_fontFamilyChoices->AddAsSorted(faceName);

    /* TRANSLATORS: Label of font face name */
    ibPGProperty* p = new ibEnumProperty(_("Face Name"), wxS("Face Name"),
                                         *ibPGGlobalVars->m_fontFamilyChoices);

    p->SetValueFromString(faceName, ibPGPropValFormatFlags::FullValue);

    AddPrivateChild( p );

    /* TRANSLATORS: Label of font style */
    AddPrivateChild( new ibEnumProperty(_("Style"), wxS("Style"),
                     gs_fp_es_style_labels.data(), gs_fp_es_style_values.data(),
                     font.GetStyle()) );

    /* TRANSLATORS: Label of font weight */
    AddPrivateChild( new ibEnumProperty(_("Weight"), wxS("Weight"),
                     gs_fp_es_weight_labels.data(), gs_fp_es_weight_values.data(),
                     font.GetWeight()) );

    /* TRANSLATORS: Label of underlined font */
    AddPrivateChild( new ibBoolProperty(_("Underlined"), wxS("Underlined"),
                     font.GetUnderlined()) );

    /* TRANSLATORS: Label of font family */
    AddPrivateChild( new ibEnumProperty(_("Family"), wxS("PointSize"),
                     gs_fp_es_family_labels.data(), gs_fp_es_family_values.data(),
                     font.GetFamily()) );
}

void ibFontProperty::OnSetValue()
{
    wxFont font;
    font << m_value;

    if ( !font.IsOk() )
    {
        m_value = IBVARIANT(*wxNORMAL_FONT);
    }
}

wxString ibFontProperty::ValueToString( ibVariant& value,
                                        ibPGPropValFormatFlags flags ) const
{
    return ibEditorDialogProperty::ValueToString(value, flags);
}

bool ibFontProperty::DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value)
{
    wxFont font;

    wxASSERT_MSG(value.GetClassName() == wxFontVariantData::s_className, "Function called for incompatible property");
    if ( value.GetClassName() == wxFontVariantData::s_className )
        font << value;

    wxFontData data;
    data.SetInitialFont(font);
    data.SetColour(*wxBLACK);

    wxFontDialog dlg(pg->GetPanel(), data);
    if ( !m_dlgTitle.empty() )
    {
        dlg.SetTitle(m_dlgTitle);
    }
    if ( dlg.ShowModal() == wxID_OK )
    {
        value = IBVARIANT(dlg.GetFontData().GetChosenFont());
        return true;
    }

    return false;
}

void ibFontProperty::RefreshChildren()
{
    if ( !GetChildCount() ) return;
    wxFont font;
    font << m_value;
    Item(0)->SetValue( (long)font.GetPointSize() );
    Item(1)->SetValueFromString( font.GetFaceName(), ibPGPropValFormatFlags::FullValue );
    Item(2)->SetValue( (long)font.GetStyle() );
    Item(3)->SetValue( (long)font.GetWeight() );
    Item(4)->SetValue( font.GetUnderlined() );
    Item(5)->SetValue( (long)font.GetFamily() );
}

ibVariant ibFontProperty::ChildChanged( ibVariant& thisValue,
                                        int ind,
                                        ibVariant& childValue ) const
{
    wxFont font;
    font << thisValue;

    if ( ind == 0 )
    {
        font.SetPointSize( static_cast<int>(childValue.GetInteger()) );
    }
    else if ( ind == 1 )
    {
        wxString faceName;
        int faceIndex = static_cast<int>(childValue.GetInteger());

        if ( faceIndex >= 0 )
            faceName = ibPGGlobalVars->m_fontFamilyChoices->GetLabel(faceIndex);

        font.SetFaceName( faceName );
    }
    else if ( ind == 2 )
    {
        int st = static_cast<int>(childValue.GetInteger());
        if ( st != wxFONTSTYLE_NORMAL &&
             st != wxFONTSTYLE_SLANT &&
             st != wxFONTSTYLE_ITALIC )
             st = wxFONTWEIGHT_NORMAL;
        font.SetStyle( static_cast<wxFontStyle>(st) );
    }
    else if ( ind == 3 )
    {
        int wt = static_cast<int>(childValue.GetInteger());
        if ( wt < wxFONTWEIGHT_THIN || wt > wxFONTWEIGHT_MAX )
             wt = wxFONTWEIGHT_NORMAL;
        font.SetWeight( static_cast<wxFontWeight>(wt) );
    }
    else if ( ind == 4 )
    {
        font.SetUnderlined( childValue.GetBoolean() );
    }
    else if ( ind == 5 )
    {
        int fam = static_cast<int>(childValue.GetInteger());
        if ( fam < wxFONTFAMILY_DEFAULT ||
             fam > wxFONTFAMILY_TELETYPE )
             fam = wxFONTFAMILY_DEFAULT;
        font.SetFamily( static_cast<wxFontFamily>(fam) );
    }

    return IBVARIANT(font);
}

/*
wxSize ibFontProperty::OnMeasureImage() const
{
    return ibPG_DEFAULT_IMAGE_SIZE;
}

void ibFontProperty::OnCustomPaint(wxDC& dc,
                                        const wxRect& rect,
                                        ibPGPaintData& paintData)
{
    wxString drawFace;
    if ( paintData.m_choiceItem >= 0 )
        drawFace = ibPGGlobalVars->m_fontFamilyChoices->GetLabel(paintData.m_choiceItem);
    else
        drawFace = m_value_wxFont.GetFaceName();

    if ( !drawFace.empty() )
    {
        // Draw the background
        dc.SetBrush( wxTHEME_COLOUR(CONTROL) );
        //dc.SetBrush( *wxWHITE_BRUSH );
        //dc.SetPen( *wxMEDIUM_GREY_PEN );
        dc.DrawRectangle( rect );

        wxFont oldFont = dc.GetFont();
        dc.SetFont(wxFontInfo(oldFont.GetPointSize().Bold().FaceName(drawFace));

        dc.SetTextForeground( wxTHEME_COLOUR(CONTROL_TEXT) );
        dc.DrawText( wxS("Aa"), rect.x+2, rect.y+1 );

        dc.SetFont(oldFont);
    }
    else
    {
        // No file - just draw a white box
        dc.SetBrush ( *wxWHITE_BRUSH );
        dc.DrawRectangle ( rect );
    }
}
*/


// -----------------------------------------------------------------------
// ibSystemColourProperty
// -----------------------------------------------------------------------

#include "wx/colordlg.h"

static constexpr std::array<const char*, 25+1> gs_cp_es_syscolour_labels
{
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("AppWorkspace"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("ActiveBorder"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("ActiveCaption"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("ButtonFace"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("ButtonHighlight"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("ButtonShadow"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("ButtonText"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("CaptionText"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("ControlDark"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("ControlLight"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("Desktop"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("GrayText"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("Highlight"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("HighlightText"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("InactiveBorder"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("InactiveCaption"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("InactiveCaptionText"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("Menu"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("Scrollbar"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("Tooltip"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("TooltipText"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("Window"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("WindowFrame"),
    /* TRANSLATORS: Keyword of system colour */ ibTRANSLATE("WindowText"),
    /* TRANSLATORS: Custom colour choice entry */ ibTRANSLATE("Custom"),
    nullptr
};

#if wxCHECK_CXX_STD(201402L) // [] is constexpr since C++14
static_assert(gs_cp_es_syscolour_labels[gs_cp_es_syscolour_labels.size() - 1] == nullptr,
    "nullptr has to mark the end of table");
#endif // >= C++ 14

static constexpr std::array<long, 25> gs_cp_es_syscolour_values
{
    wxSYS_COLOUR_APPWORKSPACE,
    wxSYS_COLOUR_ACTIVEBORDER,
    wxSYS_COLOUR_ACTIVECAPTION,
    wxSYS_COLOUR_BTNFACE,
    wxSYS_COLOUR_BTNHIGHLIGHT,
    wxSYS_COLOUR_BTNSHADOW,
    wxSYS_COLOUR_BTNTEXT ,
    wxSYS_COLOUR_CAPTIONTEXT,
    wxSYS_COLOUR_3DDKSHADOW,
    wxSYS_COLOUR_3DLIGHT,
    wxSYS_COLOUR_BACKGROUND,
    wxSYS_COLOUR_GRAYTEXT,
    wxSYS_COLOUR_HIGHLIGHT,
    wxSYS_COLOUR_HIGHLIGHTTEXT,
    wxSYS_COLOUR_INACTIVEBORDER,
    wxSYS_COLOUR_INACTIVECAPTION,
    wxSYS_COLOUR_INACTIVECAPTIONTEXT,
    wxSYS_COLOUR_MENU,
    wxSYS_COLOUR_SCROLLBAR,
    wxSYS_COLOUR_INFOBK,
    wxSYS_COLOUR_INFOTEXT,
    wxSYS_COLOUR_WINDOW,
    wxSYS_COLOUR_WINDOWFRAME,
    wxSYS_COLOUR_WINDOWTEXT,
    ibPG_COLOUR_CUSTOM
};

static_assert(gs_cp_es_syscolour_values.size() == gs_cp_es_syscolour_labels.size() - 1,
    "Colour values table has to have one item less than colour labels table");

IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_NO_EQ_NO_GETTER(ibColourPropertyValue, DESKGUI_API)
IB_PG_IMPLEMENT_VARIANT_DATA_GETTER(ibColourPropertyValue, DESKGUI_API)

// compared shallow, as wx's IMPLEMENT_VARIANT_OBJECT_EXPORTED_SHALLOWCMP compared it (wxObject::IsSameAs)
bool ibColourPropertyValueVariantData::CompareValueEQ( const ibVariant& data ) const
{
    const ibColourPropertyValueVariantData* other = nullptr;
    return data.ConvertToValue(other) && other->m_value.IsSameAs(m_value);
}

// Class body is in advprops.h

ibPG_IMPLEMENT_PROPERTY_CLASS(ibSystemColourProperty,ibEnumProperty,Choice)


void ibSystemColourProperty::Init( int type, const wxColour& colour )
{
    ibColourPropertyValue cpv;

    cpv.Init(type, colour.IsOk() ? colour : *wxWHITE);

    m_flags |= ibPGPropertyFlags_StaticChoices; // Colour selection cannot be changed.

    m_value = IBVARIANT(cpv);

    OnSetValue();
}


static ibPGChoices gs_wxSystemColourProperty_choicesCache;


ibSystemColourProperty::ibSystemColourProperty( const wxString& label, const wxString& name,
    const ibColourPropertyValue& value )
    : ibEnumProperty( label,
                      name,
                      gs_cp_es_syscolour_labels.data(),
                      gs_cp_es_syscolour_values.data(),
                      &gs_wxSystemColourProperty_choicesCache )
{
        Init( value.m_type, value.m_colour );
}


ibSystemColourProperty::ibSystemColourProperty( const wxString& label, const wxString& name,
    const char* const* labels, const long* values, ibPGChoices* choicesCache,
    const ibColourPropertyValue& value )
    : ibEnumProperty( label, name, labels, values, choicesCache )
{
        Init( value.m_type, value.m_colour );
}


ibSystemColourProperty::ibSystemColourProperty( const wxString& label, const wxString& name,
    const char* const* labels, const long* values, ibPGChoices* choicesCache,
    const wxColour& value )
    : ibEnumProperty( label, name, labels, values, choicesCache )
{
        Init( ibPG_COLOUR_CUSTOM, value );
}

ibColourPropertyValue ibSystemColourProperty::GetVal( const ibVariant* pVariant ) const
{
    if ( !pVariant )
        pVariant = &m_value;

    if ( pVariant->GetType() == ibValueTypes::TYPE_EMPTY )
        return ibColourPropertyValue(ibPG_COLOUR_UNSPECIFIED, wxColour());

    // asked by the class it holds (wx asked its type name; its "wxColour*", a pointer, the core's value holds not)
    if ( pVariant->GetClassName() == ibColourPropertyValueVariantData::s_className )
    {
        ibColourPropertyValue v;
        v << *pVariant;
        return v;
    }

    wxColour col;
    bool variantProcessed = true;

    if ( pVariant->GetClassName() == wxColourVariantData::s_className )
    {
        col << *pVariant;
    }
    else if ( pVariant->GetClassName() == wxArrayIntVariantData::s_className )
    {
        // This code is mostly needed for wxPython bindings, which
        // may offer tuple of integers as colour value.
        wxArrayInt arr;
        arr << *pVariant;

        if ( arr.size() >= 3 )
        {
            int r, g, b;
            int a = 255;

            r = arr[0];
            g = arr[1];
            b = arr[2];
            if ( arr.size() >= 4 )
                a = arr[3];

            col = wxColour(r, g, b, a);
        }
        else
        {
            variantProcessed = false;
        }
    }
    else
    {
        variantProcessed = false;
    }

    if ( !variantProcessed )
        return ibColourPropertyValue(ibPG_COLOUR_UNSPECIFIED, wxColour());

    ibColourPropertyValue v2( ibPG_COLOUR_CUSTOM, col );

    int colInd = ColToInd(col);
    if ( colInd != wxNOT_FOUND )
        v2.m_type = colInd;

    return v2;
}

ibVariant ibSystemColourProperty::DoTranslateVal( ibColourPropertyValue& v ) const
{
    return IBVARIANT(v);
}

int ibSystemColourProperty::ColToInd( const wxColour& colour ) const
{
    const unsigned int i_max = m_choices.GetCount();
    for ( unsigned int i=0; i<i_max; i++ )
    {
        const int ind = m_choices[i].GetValue();
        // Skip custom colour
        if (ind == ibPG_COLOUR_CUSTOM)
            continue;

        if ( colour == GetColour(ind) )
        {
            /*wxLogDebug(wxS("%s(%s): Index %i for ( getcolour(%i,%i,%i), colour(%i,%i,%i))"),
                GetClassName(),GetLabel(),
                (int)i,(int)GetColour(ind).Red(),(int)GetColour(ind).Green(),(int)GetColour(ind).Blue(),
                (int)colour.Red(),(int)colour.Green(),(int)colour.Blue());*/
            return ind;
        }
    }
    return wxNOT_FOUND;
}

void ibSystemColourProperty::OnSetValue()
{
    ibColourPropertyValue val = GetVal(&m_value);

    if ( val.m_type == ibPG_COLOUR_UNSPECIFIED )
    {
        m_value.Reset();
        return;
    }
    else
    {

        if ( val.m_type < ibPG_COLOUR_WEB_BASE )
            val.m_colour = GetColour( val.m_type );

        m_value = TranslateVal(val);
    }

    int ind = wxNOT_FOUND;

    if ( m_value.GetClassName() == ibColourPropertyValueVariantData::s_className )
    {
        ibColourPropertyValue cpv;
        cpv << m_value;
        wxColour col = cpv.m_colour;

        if ( !col.IsOk() )
        {
            SetValueToUnspecified();
            SetIndex(wxNOT_FOUND);
            return;
        }

        if ( cpv.m_type < ibPG_COLOUR_WEB_BASE ||
             !!(m_flags & ibPGPropertyFlags_HideCustomColour) )
        {
            ind = GetIndexForValue(cpv.m_type);
        }
        else
        {
            cpv.m_type = ibPG_COLOUR_CUSTOM;
            ind = GetCustomColourIndex();
        }
    }
    else
    {
        wxColour col;
        col << m_value;

        if ( !col.IsOk() )
        {
            SetValueToUnspecified();
            SetIndex(wxNOT_FOUND);
            return;
        }

        ind = ColToInd(col);

        if ( ind == wxNOT_FOUND &&
             !(m_flags & ibPGPropertyFlags_HideCustomColour) )
            ind = GetCustomColourIndex();
    }

    SetIndex(ind);
}


wxColour ibSystemColourProperty::GetColour( int index ) const
{
    return wxSystemSettings::GetColour( (wxSystemColour)index );
}

wxString ibSystemColourProperty::ColourToString( const wxColour& col,
                                                 int index,
                                                 ibPGPropValFormatFlags flags ) const
{

    if ( index == wxNOT_FOUND )
    {

        if ( !!(flags & ibPGPropValFormatFlags::FullValue) || !!(m_flags & ibPGPropertyFlags_ColourHasAlpha) )
        {
            return wxString::Format(wxS("(%i,%i,%i,%i)"),
                                    (int)col.Red(),
                                    (int)col.Green(),
                                    (int)col.Blue(),
                                    (int)col.Alpha());
        }
        else
        {
            return wxString::Format(wxS("(%i,%i,%i)"),
                                    (int)col.Red(),
                                    (int)col.Green(),
                                    (int)col.Blue());
        }
    }
    else
    {
        return m_choices.GetLabel(index);
    }
}

wxString ibSystemColourProperty::ValueToString( ibVariant& value,
                                                ibPGPropValFormatFlags flags ) const
{
    ibColourPropertyValue val = GetVal(&value);

    int index;

    if ( !!(flags & ibPGPropValFormatFlags::ValueIsCurrent) )
    {
        // GetIndex() only works reliably if ibPGPropValFormatFlags::ValueIsCurrent flag is set,
        // but we should use it whenever possible.
        index = GetIndex();

        // If custom colour was selected, use invalid index, so that
        // ColourToString() will return properly formatted colour text.
        if ( index == GetCustomColourIndex() &&
             !(m_flags & ibPGPropertyFlags_HideCustomColour) )
            index = wxNOT_FOUND;
    }
    else
    {
        index = m_choices.Index(val.m_type);
    }

    return ColourToString(val.m_colour, index, flags);
}


wxSize ibSystemColourProperty::OnMeasureImage( int ) const
{
    return ibPG_DEFAULT_IMAGE_SIZE;
}


int ibSystemColourProperty::GetCustomColourIndex() const
{
    return m_choices.Index(ibPG_COLOUR_CUSTOM);
}


bool ibSystemColourProperty::QueryColourFromUser( ibVariant& variant ) const
{
    wxASSERT( m_value.GetType() != ibValueTypes::TYPE_STRING );
    bool res = false;

    ibPropertyGrid* propgrid = GetGrid();
    wxASSERT( propgrid );

    // Must only occur when user triggers event
    if ( !propgrid->HasInternalFlag(ibPropertyGrid::ibPG_FL_IN_HANDLECUSTOMEDITOREVENT) )
        return res;

    ibColourPropertyValue val = GetVal();

    val.m_type = ibPG_COLOUR_CUSTOM;

    wxColourData data;
    data.SetChooseFull(true);
    data.SetChooseAlpha(!!(m_flags & ibPGPropertyFlags_ColourHasAlpha));
    data.SetColour(val.m_colour);
    for ( int i = 0; i < wxColourData::NUM_CUSTOM; i++ )
    {
        unsigned char n = i*(256/wxColourData::NUM_CUSTOM);
        data.SetCustomColour(i, wxColour(n, n, n));
    }

    wxColourDialog dialog(propgrid, &data);
    if ( dialog.ShowModal() == wxID_OK )
    {
        wxColourData retData = dialog.GetColourData();
        val.m_colour = retData.GetColour();

        variant = DoTranslateVal(val);

        SetValueInEvent(variant);

        res = true;
    }

    return res;
}


bool ibSystemColourProperty::IntToValue( ibVariant& variant, int number, ibPGPropValFormatFlags flags ) const
{
    int index = number;
    const int type = m_choices.GetValue(index);

    if ( type == ibPG_COLOUR_CUSTOM )
    {
         if ( !(flags & ibPGPropValFormatFlags::PropertySpecific) )
            return QueryColourFromUser(variant);

         // Call from event handler.
         // User will be asked for custom color later on in OnEvent().
         ibColourPropertyValue val = GetVal();
         variant = DoTranslateVal(val);
    }
    else
    {
        variant = TranslateVal( type, GetColour(type) );
    }

    return true;
}

// Need to do some extra event handling.
bool ibSystemColourProperty::OnEvent( ibPropertyGrid* propgrid,
                                      wxWindow* WXUNUSED(primary),
                                      wxEvent& event )
{
    bool askColour = false;

    if ( propgrid->IsMainButtonEvent(event) )
    {
        // We need to handle button click in case editor has been
        // switched to one that has wxButton as well.
        askColour = true;
    }
    else if ( event.GetEventType() == wxEVT_COMBOBOX )
    {
        // Must override index detection since at this point GetIndex()
        // will return old value.
        ibComboBox* cb =
            static_cast<ibComboBox*>(propgrid->GetEditorControl());

        if ( cb )
        {
            int index = cb->GetSelection();

            if ( index == GetCustomColourIndex() &&
                    !(m_flags & ibPGPropertyFlags_HideCustomColour) )
                askColour = true;
        }
    }

    if ( askColour && !propgrid->WasValueChangedInEvent() )
    {
        ibVariant variant;
        if ( QueryColourFromUser(variant) )
            return true;
    }
    return false;
}

/*class ibPGColourPropertyRenderer : public ibPGDefaultRenderer
{
public:
    virtual void Render( wxDC& dc, const wxRect& rect,
                         const ibPropertyGrid* propertyGrid, ibPGProperty* property,
                         int WXUNUSED(column), int item, int WXUNUSED(flags) ) const
    {
        wxASSERT( wxDynamicCast(property, ibSystemColourProperty) );
        ibSystemColourProperty* prop = wxStaticCast(property, ibSystemColourProperty);

        dc.SetPen(*wxBLACK_PEN);
        if ( item >= 0 &&
             ( item < (int)(GetCustomColourIndex) || (prop->HasFlag(ibPGPropertyFlags_HideCustomColour)))
           )
        {
            int colInd;
            const wxArrayInt& values = prop->GetValues();
            if ( values.GetChildCount() )
                colInd = values[item];
            else
                colInd = item;
            dc.SetBrush( wxColour( prop->GetColour( colInd ) ) );
        }
        else if ( !prop->IsValueUnspecified() )
            dc.SetBrush( prop->GetVal().m_colour );
        else
            dc.SetBrush( *wxWHITE );

        wxRect imageRect = propertyGrid->GetImageRect(property, item);
        wxLogDebug(wxS("%i, %i"),imageRect.x,imageRect.y);
        dc.DrawRectangle( rect.x+imageRect.x, rect.y+imageRect.y,
                          imageRect.width, imageRect.height );

        wxString text;
        if ( item == -1 )
            text = property->GetValueAsString();
        else
            text = property->GetChoiceString(item);
        DrawText( dc, rect, imageRect.width, text );
    }
protected:
};

ibPGColourPropertyRenderer g_wxPGColourPropertyRenderer;

ibPGCellRenderer* ibSystemColourProperty::GetCellRenderer( int column ) const
{
    if ( column == 1 )
        return &g_wxPGColourPropertyRenderer;
    return ibEnumProperty::GetCellRenderer(column);
}*/

void ibSystemColourProperty::OnCustomPaint( wxDC& dc, const wxRect& rect,
                                            ibPGPaintData& paintdata )
{
    wxColour col;

    if ( paintdata.m_choiceItem >= 0 &&
         paintdata.m_choiceItem < (int)m_choices.GetCount() &&
         (paintdata.m_choiceItem != GetCustomColourIndex() ||
          !!(m_flags & ibPGPropertyFlags_HideCustomColour)) )
    {
        int colInd = m_choices[paintdata.m_choiceItem].GetValue();
        col = GetColour( colInd );
    }
    else if ( !IsValueUnspecified() )
    {
        col = GetVal().m_colour;
    }

    if ( col.IsOk() )
    {
#if ibPG_USE_GC_FOR_ALPHA
        wxGCDC *gdc = nullptr;
        if ( col.Alpha() != wxALPHA_OPAQUE )
        {
            if ( wxPaintDC *paintdc = wxDynamicCast(&dc, wxPaintDC) )
            {
                gdc = new wxGCDC(*paintdc);
            }
            else if ( wxMemoryDC *memdc = wxDynamicCast(&dc, wxMemoryDC) )
            {
                gdc = new wxGCDC(*memdc);
            }
#if wxUSE_METAFILE && defined(wxMETAFILE_IS_ENH)
            else if ( wxMetafileDC *metadc = wxDynamicCast(&dc, wxMetafileDC) )
            {
                gdc = new wxGCDC(*metadc);
            }
#endif
            else
            {
                wxFAIL_MSG( wxS("Unknown wxDC kind") );
            }
        }

        if ( gdc )
        {
            gdc->SetBrush(col);
            gdc->DrawRectangle(rect);
            delete gdc;
        }
        else
#endif // ibPG_USE_GC_FOR_ALPHA
        {
            dc.SetBrush(col);
            dc.DrawRectangle(rect);
        }
    }
}


bool ibSystemColourProperty::StringToValue( ibVariant& value, const wxString& text, ibPGPropValFormatFlags flags ) const
{
    const int custIndex = GetCustomColourIndex();
    wxString custColName;
    if (custIndex != wxNOT_FOUND)
        custColName = m_choices.GetLabel(custIndex);

    wxString colStr(text);
    colStr.Trim(true);
    colStr.Trim(false);

    const bool isCustomColour = (colStr == custColName);

    wxColour customColour;
    bool conversionSuccess = false;

    if ( !isCustomColour )
    {
        if ( colStr.Find(wxS("(")) == 0 )
        {
            // Eliminate whitespace
            colStr.Replace(wxS(" "), wxString());

            int commaCount = colStr.Freq(wxS(','));
            if ( commaCount == 2 )
            {
                // Convert (R,G,B) to rgb(R,G,B)
                colStr = wxS("rgb") + colStr;
            }
            else if ( commaCount == 3 )
            {
                // We have int alpha, CSS format that wxColour takes as
                // input processes float alpha. So, let's parse the colour
                // ourselves instead of trying to convert it to a format
                // that wxColour::FromString() understands.
                int r = -1, g = -1, b = -1, a = -1;
                wxSscanf(colStr, wxS("(%i,%i,%i,%i)"), &r, &g, &b, &a);
                customColour.Set(r, g, b, a);
                conversionSuccess = customColour.IsOk();
            }
        }

        if ( !conversionSuccess )
            conversionSuccess = customColour.Set(colStr);
    }

    if ( !conversionSuccess && m_choices.GetCount() &&
         !(m_flags & ibPGPropertyFlags_HideCustomColour) &&
         isCustomColour )
    {
        if ( !(flags & ibPGPropValFormatFlags::EditableValue ))
        {
            // This really should not occur...
            // wxASSERT(false);
            return false;
        }

        if ( !QueryColourFromUser(value) )
        {
            if ( !(flags & ibPGPropValFormatFlags::PropertySpecific) )
                return false;
            // If query for value comes from the event handler
            // use current pending value to be processed later on in OnEvent().
            SetValueInEvent(value);
        }
    }
    else
    {
        ibColourPropertyValue val;

        bool done = false;

        if ( !conversionSuccess )
        {
            // Try predefined colour first
            int index;
            bool res = ValueFromString_(value, &index, colStr, flags);
            if ( res && index >= 0 )
            {
                val.m_type = index;
                if ( val.m_type < m_choices.GetCount() )
                    val.m_type = m_choices[val.m_type].GetValue();

                // Get proper colour for type.
                val.m_colour = GetColour(val.m_type);

                done = true;
            }
        }
        else
        {
            val.m_type = ibPG_COLOUR_CUSTOM;
            val.m_colour = customColour;
            done = true;
        }

        if ( !done )
        {
            return false;
        }

        value = DoTranslateVal(val);
    }

    return true;
}


bool ibSystemColourProperty::DoSetAttribute( const wxString& name, ibVariant& value )
{
    if ( name == ibPG_COLOUR_ALLOW_CUSTOM )
    {
        bool allow = value.GetBoolean();

        if ( allow && !!(m_flags & ibPGPropertyFlags_HideCustomColour) )
        {
            // Show custom choice
            /* TRANSLATORS: Custom colour choice entry */
            m_choices.Add(_("Custom"), ibPG_COLOUR_CUSTOM);
            m_flags &= ~(ibPGPropertyFlags_HideCustomColour);
        }
        else if ( !allow && !(m_flags & ibPGPropertyFlags_HideCustomColour) )
        {
            // Hide custom choice
            m_choices.RemoveAt(GetCustomColourIndex());
            m_flags |= ibPGPropertyFlags_HideCustomColour;
        }
        return true;
    }
    else if ( name == ibPG_COLOUR_HAS_ALPHA )
    {
        ChangeFlag(ibPGPropertyFlags_ColourHasAlpha, value.GetBoolean());
        return true;
    }
    return ibEnumProperty::DoSetAttribute(name, value);
}


// -----------------------------------------------------------------------
// ibColourProperty
// -----------------------------------------------------------------------

static constexpr std::array<const char*, 19+1> gs_cp_es_normcolour_labels
{
    ibTRANSLATE("Black"),
    ibTRANSLATE("Maroon"),
    ibTRANSLATE("Navy"),
    ibTRANSLATE("Purple"),
    ibTRANSLATE("Teal"),
    ibTRANSLATE("Gray"),
    ibTRANSLATE("Green"),
    ibTRANSLATE("Olive"),
    ibTRANSLATE("Brown"),
    ibTRANSLATE("Blue"),
    ibTRANSLATE("Fuchsia"),
    ibTRANSLATE("Red"),
    ibTRANSLATE("Orange"),
    ibTRANSLATE("Silver"),
    ibTRANSLATE("Lime"),
    ibTRANSLATE("Aqua"),
    ibTRANSLATE("Yellow"),
    ibTRANSLATE("White"),
    /* TRANSLATORS: Custom colour choice entry */ ibTRANSLATE("Custom"),
    nullptr
};

#if wxCHECK_CXX_STD(201402L) // [] is constexpr since C++14
static_assert(gs_cp_es_normcolour_labels[gs_cp_es_normcolour_labels.size() - 1] == nullptr,
              "nullptr has to mark the end of table");
#endif // >= C++ 14

static constexpr std::array<long, 19> gs_cp_es_normcolour_values
{
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    ibPG_COLOUR_CUSTOM
};

static constexpr std::array<unsigned long, 19> gs_cp_es_normcolour_colours
{
    ibPG_COLOUR(0,0,0),
    ibPG_COLOUR(128,0,0),
    ibPG_COLOUR(0,0,128),
    ibPG_COLOUR(128,0,128),
    ibPG_COLOUR(0,128,128),
    ibPG_COLOUR(128,128,128),
    ibPG_COLOUR(0,128,0),
    ibPG_COLOUR(128,128,0),
    ibPG_COLOUR(166,124,81),
    ibPG_COLOUR(0,0,255),
    ibPG_COLOUR(255,0,255),
    ibPG_COLOUR(255,0,0),
    ibPG_COLOUR(247,148,28),
    ibPG_COLOUR(192,192,192),
    ibPG_COLOUR(0,255,0),
    ibPG_COLOUR(0,255,255),
    ibPG_COLOUR(255,255,0),
    ibPG_COLOUR(255,255,255),
    ibPG_COLOUR(0,0,0)
};

static_assert(gs_cp_es_normcolour_values.size() == gs_cp_es_normcolour_labels.size() - 1,
    "Colour values table has to have one item less than colour labels table");
static_assert(gs_cp_es_normcolour_colours.size() == gs_cp_es_normcolour_values.size(),
    "Colours table and colour values table have to have the same size");

ibPG_IMPLEMENT_PROPERTY_CLASS(ibColourProperty, ibSystemColourProperty,
                              TextCtrlAndButton)

static ibPGChoices gs_wxColourProperty_choicesCache;

ibColourProperty::ibColourProperty( const wxString& label,
                      const wxString& name,
                      const wxColour& value )
    : ibSystemColourProperty(label, name, gs_cp_es_normcolour_labels.data(),
                             gs_cp_es_normcolour_values.data(),
                             &gs_wxColourProperty_choicesCache, value )
{
    wxASSERT_MSG( wxTheColourDatabase, wxS("No colour database") );
    if ( wxTheColourDatabase )
    {
        // Extend colour database with PG-specific colours.
        auto colourLabels = gs_cp_es_normcolour_labels.begin();
        for ( int i = 0; *colourLabels; ++colourLabels, i++ )
        {
            // Don't take into account user-defined custom colour.
            if (gs_cp_es_normcolour_values[i] != ibPG_COLOUR_CUSTOM)
            {
                wxColour clr = wxTheColourDatabase->Find(*colourLabels);
                // Use standard wx colour value if its label was found,
                // otherwise register custom PG colour.
                if ( !clr.IsOk() )
                {
                    clr.Set(gs_cp_es_normcolour_colours[i]);
                    wxTheColourDatabase->AddColour(*colourLabels, clr);
                }
            }
        }
    }

    Init( value );

    m_flags |= ibPGPropertyFlags_TranslateCustom;
}

void ibColourProperty::Init( wxColour colour )
{
    if ( !colour.IsOk() )
        colour = *wxWHITE;
    m_value = IBVARIANT(colour);
    int ind = ColToInd(colour);
    if ( ind < 0 )
        ind = m_choices.GetCount() - 1;
    SetIndex( ind );
}

wxString ibColourProperty::ValueToString( ibVariant& value,
                                          ibPGPropValFormatFlags flags ) const
{
    const ibPGEditor* editor = GetEditorClass();
    if ( editor != ibPGEditor_Choice &&
         editor != ibPGEditor_ChoiceAndButton &&
         editor != ibPGEditor_ComboBox )
        flags |= ibPGPropValFormatFlags::PropertySpecific;

    return ibSystemColourProperty::ValueToString(value, flags);
}

wxColour ibColourProperty::GetColour( int index ) const
{
    return wxColour(gs_cp_es_normcolour_labels[m_choices.GetValue(index)]);
}

ibVariant ibColourProperty::DoTranslateVal( ibColourPropertyValue& v ) const
{
    return IBVARIANT(v.m_colour);
}

// -----------------------------------------------------------------------
// ibCursorProperty
// -----------------------------------------------------------------------

#if ibPG_CAN_DRAW_CURSOR
#define ibPG_CURSOR_IMAGE_WIDTH     32
#endif

static constexpr std::array<const char*, 28+1> gs_cp_es_syscursors_labels
{
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Default"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Arrow"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Right Arrow"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Blank"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Bullseye"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Character"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Cross"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Hand"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "I-Beam"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Left Button"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Magnifier"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Middle Button"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "No Entry"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Paint Brush"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Pencil"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Point Left"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Point Right"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Question Arrow"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Right Button"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Sizing NE-SW"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Sizing N-S"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Sizing NW-SE"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Sizing W-E"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Sizing"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Spraycan"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Wait"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Watch"),
    ibTRANSLATE_IN_CONTEXT("system cursor name", "Wait Arrow"),
    nullptr
};

#if wxCHECK_CXX_STD(201402L) // [] is constexpr since C++14
static_assert(gs_cp_es_syscursors_labels[gs_cp_es_syscursors_labels.size() - 1] == nullptr,
    "nullptr has to mark the end of table");
#endif // >= C++ 14

static constexpr std::array<long, 28> gs_cp_es_syscursors_values
{
    wxCURSOR_NONE,
    wxCURSOR_ARROW,
    wxCURSOR_RIGHT_ARROW,
    wxCURSOR_BLANK,
    wxCURSOR_BULLSEYE,
    wxCURSOR_CHAR,
    wxCURSOR_CROSS,
    wxCURSOR_HAND,
    wxCURSOR_IBEAM,
    wxCURSOR_LEFT_BUTTON,
    wxCURSOR_MAGNIFIER,
    wxCURSOR_MIDDLE_BUTTON,
    wxCURSOR_NO_ENTRY,
    wxCURSOR_PAINT_BRUSH,
    wxCURSOR_PENCIL,
    wxCURSOR_POINT_LEFT,
    wxCURSOR_POINT_RIGHT,
    wxCURSOR_QUESTION_ARROW,
    wxCURSOR_RIGHT_BUTTON,
    wxCURSOR_SIZENESW,
    wxCURSOR_SIZENS,
    wxCURSOR_SIZENWSE,
    wxCURSOR_SIZEWE,
    wxCURSOR_SIZING,
    wxCURSOR_SPRAYCAN,
    wxCURSOR_WAIT,
    wxCURSOR_WATCH,
    wxCURSOR_ARROWWAIT
};

static_assert(gs_cp_es_syscursors_values.size() == gs_cp_es_syscursors_labels.size() - 1,
    "Values table has to have one item less than labels table");

wxIMPLEMENT_DYNAMIC_CLASS(ibCursorProperty, ibEnumProperty);

static ibPGChoices gs_wxCursorProperty_choicesCache;

ibCursorProperty::ibCursorProperty( const wxString& label, const wxString& name,
    int value )
    : ibEnumProperty( label,
                      name,
                      gs_cp_es_syscursors_labels.data(),
                      gs_cp_es_syscursors_values.data(),
                      &gs_wxCursorProperty_choicesCache,
                      value )
{
    m_flags |= ibPGPropertyFlags_StaticChoices; // Cursor selection cannot be changed.
}

wxString ibCursorProperty::ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const
{
    return ibGetTranslation(ibEnumProperty::ValueToString(value, flags), ibT("system cursor name"));
}

wxSize ibCursorProperty::OnMeasureImage( int item ) const
{
#if ibPG_CAN_DRAW_CURSOR
    if ( item != -1 && item < wxSsize(gs_cp_es_syscursors_values) )
        return wxSize(ibPG_CURSOR_IMAGE_WIDTH,ibPG_CURSOR_IMAGE_WIDTH);
#else
    wxUnusedVar(item);
#endif
    return wxSize(0,0);
}

#if ibPG_CAN_DRAW_CURSOR

void ibCursorProperty::OnCustomPaint( wxDC& dc,
                                      const wxRect& rect,
                                      ibPGPaintData& paintdata )
{
    // Background brush
    dc.SetBrush( wxTHEME_COLOUR(CONTROL) );

    if ( paintdata.m_choiceItem >= 0 )
    {
        dc.DrawRectangle( rect );

        if ( paintdata.m_choiceItem < wxSsize(gs_cp_es_syscursors_values) )
        {
            wxStockCursor cursorIndex =
                (wxStockCursor) gs_cp_es_syscursors_values[paintdata.m_choiceItem];

            if ( cursorIndex == wxCURSOR_NONE )
                cursorIndex = wxCURSOR_ARROW;

            wxCursor cursor( cursorIndex );

#if defined(__WXMSW__) || ( defined(__WXGTK__) && defined(__WXGTK__) )
            wxBitmap bmp(cursor);
            if ( bmp.IsOk() )
            {
                dc.DrawBitmap(bmp, rect.GetTopLeft(), true);
            }
#endif
        }
    }
}

#else
void ibCursorProperty::OnCustomPaint( wxDC&, const wxRect&, ibPGPaintData& ) { }
/*ibPGCellRenderer* ibCursorProperty::GetCellRenderer( int column ) const
{
    return ibEnumProperty::GetCellRenderer(column);
}*/
#endif

// -----------------------------------------------------------------------
// ibImageFileProperty
// -----------------------------------------------------------------------

#if wxUSE_IMAGE

const wxString& ibPGGetDefaultImageWildcard()
{
    // Form the wildcard, if not done yet
    if ( ibPGGlobalVars->m_pDefaultImageWildcard.empty() )
    {

        wxString str;

        // TODO: This section may require locking (using global).

        wxList& handlers = wxImage::GetHandlers();

        // Let's iterate over the image handler list.
        for ( auto node : handlers )
        {
            wxImageHandler *handler = static_cast<wxImageHandler*>(node);

            wxString ext_lo = handler->GetExtension();
            wxString ext_up = ext_lo.Upper();

            str.append( wxString::Format(_("%s files (%s)|%s"), ext_up, wxS("*.") + ext_up, wxS("*.") + ext_lo) );
            str.append( wxS("|") );
        }

        str.append ( _("All files (*.*)|*.*") );

        ibPGGlobalVars->m_pDefaultImageWildcard = str;
    }

    return ibPGGlobalVars->m_pDefaultImageWildcard;
}

wxIMPLEMENT_DYNAMIC_CLASS(ibImageFileProperty, ibFileProperty);

ibImageFileProperty::ibImageFileProperty( const wxString& label, const wxString& name,
    const wxString& value )
    : ibFileProperty(label,name,value)
{
    m_wildcard = ibPGGetDefaultImageWildcard();

    LoadImageFromFile();
}

void ibImageFileProperty::OnSetValue()
{
    ibFileProperty::OnSetValue();

    // Delete old image
    SetImage(wxNullImage);

    LoadImageFromFile();
}

void ibImageFileProperty::SetImage(const wxImage& img)
{
    m_image = img;
    m_bitmap = wxNullBitmap;
}

void ibImageFileProperty::LoadImageFromFile()
{
    wxFileName filename = GetFileName();

    // Cache the image
    if ( filename.FileExists() )
    {
        m_image.LoadFile(filename.GetFullPath());
    }
}

wxSize ibImageFileProperty::OnMeasureImage( int ) const
{
    return ibPG_DEFAULT_IMAGE_SIZE;
}

void ibImageFileProperty::OnCustomPaint( wxDC& dc,
                                         const wxRect& rect,
                                         ibPGPaintData& )
{
    if ( m_image.IsOk() )
    {
        // Draw the thumbnail
        // Create the bitmap here because required size is not known in OnSetValue().

        // Delete the cache if required size changed
        if ( m_bitmap.IsOk() && (m_bitmap.GetSize() != rect.GetSize()) )
        {
            m_bitmap = wxNullBitmap;
        }

        if ( !m_bitmap.IsOk() )
        {
            wxImage imgScaled = m_image;
            imgScaled.Rescale(rect.GetSize());
            m_bitmap = wxBitmap(imgScaled, dc);
        }
    }

    if ( m_bitmap.IsOk() )
    {
        dc.DrawBitmap(m_bitmap, rect.x, rect.y, false);
    }
    else
    {
        // No file - just draw a white box
        dc.SetBrush( *wxWHITE_BRUSH );
        dc.DrawRectangle ( rect );
    }
}

#endif // wxUSE_IMAGE

// -----------------------------------------------------------------------
// ibMultiChoiceProperty
// -----------------------------------------------------------------------

#if wxUSE_CHOICEDLG

#include "deskgui/univ/choiceDialog.h"   // ibMultiChoiceDialog — the choices ticked in the theme's dialog

ibPG_IMPLEMENT_PROPERTY_CLASS(ibMultiChoiceProperty,ibEditorDialogProperty,
                              TextCtrlAndButton)

ibMultiChoiceProperty::ibMultiChoiceProperty( const wxString& label,
                                              const wxString& name,
                                              const ibPGChoices& choices,
                                              const wxArrayString& value)
    : ibEditorDialogProperty(label,name)
{
    m_dlgStyle = wxCHOICEDLG_STYLE;
    m_userStringMode = 0;
    m_choices.Assign(choices);
    SetValue(IBVARIANT(value));
}

ibMultiChoiceProperty::ibMultiChoiceProperty( const wxString& label,
                                              const wxString& name,
                                              const wxArrayString& strings,
                                              const wxArrayString& value)
    : ibEditorDialogProperty(label,name)
{
    m_dlgStyle = wxCHOICEDLG_STYLE;
    m_userStringMode = 0;
    m_choices.Set(strings);
    SetValue(IBVARIANT(value));
}

ibMultiChoiceProperty::ibMultiChoiceProperty( const wxString& label,
                                              const wxString& name,
                                              const wxArrayString& value)
: ibEditorDialogProperty(label,name)
{
    m_dlgStyle = wxCHOICEDLG_STYLE;
    m_userStringMode = 0;
    wxArrayString strings;
    m_choices.Set(strings);
    SetValue(IBVARIANT(value));
}

void ibMultiChoiceProperty::OnSetValue()
{
    m_display = GenerateValueAsString(m_value);
}

wxString ibMultiChoiceProperty::ValueToString( ibVariant& value,
                                               ibPGPropValFormatFlags flags ) const
{
    // If possible, use cached string
    if ( !!(flags & ibPGPropValFormatFlags::ValueIsCurrent) )
        return m_display;

    return GenerateValueAsString(value);
}

wxString ibMultiChoiceProperty::GenerateValueAsString(const ibVariant& value) const
{
    wxArrayString strings;

    if ( value.GetClassName() == wxArrayStringVariantData::s_className )
        strings = wxArrayStringRefFromVariant(value);

    const size_t itemCount = strings.size();

    wxString valStr;

    for ( size_t i = 0; i < itemCount; i++ )
    {
        valStr.append("\"");
        valStr.append(strings[i]);
        valStr.append("\"");
        if ( i < (itemCount - 1) )
            valStr.append(" ");
    }

    return valStr;
}

wxArrayInt ibMultiChoiceProperty::GetValueAsIndices() const
{
    ibVariant variant = GetValue();
    const wxArrayInt& valueArr = wxArrayIntRefFromVariant(variant);

    // Translate values to string indices.
    wxArrayInt selections;

    if ( !m_choices.IsOk() || !m_choices.GetCount() )
    {
        selections.Add(-1, valueArr.size());
    }
    else
    {
        for( int val : valueArr )
        {
            int sIndex = m_choices.Index(val);
            if ( sIndex >= 0 )
                selections.Add(sIndex);
        }
    }

    return selections;
}

bool ibMultiChoiceProperty::DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value)
{
    wxASSERT_MSG(value.GetClassName() == wxArrayStringVariantData::s_className, "Function called for incompatible property");
    if ( !m_choices.IsOk() )
    {
        return false;
    }

    wxArrayString labels = m_choices.GetLabels();
    unsigned int choiceCount = m_choices.GetCount();

    // launch editor dialog - SEAM vs wx: the theme's (ibMultiChoiceDialog), as every dialog of the fork
    ibMultiChoiceDialog dlg( pg->GetPanel(),
                             _("Make a selection:"),
                             m_dlgTitle.empty() ? GetLabel() : m_dlgTitle,
                             choiceCount,
                             choiceCount?&labels[0]:nullptr,
                             m_dlgStyle );

    dlg.Move( pg->GetGoodEditorDialogPosition(this,dlg.GetSize()) );

    wxArrayString strings = wxArrayStringRefFromVariant(value);
    wxArrayString extraStrings;

    dlg.SetSelections(m_choices.GetIndicesForStrings(strings, &extraStrings));

    if ( dlg.ShowModal() == wxID_OK && choiceCount )
    {
        wxArrayInt arrInt = dlg.GetSelections();

        // Strings that were not in list of choices
        wxArrayString newValue;

        // Translate string indices to strings

        if ( m_userStringMode == 1 )
        {
            for( wxString& str : extraStrings )
                newValue.push_back(str);
        }

        for( int ind : arrInt )
            newValue.push_back(m_choices.GetLabel(ind));

        if ( m_userStringMode == 2 )
        {
            for ( wxString& str : extraStrings )
                newValue.push_back(str);
        }

        value = IBVARIANT(newValue);
        return true;
    }
    return false;
}

bool ibMultiChoiceProperty::StringToValue( ibVariant& variant, const wxString& text, ibPGPropValFormatFlags ) const
{
    wxArrayString arr;

    IB_PG_TOKENIZER2_BEGIN(text,wxT('"'))
        if ( m_userStringMode > 0 || (m_choices.IsOk() && m_choices.Index( token ) != wxNOT_FOUND) )
            arr.Add(token);
    IB_PG_TOKENIZER2_END()

    variant = IBVARIANT(arr);

    return true;
}

bool ibMultiChoiceProperty::DoSetAttribute( const wxString& name, ibVariant& value )
{
    if ( name == ibPG_ATTR_MULTICHOICE_USERSTRINGMODE )
    {
        m_userStringMode = (int)value.GetInteger();
        return true;
    }
    return ibEditorDialogProperty::DoSetAttribute(name, value);
}

#endif // wxUSE_CHOICEDLG


// -----------------------------------------------------------------------
// ibDateProperty
// -----------------------------------------------------------------------

#if wxUSE_DATETIME


#if wxUSE_DATEPICKCTRL
    #define dtCtrl      DatePickerCtrl
#else
    #define dtCtrl      TextCtrl
#endif

ibPG_IMPLEMENT_PROPERTY_CLASS(ibDateProperty,ibPGProperty,
                              dtCtrl)


wxString ibDateProperty::ms_defaultDateFormat;


ibDateProperty::ibDateProperty( const wxString& label,
                                const wxString& name,
                                const wxDateTime& value )
    : ibPGProperty(label,name)
{
    //ibPGRegisterDefaultValueType(wxDateTime)

#if wxUSE_DATEPICKCTRL
    ibPGRegisterEditorClass(DatePickerCtrl);

    m_dpStyle = wxDP_DEFAULT | wxDP_SHOWCENTURY;
#else
    m_dpStyle = 0;
#endif

    SetValue( IBVARIANT(value) );
}

void ibDateProperty::OnSetValue()
{
    //
    // Convert invalid dates to unspecified value
    if ( m_value.GetType() == ibValueTypes::TYPE_DATE )
    {
        if ( !typeConv::DateTimeToWxDateTime(m_value.GetDate()).IsValid() )
            m_value.Reset();
    }
}

bool ibDateProperty::StringToValue( ibVariant& variant, const wxString& text,
                                    ibPGPropValFormatFlags WXUNUSED(flags) ) const
{
    wxDateTime dt;

    // FIXME: do we really want to return true from here if only part of the
    //        string was parsed?
    const char* c = dt.ParseFormat(text);

    if ( c )
    {
        variant = IBVARIANT(dt);
        return true;
    }

    return false;
}

wxString ibDateProperty::ValueToString( ibVariant& value,
                                        ibPGPropValFormatFlags flags ) const
{
    wxDateTime dateTime = typeConv::DateTimeToWxDateTime(value.GetDate());

    if ( !dateTime.IsValid() )
        return wxS("Invalid");

    if ( ms_defaultDateFormat.empty() )
    {
#if wxUSE_DATEPICKCTRL
        bool showCentury = (m_dpStyle & wxDP_SHOWCENTURY) != 0;
#else
        bool showCentury = true;
#endif
        ms_defaultDateFormat = DetermineDefaultDateFormat( showCentury );
    }

    wxString format;
    if ( !m_format.empty() &&
         !(flags & ibPGPropValFormatFlags::FullValue) )
            format = m_format;

    // Determine default from locale
    if ( format.empty() )
        format = ms_defaultDateFormat;

    return dateTime.Format(format);
}

wxString ibDateProperty::DetermineDefaultDateFormat( bool showCentury )
{
    // This code is based on datectlg.cpp's GetLocaleDateFormat()
#if wxUSE_INTL
    wxString format = wxUILocale::GetCurrent().GetInfo(wxLOCALE_SHORT_DATE_FMT);
    if ( showCentury )
        format.Replace(wxS("%y"), wxS("%Y"));
    else
        format.Replace(wxS("%Y"), wxS("%y"));

    return format;
#else
    return wxS("%x");
#endif // wxUSE_INTL/!wxUSE_INTL
}

bool ibDateProperty::DoSetAttribute( const wxString& name, ibVariant& value )
{
    if ( name == ibPG_DATE_FORMAT )
    {
        m_format = value.GetString();
        return true;
    }
    else if ( name == ibPG_DATE_PICKER_STYLE )
    {
        m_dpStyle = value.GetInteger();
        ms_defaultDateFormat.clear();  // This may need recalculation
        return true;
    }
    return ibPGProperty::DoSetAttribute(name, value);
}

#endif  // wxUSE_DATETIME


// -----------------------------------------------------------------------
// ibPropertyGridInterface
// -----------------------------------------------------------------------

void ibPropertyGridInterface::InitAllTypeHandlers()
{
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::RegisterAdditionalEditors()
{
    // Register editor classes, if necessary.
    if ( ibPGGlobalVars->m_mapEditorClasses.empty() )
        ibPropertyGrid::RegisterDefaultEditors();

#if wxUSE_SPINBTN
    ibPGRegisterEditorClass(SpinCtrl);
#endif
#if wxUSE_DATEPICKCTRL
    ibPGRegisterEditorClass(DatePickerCtrl);
#endif
}

// -----------------------------------------------------------------------

#endif  // ibPG_INCLUDE_ADVPROPS

