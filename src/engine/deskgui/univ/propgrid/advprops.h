// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, include/wx/propgrid/advprops.h): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/propgrid/advprops.h
// Purpose:     ibPropertyGrid Advanced Properties (font, colour, etc.)
// Author:      Jaakko Salli
// Created:     2004-09-25
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_PROPGRID_ADVPROPS_H_
#define _DESKGUI_UNIV_PROPGRID_ADVPROPS_H_

#include "wx/defs.h"

#if wxUSE_PROPGRID

#include "deskgui/univ/propgrid/props.h"

// -----------------------------------------------------------------------

//
// Additional Value Type Handlers
//
bool DESKGUI_API
operator==(const wxArrayInt& array1, const wxArrayInt& array2);

//
// Additional Property Editors
//
#if wxUSE_SPINBTN
IB_PG_DECLARE_EDITOR_WITH_DECL(SpinCtrl,DESKGUI_API)
#endif

#if wxUSE_DATEPICKCTRL
IB_PG_DECLARE_EDITOR_WITH_DECL(DatePickerCtrl,DESKGUI_API)
#endif

// -----------------------------------------------------------------------


// Web colour is currently unsupported
constexpr wxUint32 ibPG_COLOUR_WEB_BASE = 0x10000;
//#define ibPG_TO_WEB_COLOUR(A)   ((wxUint32)(A+ibPG_COLOUR_WEB_BASE))

constexpr wxUint32 ibPG_COLOUR_CUSTOM = 0xFFFFFF;
constexpr wxUint32 ibPG_COLOUR_UNSPECIFIED = ibPG_COLOUR_CUSTOM + 1;

// Because text, background and other colours tend to differ between
// platforms, ibSystemColourProperty must be able to select between system
// colour and, when necessary, to pick a custom one. ibSystemColourProperty
// value makes this possible.
class DESKGUI_API ibColourPropertyValue : public wxObject
{
public:
    // An integer value relating to the colour, and which exact
    // meaning depends on the property with which it is used.
    // For ibSystemColourProperty:
    // Any of ibSYS_COLOUR_XXX, or any web-colour ( use ibPG_TO_WEB_COLOUR
    // macro - (currently unsupported) ), or ibPG_COLOUR_CUSTOM.
    //
    // For custom colour properties without values array specified:
    // index or ibPG_COLOUR_CUSTOM
    // For custom colour properties with values array specified:
    // m_arrValues[index] or ibPG_COLOUR_CUSTOM
    wxUint32    m_type;

    // Resulting colour. Should be correct regardless of type.
    wxColour    m_colour;

    ibColourPropertyValue()
        : wxObject()
        , m_type(0)
    {
    }

    virtual ~ibColourPropertyValue() = default;

    ibColourPropertyValue( const ibColourPropertyValue& v )
        : wxObject()
        , m_type(v.m_type)
        , m_colour(v.m_colour)
    {
    }

    void Init( wxUint32 type, const wxColour& colour )
    {
        m_type = type;
        m_colour = colour;
    }

    ibColourPropertyValue( const wxColour& colour )
        : wxObject()
        , m_type(ibPG_COLOUR_CUSTOM)
        , m_colour(colour)
    {
    }

    ibColourPropertyValue( wxUint32 type )
        : wxObject()
        , m_type(type)
    {
    }

    ibColourPropertyValue( wxUint32 type, const wxColour& colour )
        : wxObject()
    {
        Init( type, colour );
    }

    void operator=(const ibColourPropertyValue& cpv)
    {
        if (this != &cpv)
            Init( cpv.m_type, cpv.m_colour );
    }

private:
    wxDECLARE_DYNAMIC_CLASS(ibColourPropertyValue);
};

// the value's class and operators (wx's wxDECLARE_VARIANT_OBJECT_EXPORTED was in the class body: ours declares a
// class of its own, beside this one)
IB_PG_DECLARE_VARIANT_DATA_EXPORTED(ibColourPropertyValue, DESKGUI_API)


bool DESKGUI_API
operator==(const ibColourPropertyValue&, const ibColourPropertyValue&);

// -----------------------------------------------------------------------

// Property representing wxFont.
class DESKGUI_API ibFontProperty : public ibEditorDialogProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibFontProperty)
public:

    ibFontProperty(const wxString& label = ibPG_LABEL,
                   const wxString& name = ibPG_LABEL,
                   const wxFont& value = wxFont());
    virtual ~ibFontProperty() = default;
    virtual void OnSetValue() override;
    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual ibVariant ChildChanged( ibVariant& thisValue,
                                    int childIndex,
                                    ibVariant& childValue ) const override;
    virtual void RefreshChildren() override;

protected:
    virtual bool DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value) override;
};

// -----------------------------------------------------------------------

