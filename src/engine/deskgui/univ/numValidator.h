// FORKED from the wx numeric validators (wxIntegerValidator -> ibIntegerValidator, wxFloatingPointValidator ->
// ibFloatingPointValidator, mechanical ib prefixes). wx's find their field as a wxTextCtrl or a wxComboBox only, so
// on the library's field they asserted and filtered nothing; these find it as the library's text control.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/valnum.h
// Purpose:     Numeric validator classes.
// Author:      Vadim Zeitlin based on the submission of Fulvio Senore
// Created:     2010-11-06
// Copyright:   (c) 2010 wxWidgets team
//              (c) 2011 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_NUMVALIDATOR_H_
#define _DESKGUI_UNIV_NUMVALIDATOR_H_

#include "deskgui/deskgui.h"

#if wxUSE_VALIDATORS

#include <wx/validate.h>
#include <wx/valnum.h>        // wxNumValidatorStyle, the wxNUM_VAL_* styles

// This header uses std::numeric_limits<>::min/max, but these symbols are,
// unfortunately, often defined as macros and the code here wouldn't compile in
// this case, so preventively undefine them to avoid this problem.
#undef min
#undef max

#include <limits>

#include "deskgui/univ/ctrl/textCtrl.h"

// ----------------------------------------------------------------------------
// Base class for all numeric validators.
// ----------------------------------------------------------------------------

class DESKGUI_API ibNumValidatorBase : public wxValidator
{
public:
    // Change the validator style. Usually it's specified during construction.
    void SetStyle(int style) { m_style = style; }


    // Override base class method to not do anything but always return success:
    // we don't need this as we do our validation on the fly here.
    virtual bool Validate(wxWindow * WXUNUSED(parent)) override { return true; }

    // Override base class method to check that the window is a text control.
    // SEAM vs wx: the library's text control - wx's took a wxTextCtrl or a wxComboBox
    virtual void SetWindow(wxWindow *win) override;

protected:
    ibNumValidatorBase(int style)
    {
        m_style = style;
    }

    ibNumValidatorBase(const ibNumValidatorBase& other) : wxValidator(other)
    {
        m_style = other.m_style;
    }

    bool HasFlag(wxNumValidatorStyle style) const
    {
        return (m_style & style) != 0;
    }

    // Get the text entry of the associated control. Normally shouldn't ever
    // return nullptr (and will assert if it does return it) but the caller should
    // still test the return value for safety.
    ibTextCtrl *GetTextEntry() const;

    // Convert wxNUM_VAL_THOUSANDS_SEPARATOR, wxNUM_VAL_NO_TRAILING_ZEROES,
    // wxNUM_VAL_SIGN_PLUS, and wxNUM_VAL_SIGN_SPACE
    // bits of our style to the corresponding wxNumberFormatter::Style values.
    int GetFormatFlags() const;

    // Return the string which would result from inserting the given character
    // at the specified position.
    wxString GetValueAfterInsertingChar(wxString val, int pos, wxChar ch) const
    {
        val.insert(pos, ch);
        return val;
    }

    // Return true if this control allows negative numbers in it.
    //
    // If it doesn't, we don't allow entering "-" at all.
    virtual bool CanBeNegative() const = 0;

private:
    // Check whether the specified character can be inserted in the control at
    // the given position in the string representing the current controls
    // contents.
    //
    // Notice that the base class checks for '-' itself so it's never passed to
    // this function.
    virtual bool IsCharOk(const wxString& val, int pos, wxChar ch) const = 0;

    // Return the canonical form of the number corresponding to the contents of
    // the string: if the input string is invalid, return a string representing
    // some valid value.
    virtual wxString NormalizeString(const wxString& s) const = 0;


    // Event handlers.
    void OnChar(wxKeyEvent& event);
    void OnKillFocus(wxFocusEvent& event);


    // Determine the current insertion point and text in the associated control.
    void GetCurrentValueAndInsertionPoint(wxString& val, int& pos) const;

    // Return true if pressing a '-' key is acceptable for the current control
    // contents and insertion point. This is used by OnChar() to handle '-' and
    // relies on CanBeNegative() implementation in the derived class.
    bool IsMinusOk(const wxString& val, int pos) const;


    // Combination of wxVAL_NUM_XXX values.
    int m_style;


    wxDECLARE_EVENT_TABLE();

    wxDECLARE_NO_ASSIGN_CLASS(ibNumValidatorBase);
};

