// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, src/propgrid/props.cpp): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/propgrid/props.cpp
// Purpose:     Basic Property Classes
// Author:      Jaakko Salli
// Created:     2005-05-14
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"


#ifndef WX_PRECOMP
    #include "wx/dirdlg.h"
    #include "wx/filedlg.h"
    #include "wx/log.h"
    #include "wx/sizer.h"
    #include "wx/stattext.h"
#endif

#include "wx/numformatter.h"

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/editors.h"
#include "deskgui/univ/propgrid/props.h"
#include "deskgui/univ/propgrid/private.h"
#include "deskgui/univ/dialog.h"           // ibDialog — the long string's and the array's dialogs
#include "deskgui/univ/ctrl/textCtrl.h"    // ibTextCtrl — the long string's text

#include <limits>
#include <utility>
#include <array>

constexpr double ibPG_DBL_MIN = std::numeric_limits<double>::min();
constexpr double ibPG_DBL_MAX = std::numeric_limits<double>::max();
constexpr int ibPG_INT_MIN = std::numeric_limits<int>::min();
constexpr int ibPG_INT_MAX = std::numeric_limits<int>::max();
constexpr long ibPG_LONG_MIN = std::numeric_limits<long>::min();
constexpr long ibPG_LONG_MAX = std::numeric_limits<long>::max();
constexpr long long ibPG_LLONG_MIN = std::numeric_limits<long long>::min();
constexpr long long ibPG_LLONG_MAX = std::numeric_limits<long long>::max();
constexpr unsigned long ibPG_ULONG_MAX = std::numeric_limits<unsigned long>::max();
constexpr unsigned long long ibPG_ULLONG_MAX = std::numeric_limits<unsigned long long>::max();

// -----------------------------------------------------------------------
// ibStringProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibStringProperty,ibPGProperty,TextCtrl)

ibStringProperty::ibStringProperty( const wxString& label,
                                    const wxString& name,
                                    const wxString& value )
    : ibPGProperty(label,name)
{
    SetValue(value);
}

void ibStringProperty::OnSetValue()
{
    if ( m_value.GetType() != ibValueTypes::TYPE_EMPTY && m_value.GetString() == wxS("<composed>") )
        SetFlag(ibPGFlags::ComposedValue);

    if ( HasFlag(ibPGFlags::ComposedValue) )
    {
        wxString s;
        DoGenerateComposedValue(s);
        m_value = s;
    }
}

wxString ibStringProperty::ValueToString( ibVariant& value,
                                          ibPGPropValFormatFlags flags ) const
{
    wxString s = value.GetString();

    if ( HasAnyChild() && HasFlag(ibPGFlags::ComposedValue) )
    {
        // Value stored in m_value is non-editable, non-full value
        if ( !!(flags & ibPGPropValFormatFlags::FullValue) ||
             !!(flags & ibPGPropValFormatFlags::EditableValue) ||
             s.empty() )
        {
            // Calling this under incorrect conditions will fail
            wxASSERT_MSG( !!(flags & ibPGPropValFormatFlags::ValueIsCurrent),
                          wxS("Sorry, currently default ibPGProperty::ValueToString() ")
                          wxS("implementation only works if value is m_value.") );

            DoGenerateComposedValue(s, flags);
        }

        return s;
    }

    // If string is password and value is for visual purposes,
    // then return asterisks instead the actual string.
    if ( !!(m_flags & ibPGPropertyFlags_Password) && !(flags & (ibPGPropValFormatFlags::FullValue|ibPGPropValFormatFlags::EditableValue)) )
        return wxString(wxS('*'), s.length());

    return s;
}

bool ibStringProperty::StringToValue( ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags ) const
{
    if ( HasAnyChild() && HasFlag(ibPGFlags::ComposedValue) )
        return ibPGProperty::StringToValue(variant, text, flags);

    if ( variant != text )
    {
        variant = text;
        return true;
    }

    return false;
}

bool ibStringProperty::DoSetAttribute( const wxString& name, ibVariant& value )
{
    if ( name == ibPG_STRING_PASSWORD )
    {
        ChangeFlag(ibPGPropertyFlags_Password, value.GetBoolean());
        RecreateEditor();
        return true;
    }
    return ibPGProperty::DoSetAttribute(name, value);
}

// -----------------------------------------------------------------------
// ibNumericPropertyValidator
// -----------------------------------------------------------------------

#if wxUSE_VALIDATORS

ibNumericPropertyValidator::
    ibNumericPropertyValidator( NumericType numericType, int base )
    : ibTextValidator(wxFILTER_INCLUDE_CHAR_LIST)
{
    long style = GetStyle();

    wxString allowedChars;

    switch ( base )
    {
        case 2:
            allowedChars += wxS("01");
            break;
        case 8:
            allowedChars += wxS("01234567");
            break;
        case 10:
            style |= wxFILTER_DIGITS;
            break;
        case 16:
            style |= wxFILTER_XDIGITS;
            break;

        default:
            wxLogWarning( _("Unknown base %d. Base 10 will be used."), base );
            style |= wxFILTER_DIGITS;
    }

    if ( numericType == Signed )
    {
        allowedChars += wxS("-+");
    }
    else if ( numericType == Float )
    {
        allowedChars += wxS("-+eE");

        // Use locale-specific decimal point
        allowedChars.append(wxNumberFormatter::GetDecimalSeparator());
    }

    SetStyle(style);
    SetCharIncludes(allowedChars);
}

bool ibNumericPropertyValidator::Validate(wxWindow* parent)
{
    if ( !ibTextValidator::Validate(parent) )
        return false;

    ibTextCtrl* tc = wxDynamicCast(GetWindow(), ibTextCtrl);
    if ( !tc )
        return true;

    // Do not allow zero-length string
    return !tc->IsEmpty();
}

#endif // wxUSE_VALIDATORS

// -----------------------------------------------------------------------
// ibNumericProperty
// -----------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibNumericProperty, ibPGProperty);

ibNumericProperty::ibNumericProperty(const wxString& label, const wxString& name)
    : ibPGProperty(label, name)
    , m_spinMotion(false)
    , m_spinStep(1L)
    , m_spinWrap(false)
{
}

bool ibNumericProperty::DoSetAttribute(const wxString& name, ibVariant& value)
{
    if ( name == ibPG_ATTR_MIN )
    {
        m_minVal = value;
        return true;
    }
    else if ( name == ibPG_ATTR_MAX )
    {
        m_maxVal = value;
        return true;
    }
    else if ( name == ibPG_ATTR_SPINCTRL_MOTION )
    {
        m_spinMotion = value.GetBoolean();
        return true;
    }
    else if ( name == ibPG_ATTR_SPINCTRL_STEP )
    {
        m_spinStep = value;
        return true;
    }
    else if ( name == ibPG_ATTR_SPINCTRL_WRAP )
    {
        m_spinWrap = value.GetBoolean();
        return true;
    }
    return ibPGProperty::DoSetAttribute(name, value);
}
namespace {
    // This function by default doesn't modify the value.
    // For argument 'value' of type 'double' there is a specialized function (below).
    template<typename T>
    T GetRoundedValue(const ibPGProperty* prop, T value)
    {
        wxUnusedVar(prop);
        return value;
    }

    // Specialized function for floating-point values
    // takes into account required precision of the numbers
    // to avoid rounding and conversion errors.
    template<>
    double GetRoundedValue(const ibPGProperty* prop, double value)
    {
        // Round value to the required precision.
        ibVariant variant = value;
        wxString strVal = prop->ValueToString(variant, ibPGPropValFormatFlags::FullValue);
        wxNumberFormatter::FromString(strVal, &value);
        return value;
    }

    // wx's wxVariant::Convert(&number), over the core's value: a number converts, to the width asked, and
    // nothing else does
    bool ConvertNumber(const ibVariant& variant, long* number)
    {
        return variant.GetType() == ibValueTypes::TYPE_NUMBER && variant.GetNumber().ToInt(*number) == 0;
    }

    bool ConvertNumber(const ibVariant& variant, double* number)
    {
        if ( variant.GetType() != ibValueTypes::TYPE_NUMBER )
            return false;
        *number = variant.GetDouble();
        return true;
    }

    bool ConvertNumber(const ibVariant& variant, wxLongLong* number)
    {
        long long value = 0;
        if ( variant.GetType() != ibValueTypes::TYPE_NUMBER || variant.GetNumber().ToInt(value) != 0 )
            return false;
        *number = value;
        return true;
    }

    bool ConvertNumber(const ibVariant& variant, wxULongLong* number)
    {
        unsigned long long value = 0;
        if ( variant.GetType() != ibValueTypes::TYPE_NUMBER || variant.GetNumber().ToInt(value) != 0 )
            return false;
        *number = value;
        return true;
    }
} // namespace

// Common validation code to be called in ValidateValue() implementations.
// Note that 'value' is reference on purpose, so we can write
// back to it when mode is ibPGNumericValidationMode::Saturate or ibPGNumericValidationMode::Wrap.

