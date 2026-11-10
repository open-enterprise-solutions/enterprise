// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, include/wx/propgrid/props.h): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/propgrid/props.h
// Purpose:     ibPropertyGrid Property Classes
// Author:      Jaakko Salli
// Created:     2007-03-28
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_PROPGRID_PROPS_H_
#define _DESKGUI_UNIV_PROPGRID_PROPS_H_

#include "wx/defs.h"

// -----------------------------------------------------------------------

#include "deskgui/univ/propgrid/property.h"

#include "wx/filename.h"
#include "wx/dialog.h"

#include "deskgui/univ/ctrl/textCtrl.h"    // ibTextCtrl — the field a value is validated in
#include "deskgui/univ/textValidator.h"    // ibTextValidator — the numeric validator's base

class DESKGUI_API ibPGArrayEditorDialog;

// -----------------------------------------------------------------------

//
// Property class implementation helper macros.
//

#define ibPG_IMPLEMENT_PROPERTY_CLASS(NAME, UPCLASS, EDITOR) \
wxIMPLEMENT_DYNAMIC_CLASS(NAME, UPCLASS); \
ibPG_IMPLEMENT_PROPERTY_CLASS_PLAIN(NAME, EDITOR)

// -----------------------------------------------------------------------

//
// These macros help creating DoGetValidator
#define IB_PG_DOGETVALIDATOR_ENTRY() \
    static wxValidator* s_ptr = nullptr; \
    if ( s_ptr ) return s_ptr;

// Common function exit
#define IB_PG_DOGETVALIDATOR_EXIT(VALIDATOR) \
    s_ptr = VALIDATOR; \
    ibPGGlobalVars->m_arrValidators.push_back( VALIDATOR ); \
    return VALIDATOR;

// -----------------------------------------------------------------------

// Creates and manages a temporary ibTextCtrl for validation purposes.
// Uses ibPropertyGrid's current editor, if available.
class DESKGUI_API ibPGInDialogValidator
{
public:
    ibPGInDialogValidator()
        : m_textCtrl(nullptr)
    {
    }

    ~ibPGInDialogValidator()
    {
        if ( m_textCtrl )
            m_textCtrl->Destroy();
    }

    bool DoValidate( ibPropertyGrid* propGrid,
                     wxValidator* validator,
                     const wxString& value );

private:
    ibTextCtrl*         m_textCtrl;
};


// -----------------------------------------------------------------------
// Property classes
// -----------------------------------------------------------------------

// Basic property with string value.
// If value "<composed>" is set, then actual value is formed (or composed)
// from values of child properties.
class DESKGUI_API ibStringProperty : public ibPGProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibStringProperty)
public:
    ibStringProperty( const wxString& label = ibPG_LABEL,
                      const wxString& name = ibPG_LABEL,
                      const wxString& value = wxString() );
    virtual ~ibStringProperty() = default;

    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

    virtual bool DoSetAttribute( const wxString& name, ibVariant& value ) override;

    // This is updated so "<composed>" special value can be handled.
    virtual void OnSetValue() override;
};

// -----------------------------------------------------------------------

// Constants used with NumericValidation<>().
enum class ibPGNumericValidationMode
{
    // Instead of modifying the value, show an error message.
    ErrorMessage,

    // Modify value, but stick with the limitations.
    Saturate,

    // Modify value, wrap around on overflow.
    Wrap
};

// -----------------------------------------------------------------------

#if wxUSE_VALIDATORS

// A more comprehensive numeric validator class.
class DESKGUI_API ibNumericPropertyValidator : public ibTextValidator
{
public:
    enum NumericType
    {
        Signed = 0,
        Unsigned,
        Float
    };

    ibNumericPropertyValidator( NumericType numericType, int base = 10 );
    virtual ~ibNumericPropertyValidator() = default;
    virtual bool Validate(wxWindow* parent) override;
};

#endif // wxUSE_VALIDATORS

// Base class for numeric properties.
// Cannot be instantiated directly.
class DESKGUI_API ibNumericProperty : public ibPGProperty
{
    wxDECLARE_ABSTRACT_CLASS(ibNumericProperty);
public:
    virtual ~ibNumericProperty() = default;