namespace ibPrivate
{

// This is a helper class used by ibIntegerValidator and ibFloatingPointValidator
// below that implements Transfer{To,From}Window() adapted to the type of the
// variable.
//
// The template argument B is the name of the base class which must derive from
// ibNumValidatorBase and define LongestValueType type and {To,As}String()
// methods i.e. basically be one of wx{Integer,Number}ValidatorBase classes.
//
// The template argument T is just the type handled by the validator that will
// inherit from this one.
template <class B, typename T>
class ibNumValidator : public B
{
public:
    typedef B BaseValidator;
    typedef T ValueType;

    typedef typename BaseValidator::LongestValueType LongestValueType;

    wxCOMPILE_TIME_ASSERT
    (
        sizeof(ValueType) <= sizeof(LongestValueType),
        UnsupportedType
    );

    void SetMin(ValueType min)
    {
        m_min = min;
    }

    ValueType GetMin() const
    {
        return m_min;
    }

    void SetMax(ValueType max)
    {
        m_max = max;
    }

    ValueType GetMax() const
    {
        return m_max;
    }

    void SetRange(ValueType min, ValueType max)
    {
        SetMin(min);
        SetMax(max);
    }

    void GetRange(ValueType& min, ValueType& max) const
    {
        min = GetMin();
        max = GetMax();
    }

    virtual bool TransferToWindow()  override
    {
        if ( m_value )
        {
            ibTextCtrl * const control = BaseValidator::GetTextEntry();
            if ( !control )
                return false;

            control->SetValue(NormalizeValue(static_cast<LongestValueType>(*m_value)));
        }

        return true;
    }

    virtual bool TransferFromWindow() override
    {
        if ( m_value )
        {
            ibTextCtrl * const control = BaseValidator::GetTextEntry();
            if ( !control )
                return false;

            const wxString s(control->GetValue());
            LongestValueType value;
            if ( s.empty() && BaseValidator::HasFlag(wxNUM_VAL_ZERO_AS_BLANK) )
                value = 0;
            else if ( !BaseValidator::FromString(s, &value) )
                return false;

            if ( !this->IsInRange(value) )
                return false;

            *m_value = static_cast<ValueType>(value);
        }

        return true;
    }

protected:
    ibNumValidator(ValueType *value, int style)
        : BaseValidator(style),
          m_value(value)
    {
    }

    // Implement ibNumValidatorBase virtual method which is the same for
    // both integer and floating point numbers.
    virtual wxString NormalizeString(const wxString& s) const override
    {
        // If empty value is handled as zero, just return it as is for
        // consistency with TransferFromWindow() and NormalizeValue().
        if ( s.empty() && BaseValidator::HasFlag(wxNUM_VAL_ZERO_AS_BLANK) )
            return wxString();

        LongestValueType value;
        if ( !BaseValidator::FromString(s, &value) )
        {
            // We don't have any valid number at all, just arbitrarily decide
            // to return the minimum value.
            value = static_cast<LongestValueType>(m_min);
        }
        else if ( !this->IsInRange(value) )
        {
            // We do have a value, but it's out of range: clamp it to the
            // closest limit.
            if ( value > static_cast<LongestValueType>(m_max) )
                value = static_cast<LongestValueType>(m_max);
            else
                value = static_cast<LongestValueType>(m_min);
        }

        return NormalizeValue(value);
    }

    virtual bool CanBeNegative() const override { return m_min < 0; }


    // This member is protected because it can be useful to the derived classes
    // in their Transfer{From,To}Window() implementations.
    ValueType * const m_value;

private:
    // Just a helper which is a common part of TransferToWindow() and
    // NormalizeString(): returns string representation of a number honouring
    // wxNUM_VAL_ZERO_AS_BLANK flag.
    wxString NormalizeValue(LongestValueType value) const
    {
        // We really want to compare with the exact 0 here, so disable gcc
        // warning about doing this.
        wxGCC_WARNING_SUPPRESS(float-equal)

        wxString s;
        if ( value != 0 || !BaseValidator::HasFlag(wxNUM_VAL_ZERO_AS_BLANK) )
            s = this->ToString(value);

        wxGCC_WARNING_RESTORE(float-equal)

        return s;
    }

    // Minimal and maximal values accepted (inclusive).
    ValueType m_min, m_max;

    wxDECLARE_NO_ASSIGN_DEF_COPY(ibNumValidator);
};

} // namespace ibPrivate

// ----------------------------------------------------------------------------
// Validators for integer numbers.
// ----------------------------------------------------------------------------

// Base class for integer numbers validator. This class contains all non
// type-dependent code of ibIntegerValidator<> and always works with values of
// type LongestValueType. It is not meant to be used directly, please use
// ibIntegerValidator<> only instead.
class DESKGUI_API ibIntegerValidatorBase : public ibNumValidatorBase
{
protected:
    // Define the type we use here, it should be the maximal-sized integer type
    // we support to make it possible to base ibIntegerValidator<> for any type
    // on it.
    typedef wxLongLong_t LongestValueType;
    typedef wxULongLong_t ULongestValueType;