template<typename T>
bool ibNumericProperty::DoNumericValidation(T& value, ibPGValidationInfo* pValidationInfo,
                                            ibPGNumericValidationMode mode, T defMin, T defMax) const
{
    T min = defMin;
    T max = defMax;
    ibVariant variant;
    bool minOk = false;
    bool maxOk = false;

    // Round current value to the required precision, if applicable
    value = GetRoundedValue<T>(this, value);

    variant = m_minVal;
    if ( variant.GetType() != ibValueTypes::TYPE_EMPTY )
    {
        minOk = ConvertNumber(variant, &min);
    }
    if ( minOk )
    {
        // Round minimal value to the required precision, if applicable
        min = GetRoundedValue<T>(this, min);
    }

    variant = m_maxVal;
    if ( variant.GetType() != ibValueTypes::TYPE_EMPTY )
    {
        maxOk = ConvertNumber(variant, &max);
    }
    if ( maxOk )
    {
        // Round maximal value to the required precision, if applicable
        max = GetRoundedValue<T>(this, max);
    }

    if ( minOk )
    {
        if ( value < min )
        {
            if ( mode == ibPGNumericValidationMode::ErrorMessage )
            {
                wxString msg;
                ibVariant vmin = IBVARIANT(min);
                wxString smin = ValueToString(vmin);
                if ( !maxOk )
                    msg = wxString::Format(_("Value must be %s or higher."), smin);
                else
                {
                    ibVariant vmax = IBVARIANT(max);
                    wxString smax = ValueToString(vmax);
                    msg = wxString::Format(_("Value must be between %s and %s."), smin, smax);
                }
                pValidationInfo->SetFailureMessage(msg);
            }
            else if ( mode == ibPGNumericValidationMode::Saturate )
                value = min;
            else
                value = max - (min - value);
            return false;
        }
    }

    if ( maxOk )
    {
        if ( value > max )
        {
            if ( mode == ibPGNumericValidationMode::ErrorMessage )
            {
                wxString msg;
                ibVariant vmax = IBVARIANT(max);
                wxString smax = ValueToString(vmax);
                if ( !minOk )
                    msg = wxString::Format(_("Value must be %s or less."), smax);
                else
                {
                    ibVariant vmin = IBVARIANT(min);
                    wxString smin = ValueToString(vmin);
                    msg = wxString::Format(_("Value must be between %s and %s."), smin, smax);
                }
                pValidationInfo->SetFailureMessage(msg);
            }
            else if ( mode == ibPGNumericValidationMode::Saturate )
                value = max;
            else
                value = min + (value - max);
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------
// ibIntProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibIntProperty,ibNumericProperty,TextCtrl)

ibIntProperty::ibIntProperty( const wxString& label, const wxString& name,
    long value ) : ibNumericProperty(label,name)
{
    SetValue(value);
}

ibIntProperty::ibIntProperty( const wxString& label, const wxString& name,
    const wxLongLong& value ) : ibNumericProperty(label,name)
{
    SetValue(IBVARIANT(value));
}

wxString ibIntProperty::ValueToString( ibVariant& value,
                                       ibPGPropValFormatFlags WXUNUSED(flags) ) const
{
    // a long and a long long alike: each is the core's number
    if ( value.GetType() == ibValueTypes::TYPE_NUMBER )
    {
        long long ll = 0;
        value.GetNumber().ToInt(ll);
        return wxLongLong(ll).ToString();
    }

    return wxString();
}

bool ibIntProperty::StringToValue( ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags ) const
{
    if ( text.empty() )
    {
        variant.Reset();
        return true;
    }

    // We know it is a number, but let's still check
    // the return value.
    if ( text.IsNumber() )
    {
        // Remove leading zeros, so that the number is not interpreted as octal
        // Let's allow one, last zero though
        wxString::const_iterator itFirstNonZero = std::find_if(text.begin(), text.end()-1,
                                                               [](wxUniChar c) { return c != '0' && c != ' '; });
        wxString useText(itFirstNonZero, text.end());

        // a long and a long long are each the core's number: one value, whatever its size
        wxLongLong_t value64 = 0;

        if ( useText.ToLongLong(&value64, 10) )
        {
            const ibVariant number(ibNumber(static_cast<long long>(value64)));
            if ( variant != number )
            {
                variant = number;
                return true;
            }
        }
    }
    else if ( !!(flags & ibPGPropValFormatFlags::ReportError) )
    {
    }
    return false;
}

bool ibIntProperty::IntToValue( ibVariant& variant, int value, ibPGPropValFormatFlags WXUNUSED(flags) ) const
{
    if ( variant.GetType() != ibValueTypes::TYPE_NUMBER || variant != (long)value )
    {
        variant = (long)value;
        return true;
    }
    return false;
}

bool ibIntProperty::DoValidation( const ibNumericProperty* property,
                                  wxLongLong& value,
                                  ibPGValidationInfo* pValidationInfo,
                                  ibPGNumericValidationMode mode )
{
    return property->DoNumericValidation<wxLongLong>(value,
                                           pValidationInfo,
                                           mode, wxLongLong(ibPG_LLONG_MIN), wxLongLong(ibPG_LLONG_MAX));
}

bool ibIntProperty::DoValidation(const ibNumericProperty* property,
                                 long& value,
                                 ibPGValidationInfo* pValidationInfo,
                                 ibPGNumericValidationMode mode)
{
    return property->DoNumericValidation<long>(value, pValidationInfo,
                                     mode, ibPG_LONG_MIN, ibPG_LONG_MAX);
}

bool ibIntProperty::ValidateValue( ibVariant& value,
                                   ibPGValidationInfo& validationInfo ) const
{
    long long number = 0;
    value.GetNumber().ToInt(number);
    wxLongLong ll(number);
    return DoValidation(this, ll, &validationInfo,
                        ibPGNumericValidationMode::ErrorMessage);
}

wxValidator* ibIntProperty::GetClassValidator()
{
#if wxUSE_VALIDATORS
    IB_PG_DOGETVALIDATOR_ENTRY()

    wxValidator* validator = new ibNumericPropertyValidator(
                                    ibNumericPropertyValidator::Signed);

    IB_PG_DOGETVALIDATOR_EXIT(validator)
#else
    return nullptr;
#endif
}

wxValidator* ibIntProperty::DoGetValidator() const
{
    return GetClassValidator();
}

ibVariant ibIntProperty::AddSpinStepValue(long stepScale) const
{
    ibPGNumericValidationMode mode = m_spinWrap ? ibPGNumericValidationMode::Wrap
        : ibPGNumericValidationMode::Saturate;
    ibVariant value = GetValue();
    if ( value.GetType() == ibValueTypes::TYPE_NUMBER )
    {
        // a long and a long long alike (the core's number): stepped in 64 bits
        long long number = 0, stepNumber = 0;
        value.GetNumber().ToInt(number);
        m_spinStep.GetNumber().ToInt(stepNumber);
        wxLongLong v(number);
        wxLongLong step(stepNumber);
        v += (step * stepScale);
        DoValidation(this, v, nullptr, mode);
        value = IBVARIANT(v);
    }
    else
    {
        wxFAIL_MSG("Unknown value type");
    }

    return value;
}

// -----------------------------------------------------------------------
// ibUIntProperty
// -----------------------------------------------------------------------

enum
{
    ibPG_UINT_HEX_LOWER,
    ibPG_UINT_HEX_LOWER_PREFIX,
    ibPG_UINT_HEX_LOWER_DOLLAR,
    ibPG_UINT_HEX_UPPER,
    ibPG_UINT_HEX_UPPER_PREFIX,
    ibPG_UINT_HEX_UPPER_DOLLAR,
    ibPG_UINT_DEC,
    ibPG_UINT_OCT,
    ibPG_UINT_TEMPLATE_MAX
};

ibPG_IMPLEMENT_PROPERTY_CLASS(ibUIntProperty,ibNumericProperty,TextCtrl)

void ibUIntProperty::Init()
{
    m_base = ibPG_UINT_DEC;
    m_realBase = 10;
    m_prefix = ibPG_PREFIX_NONE;
}

ibUIntProperty::ibUIntProperty( const wxString& label, const wxString& name,
    unsigned long value ) : ibNumericProperty(label,name)
{
    Init();
    SetValue((long)value);
}

ibUIntProperty::ibUIntProperty( const wxString& label, const wxString& name,
    const wxULongLong& value ) : ibNumericProperty(label,name)
{
    Init();
    SetValue(IBVARIANT(value));
}

wxString ibUIntProperty::ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const
{
    // an unsigned long and an unsigned long long alike (the core's number): written in 64 bits
    static constexpr std::array<const wxStringCharType*, ibPG_UINT_TEMPLATE_MAX> gs_uintTemplates64
    {
        wxS("%") wxS(wxLongLongFmtSpec) wxS("x"),
        wxS("0x%") wxS(wxLongLongFmtSpec) wxS("x"),
        wxS("$%") wxS(wxLongLongFmtSpec) wxS("x"),
        wxS("%") wxS(wxLongLongFmtSpec) wxS("X"),
        wxS("0x%") wxS(wxLongLongFmtSpec) wxS("X"),
        wxS("$%") wxS(wxLongLongFmtSpec) wxS("X"),
        wxS("%") wxS(wxLongLongFmtSpec) wxS("u"),
        wxS("%") wxS(wxLongLongFmtSpec) wxS("o")
    };

    // In the edit mode we want to display just the numeric value,
    // without prefixes.
    static constexpr std::array<const wxStringCharType*, ibPG_UINT_TEMPLATE_MAX> gs_uintEditTemplates64
    {
        wxS("%") wxS(wxLongLongFmtSpec) wxS("x"),
        wxS("%") wxS(wxLongLongFmtSpec) wxS("x"),
        wxS("%") wxS(wxLongLongFmtSpec) wxS("x"),
        wxS("%") wxS(wxLongLongFmtSpec) wxS("X"),
        wxS("%") wxS(wxLongLongFmtSpec) wxS("X"),
        wxS("%") wxS(wxLongLongFmtSpec) wxS("X"),
        wxS("%") wxS(wxLongLongFmtSpec) wxS("u"),
        wxS("%") wxS(wxLongLongFmtSpec) wxS("o")
    };

    size_t index = m_base + m_prefix;
    if ( index >= ibPG_UINT_TEMPLATE_MAX )
        index = ibPG_UINT_DEC;

    if ( value.GetType() == ibValueTypes::TYPE_NUMBER )
    {
        const wxStringCharType* fmt = !!(flags & ibPGPropValFormatFlags::EditableValue) ?
                                        gs_uintEditTemplates64[index] :
                                        gs_uintTemplates64[index];
        unsigned long long ull = 0;
        value.GetNumber().ToInt(ull);
        return wxString::Format(fmt, static_cast<wxULongLong_t>(ull));
    }
    return wxString();
}

bool ibUIntProperty::StringToValue(ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags) const
{
    if ( text.empty() )
    {
        variant.Reset();
        return true;
    }

    size_t start = 0;
    if ( text[0] == wxS('$') )
        start++;

    wxString s = text.substr(start, text.length() - start);

    // an unsigned long and an unsigned long long are each the core's number: one value, whatever its size
    wxULongLong_t value64 = 0;

    if ( s.ToULongLong(&value64, (unsigned int)m_realBase) )
    {
        const ibVariant number(ibNumber(static_cast<unsigned long long>(value64)));
        if ( variant != number )
        {
            variant = number;
            return true;
        }
    }
    else if ( !!(flags & ibPGPropValFormatFlags::ReportError) )
    {
    }

    return false;
}

bool ibUIntProperty::IntToValue( ibVariant& variant, int number, ibPGPropValFormatFlags WXUNUSED(flags) ) const
{
    if ( variant != (long)number )
    {
        variant = (long)number;
        return true;
    }
    return false;
}

bool ibUIntProperty::DoValidation(const ibNumericProperty* property,
                                  wxULongLong& value,
                                  ibPGValidationInfo* pValidationInfo,
                                  ibPGNumericValidationMode mode )
{
    return property->DoNumericValidation<wxULongLong>(value, pValidationInfo,
                                            mode, wxULongLong(0), wxULongLong(ibPG_ULLONG_MAX));
}

bool ibUIntProperty::DoValidation(const ibNumericProperty* property,
                                  long& value,
                                  ibPGValidationInfo* pValidationInfo,
                                  ibPGNumericValidationMode mode)
{
    return property->DoNumericValidation<long>(value, pValidationInfo,
                                     mode, 0, ibPG_ULONG_MAX);
}

bool ibUIntProperty::ValidateValue( ibVariant& value, ibPGValidationInfo& validationInfo ) const
{
    unsigned long long number = 0;
    value.GetNumber().ToInt(number);
    wxULongLong uul(number);
    return DoValidation(this, uul, &validationInfo,
                        ibPGNumericValidationMode::ErrorMessage);
}

wxValidator* ibUIntProperty::DoGetValidator() const
{
#if wxUSE_VALIDATORS
    IB_PG_DOGETVALIDATOR_ENTRY()

    wxValidator* validator = new ibNumericPropertyValidator(
                                    ibNumericPropertyValidator::Unsigned,
                                    m_realBase);

    IB_PG_DOGETVALIDATOR_EXIT(validator)
#else
    return nullptr;
#endif
}

bool ibUIntProperty::DoSetAttribute( const wxString& name, ibVariant& value )
{
    if ( name == ibPG_UINT_BASE )
    {
        int val = value.GetInteger();

        m_realBase = (wxByte) val;
        if ( m_realBase > 16 )
            m_realBase = 16;

        //
        // Translate logical base to a template array index
        m_base = ibPG_UINT_OCT;
        if ( val == ibPG_BASE_HEX )
            m_base = ibPG_UINT_HEX_UPPER;
        else if ( val == ibPG_BASE_DEC )
            m_base = ibPG_UINT_DEC;
        else if ( val == ibPG_BASE_HEXL )
            m_base = ibPG_UINT_HEX_LOWER_DOLLAR;
        return true;
    }
    else if ( name == ibPG_UINT_PREFIX )
    {
        m_prefix = (wxByte) value.GetInteger();
        return true;
    }
    return ibNumericProperty::DoSetAttribute(name, value);
}

ibVariant ibUIntProperty::AddSpinStepValue(long stepScale) const
{
    ibPGNumericValidationMode mode = m_spinWrap ? ibPGNumericValidationMode::Wrap
                          : ibPGNumericValidationMode::Saturate;
    ibVariant value = GetValue();
    if ( value.GetType() == ibValueTypes::TYPE_NUMBER )
    {
        // an unsigned long and an unsigned long long alike (the core's number): stepped in 64 bits
        unsigned long long number = 0, stepNumber = 0;
        value.GetNumber().ToInt(number);
        m_spinStep.GetNumber().ToInt(stepNumber);
        wxULongLong v(number);
        wxULongLong step(stepNumber);
        v += (step * stepScale);
        DoValidation(this, v, nullptr, mode);
        value = IBVARIANT(v);
    }
    else
    {
        wxFAIL_MSG("Unknown value type");
    }

    return value;
}

// -----------------------------------------------------------------------
// ibFloatProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibFloatProperty,ibNumericProperty,TextCtrl)

ibFloatProperty::ibFloatProperty( const wxString& label,
                                            const wxString& name,
                                            double value )
    : ibNumericProperty(label,name)
{
    m_precision = -1;
    SetValue(value);
}

wxString ibFloatProperty::ValueToString( ibVariant& value,
                                         ibPGPropValFormatFlags flags ) const
{
    wxString text;
    if ( value.GetType() != ibValueTypes::TYPE_EMPTY )
    {
        text = wxNumberFormatter::ToString(value.GetDouble(), m_precision,
                                           !!(flags & ibPGPropValFormatFlags::FullValue) ? wxNumberFormatter::Style_None
                                                                                  : wxNumberFormatter::Style_NoTrailingZeroes);
    }
    return text;
}

bool ibFloatProperty::StringToValue( ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags ) const
{
    if ( text.empty() )
    {
        variant.Reset();
        return true;
    }

    double value;
    bool res = wxNumberFormatter::FromString(text, &value);
    if ( res )
    {
        if ( variant != value )
        {
            variant = value;
            return true;
        }
    }
    else if ( !!(flags & ibPGPropValFormatFlags::ReportError) )
    {
    }
    return false;
}

bool ibFloatProperty::DoValidation( const ibNumericProperty* property,
                                    double& value,
                                    ibPGValidationInfo* pValidationInfo,
                                    ibPGNumericValidationMode mode )
{
    return property->DoNumericValidation<double>(value, pValidationInfo,
                                       mode, ibPG_DBL_MIN, ibPG_DBL_MAX);
}

bool
ibFloatProperty::ValidateValue( ibVariant& value,
                                ibPGValidationInfo& validationInfo ) const
{
    double fpv = value.GetDouble();
    return DoValidation(this, fpv, &validationInfo,
                        ibPGNumericValidationMode::ErrorMessage);
}

bool ibFloatProperty::DoSetAttribute( const wxString& name, ibVariant& value )
{
    if ( name == ibPG_FLOAT_PRECISION )
    {
        m_precision = value.GetInteger();
        return true;
    }
    return ibNumericProperty::DoSetAttribute(name, value);
}

wxValidator*
ibFloatProperty::GetClassValidator()
{
#if wxUSE_VALIDATORS
    IB_PG_DOGETVALIDATOR_ENTRY()

    wxValidator* validator = new ibNumericPropertyValidator(
                                    ibNumericPropertyValidator::Float);

    IB_PG_DOGETVALIDATOR_EXIT(validator)
#else
    return nullptr;
#endif
}

wxValidator* ibFloatProperty::DoGetValidator() const
{
    return GetClassValidator();
}

ibVariant ibFloatProperty::AddSpinStepValue(long stepScale) const
{
    ibPGNumericValidationMode mode = m_spinWrap ? ibPGNumericValidationMode::Wrap
                          : ibPGNumericValidationMode::Saturate;
    ibVariant value = GetValue();
    double v = value.GetDouble();
    double step = m_spinStep.GetDouble();
    v += (step * stepScale);
    DoValidation(this, v, nullptr, mode);
    value = v;

    return value;
}

// -----------------------------------------------------------------------
// ibBoolProperty
// -----------------------------------------------------------------------

// We cannot use standard IB_PG_IMPLEMENT_PROPERTY_CLASS macro, since
// there is a custom GetEditorClass.

wxIMPLEMENT_DYNAMIC_CLASS(ibBoolProperty, ibPGProperty);

const ibPGEditor* ibBoolProperty::DoGetEditorClass() const
{
    // Select correct editor control.
#if ibPG_INCLUDE_CHECKBOX
    if ( !(m_flags & ibPGPropertyFlags_UseCheckBox) )
        return ibPGEditor_Choice;
    return ibPGEditor_CheckBox;
#else
    return ibPGEditor_Choice;
#endif
}

ibBoolProperty::ibBoolProperty( const wxString& label, const wxString& name, bool value ) :
    ibPGProperty(label,name)
{
    m_choices.Assign(ibPGGlobalVars->m_boolChoices);

    SetValue(ibVariant(value));

    m_flags |= ibPGPropertyFlags_UseDCC;
}

wxString ibBoolProperty::ValueToString( ibVariant& value,
                                        ibPGPropValFormatFlags flags ) const
{
    bool boolValue = value.GetBoolean();

    // As a fragment of composite string value,
    // make it a little more readable.
    if ( !!(flags & ibPGPropValFormatFlags::CompositeFragment) )
    {
        if ( boolValue )
        {
            return m_label;
        }
        else
        {
            if ( !!(flags & ibPGPropValFormatFlags::UneditableCompositeFragment) )
                return wxString();

            wxString notFmt;
            if ( ibPGGlobalVars->m_autoGetTranslation )
                notFmt = _("Not %s");
            else
                notFmt = wxS("Not %s");

            return wxString::Format(notFmt, m_label);
        }
    }

    if ( !(flags & ibPGPropValFormatFlags::FullValue) )
    {
        return ibPGGlobalVars->m_boolChoices[boolValue?1:0].GetText();
    }

    return boolValue? wxS("true"): wxS("false");
}

bool ibBoolProperty::StringToValue( ibVariant& variant, const wxString& text, ibPGPropValFormatFlags WXUNUSED(flags) ) const
{
    bool boolValue = false;
    if ( text.CmpNoCase(ibPGGlobalVars->m_boolChoices[1].GetText()) == 0 ||
         text.CmpNoCase(wxS("true")) == 0 ||
         text.CmpNoCase(m_label) == 0 )
        boolValue = true;

    if ( text.empty() )
    {
        variant.Reset();
        return true;
    }

    if ( variant != boolValue )
    {
        variant = ibVariant(boolValue);
        return true;
    }
    return false;
}

bool ibBoolProperty::IntToValue( ibVariant& variant, int value, ibPGPropValFormatFlags ) const
{
    bool boolValue = (bool)value;

    if ( variant != boolValue )
    {
        variant = ibVariant(boolValue);
        return true;
    }
    return false;
}

bool ibBoolProperty::DoSetAttribute( const wxString& name, ibVariant& value )
{
#if ibPG_INCLUDE_CHECKBOX
    if ( name == ibPG_BOOL_USE_CHECKBOX )
    {
        ChangeFlag(ibPGPropertyFlags_UseCheckBox, value.GetBoolean());
        return true;
    }
#endif
    if ( name == ibPG_BOOL_USE_DOUBLE_CLICK_CYCLING )
    {
        ChangeFlag(ibPGPropertyFlags_UseDCC, value.GetBoolean());
        return true;
    }
    return ibPGProperty::DoSetAttribute(name, value);
}

// -----------------------------------------------------------------------
// ibEnumProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibEnumProperty,ibPGProperty,Choice)

ibEnumProperty::ibEnumProperty( const wxString& label, const wxString& name, const wxChar* const* labels,
    const long* values, int value ) : ibPGProperty(label,name)
{
    SetIndex(0);

    if ( labels )
    {
        m_choices.Add(labels,values);

        if ( GetItemCount() )
            SetValue( (long)value );
    }
}

ibEnumProperty::ibEnumProperty( const wxString& label, const wxString& name,
    const char* const* untranslatedLabels,
    const long* values, ibPGChoices* choicesCache, int value )
    : ibPGProperty(label,name)
{
    SetIndex(0);

    wxASSERT( choicesCache );

    if ( choicesCache->IsOk() )
    {
        m_choices.Assign( *choicesCache );
        m_value = ibVariant(0L);
    }
    else
    {
        for ( int i = 0; *untranslatedLabels; untranslatedLabels++ )
        {
            const long val = values ? *values++ : i++;
            m_choices.Add(ibGetTranslation(wxString(*untranslatedLabels)), val);
        }

        if ( GetItemCount() )
            SetValue( (long)value );
    }
}

ibEnumProperty::ibEnumProperty( const wxString& label, const wxString& name,
    const wxArrayString& labels, const wxArrayInt& values, int value )
    : ibPGProperty(label,name)
{
    SetIndex(0);

    if ( !labels.empty() )
    {
        m_choices.Set(labels, values);

        if ( GetItemCount() )
            SetValue( (long)value );
    }
}

ibEnumProperty::ibEnumProperty( const wxString& label, const wxString& name,
    ibPGChoices& choices, int value )
    : ibPGProperty(label,name)
{
    SetIndex(0);

    m_choices.Assign( choices );

    if ( GetItemCount() )
        SetValue( (long)value );
}

int ibEnumProperty::GetIndexForValue( int value ) const
{
    if ( !m_choices.IsOk() )
        return -1;

    const int intVal = m_choices.Index(value);
    if ( intVal >= 0 )
        return intVal;

    return -1;
}

void ibEnumProperty::OnSetValue()
{
    const ibValueTypes valType = m_value.GetType();

    int index = -1;
    if ( valType == ibValueTypes::TYPE_NUMBER )
    {
        ValueFromInt_(m_value, &index, m_value.GetInteger(), ibPGPropValFormatFlags::FullValue);
    }
    else if ( valType == ibValueTypes::TYPE_STRING )
    {
        ValueFromString_(m_value, &index, m_value.GetString(), ibPGPropValFormatFlags::Null);
    }
    else
    {
        wxFAIL_MSG( wxS("Unexpected value type") );
        return;
    }

    m_index = index;
}

bool ibEnumProperty::ValidateValue( ibVariant& value, ibPGValidationInfo& WXUNUSED(validationInfo) ) const
{
    // Make sure string value is in the list,
    // unless property has string as preferred value type
    // To reduce code size, use conversion here as well
    if ( value.GetType() == ibValueTypes::TYPE_STRING )
        return ValueFromString_(value, nullptr, value.GetString(), ibPGPropValFormatFlags::PropertySpecific);

    return true;
}

wxString ibEnumProperty::ValueToString( ibVariant& value,
                                        ibPGPropValFormatFlags WXUNUSED(flags) ) const
{
    if ( value.GetType() == ibValueTypes::TYPE_STRING )
        return value.GetString();

    int index = m_choices.Index(value.GetInteger());
    if ( index < 0 )
        return wxString();

    return m_choices.GetLabel(index);
}

bool ibEnumProperty::StringToValue( ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags ) const
{
    return ValueFromString_(variant, nullptr, text, flags);
}

bool ibEnumProperty::IntToValue( ibVariant& variant, int intVal, ibPGPropValFormatFlags flags ) const
{
    return ValueFromInt_(variant, nullptr, intVal, flags);
}

bool ibEnumProperty::ValueFromString_(ibVariant& value, int* pIndex, const wxString& text, ibPGPropValFormatFlags WXUNUSED(flags)) const
{
    int useIndex = -1;
    long useValue = 0;

    for ( unsigned int i=0; i<m_choices.GetCount(); i++ )
    {
        const wxString& entryLabel = m_choices.GetLabel(i);
        if ( text.CmpNoCase(entryLabel) == 0 )
        {
            useIndex = (int)i;
            useValue = m_choices.GetValue(i);
            break;
        }
    }

    if (pIndex)
    {
        *pIndex = useIndex;
    }

    if ( useIndex != GetIndex() )
    {
        if ( useIndex != -1 )
        {
            value = (long)useValue;
            return true;
        }

        value.Reset();
    }

    return false;
}

bool ibEnumProperty::ValueFromInt_(ibVariant& value, int* pIndex, int intVal, ibPGPropValFormatFlags flags) const
{
    // If ibPGPropValFormatFlags::FullValue is *not* in flags, then intVal is index from combo box.
    //
    int setAsNextIndex = -2;

    if ( !!(flags & ibPGPropValFormatFlags::FullValue) )
    {
        setAsNextIndex = GetIndexForValue( intVal );
    }
    else
    {
        if ( intVal != GetIndex() )
        {
           setAsNextIndex = intVal;
        }
    }

    if ( setAsNextIndex != -2 )
    {
        if ( !(flags & ibPGPropValFormatFlags::FullValue) )
            intVal = m_choices.GetValue(intVal);

        value = (long)intVal;

        if (pIndex)
        {
            *pIndex = setAsNextIndex;
        }
        return true;
    }

    if (pIndex)
    {
        *pIndex = intVal;
    }
    return false;
}

void ibEnumProperty::SetIndex( int index )
{
    m_index = index;
}

int ibEnumProperty::GetIndex() const
{
    if ( m_value.GetType() == ibValueTypes::TYPE_EMPTY )
        return -1;

    return m_index;
}

// -----------------------------------------------------------------------
// ibEditEnumProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibEditEnumProperty, ibPGProperty, ComboBox)

ibEditEnumProperty::ibEditEnumProperty( const wxString& label, const wxString& name, const wxChar* const* labels,
    const long* values, const wxString& value )
    : ibEnumProperty(label,name,labels,values,0)
{
    SetValue( value );
}

ibEditEnumProperty::ibEditEnumProperty( const wxString& label, const wxString& name, const char* const* labels,
    const long* values, ibPGChoices* choicesCache, const wxString& value )
    : ibEnumProperty(label,name,labels,values,choicesCache,0)
{
    SetValue( value );
}

ibEditEnumProperty::ibEditEnumProperty( const wxString& label, const wxString& name,
    const wxArrayString& labels, const wxArrayInt& values, const wxString& value )
    : ibEnumProperty(label,name,labels,values,0)
{
    SetValue( value );
}

ibEditEnumProperty::ibEditEnumProperty( const wxString& label, const wxString& name,
    ibPGChoices& choices, const wxString& value )
    : ibEnumProperty(label,name,choices,0)
{
    SetValue( value );
}

void ibEditEnumProperty::OnSetValue()
{
    const ibValueTypes valType = m_value.GetType();

    int index = -1;
    if ( valType == ibValueTypes::TYPE_NUMBER )
    {
        ValueFromInt_(m_value, &index, m_value.GetInteger(), ibPGPropValFormatFlags::FullValue);
    }
    else if ( valType == ibValueTypes::TYPE_STRING )
    {
        wxString val = m_value.GetString();
        ValueFromString_(m_value, &index, val, ibPGPropValFormatFlags::Null);
        // If text is not any of the choices, store as plain text instead.
        if (index == -1)
        {
            m_value = val;
        }
    }
    else
    {
        wxFAIL_MSG( wxS("Unexpected value type") );
        return;
    }

    SetIndex(index);
}

bool ibEditEnumProperty::StringToValue(ibVariant& variant,
                                       const wxString& text, ibPGPropValFormatFlags flags) const
{
    int index;
    bool res = ValueFromString_(variant, &index, text, flags);
    // If text is not any of the choices, store as plain text instead.
    if (index == -1)
    {
        variant = text;
        res = true;
    }
    return res;
}

bool ibEditEnumProperty::ValidateValue(
                          ibVariant& WXUNUSED(value),
                          ibPGValidationInfo& WXUNUSED(validationInfo)) const
{
    return true;
}

// -----------------------------------------------------------------------
// ibFlagsProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibFlagsProperty, ibPGProperty, TextCtrl)

void ibFlagsProperty::Init(long value)
{
    m_allValueFlags = 0;

    if ( m_choices.IsOk() )
    {
        for ( unsigned int i = 0; i < GetItemCount(); i++ )
        {
            m_allValueFlags |= m_choices.GetValue(i);
        }

        // Relay ibPG_BOOL_USE_CHECKBOX and ibPG_BOOL_USE_DOUBLE_CLICK_CYCLING
        // to child bool property controls.
        bool attrUseCheckBox = !!(m_flags & ibPGPropertyFlags_UseCheckBox);
        bool attrUseDCC = !!(m_flags & ibPGPropertyFlags_UseDCC);
        for ( unsigned int i = 0; i < GetItemCount(); i++ )
        {
            bool child_val = (value & m_choices.GetValue(i)) != 0;

            wxString label = GetLabel(i);
            wxString name = label;
#if wxUSE_INTL
            if ( ibPGGlobalVars->m_autoGetTranslation )
            {
                label = ibGetTranslation(label);
            }
#endif

            ibPGProperty* boolProp = new ibBoolProperty(label, name, child_val);
            boolProp->SetAttribute(ibPG_BOOL_USE_CHECKBOX, attrUseCheckBox);
            boolProp->SetAttribute(ibPG_BOOL_USE_DOUBLE_CLICK_CYCLING, attrUseDCC);
            AddPrivateChild(boolProp);
        }
    }

    m_oldValue = value;
}

ibFlagsProperty::ibFlagsProperty( const wxString& label, const wxString& name,
    const wxChar* const* labels, const long* values, long value ) : ibPGProperty(label,name)
{
    m_flags |= ibPGPropertyFlags_UseDCC; // same default like ibBoolProperty

    if ( labels )
    {
        m_choices.Set(labels,values);

        wxASSERT( GetItemCount() > 0 );
        Init(value);

        SetValue( value );
    }
    else
    {
        m_oldValue = 0;
        m_value = ibVariant(0L);
        m_allValueFlags = 0;
    }
}

ibFlagsProperty::ibFlagsProperty( const wxString& label, const wxString& name,
        const wxArrayString& labels, const wxArrayInt& values, int value )
    : ibPGProperty(label,name)
{
    m_flags |= ibPGPropertyFlags_UseDCC; // same default like ibBoolProperty

    if ( !labels.empty() )
    {
        m_choices.Set(labels,values);

        wxASSERT( GetItemCount() > 0 );
        Init(value);

        SetValue( (long)value );
    }
    else
    {
        m_oldValue = 0;
        m_value = ibVariant(0L);
        m_allValueFlags = 0;
    }
}

ibFlagsProperty::ibFlagsProperty( const wxString& label, const wxString& name,
    ibPGChoices& choices, long value )
    : ibPGProperty(label,name)
{
    m_flags |= ibPGPropertyFlags_UseDCC; // same default like ibBoolProperty

    if ( choices.IsOk() )
    {
        m_choices.Assign(choices);

        wxASSERT( GetItemCount() > 0 );
        Init(value);

        SetValue( value );
    }
    else
    {
        m_oldValue = 0;
        m_value = ibVariant(0L);
        m_allValueFlags = 0;
    }
}

void ibFlagsProperty::OnSetValue()
{
    if ( !m_choices.IsOk() || GetItemCount() == 0 )
    {
        m_value = ibVariant(0L);
    }
    else
    {
        // normalize the value (i.e. remove extra flags)
        long val = m_value.GetInteger();
        m_value = val & m_allValueFlags;
    }

    long newFlags = m_value.GetInteger();

    if ( newFlags != m_oldValue )
    {
        // Set child modified states
        for ( unsigned int i = 0; i < GetItemCount(); i++ )
        {
            long flag = m_choices.GetValue(i);

            if ( (newFlags & flag) != (m_oldValue & flag) )
                Item(i)->ChangeFlag(ibPGFlags::Modified, true );
        }

        m_oldValue = newFlags;
    }
}

wxString ibFlagsProperty::ValueToString( ibVariant& value,
                                         ibPGPropValFormatFlags WXUNUSED(flags) ) const
{
    wxString text;

    if ( !m_choices.IsOk() )
        return text;

    long flags = value.GetInteger();

    const ibPGChoices& choices = m_choices;
    for ( unsigned int i = 0; i < GetItemCount(); i++ )
    {
        bool doAdd = ( (flags & choices.GetValue(i)) == choices.GetValue(i) );
        if ( doAdd )
        {
            text += choices.GetLabel(i);
            text += wxS(", ");
        }
    }

    // remove last comma
    if ( text.Len() > 1 )
        text.Truncate ( text.Len() - 2 );

    return text;
}

// Translate string into flag tokens
bool ibFlagsProperty::StringToValue( ibVariant& variant, const wxString& text, ibPGPropValFormatFlags ) const
{
    if ( !m_choices.IsOk() )
        return false;

    long newFlags = 0;

    // semicolons are no longer valid delimiters
    IB_PG_TOKENIZER1_BEGIN(text,wxS(','))

        if ( !token.empty() )
        {
            // Determine which one it is
            long bit = IdToBit( token );

            if ( bit != -1 )
            {
                // Changed?
                newFlags |= bit;
            }
            else
            {
                break;
            }
        }

    IB_PG_TOKENIZER1_END()

    if ( variant != newFlags )
    {
        variant = newFlags;
        return true;
    }

    return false;
}

// Converts string id to a relevant bit.
long ibFlagsProperty::IdToBit( const wxString& id ) const
{
    for ( unsigned int i = 0; i < GetItemCount(); i++ )
    {
        if ( id == GetLabel(i) )
        {
            return m_choices.GetValue(i);
        }
    }
    return -1;
}

void ibFlagsProperty::RefreshChildren()
{
    if ( !m_choices.IsOk() || !HasAnyChild() ) return;

    int flags = m_value.GetInteger();

    const ibPGChoices& choices = m_choices;
    for ( unsigned int i = 0; i < GetItemCount(); i++ )
    {
        long flag = choices.GetValue(i);

        long subVal = flags & flag;
        ibPGProperty* p = Item(i);

        if ( subVal != (m_oldValue & flag) )
            p->ChangeFlag(ibPGFlags::Modified, true );

        p->SetValue( subVal == flag?true:false );
    }

    m_oldValue = flags;
}

ibVariant ibFlagsProperty::ChildChanged( ibVariant& thisValue,
                                         int childIndex,
                                         ibVariant& childValue ) const
{
    long oldValue = thisValue.GetInteger();
    long val = childValue.GetInteger();
    unsigned long vi = m_choices.GetValue(childIndex);

    if ( val )
        return (long) (oldValue | vi);

    return (long) (oldValue & ~(vi));
}

bool ibFlagsProperty::DoSetAttribute( const wxString& name, ibVariant& value )
{
    if ( name == ibPG_BOOL_USE_CHECKBOX )
    {
        ChangeFlag(ibPGPropertyFlags_UseCheckBox, value.GetBoolean());

        for ( ibPGProperty* child : m_children )
        {
            child->SetAttribute(name, value);
        }
        return true;
    }
    else if ( name == ibPG_BOOL_USE_DOUBLE_CLICK_CYCLING )
    {
        ChangeFlag(ibPGPropertyFlags_UseDCC, value.GetBoolean());

        for ( ibPGProperty* child : m_children )
        {
            child->SetAttribute(name, value);
        }
        return true;
    }
    return ibPGProperty::DoSetAttribute(name, value);
}

// -----------------------------------------------------------------------
// ibDirProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibDirProperty, ibEditorDialogProperty, TextCtrlAndButton)

ibDirProperty::ibDirProperty( const wxString& label, const wxString& name, const wxString& value )
    : ibEditorDialogProperty(label, name)
{
    m_flags &= ~ibPGPropertyFlags_ActiveButton; // Property button enabled only in not read-only mode.
    SetValue(value);
}

wxString ibDirProperty::ValueToString(ibVariant& value, ibPGPropValFormatFlags WXUNUSED(flags)) const
{
    return value.GetString();
}

bool ibDirProperty::StringToValue(ibVariant& variant, const wxString& text, ibPGPropValFormatFlags) const
{
    if ( variant != text )
    {
        variant = text;
        return true;
    }
    return false;
}

wxValidator* ibDirProperty::DoGetValidator() const
{
    return ibFileProperty::GetClassValidator();
}

bool ibDirProperty::DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value)
{
    wxASSERT_MSG(value.GetType() == ibValueTypes::TYPE_STRING, "Function called for incompatible property");

    // Update property value from editor, if necessary
    wxSize dlg_sz;
    wxPoint dlg_pos;

    if ( ibPropertyGrid::IsSmallScreen() )
    {
        dlg_sz = wxDefaultSize;
        dlg_pos = wxDefaultPosition;
    }
    else
    {
        dlg_sz.Set(300, 400);
        dlg_pos = pg->GetGoodEditorDialogPosition(this, dlg_sz);
    }

    wxDirDialog dlg(pg->GetPanel(),
                    m_dlgTitle.empty() ? _("Choose a directory:") : m_dlgTitle,
                    value.GetString(), m_dlgStyle, dlg_pos, dlg_sz);
    if ( dlg.ShowModal() == wxID_OK )
    {
        value = dlg.GetPath();
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// ibPGDialogAdapter
// -----------------------------------------------------------------------

class DESKGUI_API ibPGDialogAdapter : public ibPGEditorDialogAdapter
{
public:
    ibPGDialogAdapter() = default;

    virtual ~ibPGDialogAdapter() = default;

    virtual bool DoShowDialog(ibPropertyGrid* pg, ibPGProperty* prop) override
    {
        ibEditorDialogProperty* dlgProp = wxDynamicCast(prop, ibEditorDialogProperty);
        wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

        ibVariant val = pg->GetUncommittedPropertyValue();
        if ( dlgProp->DisplayEditorDialog(pg, val) )
        {
            SetValue(val);
            return true;
        }

        return false;
    }
};

// -----------------------------------------------------------------------
// ibDialogProperty
// -----------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibEditorDialogProperty, ibPGProperty);

ibEditorDialogProperty::ibEditorDialogProperty(const wxString& label, const wxString& name)
    : ibPGProperty(label, name)
    , m_dlgStyle(0)
{
}

ibPGEditorDialogAdapter* ibEditorDialogProperty::GetEditorDialog() const
{
    return new ibPGDialogAdapter();
}

bool ibEditorDialogProperty::DoSetAttribute(const wxString& name, ibVariant& value)
{
    if ( name == ibPG_DIALOG_TITLE )
    {
        m_dlgTitle = value.GetString();
        return true;
    }
    return ibPGProperty::DoSetAttribute(name, value);
}

// -----------------------------------------------------------------------
// ibFileProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibFileProperty,ibEditorDialogProperty,TextCtrlAndButton)

ibFileProperty::ibFileProperty( const wxString& label, const wxString& name,
                                const wxString& value )
    : ibEditorDialogProperty(label, name)
{
    m_flags |= ibPGPropertyFlags_ShowFullFileName;
    m_flags &= ~ibPGPropertyFlags_ActiveButton; // Property button enabled only in not read-only mode.
    m_indFilter = -1;
    m_wildcard = _("All files (*.*)|*.*");

    SetValue(value);
}

wxValidator* ibFileProperty::GetClassValidator()
{
#if wxUSE_VALIDATORS
    IB_PG_DOGETVALIDATOR_ENTRY()

    // At least wxPython 2.6.2.1 required that the string argument is given
    static wxString v;
    ibTextValidator* validator = new ibTextValidator(wxFILTER_EXCLUDE_CHAR_LIST,&v);

    validator->SetCharExcludes(wxString("?*|<>\""));

    IB_PG_DOGETVALIDATOR_EXIT(validator)
#else
    return nullptr;
#endif
}

wxValidator* ibFileProperty::DoGetValidator() const
{
    return GetClassValidator();
}

void ibFileProperty::OnSetValue()
{
    const wxString& fnstr = m_value.GetString();

    wxFileName filename = fnstr;

    if ( !filename.HasName() )
    {
        m_value = ibVariant(wxString());
    }

    // Find index for extension.
    if ( m_indFilter < 0 && !fnstr.empty() )
    {
        wxString ext = filename.GetExt();
        int curind = 0;
        size_t pos = 0;
        size_t len = m_wildcard.length();

        pos = m_wildcard.find(wxS("|"), pos);
        while ( pos != wxString::npos && pos < (len-3) )
        {
            size_t ext_begin = pos + 3;

            pos = m_wildcard.find(wxS("|"), ext_begin);
            if ( pos == wxString::npos )
                pos = len;
            wxString found_ext = m_wildcard.substr(ext_begin, pos-ext_begin);

            if ( !found_ext.empty() )
            {
                if ( found_ext[0] == wxS('*') )
                {
                    m_indFilter = curind;
                    break;
                }
                if ( ext.CmpNoCase(found_ext) == 0 )
                {
                    m_indFilter = curind;
                    break;
                }
            }

            if ( pos != len )
                pos = m_wildcard.find(wxS("|"), pos+1);

            curind++;
        }
    }
}

wxFileName ibFileProperty::GetFileName() const
{
    wxFileName filename;

    if ( m_value.GetType() != ibValueTypes::TYPE_EMPTY )
        filename = m_value.GetString();

    return filename;
}

wxString ibFileProperty::ValueToString( ibVariant& value,
                                        ibPGPropValFormatFlags flags ) const
{
    wxFileName filename = value.GetString().ToWxString();

    if ( !filename.HasName() )
        return wxString();

    wxString fullName = filename.GetFullName();
    if ( fullName.empty() )
        return wxString();

    if ( !!(flags & ibPGPropValFormatFlags::FullValue) )
    {
        return filename.GetFullPath();
    }
    else if ( !!(m_flags & ibPGFlags::ShowFullFileName) )
    {
        if ( !m_basePath.empty() )
        {
            wxFileName fn2(filename);
            fn2.MakeRelativeTo(m_basePath);
            return fn2.GetFullPath();
        }
        return filename.GetFullPath();
    }

    return filename.GetFullName();
}

bool ibFileProperty::StringToValue( ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags ) const
{
    wxFileName filename = variant.GetString().ToWxString();

    if ( !!(m_flags & ibPGFlags::ShowFullFileName) || !!(flags & ibPGPropValFormatFlags::FullValue) )
    {
        if ( filename != text )
        {
            variant = text;
            return true;
        }
    }
    else
    {
        if ( filename.GetFullName() != text )
        {
            wxFileName fn = filename;
            fn.SetFullName(text);
            variant = fn.GetFullPath();
            return true;
        }
    }

    return false;
}

bool ibFileProperty::DoSetAttribute( const wxString& name, ibVariant& value )
{
    if ( name == ibPG_FILE_SHOW_FULL_PATH )
    {
        ChangeFlag(ibPGPropertyFlags_ShowFullFileName, value.GetBoolean());
        return true;
    }
    else if ( name == ibPG_FILE_WILDCARD )
    {
        m_wildcard = value.GetString();
        return true;
    }
    else if ( name == ibPG_FILE_SHOW_RELATIVE_PATH )
    {
        m_basePath = value.GetString();

        // Make sure ibPG_FILE_SHOW_FULL_PATH is also set
        m_flags |= ibPGPropertyFlags_ShowFullFileName;
        return true;
    }
    else if ( name == ibPG_FILE_INITIAL_PATH )
    {
        m_initialPath = value.GetString();
        return true;
    }
    else if ( name == ibPG_FILE_DIALOG_STYLE )
    {
        m_dlgStyle = value.GetInteger();
        return true;
    }
    return ibEditorDialogProperty::DoSetAttribute(name, value);
}

bool ibFileProperty::DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value)
{
    wxASSERT_MSG(value.GetType() == ibValueTypes::TYPE_STRING, "Function called for incompatible property");

    wxFileName filename(value.GetString());
    wxString path = filename.GetPath();
    wxString file = filename.GetFullName();

    if ( path.empty() && !m_basePath.empty() )
        path = m_basePath;

    wxFileDialog dlg(pg->GetPanel(),
        m_dlgTitle.empty() ? _("Choose a file") : m_dlgTitle,
        m_initialPath.empty() ? path : m_initialPath,
        file,
        m_wildcard.empty() ? wxString(_("All files (*.*)|*.*")) : m_wildcard,
        m_dlgStyle,
        wxDefaultPosition);

    if ( m_indFilter >= 0 )
        dlg.SetFilterIndex(m_indFilter);

    if ( dlg.ShowModal() == wxID_OK )
    {
        m_indFilter = dlg.GetFilterIndex();
        value = dlg.GetPath();
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// ibLongStringProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibLongStringProperty,ibEditorDialogProperty,TextCtrlAndButton)

ibLongStringProperty::ibLongStringProperty( const wxString& label, const wxString& name,
                                            const wxString& value )
    : ibEditorDialogProperty(label, name)
{
    m_flags |= ibPGPropertyFlags_ActiveButton; // Property button always enabled.
    m_dlgStyle = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN;
    SetValue(value);
}

wxString ibLongStringProperty::ValueToString( ibVariant& value,
                                              ibPGPropValFormatFlags WXUNUSED(flags) ) const
{
    return value.GetString();
}

bool ibLongStringProperty::DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value)
{
    wxASSERT_MSG(value.GetType() == ibValueTypes::TYPE_STRING, "Function called for incompatible property");

    // launch editor dialog (the library's, as the client's multi-line string has it: ibMStringProperty)
    ibDialog* dlg = new ibDialog(pg->GetPanel(), wxID_ANY,
                                 m_dlgTitle.empty() ? GetLabel() : m_dlgTitle,
                                 wxDefaultPosition, wxDefaultSize, m_dlgStyle);

    dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

    // Multi-line text editor dialog.
    const int spacing = ibPropertyGrid::IsSmallScreen()? 4 : 8;
    wxBoxSizer* topsizer = new wxBoxSizer( wxVERTICAL );
    wxBoxSizer* rowsizer = new wxBoxSizer( wxHORIZONTAL );
    long edStyle = wxTE_MULTILINE;
    if ( HasFlag(ibPGFlags::ReadOnly) )
        edStyle |= wxTE_READONLY;
    wxString strVal;
    ibPropertyGrid::ExpandEscapeSequences(strVal, value.GetString());
    ibTextCtrl* ed = new ibTextCtrl(dlg,wxID_ANY,strVal,
        wxDefaultPosition,wxDefaultSize,edStyle);
    if ( m_maxLen > 0 )
        ed->SetMaxLength(m_maxLen);

    rowsizer->Add(ed, wxSizerFlags(1).Expand().Border(wxALL, spacing));
    topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

    long btnSizerFlags = wxCANCEL;
    if ( !HasFlag(ibPGFlags::ReadOnly) )
        btnSizerFlags |= wxOK;
    ibStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(btnSizerFlags);
    topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM|wxRIGHT, spacing));

    dlg->SetSizer( topsizer );
    topsizer->SetSizeHints( dlg );

    if ( !ibPropertyGrid::IsSmallScreen())
    {
        dlg->SetSize(dlg->FromDIP(wxSize(400, 300)));
        dlg->Move( pg->GetGoodEditorDialogPosition(this,dlg->GetSize()) );
    }

    int res = dlg->ShowModal();

    if ( res == wxID_OK )
    {
        strVal = ed->GetValue();
        wxString strValEscaped;
        ibPropertyGrid::CreateEscapeSequences(strValEscaped, strVal);
        value = strValEscaped;
        dlg->Destroy();
        return true;
    }
    dlg->Destroy();
    return false;
}

bool ibLongStringProperty::StringToValue( ibVariant& variant, const wxString& text, ibPGPropValFormatFlags ) const
{
    if ( variant != text )
    {
        variant = text;
        return true;
    }
    return false;
}

#if wxUSE_EDITABLELISTBOX

// -----------------------------------------------------------------------
// ibPGArrayEditorDialog
// -----------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibPGArrayEditorDialog, ibDialog)
    EVT_IDLE(ibPGArrayEditorDialog::OnIdle)
wxEND_EVENT_TABLE()

wxIMPLEMENT_ABSTRACT_CLASS(ibPGArrayEditorDialog, ibDialog);

#include "deskgui/univ/ctrl/listCtrl.h"     // ibListCtrl — the editable list's list
#include "deskgui/univ/ctrl/staticText.h"   // ibStaticText — the message

// -----------------------------------------------------------------------

void ibPGArrayEditorDialog::OnIdle(wxIdleEvent& event)
{
    // Repair focus - ibEditableListBox has bitmap buttons, which
    // get focus, and lose focus (into the oblivion) when they
    // become disabled due to change in control state.

    wxWindow* lastFocused = m_lastFocused;
    wxWindow* focus = ::wxWindow::FindFocus();

    // If last focused control became disabled, set focus back to
    // ibEditableListBox
    if ( lastFocused && focus != lastFocused &&
         lastFocused->GetParent() == m_elbSubPanel &&
         !lastFocused->IsEnabled() )
    {
        m_elb->GetListCtrl()->SetFocus();
    }

    m_lastFocused = focus;

    event.Skip();
}

// -----------------------------------------------------------------------

ibPGArrayEditorDialog::ibPGArrayEditorDialog()
    : ibDialog()
{
    Init();
}

// -----------------------------------------------------------------------

void ibPGArrayEditorDialog::Init()
{
    m_elb = nullptr;
    m_elbSubPanel = nullptr;
    m_lastFocused = nullptr;
    m_hasCustomNewAction = false;
    m_itemPendingAtIndex = -1;
    m_modified = false;
}

// -----------------------------------------------------------------------

bool ibPGArrayEditorDialog::Create( wxWindow *parent,
                                  const wxString& message,
                                  const wxString& caption,
                                  long style,
                                  const wxPoint& pos,
                                  const wxSize& sz )
{
    // On ibMAC the dialog shows incorrectly if style is not exactly wxCAPTION
    // FIXME: This should be only a temporary fix.
#ifdef __WXMAC__
    wxUnusedVar(style);
    int useStyle = wxCAPTION;
#else
    int useStyle = style;
#endif

    bool res = ibDialog::Create(parent, wxID_ANY, caption, pos, sz, useStyle);

    SetFont(parent->GetFont()); // To allow entering chars of the same set as the propGrid

    const int spacing = ibPropertyGrid::IsSmallScreen()? 3: 4;

    m_modified = false;

    wxBoxSizer* topsizer = new wxBoxSizer( wxVERTICAL );

    // Message
    if ( !message.empty() )
        topsizer->Add( new ibStaticText(this, wxID_ANY, message),
            wxSizerFlags(0).Left().Border(wxALL, spacing) );

    m_elb = new ibEditableListBox(this, wxID_ANY, message,
                                  wxDefaultPosition,
                                  wxDefaultSize,
                                  ibEL_ALLOW_NEW |
                                  ibEL_ALLOW_EDIT |
                                  ibEL_ALLOW_DELETE);

    // Set custom text for "New" button, if provided
    if ( !m_customNewButtonText.empty() )
    {
        m_elb->GetNewButton()->SetToolTip(m_customNewButtonText);
    }

    // Populate the list box
    wxArrayString arr;
    for ( size_t i=0; i<ArrayGetCount(); i++ )
        arr.push_back(ArrayGet(i));
    m_elb->SetStrings(arr);

    m_elbSubPanel = m_elb->GetNewButton()->GetParent();

    // Connect event handlers
    ibListCtrl* lc = m_elb->GetListCtrl();

    m_elb->GetNewButton()->Bind(wxEVT_BUTTON,
                                &ibPGArrayEditorDialog::OnAddClick, this);
    m_elb->GetDelButton()->Bind(wxEVT_BUTTON,
                                &ibPGArrayEditorDialog::OnDeleteClick, this);
    m_elb->GetUpButton()->Bind(wxEVT_BUTTON,
                               &ibPGArrayEditorDialog::OnUpClick, this);
    m_elb->GetDownButton()->Bind(wxEVT_BUTTON,
                                 &ibPGArrayEditorDialog::OnDownClick, this);

    lc->Bind(wxEVT_LIST_BEGIN_LABEL_EDIT,
             &ibPGArrayEditorDialog::OnBeginLabelEdit, this);

    lc->Bind(wxEVT_LIST_END_LABEL_EDIT,
             &ibPGArrayEditorDialog::OnEndLabelEdit, this);

    topsizer->Add(m_elb, wxSizerFlags(1).Expand().Border(0, spacing));

    // Standard dialog buttons
    ibStdDialogButtonSizer* buttonSizer = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxALL, spacing));

    m_elb->SetFocus();

    SetSizer( topsizer );
    topsizer->SetSizeHints( this );

    if ( !ibPropertyGrid::IsSmallScreen() )
    {
        if ( sz.x == wxDefaultSize.x &&
             sz.y == wxDefaultSize.y )
            SetSize(275, 360);
        else
            SetSize(sz);
    }

    return res;
}