    virtual bool DoSetAttribute(const wxString& name, ibVariant& value) override;

    virtual ibVariant AddSpinStepValue(long stepScale) const = 0;

    bool UseSpinMotion() const { return m_spinMotion; }

    // Common validation code - for internal use.

    template<typename T>
    bool DoNumericValidation(T& value, ibPGValidationInfo* pValidationInfo,
                             ibPGNumericValidationMode mode, T defMin, T defMax) const;

protected:
    ibNumericProperty(const wxString& label, const wxString& name);

    ibVariant m_minVal;
    ibVariant m_maxVal;
    bool      m_spinMotion;
    ibVariant m_spinStep;
    bool      m_spinWrap;
};

// Basic property with integer value.
// Seamlessly supports 64-bit integer (wxLongLong) on overflow.
class DESKGUI_API ibIntProperty : public ibNumericProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibIntProperty)
public:
    ibIntProperty( const wxString& label = ibPG_LABEL,
                   const wxString& name = ibPG_LABEL,
                   long value = 0 );
    virtual ~ibIntProperty() = default;

    ibIntProperty( const wxString& label,
                   const wxString& name,
                   const wxLongLong& value );
    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool ValidateValue( ibVariant& value,
                                ibPGValidationInfo& validationInfo ) const override;
    virtual bool IntToValue(ibVariant& variant, int number,
                            ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    static wxValidator* GetClassValidator();
    virtual wxValidator* DoGetValidator() const override;
    virtual ibVariant AddSpinStepValue(long stepScale) const override;

private:
    // Validation helpers.
    static bool DoValidation( const ibNumericProperty* property,
                              wxLongLong& value,
                              ibPGValidationInfo* pValidationInfo,
                              ibPGNumericValidationMode = ibPGNumericValidationMode::ErrorMessage);
    static bool DoValidation(const ibNumericProperty* property,
                             long& value,
                             ibPGValidationInfo* pValidationInfo,
                             ibPGNumericValidationMode mode = ibPGNumericValidationMode::ErrorMessage);
};

// -----------------------------------------------------------------------

// Basic property with unsigned integer value.
// Seamlessly supports 64-bit integer (wxULongLong) on overflow.
class DESKGUI_API ibUIntProperty : public ibNumericProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibUIntProperty)
public:
    ibUIntProperty( const wxString& label = ibPG_LABEL,
                    const wxString& name = ibPG_LABEL,
                    unsigned long value = 0 );
    virtual ~ibUIntProperty() = default;
    ibUIntProperty( const wxString& label,
                    const wxString& name,
                    const wxULongLong& value );
    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool DoSetAttribute( const wxString& name, ibVariant& value ) override;
    virtual bool ValidateValue( ibVariant& value,
                                ibPGValidationInfo& validationInfo ) const override;
    virtual wxValidator* DoGetValidator () const override;
    virtual bool IntToValue(ibVariant& variant, int number,
                            ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual ibVariant AddSpinStepValue(long stepScale) const override;

protected:
    wxByte      m_base;
    wxByte      m_realBase; // translated to 8,16,etc.
    wxByte      m_prefix;
private:
    void Init();

    // Validation helpers.
    static bool DoValidation(const ibNumericProperty* property,
                             wxULongLong& value,
                             ibPGValidationInfo* pValidationInfo,
                             ibPGNumericValidationMode = ibPGNumericValidationMode::ErrorMessage);
    static bool DoValidation(const ibNumericProperty* property,
                             long& value,
                             ibPGValidationInfo* pValidationInfo,
                             ibPGNumericValidationMode mode = ibPGNumericValidationMode::ErrorMessage);
};

// -----------------------------------------------------------------------

