// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, include/wx/propgrid/propgriddefs.h): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/propgrid/propgriddefs.h
// Purpose:     ibPropertyGrid miscellaneous definitions
// Author:      Jaakko Salli
// Created:     2008-08-31
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_PROPGRID_PROPGRIDDEFS_H_
#define _DESKGUI_UNIV_PROPGRID_PROPGRIDDEFS_H_

#include "wx/defs.h"

#include "deskgui/deskgui.h"
#include "deskgui/dateconv.h"   // typeConv — a date property's value, ibDateTime, across to a picker's wxDateTime

#include "core/variant.h"       // ibVariant — the value a property holds (wx's wxVariant, before the fork)

#if wxUSE_PROPGRID

#include "wx/colour.h"
// complete: each is held by its value's own class (IB_PG_DECLARE_VARIANT_DATA below), not only named
#include "wx/gdicmn.h"          // wxPoint, wxSize
#include "wx/font.h"
#include "wx/arrstr.h"
#include "wx/dynarray.h"        // wxArrayInt

#include <limits>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------

// Set 1 to include advanced properties (ibFontProperty, ibColourProperty, etc.)
#ifndef ibPG_INCLUDE_ADVPROPS
    #define ibPG_INCLUDE_ADVPROPS           1
#endif

// Set 1 to include checkbox editor class
#define ibPG_INCLUDE_CHECKBOX           1

// -----------------------------------------------------------------------


class DESKGUI_API ibPGEditor;
class DESKGUI_API ibPGProperty;
class DESKGUI_API ibPGCategoryProperty;
class DESKGUI_API ibPGChoices;
class DESKGUI_API ibPropertyGridPageState;
class DESKGUI_API ibPGCell;
class DESKGUI_API ibPGCellRenderer;
class DESKGUI_API ibPGChoiceEntry;
class DESKGUI_API ibPGPropArgCls;
class DESKGUI_API ibPropertyGridInterface;
class DESKGUI_API ibPropertyGrid;
class DESKGUI_API ibPropertyGridEvent;
class ibPropertyGridManager;
class DESKGUI_API ibPGEditorDialogAdapter;
class DESKGUI_API ibPGValidationInfo;


// -----------------------------------------------------------------------

// Some miscellaneous values, types and macros.

// Used to tell ibPGProperty to use label as name as well
#define ibPG_LABEL              (ibPGProperty::sm_labelItem)

// Convert Red, Green and Blue to a single 32-bit value.
#define ibPG_COLOUR(R,G,B) ((wxUint32)((R)+((G)<<8)+((B)<<16)))


// If property is supposed to have custom-painted image, then returning
// this in OnMeasureImage() will usually be enough.
#define ibPG_DEFAULT_IMAGE_SIZE  wxDefaultSize


// This callback function is used for sorting properties.
// Call ibPropertyGrid::SetSortFunction() to set it.
// Sort function should return a value greater than 0 if position of p1 is
// after p2. So, for instance, when comparing property names, you can use
// following implementation:
//   int MyPropertySortFunction(ibPropertyGrid* propGrid,
//                              ibPGProperty* p1,
//                              ibPGProperty* p2)
//   {
//      return p1->GetBaseName().compare( p2->GetBaseName() );
//   }
typedef int (*ibPGSortCallback)(ibPropertyGrid* propGrid,
                                ibPGProperty* p1,
                                ibPGProperty* p2);


// -----------------------------------------------------------------------

// Used to indicate ibPGChoices::Add etc. that the value is actually not given
// by the caller.
constexpr int ibPG_INVALID_VALUE = std::numeric_limits<int>::max();

// -----------------------------------------------------------------------

WX_DEFINE_TYPEARRAY_WITH_DECL_PTR(ibPGProperty*, ibArrayPGProperty,
                                  wxBaseArrayPtrVoid,
                                  class DESKGUI_API);

using ibPGHashMapS2S = std::unordered_map<wxString, wxString>;

// -----------------------------------------------------------------------

enum class ibPGPropertyValuesFlags : int
{
    // Flag for ibPropertyGridInterface::SetProperty* functions,
    // ibPropertyGridInterface::HideProperty(), etc.
    // Apply changes only for the property in question.
    DontRecurse      = 0x00000000,

    // Flag for ibPropertyGridInterface::GetPropertyValues().
    // Use this flag to retain category structure; each sub-category
    // will be its own ibPGVariantList.
    KeepStructure    = 0x00000010,