// -----------------------------------------------------------------------

int ibPGArrayEditorDialog::GetSelection() const
{
    ibListCtrl* lc = m_elb->GetListCtrl();

    int index = lc->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if ( index == -1 )
        return wxNOT_FOUND;

    return index;
}

// -----------------------------------------------------------------------

void ibPGArrayEditorDialog::OnAddClick(wxCommandEvent& event)
{
    ibListCtrl* lc = m_elb->GetListCtrl();
    int newItemIndex = lc->GetItemCount() - 1;

    if ( m_hasCustomNewAction )
    {
        wxString str;
        if ( OnCustomNewAction(&str) )
        {
            if ( ArrayInsert(str, newItemIndex) )
            {
                lc->InsertItem(newItemIndex, str);
                m_modified = true;
            }
        }

        // Do *not* skip the event! We do not want the ibEditableListBox
        // to do anything.
    }
    else
    {
        event.Skip();
    }
}

// -----------------------------------------------------------------------

void ibPGArrayEditorDialog::OnDeleteClick(wxCommandEvent& event)
{
    int index = GetSelection();
    if ( index >= 0 )
    {
        ArrayRemoveAt( index );
        m_modified = true;
    }

    event.Skip();
}

// -----------------------------------------------------------------------

void ibPGArrayEditorDialog::OnUpClick(wxCommandEvent& event)
{
    int index = GetSelection();
    if ( index > 0 )
    {
        ArraySwap(index-1,index);
        m_modified = true;
    }

    event.Skip();
}