// Basic property with double-precision floating point value.
class DESKGUI_API ibFloatProperty : public ibNumericProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibFloatProperty)
public:
    ibFloatProperty( const wxString& label = ibPG_LABEL,
                     const wxString& name = ibPG_LABEL,
                     double value = 0.0 );
    virtual ~ibFloatProperty() = default;

    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool DoSetAttribute( const wxString& name, ibVariant& value ) override;

    virtual bool ValidateValue( ibVariant& value,
                                ibPGValidationInfo& validationInfo ) const override;

    static wxValidator* GetClassValidator();
    virtual wxValidator* DoGetValidator () const override;
    virtual ibVariant AddSpinStepValue(long stepScale) const override;

protected:
    int m_precision;

private:
    // Validation helper.
    static bool DoValidation(const ibNumericProperty* property,
                             double& value,
                             ibPGValidationInfo* pValidationInfo,
                             ibPGNumericValidationMode mode = ibPGNumericValidationMode::ErrorMessage);
};

// -----------------------------------------------------------------------

// Basic property with boolean value.
class DESKGUI_API ibBoolProperty : public ibPGProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibBoolProperty)
public:
    ibBoolProperty( const wxString& label = ibPG_LABEL,
                    const wxString& name = ibPG_LABEL,
                    bool value = false );
    virtual ~ibBoolProperty() = default;

    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool IntToValue(ibVariant& variant, int number,
                            ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool DoSetAttribute( const wxString& name, ibVariant& value ) override;

};

// -----------------------------------------------------------------------

// Represents a single selection from a list of choices
// You can derive custom properties with choices from this class.
// Updating private index is important. You can do this either by calling
// SetIndex() in IntToValue, and then letting ibBaseEnumProperty::OnSetValue
// be called (by not implementing it, or by calling super class function in
// it) -OR- you can just call SetIndex in OnSetValue.
class DESKGUI_API ibEnumProperty : public ibPGProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibEnumProperty)
public:

#ifndef SWIG
    ibEnumProperty( const wxString& label = ibPG_LABEL,
                    const wxString& name = ibPG_LABEL,
                    const wxChar* const* labels = nullptr,
                    const long* values = nullptr,
                    int value = 0 );
    ibEnumProperty( const wxString& label,
                    const wxString& name,
                    ibPGChoices& choices,
                    int value = 0 );

    // Special constructor for caching choices (used by derived class)
    ibEnumProperty( const wxString& label,
                    const wxString& name,
                    const char* const* untranslatedLabels,
                    const long* values,
                    ibPGChoices* choicesCache,
                    int value = 0 );

    ibEnumProperty( const wxString& label,
                    const wxString& name,
                    const wxArrayString& labels,
                    const wxArrayInt& values = wxArrayInt(),
                    int value = 0 );
#else
    ibEnumProperty( const wxString& label = ibPG_LABEL,
                    const wxString& name = ibPG_LABEL,
                    const wxArrayString& labels = wxArrayString(),
                    const wxArrayInt& values = wxArrayInt(),
                    int value = 0 );