    ibIntegerValidatorBase(int style)
        : ibNumValidatorBase(style)
    {
        wxASSERT_MSG( !(style & wxNUM_VAL_NO_TRAILING_ZEROES),
                      "This style doesn't make sense for integers." );
        wxASSERT_MSG(!(style & wxNUM_VAL_SIGN_PLUS) ||
                        !(style & wxNUM_VAL_SIGN_SPACE),
                        "Sign prefix conflict.");
    }

    // Default copy ctor is ok.

    // Provide methods for ibNumValidator use.
    wxString ToString(LongestValueType value) const;
    bool FromString(const wxString& s, LongestValueType *value) const;

    virtual bool IsInRange(LongestValueType value) const = 0;

    // Check whether the value is in the extended range allowed on input.
    //
    // Notice that the minimal and maximal values that have to be accepted on
    // input may differ from the actually defined range to allow entering
    // a temporarily invalid number because otherwise it would not be possible
    // to enter any digits in an initially empty control limited to the values
    // between "10" and "20".
    virtual bool IsInInputRange(LongestValueType value) const = 0;

    // Implement ibNumValidatorBase pure virtual method.
    virtual bool IsCharOk(const wxString& val, int pos, wxChar ch) const override;

private:
    wxDECLARE_NO_ASSIGN_DEF_COPY(ibIntegerValidatorBase);
};

// Validator for integer numbers. It can actually work with any integer type
// (short, int or long and long long if supported) and their unsigned versions
// as well.
template <typename T>
class ibIntegerValidator
    : public ibPrivate::ibNumValidator<ibIntegerValidatorBase, T>
{
public:
    typedef T ValueType;

    typedef
        ibPrivate::ibNumValidator<ibIntegerValidatorBase, T> Base;
    typedef
        ibIntegerValidatorBase::LongestValueType LongestValueType;

    // Ctor for an integer validator.
    //
    // Sets the range appropriately for the type, including setting 0 as the
    // minimal value for the unsigned types.
    ibIntegerValidator(ValueType *value = nullptr, int style = wxNUM_VAL_DEFAULT)
        : Base(value, style)
    {
        this->SetMin(std::numeric_limits<ValueType>::min());
        this->SetMax(std::numeric_limits<ValueType>::max());
    }

  // Ctor for an integer validator.
  //
  // Sets the range to the specified interval [min, max].
  ibIntegerValidator(ValueType *value,
                     ValueType min,
                     ValueType max,
                     int style = wxNUM_VAL_DEFAULT)
      : Base(value, style)
  {
    this->SetMin(min);
    this->SetMax(max);
  }

    wxNODISCARD virtual wxObject *Clone() const override { return new ibIntegerValidator(*this); }

    virtual bool IsInRange(LongestValueType value) const override
    {
        return IsValueInRange( value, this->GetMin(), this->GetMax() );
    }

    virtual bool IsInInputRange(LongestValueType value) const override
    {
        ValueType min = wxMin( 1, this->GetMin() );
        ValueType max = wxMax( 0, this->GetMax() );

        return IsValueInRange( value, min, max );
    }

private:
    bool IsValueInRange(LongestValueType value, ValueType min, ValueType max ) const
    {
        // LongestValueType is used as a container for the values of any type
        // which can be used in type-independent ibIntegerValidatorBase code,
        // but we need to use the correct type for comparisons, notably for
        // comparing unsigned values correctly, so cast to this type and check
        // that we don't lose precision while doing it.
        const ValueType valueT = static_cast<ValueType>(value);
        if ( static_cast<LongestValueType>(valueT) != value )
        {
            // The conversion wasn't lossless, so the value must not be exactly
            // representable in this type and so is definitely not in range.
            return false;
        }

        return min <= valueT && valueT <= max;
    }

    wxDECLARE_NO_ASSIGN_DEF_COPY(ibIntegerValidator);
};

// Helper function for creating integer validators which allows to avoid
// explicitly specifying the type as it deduces it from its parameter.
template <typename T>
inline ibIntegerValidator<T>
ibMakeIntegerValidator(T *value, int style = wxNUM_VAL_DEFAULT)
{
    return ibIntegerValidator<T>(value, style);
}

// ----------------------------------------------------------------------------
// Validators for floating point numbers.
// ----------------------------------------------------------------------------