// -----------------------------------------------------------------------

void ibPGArrayEditorDialog::OnDownClick(wxCommandEvent& event)
{
    ibListCtrl* lc = m_elb->GetListCtrl();
    int index = GetSelection();
    int lastStringIndex = lc->GetItemCount() - 1;
    if ( index >= 0 && index < lastStringIndex )
    {
        ArraySwap(index, index+1);
        m_modified = true;
    }

    event.Skip();
}

// -----------------------------------------------------------------------

void ibPGArrayEditorDialog::OnEndLabelEdit(wxListEvent& event)
{
    wxString str = event.GetLabel();

    if ( m_itemPendingAtIndex >= 0 )
    {
        // Add a new item
        if ( ArrayInsert(str, m_itemPendingAtIndex) )
        {
            m_modified = true;
        }
        else
        {
            // Editable list box doesn't really respect Veto(), but
            // it recognizes if no text was added, so we simulate
            // Veto() using it.
            event.m_item.SetText(wxString());
            m_elb->GetListCtrl()->SetItemText(m_itemPendingAtIndex,
                                              wxString());

            event.Veto();
        }
    }
    else
    {
        // Change an existing item
        long index = event.GetIndex();
        if ( ArraySet(index, str) )
            m_modified = true;
        else
            event.Veto();
    }

    event.Skip();
}