#endif

    virtual ~ibEnumProperty() = default;

    size_t GetItemCount() const { return m_choices.GetCount(); }

    virtual void OnSetValue() override;
    virtual wxString ValueToString(ibVariant& value, ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool ValidateValue( ibVariant& value,
                                ibPGValidationInfo& validationInfo ) const override;

    // If ibPGPropValFormatFlags::FullValue is not set in flags, then the value is interpreted
    // as index to choices list. Otherwise, it is actual value.
    virtual bool IntToValue(ibVariant& variant, int number,
                            ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

    //
    // Additional virtuals

    // This must be overridden to have non-index based value
    virtual int GetIndexForValue( int value ) const;

    // GetChoiceSelection needs to overridden since m_index is
    // the true index, and various property classes derived from
    // this take advantage of it.
    virtual int GetChoiceSelection() const override { return m_index; }

protected:

    int GetIndex() const;
    void SetIndex( int index );

    // Converts text to value and returns corresponding index in the collection
    bool ValueFromString_(ibVariant& value, int* pIndex,
                          const wxString& text, ibPGPropValFormatFlags flags) const;
    // Converts number to value and returns corresponding index in the collection
    bool ValueFromInt_(ibVariant& value, int* pIndex, int intVal, ibPGPropValFormatFlags flags) const;

private:
    // This is private so that classes are guaranteed to use GetIndex
    // for up-to-date index value.
    int                     m_index;
};

// -----------------------------------------------------------------------

// ibEnumProperty with wxString value and writable combo box editor.
// Uses int value, similar to ibEnumProperty, unless text entered by user
// is not in choices (in which case string value is used).
class DESKGUI_API ibEditEnumProperty : public ibEnumProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibEditEnumProperty)
public:

    ibEditEnumProperty( const wxString& label,
                        const wxString& name,
                        const wxChar* const* labels,
                        const long* values,
                        const wxString& value );
    ibEditEnumProperty( const wxString& label = ibPG_LABEL,
                        const wxString& name = ibPG_LABEL,
                        const wxArrayString& labels = wxArrayString(),
                        const wxArrayInt& values = wxArrayInt(),
                        const wxString& value = wxString() );
    ibEditEnumProperty( const wxString& label,
                        const wxString& name,
                        ibPGChoices& choices,
                        const wxString& value = wxString() );

    // Special constructor for caching choices (used by derived class)
    ibEditEnumProperty( const wxString& label,
                        const wxString& name,
                        const char* const* untranslatedLabels,
                        const long* values,
                        ibPGChoices* choicesCache,
                        const wxString& value );

    virtual ~ibEditEnumProperty() = default;

    void OnSetValue() override;
    bool StringToValue(ibVariant& variant, const wxString& text,
                       ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    bool ValidateValue(ibVariant& value,
                       ibPGValidationInfo& validationInfo) const override;

protected:
};

// -----------------------------------------------------------------------

// Represents a bit set that fits in a long integer. ibBoolProperty
// sub-properties are created for editing individual bits. Textctrl is created
// to manually edit the flags as a text; a continuous sequence of spaces,
// commas and semicolons is considered as a flag id separator.
// Note: When changing "choices" (ie. flag labels) of ibFlagsProperty,
// you will need to use SetPropertyChoices - otherwise they will not get
//    updated properly.
class DESKGUI_API ibFlagsProperty : public ibPGProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibFlagsProperty)
public:

#ifndef SWIG
    ibFlagsProperty( const wxString& label,
                     const wxString& name,
                     const wxChar* const* labels,
                     const long* values = nullptr,
                     long value = 0 );
    ibFlagsProperty( const wxString& label,
                     const wxString& name,
                     ibPGChoices& choices,
                     long value = 0 );
#endif
    ibFlagsProperty( const wxString& label = ibPG_LABEL,
                     const wxString& name = ibPG_LABEL,
                     const wxArrayString& labels = wxArrayString(),
                     const wxArrayInt& values = wxArrayInt(),
                     int value = 0 );
    virtual ~ibFlagsProperty () = default;

    virtual void OnSetValue() override;
    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags) const override;
    virtual ibVariant ChildChanged( ibVariant& thisValue,
                                    int childIndex,
                                    ibVariant& childValue ) const override;
    virtual void RefreshChildren() override;
    virtual bool DoSetAttribute( const wxString& name, ibVariant& value ) override;

    // GetChoiceSelection needs to overridden since m_choices is
    // used and value is integer, but it is not index.
    virtual int GetChoiceSelection() const override { return wxNOT_FOUND; }

    // helpers
    size_t GetItemCount() const { return m_choices.GetCount(); }
    const wxString& GetLabel( size_t ind ) const
        { return m_choices.GetLabel(static_cast<unsigned int>(ind)); }

protected:
    // Needed to properly mark changed sub-properties
    long                    m_oldValue;

    long                    m_allValueFlags;

    // Converts string id to a relevant bit.
    long IdToBit( const wxString& id ) const;

    // Creates children and sets value.
    void Init(long value);
};

// -----------------------------------------------------------------------
class DESKGUI_API ibEditorDialogProperty : public ibPGProperty
{
    friend class ibPGDialogAdapter;
    wxDECLARE_ABSTRACT_CLASS(ibEditorDialogProperty);

public:
    virtual ~ibEditorDialogProperty() = default;