    // Flag for ibPropertyGridInterface::SetProperty* functions,
    // ibPropertyGridInterface::HideProperty(), etc.
    // Apply changes recursively for the property and all its children.
    Recurse          = 0x00000020,

    // Flag for ibPropertyGridInterface::GetPropertyValues().
    // Use this flag to include property attributes as well.
    IncAttributes    = 0x00000040,

    // Used when first starting recursion.
    RecurseStarts    = 0x00000080,

    // Force value change.
    Force            = 0x00000100,

    // Only sort categories and their immediate children.
    // Sorting done by ibPG_AUTO_SORT option uses this.
    SortTopLevelOnly = 0x00000200
};

constexpr ibPGPropertyValuesFlags operator|(ibPGPropertyValuesFlags a, ibPGPropertyValuesFlags b)
{
    return static_cast<ibPGPropertyValuesFlags>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr ibPGPropertyValuesFlags operator&(ibPGPropertyValuesFlags a, ibPGPropertyValuesFlags b)
{
    return static_cast<ibPGPropertyValuesFlags>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr bool operator!(ibPGPropertyValuesFlags a)
{
    return static_cast<int>(a) == 0;
}

// -----------------------------------------------------------------------

// Miscellaneous property value format flags
enum class ibPGPropValFormatFlags : int
{
    // No flags.
    Null = 0,

    // Get/Store full value instead of displayed value.
    FullValue                   = 0x00000001,

    // Perform special action in case of unsuccessful conversion.
    ReportError                 = 0x00000002,

    PropertySpecific            = 0x00000004,

    // Get/Store editable value instead of displayed one (should only be
    // different in the case of common values)
    EditableValue               = 0x00000008,

    // Used when dealing with fragments of composite string value
    CompositeFragment           = 0x00000010,

    // Means property for which final string value is for cannot really be
    // edited.
    UneditableCompositeFragment = 0x00000020,

    // ValueToString() called from GetValueAsString()
    // (guarantees that input ibVariant value is current own value)
    ValueIsCurrent              = 0x00000040,

    // Value is being set programmatically (i.e. not by user)
    ProgrammaticValue           = 0x00000080
};

constexpr ibPGPropValFormatFlags operator&(ibPGPropValFormatFlags a, ibPGPropValFormatFlags b)
{
    return static_cast<ibPGPropValFormatFlags>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr ibPGPropValFormatFlags operator|(ibPGPropValFormatFlags a, ibPGPropValFormatFlags b)
{
    return static_cast<ibPGPropValFormatFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline ibPGPropValFormatFlags operator|=(ibPGPropValFormatFlags& a, ibPGPropValFormatFlags b)
{
    return a = a | b;
}

constexpr bool operator!(ibPGPropValFormatFlags a)
{
    return static_cast<int>(a) == 0;
}

// -----------------------------------------------------------------------

// ibPGProperty::SetValue() flags
enum class ibPGSetValueFlags : int
{
    RefreshEditor = 0x0001,
    Aggregated    = 0x0002,
    FromParent    = 0x0004,
    ByUser        = 0x0008  // Set if value changed by user
};

constexpr ibPGSetValueFlags operator|(ibPGSetValueFlags a, ibPGSetValueFlags b)
{
    return static_cast<ibPGSetValueFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline ibPGSetValueFlags operator|=(ibPGSetValueFlags& a, ibPGSetValueFlags b)
{
    return a = a | b;
}

constexpr ibPGSetValueFlags operator&(ibPGSetValueFlags a, ibPGSetValueFlags b)
{
    return static_cast<ibPGSetValueFlags>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr bool operator!(ibPGSetValueFlags a)
{
    return static_cast<int>(a) == 0;
}

// -----------------------------------------------------------------------

//
// Valid constants for ibPG_UINT_BASE attribute
// (long because of wx's wxVariant constructor, which the fork's values replaced)
constexpr long ibPG_BASE_OCT  =  8L;
constexpr long ibPG_BASE_DEC  = 10L;
constexpr long ibPG_BASE_HEX  = 16L;
constexpr long ibPG_BASE_HEXL = 32L;

//
// Valid constants for ibPG_UINT_PREFIX attribute
constexpr long ibPG_PREFIX_NONE        = 0L;
constexpr long ibPG_PREFIX_0x          = 1L;
constexpr long ibPG_PREFIX_DOLLAR_SIGN = 2L;

// -----------------------------------------------------------------------
// Editor class.

// Editor accessor (for backwards compatibility use only).
#define ibPG_EDITOR(T)          ibPGEditor_##T

// Macro for declaring editor class, with optional impexpdecl part.
#ifndef IB_PG_DECLARE_EDITOR_WITH_DECL

    #define IB_PG_DECLARE_EDITOR_WITH_DECL(EDITOR,DECL) \
    extern DECL ibPGEditor* ibPGEditor_##EDITOR; \
    extern DECL ibPGEditor* ibPGConstruct##EDITOR##EditorClass();

#endif

// Declare editor class.
#define IB_PG_DECLARE_EDITOR(EDITOR) \
extern ibPGEditor* ibPGEditor_##EDITOR; \
extern ibPGEditor* ibPGConstruct##EDITOR##EditorClass();

// Declare built-in editor classes.
IB_PG_DECLARE_EDITOR_WITH_DECL(TextCtrl,DESKGUI_API)
IB_PG_DECLARE_EDITOR_WITH_DECL(Choice,DESKGUI_API)
IB_PG_DECLARE_EDITOR_WITH_DECL(ComboBox,DESKGUI_API)
IB_PG_DECLARE_EDITOR_WITH_DECL(TextCtrlAndButton,DESKGUI_API)
#if ibPG_INCLUDE_CHECKBOX
IB_PG_DECLARE_EDITOR_WITH_DECL(CheckBox,DESKGUI_API)
#endif
IB_PG_DECLARE_EDITOR_WITH_DECL(ChoiceAndButton,DESKGUI_API)

// -----------------------------------------------------------------------

#ifndef SWIG

//
// Macro IBVARIANT allows creation of ibVariant from any type the core's value
// holds itself (a flag, a number, a text, a date), and of all types created
// using IB_PG_DECLARE_VARIANT_DATA.
template<class T>
ibVariant IBVARIANT( const T& WXUNUSED(value) )
{
    wxFAIL_MSG(wxS("Code should always call specializations of this template"));
    return ibVariant();
}

template<> inline ibVariant IBVARIANT( const int& value )
    { return ibVariant(value); }
template<> inline ibVariant IBVARIANT( const long& value )
    { return ibVariant(value); }
template<> inline ibVariant IBVARIANT( const bool& value )
    { return ibVariant(value); }
template<> inline ibVariant IBVARIANT( const double& value )
    { return ibVariant(value); }
template<> inline ibVariant IBVARIANT( const wxString& value )
    { return ibVariant(value); }
template<> inline ibVariant IBVARIANT( const wxLongLong& value )
    { return ibVariant(ibNumber(static_cast<long long>(value.GetValue()))); }
template<> inline ibVariant IBVARIANT( const wxULongLong& value )
    { return ibVariant(ibNumber(static_cast<unsigned long long>(value.GetValue()))); }
#if wxUSE_DATETIME
template<> inline ibVariant IBVARIANT( const wxDateTime& value )
    { return ibVariant(typeConv::WxDateTimeToDateTime(value)); }
#endif


//
// A VALUE OF A CLASS THE CORE'S VALUE DOES NOT HOLD ITSELF (a point, a colour,
// a list of texts): wx's DECLARE/IMPLEMENT_VARIANT_DATA, over ibVariant. The
// shape is the server's cells' (backend/propertyManager/property/variant/): a
// class derived from ibVariant, an object (TYPE_VALUE) holding the data, held
// by a value by its count, told by its class's name (s_className, what
// GetClassName answers) and read by what it is (ConvertToValue).
//   * classname##VariantData is declared here, with the value's operators:
//     what a value holds is asked for by its class wherever it is read.
//   * classname##RefFromVariant returns a reference to the data within.
//   * IBVARIANT
//
#define IB_PG_DECLARE_VARIANT_DATA(classname) \
    IB_PG_DECLARE_VARIANT_DATA_EXPORTED(classname, wxEMPTY_PARAMETER_VALUE)

#define IB_PG_DECLARE_VARIANT_DATA_EXPORTED(classname,expdecl) \
class expdecl classname##VariantData : public ibVariant \
{ \
public: \
    classname##VariantData() : ibVariant(ibValueTypes::TYPE_VALUE) { } \
    classname##VariantData( const classname &value ) : ibVariant(ibValueTypes::TYPE_VALUE), m_value(value) { } \
    classname##VariantData( const classname##VariantData& src ) : ibVariant(ibValueTypes::TYPE_VALUE), m_value(src.m_value) { } \
\
    classname &GetValue() { return m_value; } \
\
    const classname &GetValue() const { return m_value; } \
\
    virtual ibString GetClassName() const override { return s_className; } \
    virtual bool CompareValueEQ( const ibVariant& data ) const override; \
\
    static constexpr const ibChar* s_className = ibT(#classname); \
\
protected: \
    classname m_value; \
}; \
expdecl classname& operator << ( classname &object, const ibVariant &variant ); \
expdecl ibVariant& operator << ( ibVariant &variant, const classname &object ); \
expdecl const classname& classname##RefFromVariant( const ibVariant& variant ); \
expdecl classname& classname##RefFromVariant( ibVariant& variant ); \
template<> inline ibVariant IBVARIANT( const classname& value ) \
{ \
    ibVariant variant; \
    variant << value; \
    return variant; \
}


#define IB_PG_IMPLEMENT_VARIANT_DATA(classname) \
    IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED(classname, wxEMPTY_PARAMETER_VALUE)

// Add getter (i.e. classname << variant) separately to allow
// custom implementations.
#define IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_NO_EQ_NO_GETTER(classname,expdecl) \
expdecl ibVariant& operator << ( ibVariant &variant, const classname &value )\
{\
    variant = ibVariant(new classname##VariantData( value ));\
    return variant;\
} \
expdecl classname& classname##RefFromVariant( ibVariant& variant ) \
{ \
    classname##VariantData* data = nullptr; \
    variant.ConvertToValue(data); \
    wxASSERT_MSG( data != nullptr, wxS("Variant should have held a ") wxS(#classname) ); \
    return data->GetValue();\
} \
expdecl const classname& classname##RefFromVariant( const ibVariant& variant ) \
{ \
    const classname##VariantData* data = nullptr; \
    variant.ConvertToValue(data); \
    wxASSERT_MSG( data != nullptr, wxS("Variant should have held a ") wxS(#classname) ); \
    return data->GetValue();\
}

#define IB_PG_IMPLEMENT_VARIANT_DATA_GETTER(classname, expdecl) \
expdecl classname& operator << ( classname &value, const ibVariant &variant )\
{\
    const classname##VariantData* data = nullptr; \
    variant.ConvertToValue(data); \
    wxASSERT( data != nullptr );\
    \
    value = data->GetValue();\
    return value;\
}

#define IB_PG_IMPLEMENT_VARIANT_DATA_EQ(classname, expdecl) \
bool classname##VariantData::CompareValueEQ( const ibVariant& data ) const \
{\
    const classname##VariantData* other = nullptr; \
    return data.ConvertToValue(other) && other->m_value == m_value;\
}

// implements an ibVariant-derived class using for the CompareValueEQ() method
// the operator== which must have been provided by "classname"
#define IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED(classname,expdecl) \
IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_NO_EQ_NO_GETTER(classname,wxEMPTY_PARAMETER_VALUE expdecl) \
IB_PG_IMPLEMENT_VARIANT_DATA_GETTER(classname,wxEMPTY_PARAMETER_VALUE expdecl) \
IB_PG_IMPLEMENT_VARIANT_DATA_EQ(classname,wxEMPTY_PARAMETER_VALUE expdecl)

#define IB_PG_IMPLEMENT_VARIANT_DATA(classname) \
IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED(classname, wxEMPTY_PARAMETER_VALUE)

// with CompareValueEQ() implementation that always returns false
#define IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_DUMMY_EQ(classname,expdecl) \
IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_NO_EQ_NO_GETTER(classname,wxEMPTY_PARAMETER_VALUE expdecl) \
IB_PG_IMPLEMENT_VARIANT_DATA_GETTER(classname,wxEMPTY_PARAMETER_VALUE expdecl) \
\
bool classname##VariantData::CompareValueEQ( const ibVariant& WXUNUSED(data) ) const \
{\
    return false; \
}

#define IB_PG_IMPLEMENT_VARIANT_DATA_DUMMY_EQ(classname) \
IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_DUMMY_EQ(classname, wxEMPTY_PARAMETER_VALUE)

IB_PG_DECLARE_VARIANT_DATA_EXPORTED(wxPoint, DESKGUI_API)
IB_PG_DECLARE_VARIANT_DATA_EXPORTED(wxSize, DESKGUI_API)
IB_PG_DECLARE_VARIANT_DATA_EXPORTED(wxArrayInt, DESKGUI_API)
// …and what wx's own variant held itself (its "arrstring") or its objects (wxFont, wxColour)
IB_PG_DECLARE_VARIANT_DATA_EXPORTED(wxArrayString, DESKGUI_API)
IB_PG_DECLARE_VARIANT_DATA_EXPORTED(wxFont, DESKGUI_API)
IB_PG_DECLARE_VARIANT_DATA_EXPORTED(wxColour, DESKGUI_API)

// -----------------------------------------------------------------------

// THE CHILDREN'S VALUES ON THEIR WAY TO THEIR PARENT — what wx's list variant
// was here (a wxVariant of type "list": a wxVariantList of wxVariants, each
// named by its child): each value under its child's name, in order. A value
// itself, as the classes above are: it is passed where a value is, told by its
// class's name where wx told a value by its type's, and read by what it is.
class DESKGUI_API ibPGVariantList : public ibVariant
{
public:
    struct Entry
    {
        wxString  m_name;
        ibVariant m_value;
    };

    ibPGVariantList() : ibVariant(ibValueTypes::TYPE_VALUE) { }
    ibPGVariantList( const ibPGVariantList& src ) : ibVariant(ibValueTypes::TYPE_VALUE), m_entries(src.m_entries) { }

    void Append( const wxString& name, const ibVariant& value ) { m_entries.push_back({name, value}); }
    void Clear() { m_entries.clear(); }

    size_t GetCount() const { return m_entries.size(); }

    Entry& operator[]( size_t index ) { return m_entries[index]; }
    const Entry& operator[]( size_t index ) const { return m_entries[index]; }

    typedef std::vector<Entry>::iterator iterator;
    typedef std::vector<Entry>::const_iterator const_iterator;

    iterator begin() { return m_entries.begin(); }
    iterator end() { return m_entries.end(); }
    const_iterator begin() const { return m_entries.begin(); }
    const_iterator end() const { return m_entries.end(); }

    virtual ibString GetClassName() const override { return s_className; }
    virtual bool CompareValueEQ( const ibVariant& data ) const override;

    static constexpr const ibChar* s_className = ibT("list");

private:
    std::vector<Entry> m_entries;
};

#endif // !SWIG

// -----------------------------------------------------------------------

//
// Tokenizer macros.
// NOTE: I have made two versions - worse ones (performance and consistency
//   wise) use wxStringTokenizer and better ones (may have unfound bugs)
//   use custom code.
//

#include "wx/tokenzr.h"

// TOKENIZER1 can be done with wxStringTokenizer
#define IB_PG_TOKENIZER1_BEGIN(IBSTRING,DELIMITER) \
    wxStringTokenizer tkz(IBSTRING,DELIMITER,wxTOKEN_RET_EMPTY); \
    while ( tkz.HasMoreTokens() ) \
    { \
        wxString token = tkz.GetNextToken(); \
        token.Trim(true); \
        token.Trim(false);

#define IB_PG_TOKENIZER1_END() \
    }


//
// 2nd version: tokens are surrounded by DELIMITERs (for example, C-style
// strings). TOKENIZER2 must use custom code (a class) for full compliance with
// " surrounded strings with \" inside.
//
// class implementation is in propgrid.cpp
//

class DESKGUI_API ibPGStringTokenizer
{
public:
    ibPGStringTokenizer( const wxString& str, wxChar delimiter );
    ~ibPGStringTokenizer() = default;

    bool HasMoreTokens(); // not const so we can do some stuff in it
    wxString GetNextToken();

protected:
    const wxString&             m_str;
    wxString::const_iterator    m_curPos;
    wxString                    m_readyToken;
    wxUniChar                   m_delimiter;
};

#define IB_PG_TOKENIZER2_BEGIN(IBSTRING,DELIMITER) \
    ibPGStringTokenizer tkz(IBSTRING,DELIMITER); \
    while ( tkz.HasMoreTokens() ) \
    { \
        wxString token = tkz.GetNextToken();

#define IB_PG_TOKENIZER2_END() \
    }

#endif // wxUSE_PROPGRID

#endif // _DESKGUI_UNIV_PROPGRID_PROPGRIDDEFS_H_