void ibPGArrayEditorDialog::OnBeginLabelEdit(wxListEvent& evt)
{
    ibListCtrl* lc = m_elb->GetListCtrl();
    const int lastStringIndex = lc->GetItemCount() - 1;
    const int curItemIndex = evt.GetIndex();
    // If current index is >= then last available index
    // then we have a new pending element.
    m_itemPendingAtIndex  = curItemIndex < lastStringIndex? -1: curItemIndex;

    evt.Skip();
}

#endif // wxUSE_EDITABLELISTBOX

// -----------------------------------------------------------------------
// ibPGArrayStringEditorDialog
// -----------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibPGArrayStringEditorDialog, ibPGArrayEditorDialog);

wxBEGIN_EVENT_TABLE(ibPGArrayStringEditorDialog, ibPGArrayEditorDialog)
wxEND_EVENT_TABLE()

// -----------------------------------------------------------------------

wxString ibPGArrayStringEditorDialog::ArrayGet( size_t index )
{
    return m_array[index];
}

size_t ibPGArrayStringEditorDialog::ArrayGetCount()
{
    return m_array.size();
}

bool ibPGArrayStringEditorDialog::ArrayInsert( const wxString& str, int index )
{
    if (index<0)
        m_array.Add(str);
    else
        m_array.Insert(str,index);
    return true;
}