    virtual ibPGEditorDialogAdapter* GetEditorDialog() const override;
    virtual bool DoSetAttribute(const wxString& name, ibVariant& value) override;

protected:
    ibEditorDialogProperty(const wxString& label, const wxString& name);

    virtual bool DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value) = 0;

    wxString  m_dlgTitle;  // Title for a dialog
    long      m_dlgStyle;  // Dialog style
};

// -----------------------------------------------------------------------

// Like ibLongStringProperty, but the button triggers file selector instead.
class DESKGUI_API ibFileProperty : public ibEditorDialogProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibFileProperty)
public:

    ibFileProperty( const wxString& label = ibPG_LABEL,
                    const wxString& name = ibPG_LABEL,
                    const wxString& value = wxString() );
    virtual ~ibFileProperty() = default;

    virtual void OnSetValue() override;
    virtual wxString ValueToString(ibVariant& value, ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool DoSetAttribute( const wxString& name, ibVariant& value ) override;

    static wxValidator* GetClassValidator();
    virtual wxValidator* DoGetValidator() const override;

    // Returns filename to file represented by current value.
    wxFileName GetFileName() const;

protected:
    virtual bool DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value) override;

    wxString    m_wildcard;
    wxString    m_basePath; // If set, then show path relative to it
    wxString    m_initialPath; // If set, start the file dialog here
    int         m_indFilter; // index to the selected filter
};

// -----------------------------------------------------------------------

// Like ibStringProperty, but has a button that triggers a small text
// editor dialog.
class DESKGUI_API ibLongStringProperty : public ibEditorDialogProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibLongStringProperty)
public:

    ibLongStringProperty( const wxString& label = ibPG_LABEL,
                          const wxString& name = ibPG_LABEL,
                          const wxString& value = wxString() );
    virtual ~ibLongStringProperty() = default;

    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

protected:
    virtual bool DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value) override;
};

// -----------------------------------------------------------------------


// Like ibLongStringProperty, but the button triggers dir selector instead.
class DESKGUI_API ibDirProperty : public ibEditorDialogProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibDirProperty)
public:
    ibDirProperty( const wxString& label = ibPG_LABEL,
                   const wxString& name = ibPG_LABEL,
                   const wxString& value = wxString() );
    virtual ~ibDirProperty() = default;

    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual wxValidator* DoGetValidator() const override;

protected:
    virtual bool DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value) override;
};

// -----------------------------------------------------------------------

// Property that manages a list of strings.
class DESKGUI_API ibArrayStringProperty : public ibEditorDialogProperty
{
    IB_PG_DECLARE_PROPERTY_CLASS(ibArrayStringProperty)
public:
    ibArrayStringProperty( const wxString& label = ibPG_LABEL,
                           const wxString& name = ibPG_LABEL,
                           const wxArrayString& value = wxArrayString() );
    virtual ~ibArrayStringProperty() = default;

    virtual void OnSetValue() override;
    virtual wxString ValueToString(ibVariant& value,
                                   ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
    virtual bool DoSetAttribute( const wxString& name, ibVariant& value ) override;

    // Implement in derived class for custom array-to-string conversion.
    virtual wxString ConvertArrayToString(const wxArrayString& arr, const wxUniChar& delimiter) const;

    // Shows string editor dialog. Value to be edited should be read from
    // value, and if dialog is not cancelled, it should be stored back and true
    // should be returned if that was the case.
    virtual bool OnCustomStringEdit( wxWindow* parent, wxString& value );

    // Creates ibPGArrayEditorDialog for string editing. Called in OnButtonClick.
    virtual ibPGArrayEditorDialog* CreateEditorDialog();

    enum ConversionFlags
    {
        Escape          = 0x01,
        QuoteStrings    = 0x02
    };

    // Generates string based on the contents of wxArrayString src.
    static wxString ArrayStringToString(const wxArrayString& src,
                                        wxUniChar delimiter, int flags);

protected:
    virtual bool DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value) override;

    // Previously this was to be implemented in derived class for array-to-
    // string conversion. Now you should implement ConvertValueToString()
    // instead.
    virtual void GenerateValueAsString();