// Has dropdown list of wxWidgets system colours. Value used is
// of ibColourPropertyValue type.
class DESKGUI_API ibSystemColourProperty : public ibEnumProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibSystemColourProperty)
public:

    ibSystemColourProperty( const wxString& label = ibPG_LABEL,
                            const wxString& name = ibPG_LABEL,
                            const ibColourPropertyValue&
                                value = ibColourPropertyValue() );
    virtual ~ibSystemColourProperty() = default;

    virtual void OnSetValue() override;
    virtual bool IntToValue(ibVariant& variant, int number,
                            ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

    // Override in derived class to customize how colours are printed as
    // strings.
    virtual wxString ColourToString(const wxColour& col, int index,
                                    ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const;

    // Returns index of entry that triggers colour picker dialog
    // (default is last).
    virtual int GetCustomColourIndex() const;

    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool OnEvent( ibPropertyGrid* propgrid,
                          wxWindow* primary, wxEvent& event ) override;
    virtual bool DoSetAttribute( const wxString& name, ibVariant& value ) override;
    virtual wxSize OnMeasureImage( int item ) const override;
    virtual void OnCustomPaint( wxDC& dc,
                                const wxRect& rect, ibPGPaintData& paintdata ) override;

    // Helper function to show the colour dialog
    bool QueryColourFromUser( ibVariant& variant ) const;

    // Default is to use wxSystemSettings::GetColour(index). Override to use
    // custom colour tables etc.
    virtual wxColour GetColour( int index ) const;

    ibColourPropertyValue GetVal( const ibVariant* pVariant = nullptr ) const;

protected:

    // Special constructors to be used by derived classes.
    ibSystemColourProperty( const wxString& label, const wxString& name,
        const char* const* labels, const long* values, ibPGChoices* choicesCache,
        const ibColourPropertyValue& value );
    ibSystemColourProperty( const wxString& label, const wxString& name,
        const char* const* labels, const long* values, ibPGChoices* choicesCache,
        const wxColour& value );

    void Init( int type, const wxColour& colour );

    // Utility functions for internal use
    virtual ibVariant DoTranslateVal( ibColourPropertyValue& v ) const;
    ibVariant TranslateVal( ibColourPropertyValue& v ) const
    {
        return DoTranslateVal( v );
    }
    ibVariant TranslateVal( int type, const wxColour& colour ) const
    {
        ibColourPropertyValue v(type, colour);
        return DoTranslateVal( v );
    }

    // Translates colour to a int value, return wxNOT_FOUND if no match.
    int ColToInd( const wxColour& colour ) const;
};

// -----------------------------------------------------------------------

class DESKGUI_API ibColourProperty : public ibSystemColourProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibColourProperty)
public:
    ibColourProperty( const wxString& label = ibPG_LABEL,
                      const wxString& name = ibPG_LABEL,
                      const wxColour& value = *wxWHITE );
    virtual ~ibColourProperty() = default;

    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual wxColour GetColour( int index ) const override;

protected:
    virtual ibVariant DoTranslateVal( ibColourPropertyValue& v ) const override;

private:
    void Init( wxColour colour );
};

// -----------------------------------------------------------------------

// Property representing wxCursor.
class DESKGUI_API ibCursorProperty : public ibEnumProperty
{
    wxDECLARE_DYNAMIC_CLASS(ibCursorProperty);

    ibCursorProperty( const wxString& label= ibPG_LABEL,
                      const wxString& name= ibPG_LABEL,
                      int value = 0 );
    virtual ~ibCursorProperty() = default;

    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual wxSize OnMeasureImage( int item ) const override;
    virtual void OnCustomPaint( wxDC& dc,
                                const wxRect& rect, ibPGPaintData& paintdata ) override;

};

// -----------------------------------------------------------------------

#if wxUSE_IMAGE

DESKGUI_API const wxString& ibPGGetDefaultImageWildcard();
class WXDLLIMPEXP_FWD_CORE wxBitmap;
class WXDLLIMPEXP_FWD_CORE wxImage;

// Property representing image file(name).
class DESKGUI_API ibImageFileProperty : public ibFileProperty
{
    wxDECLARE_DYNAMIC_CLASS(ibImageFileProperty);
public:

    ibImageFileProperty( const wxString& label= ibPG_LABEL,
                         const wxString& name = ibPG_LABEL,
                         const wxString& value = wxString());
    virtual ~ibImageFileProperty() = default;

    virtual void OnSetValue() override;

    virtual wxSize OnMeasureImage( int item ) const override;
    virtual void OnCustomPaint( wxDC& dc,
                                const wxRect& rect, ibPGPaintData& paintdata ) override;

protected:
    void SetImage(const wxImage& img);
    wxImage    m_image; // original thumbnail area

private:
    // Initialize m_image using the current file name.
    void LoadImageFromFile();