bool ibPGArrayStringEditorDialog::ArraySet( size_t index, const wxString& str )
{
    wxCHECK_MSG(index < m_array.size(), false, "Index out of range");
    m_array[index] = str;
    return true;
}

void ibPGArrayStringEditorDialog::ArrayRemoveAt( int index )
{
    m_array.RemoveAt(index);
}

void ibPGArrayStringEditorDialog::ArraySwap( size_t first, size_t second )
{
    std::swap(m_array[first], m_array[second]);
}

ibPGArrayStringEditorDialog::ibPGArrayStringEditorDialog()
    : ibPGArrayEditorDialog()
{
    Init();
}

void ibPGArrayStringEditorDialog::Init()
{
    m_pCallingClass = nullptr;
}

bool
ibPGArrayStringEditorDialog::OnCustomNewAction(wxString* resString)
{
    return m_pCallingClass->OnCustomStringEdit(m_parent, *resString);
}

// -----------------------------------------------------------------------
// ibArrayStringProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibArrayStringProperty, ibEditorDialogProperty, TextCtrlAndButton)

ibArrayStringProperty::ibArrayStringProperty( const wxString& label,
                                                        const wxString& name,
                                                        const wxArrayString& array )
    : ibEditorDialogProperty(label,name)
    , m_delimiter(',')
{
    m_dlgStyle = ibAEDIALOG_STYLE;
    SetValue( IBVARIANT(array) );
}