    wxString        m_display; // Cache for displayed text.
    wxUniChar       m_delimiter;
    wxString        m_customBtnText;
};

// -----------------------------------------------------------------------

#define IB_PG_DECLARE_ARRAYSTRING_PROPERTY_WITH_VALIDATOR_WITH_DECL(PROPNAME, \
                                                                    DECL) \
DECL PROPNAME : public ibArrayStringProperty \
{ \
    IB_PG_DECLARE_PROPERTY_CLASS(PROPNAME) \
public: \
    PROPNAME( const wxString& label = ibPG_LABEL, \
              const wxString& name = ibPG_LABEL, \
              const wxArrayString& value = wxArrayString() ); \
    ~PROPNAME(); \
    virtual bool OnCustomStringEdit( wxWindow* parent, wxString& value ) override; \
    virtual wxValidator* DoGetValidator() const override; \
};

#define IB_PG_DECLARE_ARRAYSTRING_PROPERTY_WITH_VALIDATOR(PROPNAM) \
IB_PG_DECLARE_ARRAYSTRING_PROPERTY_WITH_VALIDATOR(PROPNAM, class)

#define IB_PG_IMPLEMENT_ARRAYSTRING_PROPERTY_WITH_VALIDATOR(PROPNAME, \
                                                            DELIMCHAR, \
                                                            CUSTBUTTXT) \
ibPG_IMPLEMENT_PROPERTY_CLASS(PROPNAME, ibArrayStringProperty, \
                               TextCtrlAndButton) \
PROPNAME::PROPNAME( const wxString& label, \
                    const wxString& name, \
                    const wxArrayString& value ) \
    : ibArrayStringProperty(label,name,value) \
{ \
    PROPNAME::GenerateValueAsString(); \
    m_delimiter = DELIMCHAR; \
    m_customBtnText = CUSTBUTTXT; \
} \
PROPNAME::~PROPNAME() = default;

#define IB_PG_DECLARE_ARRAYSTRING_PROPERTY(PROPNAME) \
IB_PG_DECLARE_ARRAYSTRING_PROPERTY_WITH_VALIDATOR(PROPNAME)

#define IB_PG_DECLARE_ARRAYSTRING_PROPERTY_WITH_DECL(PROPNAME, DECL) \
IB_PG_DECLARE_ARRAYSTRING_PROPERTY_WITH_VALIDATOR_WITH_DECL(PROPNAME, DECL)

#define IB_PG_IMPLEMENT_ARRAYSTRING_PROPERTY(PROPNAME,DELIMCHAR,CUSTBUTTXT) \
IB_PG_IMPLEMENT_ARRAYSTRING_PROPERTY_WITH_VALIDATOR(PROPNAME, \
                                                    DELIMCHAR, \
                                                    CUSTBUTTXT) \
wxValidator* PROPNAME::DoGetValidator () const \
{ return nullptr; }


// -----------------------------------------------------------------------
// ibPGArrayEditorDialog
// -----------------------------------------------------------------------

#if wxUSE_EDITABLELISTBOX

#include "deskgui/univ/dialog.h"                // ibDialog — the array's editor
#include "deskgui/univ/ctrl/bmpButton.h"       // ibBitmapButton — its buttons
#include "deskgui/univ/ctrl/editableListBox.h" // ibEditableListBox — its list

#define ibAEDIALOG_STYLE \
    (wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxOK | wxCANCEL | wxCENTRE)

class DESKGUI_API ibPGArrayEditorDialog : public ibDialog
{
public:
    ibPGArrayEditorDialog();
    virtual ~ibPGArrayEditorDialog() = default;

    void Init();

    bool Create( wxWindow *parent,
                 const wxString& message,
                 const wxString& caption,
                 long style = ibAEDIALOG_STYLE,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& sz = wxDefaultSize );

    void EnableCustomNewAction()
    {
        m_hasCustomNewAction = true;
    }

    void SetNewButtonText(const wxString& text)
    {
        m_customNewButtonText = text;
        if ( m_elb && m_elb->GetNewButton() )
        {
            m_elb->GetNewButton()->SetToolTip(text);
        }
    }