    wxBitmap   m_bitmap; // final thumbnail area
};

#endif

#if wxUSE_CHOICEDLG

// Property that manages a value resulting from wxMultiChoiceDialog. Value is
// array of strings. You can get value as array of choice values/indices by
// calling ibMultiChoiceProperty::GetValueAsArrayInt().
class DESKGUI_API ibMultiChoiceProperty : public ibEditorDialogProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibMultiChoiceProperty)
public:

    ibMultiChoiceProperty( const wxString& label,
                           const wxString& name,
                           const wxArrayString& strings,
                           const wxArrayString& value );
    ibMultiChoiceProperty( const wxString& label,
                           const wxString& name,
                           const ibPGChoices& choices,
                           const wxArrayString& value = wxArrayString() );

    ibMultiChoiceProperty( const wxString& label = ibPG_LABEL,
                           const wxString& name = ibPG_LABEL,
                           const wxArrayString& value = wxArrayString() );

    virtual ~ibMultiChoiceProperty() = default;

    virtual void OnSetValue() override;
    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool DoSetAttribute( const wxString& name, ibVariant& value ) override;

    wxArrayInt GetValueAsArrayInt() const
    {
        return m_choices.GetValuesForStrings(wxArrayStringRefFromVariant(m_value));
    }

protected:
    virtual bool DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value) override;

    wxString GenerateValueAsString(const ibVariant& value) const;

    // Returns translation of values into string indices.
    wxArrayInt GetValueAsIndices() const;

    // Cache displayed text since generating it is relatively complicated.
    wxString            m_display;
    // How to handle user strings
    int                 m_userStringMode;
};

#endif // wxUSE_CHOICEDLG

// -----------------------------------------------------------------------

#if wxUSE_DATETIME

// Property representing wxDateTime.
class DESKGUI_API ibDateProperty : public ibPGProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibDateProperty)
public:

    ibDateProperty( const wxString& label = ibPG_LABEL,
                    const wxString& name = ibPG_LABEL,
                    const wxDateTime& value = wxDateTime() );
    virtual ~ibDateProperty() = default;

    virtual void OnSetValue() override;
    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

    virtual bool DoSetAttribute( const wxString& name, ibVariant& value ) override;

    void SetFormat( const wxString& format )
    {
        m_format = format;
    }

    const wxString& GetFormat() const
    {
        return m_format;
    }

    void SetDateValue( const wxDateTime& dt )
    {
        //m_valueDateTime = dt;
        m_value = IBVARIANT(dt);
    }

    wxDateTime GetDateValue() const
    {
        //return m_valueDateTime;
        return m_value.GetType() == ibValueTypes::TYPE_DATE ? typeConv::DateTimeToWxDateTime(m_value.GetDate()) : wxDateTime();
    }

    long GetDatePickerStyle() const
    {
        return m_dpStyle;
    }

protected:
    wxString        m_format;
    long            m_dpStyle;  // DatePicker style

    static wxString ms_defaultDateFormat;
    static wxString DetermineDefaultDateFormat( bool showCentury );
};

#endif // wxUSE_DATETIME

// -----------------------------------------------------------------------

#if wxUSE_SPINBTN

//
// Implement an editor control that allows using wxSpinCtrl (actually, a
// combination of ibTextCtrl and wxSpinButton) to edit value of ibIntProperty
// and ibFloatProperty (and similar).
//
// Note that new editor classes needs to be registered before use. This can be
// accomplished using ibPGRegisterEditorClass macro, which is used for SpinCtrl
// in ibPropertyGridInterface::RegisterAdditionalEditors (see below).
// Registration can also be performed in a constructor of a property that is
// likely to require the editor in question.
//


#include "wx/spinbutt.h"
#include "deskgui/univ/propgrid/editors.h"


// NOTE: Regardless that this class inherits from a working editor, it has
//   all necessary methods to work independently. ibTextCtrl stuff is only
//   used for event handling here.
class DESKGUI_API ibPGSpinCtrlEditor : public ibPGTextCtrlEditor
{
    wxDECLARE_DYNAMIC_CLASS(ibPGSpinCtrlEditor);
public:
    virtual ~ibPGSpinCtrlEditor();

    wxString GetName() const override;
    virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid,
                                          ibPGProperty* property,
                                          const wxPoint& pos,
                                          const wxSize& size) const override;
    virtual bool OnEvent( ibPropertyGrid* propgrid, ibPGProperty* property,
        wxWindow* wnd, wxEvent& event ) const override;

private:
    mutable wxString m_tempString;
};

#endif // wxUSE_SPINBTN

// -----------------------------------------------------------------------

#endif // wxUSE_PROPGRID

#endif // _DESKGUI_UNIV_PROPGRID_ADVPROPS_H_