void ibArrayStringProperty::OnSetValue()
{
    GenerateValueAsString();
}

wxString ibArrayStringProperty::ConvertArrayToString(const wxArrayString& arr,
                                                     const wxUniChar& delimiter) const
{
    int flag;
    if ( delimiter == '"' || delimiter == '\'' )
    {
        // Quoted strings
        flag = Escape | QuoteStrings;
    }
    else
    {
        // Regular delimiter
        flag = 0;
    }

    return ArrayStringToString(arr, delimiter, flag);
}

wxString ibArrayStringProperty::ValueToString( ibVariant& WXUNUSED(value),
                                               ibPGPropValFormatFlags flags ) const
{
    //
    // If this is called from GetValueAsString(), return cached string
    if ( !!(flags & ibPGPropValFormatFlags::ValueIsCurrent) )
    {
        return m_display;
    }

    wxArrayString arr = wxArrayStringRefFromVariant(m_value);
    return ConvertArrayToString(arr, m_delimiter);
}

// Converts wxArrayString to a string separated by delimiters and spaces.
// preDelim is useful for "str1" "str2" style. Set flags to 1 to do slash
// conversion.
wxString ibArrayStringProperty::ArrayStringToString(const wxArrayString& src,
                                                    wxUniChar delimiter, int flags)
{
    wxString pdr;
    wxString preas;

    size_t itemCount = src.size();

    if ( flags & Escape )
    {
        preas = delimiter;
        pdr = wxS("\\");
        pdr += delimiter;
    }

    wxString dst;

    if ( itemCount )
        dst.append( preas );

    wxString delimStr(delimiter);

    for ( size_t i = 0; i < itemCount; i++ )
    {
        wxString str( src.Item(i) );

        // Do some character conversion.
        // Converts \ to \\ and $delimiter to \$delimiter
        // Useful when quoting.
        if ( flags & Escape )
        {
            str.Replace( wxS("\\"), wxS("\\\\"), true );
            if ( !pdr.empty() )
                str.Replace( preas, pdr, true );
        }

        dst.append( str );

        if ( i < (itemCount-1) )
        {
            dst.append( delimStr );
            dst.append( wxS(" ") );
            dst.append( preas );
        }
        else if ( flags & QuoteStrings )
            dst.append( delimStr );
    }

    return dst;
}