    // Set value modified by dialog.
    virtual void SetDialogValue( const ibVariant& WXUNUSED(value) )
    {
        wxFAIL_MSG(wxS("re-implement this member function in derived class"));
    }

    // Return value modified by dialog.
    virtual ibVariant GetDialogValue() const
    {
        wxFAIL_MSG(wxS("re-implement this member function in derived class"));
        return ibVariant();
    }

    // Override to return wxValidator to be used with the ibTextCtrl
    // in dialog. Note that the validator is used in the standard
    // wx way, i.e. it immediately prevents user from entering invalid
    // input.
    // Note: Dialog frees the validator.
    virtual wxValidator* GetTextCtrlValidator() const
    {
        return nullptr;
    }

    // Returns true if array was actually modified
    bool IsModified() const { return m_modified; }

    // ibEditableListBox utilities
    int GetSelection() const;

    // implementation from now on
    void OnAddClick(wxCommandEvent& event);
    void OnDeleteClick(wxCommandEvent& event);
    void OnUpClick(wxCommandEvent& event);
    void OnDownClick(wxCommandEvent& event);
    void OnEndLabelEdit(wxListEvent& event);
    void OnBeginLabelEdit(wxListEvent& evt);
    void OnIdle(wxIdleEvent& event);

protected:
    ibEditableListBox*  m_elb;

    // These are used for focus repair
    wxWindow*           m_elbSubPanel;
    wxWindow*           m_lastFocused;

    // A new item, edited by user, is pending at this index.
    // It will be committed once list ctrl item editing is done.
    int             m_itemPendingAtIndex;

    bool            m_modified;
    bool            m_hasCustomNewAction;
    wxString        m_customNewButtonText;

    // These must be overridden - must return true on success.
    virtual wxString ArrayGet( size_t index ) = 0;
    virtual size_t ArrayGetCount() = 0;
    virtual bool ArrayInsert( const wxString& str, int index ) = 0;
    virtual bool ArraySet( size_t index, const wxString& str ) = 0;
    virtual void ArrayRemoveAt( int index ) = 0;
    virtual void ArraySwap( size_t first, size_t second ) = 0;
    virtual bool OnCustomNewAction(wxString* WXUNUSED(resString))
    {
        return false;
    }

private:
    wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibPGArrayEditorDialog);
    wxDECLARE_EVENT_TABLE();
};

#endif // wxUSE_EDITABLELISTBOX

// -----------------------------------------------------------------------
// ibPGArrayStringEditorDialog
// -----------------------------------------------------------------------

class DESKGUI_API
    ibPGArrayStringEditorDialog : public ibPGArrayEditorDialog
{
public:
    ibPGArrayStringEditorDialog();
    virtual ~ibPGArrayStringEditorDialog() = default;

    void Init();

    virtual void SetDialogValue( const ibVariant& value ) override
    {
        m_array = wxArrayStringRefFromVariant(value);
    }

    virtual ibVariant GetDialogValue() const override
    {
        return IBVARIANT(m_array);
    }

    void SetCustomButton( const wxString& custBtText,
                          ibArrayStringProperty* pcc )
    {
        if ( !custBtText.empty() )
        {
            EnableCustomNewAction();
            SetNewButtonText(custBtText);
            m_pCallingClass = pcc;
        }
    }

    virtual bool OnCustomNewAction(wxString* resString) override;

protected:
    wxArrayString   m_array;

    ibArrayStringProperty*     m_pCallingClass;

    virtual wxString ArrayGet( size_t index ) override;
    virtual size_t ArrayGetCount() override;
    virtual bool ArrayInsert( const wxString& str, int index ) override;
    virtual bool ArraySet( size_t index, const wxString& str ) override;
    virtual void ArrayRemoveAt( int index ) override;
    virtual void ArraySwap( size_t first, size_t second ) override;

private:
    wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibPGArrayStringEditorDialog);
    wxDECLARE_EVENT_TABLE();
};

// -----------------------------------------------------------------------

#endif // _DESKGUI_UNIV_PROPGRID_PROPS_H_