// Similar to ibIntegerValidatorBase, this class is not meant to be used
// directly, only ibFloatingPointValidator<> should be used in the user code.
class DESKGUI_API ibFloatingPointValidatorBase : public ibNumValidatorBase
{
public:
    // Set precision i.e. the number of digits shown (and accepted on input)
    // after the decimal point. By default this is set to the maximal precision
    // supported by the type handled by the validator.
    void SetPrecision(unsigned precision) { m_precision = precision; }

    // Set multiplier applied for displaying the value, e.g. 100 if the value
    // should be displayed in percents, so that the variable containing 0.5
    // would be displayed as 50.
    void SetFactor(double factor) { m_factor = factor; }

protected:
    // Notice that we can't use "long double" here because it's not supported
    // by wxNumberFormatter yet, so restrict ourselves to just double (and
    // float).
    typedef double LongestValueType;

    ibFloatingPointValidatorBase(int style)
        : ibNumValidatorBase(style)
    {
        m_factor = 1.0;
    }

    // Default copy ctor is ok.

    // Provide methods for ibNumValidator use.
    wxString ToString(LongestValueType value) const;
    bool FromString(const wxString& s, LongestValueType *value) const;

    virtual bool IsInRange(LongestValueType value) const = 0;

    // Check whether the value is in the extended range allowed on input.
    //
    // Notice that the minimal and maximal values that have to be accepted on
    // input may differ from the actually defined range to allow entering
    // a temporarily invalid number because otherwise it would not be possible
    // to enter any digits in an initially empty control limited to the values
    // between "10" and "20".
    virtual bool IsInInputRange(LongestValueType value) const = 0;

    // Implement ibNumValidatorBase pure virtual method.
    virtual bool IsCharOk(const wxString& val, int pos, wxChar ch) const override;

private:
    // Maximum number of decimals digits after the decimal separator.
    unsigned m_precision;

    // Factor applied for the displayed the value.
    double m_factor;

    wxDECLARE_NO_ASSIGN_DEF_COPY(ibFloatingPointValidatorBase);
};

// Validator for floating point numbers. It can be used with float, double or
// long double values.
template <typename T>
class ibFloatingPointValidator
    : public ibPrivate::ibNumValidator<ibFloatingPointValidatorBase, T>
{
public:
    typedef T ValueType;
    typedef ibPrivate::ibNumValidator<ibFloatingPointValidatorBase, T> Base;
    typedef ibFloatingPointValidatorBase::LongestValueType LongestValueType;


    // Ctor using implicit (maximal) precision for this type.
    ibFloatingPointValidator(ValueType *value = nullptr,
                             int style = wxNUM_VAL_DEFAULT)
        : Base(value, style)
    {
        DoSetMinMax();

        this->SetPrecision(std::numeric_limits<ValueType>::digits10);
    }

    // Ctor specifying an explicit precision.
    ibFloatingPointValidator(int precision,
                      ValueType *value = nullptr,
                      int style = wxNUM_VAL_DEFAULT)
        : Base(value, style)
    {
        DoSetMinMax();

        this->SetPrecision(precision);
    }

    wxNODISCARD virtual wxObject *Clone() const override
    {
        return new ibFloatingPointValidator(*this);
    }

    virtual bool IsInRange(LongestValueType value) const override
    {
        const ValueType valueT = static_cast<ValueType>(value);

        return this->GetMin() <= valueT && valueT <= this->GetMax();
    }

    virtual bool IsInInputRange(LongestValueType value) const override
    {
        const ValueType valueT = static_cast<ValueType>(value);

        ValueType min = wxMin( 0, this->GetMin() );
        ValueType max = wxMax( 0, this->GetMax() );

        return min <= valueT && valueT <= max;
    }

private:
    void DoSetMinMax()
    {
        // NB: Do not use min(), it's not the smallest representable value for
        //     the floating point types but rather the smallest representable
        //     positive value.
        this->SetMin(-std::numeric_limits<ValueType>::max());
        this->SetMax( std::numeric_limits<ValueType>::max());
    }
};

// Helper similar to wxMakeIntValidator().
//
// NB: Unfortunately we can't just have a wxMakeNumericValidator() which would
//     return either ibIntegerValidator<> or ibFloatingPointValidator<> so we
//     do need two different functions.
template <typename T>
inline ibFloatingPointValidator<T>
ibMakeFloatingPointValidator(T *value, int style = wxNUM_VAL_DEFAULT)
{
    return ibFloatingPointValidator<T>(value, style);
}

template <typename T>
inline ibFloatingPointValidator<T>
ibMakeFloatingPointValidator(int precision, T *value, int style = wxNUM_VAL_DEFAULT)
{
    return ibFloatingPointValidator<T>(precision, value, style);
}

#endif // wxUSE_VALIDATORS

#endif // _DESKGUI_UNIV_NUMVALIDATOR_H_