void ibArrayStringProperty::GenerateValueAsString()
{
    wxArrayString arr = wxArrayStringRefFromVariant(m_value);
    m_display = ConvertArrayToString(arr, m_delimiter);
}

// Default implementation doesn't do anything.
bool ibArrayStringProperty::OnCustomStringEdit( wxWindow*, wxString& )
{
    return false;
}

ibPGArrayEditorDialog* ibArrayStringProperty::CreateEditorDialog()
{
    return new ibPGArrayStringEditorDialog();
}

bool ibArrayStringProperty::DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value)
{
    wxASSERT_MSG(value.GetClassName() == wxArrayStringVariantData::s_className, "Function called for incompatible property");
    if ( !pg->EditorValidate() )
        return false;

    // Create editor dialog.
    ibPGArrayEditorDialog* dlg = CreateEditorDialog();
#if wxUSE_VALIDATORS
    wxValidator* validator = GetValidator();
    ibPGInDialogValidator dialogValidator;
#endif

    ibPGArrayStringEditorDialog* strEdDlg = wxDynamicCast(dlg, ibPGArrayStringEditorDialog);

    if ( strEdDlg )
        strEdDlg->SetCustomButton(m_customBtnText, this);

    dlg->SetDialogValue( value );
    dlg->Create(pg->GetPanel(), wxString(),
                m_dlgTitle.empty() ? GetLabel() : m_dlgTitle, m_dlgStyle);

    if ( !ibPropertyGrid::IsSmallScreen() )
    {
        dlg->Move( pg->GetGoodEditorDialogPosition(this,dlg->GetSize()) );
    }

    bool retVal;

    for (;;)
    {
        retVal = false;

        int res = dlg->ShowModal();

        if ( res != wxID_OK || !dlg->IsModified() )
            break;

        ibVariant curValue = dlg->GetDialogValue();
        if ( curValue.GetType() == ibValueTypes::TYPE_EMPTY )
            break;

        wxArrayString actualValue = wxArrayStringRefFromVariant(curValue);
#if wxUSE_VALIDATORS
        wxString tempStr = ConvertArrayToString(actualValue, m_delimiter);
        if ( dialogValidator.DoValidate(pg, validator, tempStr) )
#endif
        {
            value = IBVARIANT(actualValue);
            retVal = true;
            break;
        }
    }

    delete dlg;

    return retVal;
}

bool ibArrayStringProperty::StringToValue( ibVariant& variant,
                                           const wxString& text, ibPGPropValFormatFlags ) const
{
    wxArrayString arr;

    if ( m_delimiter == '"' || m_delimiter == '\'' )
    {
        // Quoted strings
        IB_PG_TOKENIZER2_BEGIN(text, m_delimiter)

            // Need to replace backslashes with empty characters
            // (opposite what is done in ConvertArrayToString()).
            token.Replace ( wxS("\\\\"), wxS("\\"), true );

            arr.Add( token );

        IB_PG_TOKENIZER2_END()
    }
    else
    {
        // Regular delimiter
        IB_PG_TOKENIZER1_BEGIN(text, m_delimiter)
            arr.Add( token );
        IB_PG_TOKENIZER1_END()
    }

    variant = IBVARIANT(arr);

    return true;
}

bool ibArrayStringProperty::DoSetAttribute( const wxString& name, ibVariant& value )
{
    if ( name == ibPG_ARRAY_DELIMITER )
    {
        // the core's value holds no character: the delimiter comes as a text, its first one
        const wxString delimiter = value.GetString();
        m_delimiter = delimiter.empty() ? wxUniChar() : delimiter.GetChar(0);
        GenerateValueAsString();
        return true;
    }
    return ibEditorDialogProperty::DoSetAttribute(name, value);
}

// -----------------------------------------------------------------------
// ibPGInDialogValidator
// -----------------------------------------------------------------------

#if wxUSE_VALIDATORS
bool ibPGInDialogValidator::DoValidate( ibPropertyGrid* propGrid,
                                        wxValidator* validator,
                                        const wxString& value )
{
    if ( !validator )
        return true;

    ibTextCtrl* tc = m_textCtrl;

    if ( !tc )
    {
        {
            tc = new ibTextCtrl( propGrid, wxID_ANY, wxString(),
                                 wxPoint(30000,30000));
            tc->Hide();
        }

        m_textCtrl = tc;
    }

    tc->SetValue(value);

    validator->SetWindow(tc);
    bool res = validator->Validate(propGrid);

    return res;
}
#else
bool ibPGInDialogValidator::DoValidate( ibPropertyGrid* WXUNUSED(propGrid),
                                        wxValidator* WXUNUSED(validator),
                                        const wxString& WXUNUSED(value) )
{
    return true;
}
#endif

// -----------------------------------------------------------------------
