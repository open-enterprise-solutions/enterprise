// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, include/wx/propgrid/property.h): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/propgrid/property.h
// Purpose:     ibPGProperty and related support classes
// Author:      Jaakko Salli
// Created:     2008-08-23
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_PROPGRID_PROPERTY_H_
#define _DESKGUI_UNIV_PROPGRID_PROPERTY_H_

#include "wx/defs.h"

#include "deskgui/univ/propgrid/propgriddefs.h"
#include "wx/bitmap.h"
#include "wx/bmpbndl.h"
#include "wx/font.h"
#include "wx/validate.h"

#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------

constexpr ibPGProperty* ibNullProperty = nullptr;


// Contains information relayed to property's OnCustomPaint.
struct ibPGPaintData
{
    // ibPropertyGrid
    const ibPropertyGrid*   m_parent;

    // Normally -1, otherwise index to drop-down list item
    // that has to be drawn.
    int                     m_choiceItem;

    // Set to drawn width in OnCustomPaint (optional).
    int                     m_drawnWidth;

    // In a measure item call, set this to the height of item
    // at m_choiceItem index.
    int                     m_drawnHeight;
};


// Base class for ibPropertyGrid cell renderers.
class DESKGUI_API ibPGCellRenderer : public wxObjectRefData
{
public:

    ibPGCellRenderer() = default;
    virtual ~ibPGCellRenderer() = default;

    // Render flags
    enum
    {
        // We are painting selected item
        Selected        = 0x00010000,

        // We are painting item in choice popup
        ChoicePopup     = 0x00020000,

        // We are rendering ibComboBox control
        // (or other owner drawn control, but that is only
        // officially supported one ATM).
        Control         = 0x00040000,

        // We are painting a disable property
        Disabled        = 0x00080000,

        // We are painting selected, disabled, or similar
        // item that dictates fore- and background colours,
        // overriding any cell values.
        DontUseCellFgCol    = 0x00100000,
        DontUseCellBgCol    = 0x00200000,
        DontUseCellColours  = DontUseCellFgCol |
                              DontUseCellBgCol
    };

    // Returns true if rendered something in the foreground
    // (text or bitmap).
    virtual bool Render( wxDC& dc,
                         const wxRect& rect,
                         const ibPropertyGrid* propertyGrid,
                         ibPGProperty* property,
                         int column,
                         int item,
                         int flags ) const = 0;

    // Returns size of the image in front of the editable area.
    // If property is null, then this call is for a custom value.
    // In that case the item is index to ibPropertyGrid's custom values.
    virtual wxSize GetImageSize( const ibPGProperty* property,
                                 int column,
                                 int item ) const;

    // Paints property category selection rectangle.
    virtual void DrawCaptionSelectionRect(wxWindow *win, wxDC& dc,
                                          int x, int y, int w, int h) const;

    // Utility to draw vertically centered text.
    void DrawText( wxDC& dc,
                   const wxRect& rect,
                   int imageWidth,
                   const wxString& text ) const;

    // Utility to draw editor's value, or vertically
    // aligned text if editor is null.
    void DrawEditorValue( wxDC& dc, const wxRect& rect,
                          int xOffset, const wxString& text,
                          ibPGProperty* property,
                          const ibPGEditor* editor ) const;

    // Utility to render cell bitmap and set text
    // colour plus bg brush colour.
    // Returns image width, which, for instance,
    // can be passed to DrawText.
    int PreDrawCell( wxDC& dc,
                     const wxRect& rect,
                     const ibPropertyGrid* propGrid,
                     const ibPGCell& cell,
                     int flags ) const;

    // Utility to be called after drawing is done, to revert
    // whatever changes PreDrawCell() did.
    // Flags are the same as those passed to PreDrawCell().
    void PostDrawCell( wxDC& dc,
                       const ibPropertyGrid* propGrid,
                       const ibPGCell& cell,
                       int flags ) const;
};


// Default cell renderer, that can handles the common
// scenarios.
class DESKGUI_API ibPGDefaultRenderer : public ibPGCellRenderer
{
public:
    virtual bool Render( wxDC& dc,
                         const wxRect& rect,
                         const ibPropertyGrid* propertyGrid,
                         ibPGProperty* property,
                         int column,
                         int item,
                         int flags ) const override;

    virtual wxSize GetImageSize( const ibPGProperty* property,
                                 int column,
                                 int item ) const override;

protected:
};


class DESKGUI_API ibPGCellData : public wxObjectRefData
{
    friend class ibPGCell;
public:
    ibPGCellData();

    void SetText( const wxString& text )
    {
        m_text = text;
        m_hasValidText = true;
    }
    void SetBitmap( const wxBitmapBundle& bitmap ) { m_bitmapBundle = bitmap; }
    void SetFgCol( const wxColour& col ) { m_fgCol = col; }
    void SetBgCol( const wxColour& col ) { m_bgCol = col; }
    void SetFont( const wxFont& font ) { m_font = font; }

protected:
    virtual ~ibPGCellData() = default;

    wxString    m_text;
    wxBitmapBundle m_bitmapBundle;
    wxColour    m_fgCol;
    wxColour    m_bgCol;
    wxFont      m_font;

    // True if m_text is valid and specified
    bool        m_hasValidText;
};


// Base class for ibPropertyGrid cell information.
class DESKGUI_API ibPGCell : public wxObject
{
public:
    ibPGCell() = default;
    ibPGCell(const ibPGCell& other)
        : wxObject(other)
    {
    }

    ibPGCell( const wxString& text,
              const wxBitmapBundle& bitmap = wxBitmapBundle(),
              const wxColour& fgCol = wxNullColour,
              const wxColour& bgCol = wxNullColour );

    virtual ~ibPGCell() = default;

    ibPGCellData* GetData()
    {
        return (ibPGCellData*) m_refData;
    }

    const ibPGCellData* GetData() const
    {
        return (const ibPGCellData*) m_refData;
    }

    bool HasText() const
    {
        return (m_refData && GetData()->m_hasValidText);
    }

    // Sets empty but valid data to this cell object.
    void SetEmptyData();

    // Merges valid data from srcCell into this.
    void MergeFrom( const ibPGCell& srcCell );

    void SetText( const wxString& text );
    void SetBitmap( const wxBitmapBundle& bitmap );
    void SetFgCol( const wxColour& col );

    // Sets font of the cell.
    // Because ibPropertyGrid does not support rows of
    // different height, it makes little sense to change
    // size of the font. Therefore it is recommended
    // to use return value of ibPropertyGrid::GetFont()
    // or ibPropertyGrid::GetCaptionFont() as a basis
    // for the font that, after modifications, is passed
    // to this member function.
    void SetFont( const wxFont& font );

    void SetBgCol( const wxColour& col );

    const wxString& GetText() const { return GetData()->m_text; }
    const wxBitmapBundle& GetBitmap() const { return GetData()->m_bitmapBundle; }
    const wxColour& GetFgCol() const { return GetData()->m_fgCol; }

    // Returns font of the cell. If no specific font is set for this
    // cell, then the font will be invalid.
    const wxFont& GetFont() const { return GetData()->m_font; }

    const wxColour& GetBgCol() const { return GetData()->m_bgCol; }

    ibPGCell& operator=( const ibPGCell& other )
    {
        if ( this != &other )
        {
            Ref(other);
        }
        return *this;
    }

    // Used mostly internally to figure out if this cell is supposed
    // to have default values when attached to a grid.
    bool IsInvalid() const
    {
        return ( m_refData == nullptr );
    }

private:
    virtual wxObjectRefData *CreateRefData() const override
        { return new ibPGCellData(); }

    wxNODISCARD virtual wxObjectRefData *CloneRefData(const wxObjectRefData *data) const override;
};

// -----------------------------------------------------------------------

// ibPGAttributeStorage is somewhat optimized storage for
// key=variant pairs (ie. a map).
class DESKGUI_API ibPGAttributeStorage
{
public:
    // a value counts its own holders: the map is copied as it is
    ibPGAttributeStorage() = default;

    void Set( const wxString& name, const ibVariant& value );
    unsigned int GetCount() const { return (unsigned int) m_map.size(); }
    ibVariant FindValue(const wxString& name) const;

    typedef std::unordered_map<wxString, ibVariant>::const_iterator const_iterator;
    const_iterator StartIteration() const;
    // the attribute's name beside its value: wx's value carried the name in itself
    bool GetNext(const_iterator& it, wxString& name, ibVariant& variant) const;

protected:
    std::unordered_map<wxString, ibVariant> m_map;
};

enum class ibPGFlags : int
{
    // No flags.
    Null = 0,

    // Indicates bold font.
    Modified = 0x0001,

    // Disables ('greyed' text and editor does not activate) property.
    Disabled = 0x0002,

    // Hider button will hide this property.
    Hidden = 0x0004,

    // This property has custom paint image just in front of its value.
    // If property only draws custom images into a popup list, then this
    // flag should not be set.
    CustomImage = 0x0008,

    // Do not create text based editor for this property (but button-triggered
    // dialog and choice are ok).
    NoEditor = 0x0010,

    // Property is collapsed, ie. it's children are hidden.
    Collapsed = 0x0020,

    // If property is selected, then indicates that validation failed for pending
    // value.
    // If property is not selected, that indicates that the actual property
    // value has failed validation (NB: this behaviour is not currently supported,
    // but may be used in future).
    InvalidValue = 0x0040,

    // 0x0080,

    // Switched via SetWasModified(). Temporary flag - only used when
    // setting/changing property value.
    WasModified = 0x0200,

    // If set, then child properties (if any) are private, and should be
    // "invisible" to the application.
    Aggregate = 0x0400,

    // If set, then child properties (if any) are copies and should not
    // be deleted in dtor.
    ChildrenAreCopies = 0x0800,

    // Classifies this item as a non-category.
    //    Used for faster item type identification.
    Property = 0x1000,

    // Classifies this item as a category.
    // Used for faster item type identification.
    Category = 0x2000,

    // Classifies this item as a property that has children,
    //but is not aggregate (i.e. children are not private).
    MiscParent = 0x4000,

    // Property is read-only. Editor is still created for ibTextCtrl-based
    // property editors. For others, editor is not usually created because
    // they do implement wxTE_READONLY style or equivalent.
    ReadOnly = 0x8000,

    //
    // NB: FLAGS ABOVE 0x8000 CANNOT BE USED WITH PROPERTY ITERATORS
    //

    // Property's value is composed from values of child properties.
    // This flag cannot be used with property iterators.
    ComposedValue = 0x00010000,

    // Common value of property is selectable in editor.
    // This flag cannot be used with property iterators.
    UsesCommonValue = 0x00020000,

    // Property can be set to unspecified value via editor.
    // Currently, this applxPGies to following properties:
    // - ibIntProperty, ibUIntProperty, ibFloatProperty, ibEditEnumProperty:
    // Clear the text field
    // This flag cannot be used with property iterators.
    // See ibPGProperty::SetAutoUnspecified().
    AutoUnspecified = 0x00040000,

    // Indicates that the property is being deleted and should be ignored.
    BeingDeleted = 0x00080000,

    // If set, full path is shown in ibFileProperty.
    ShowFullFileName = 0x00100000,

    // For internal use only.
    Reserved_1 = 0x10000000,

    // For internal use only.
    Reserved_2 = 0x20000000,

    // For internal use only.
    Reserved_3 = 0x40000000,

    // Topmost flag.
    Max = ShowFullFileName,

    // Property with children must have one of these set, otherwise iterators
    // will not work correctly.
    // Code should automatically take care of this, however.
    ParentalFlags = Aggregate | Category | MiscParent,

    // Combination of flags that can be stored by GetFlagsAsString
    StringStoredFlags = Disabled | Hidden | NoEditor | Collapsed
};

constexpr ibPGFlags operator|(ibPGFlags a, ibPGFlags b)
{
    return static_cast<ibPGFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline ibPGFlags operator|=(ibPGFlags & a, ibPGFlags b)
{
    return a = a | b;
}

constexpr ibPGFlags operator&(ibPGFlags a, ibPGFlags b)
{
    return static_cast<ibPGFlags>(static_cast<int>(a) & static_cast<int>(b));
}

inline ibPGFlags operator&=(ibPGFlags & a, ibPGFlags b)
{
    return a = a & b;
}

constexpr ibPGFlags operator^(ibPGFlags a, ibPGFlags b)
{
    return static_cast<ibPGFlags>(static_cast<int>(a) ^ static_cast<int>(b));
}

constexpr ibPGFlags operator~(ibPGFlags a)
{
    return static_cast<ibPGFlags>(~static_cast<int>(a));
}

constexpr bool operator!(ibPGFlags a)
{
    return static_cast<int>(a) == 0;
}

// We need these operators with int arguments for interoperability
// with ibPG_ITERATOR_FLAGS as plain enumeration).
// =====
constexpr int operator<<(ibPGFlags a, int n)
{
    return static_cast<int>(a) << n;
}

constexpr int operator|(ibPGFlags a, int b)
{
    return static_cast<int>(a) | b;
}

constexpr int operator|(int a, ibPGFlags b)
{
    return a | static_cast<int>(b);
}

constexpr int operator&(int a, ibPGFlags b)
{
    return a & static_cast<int>(b);
}
// =====

// -----------------------------------------------------------------------

// ibPGProperty::SetAttribute() and
// ibPropertyGridInterface::SetPropertyAttribute() accept one of these as
// attribute name argument.
// You can use strings instead of constants. However, some of these
// constants are redefined to use cached strings which may reduce
// your binary size by some amount.

// Set default value for property.
#define ibPG_ATTR_DEFAULT_VALUE           wxS("DefaultValue")

// Universal, int or double. Minimum value for numeric properties.
#define ibPG_ATTR_MIN                     wxS("Min")

// Universal, int or double. Maximum value for numeric properties.
#define ibPG_ATTR_MAX                     wxS("Max")

// Universal, string. When set, will be shown as text after the displayed
// text value. Alternatively, if third column is enabled, text will be shown
// there (for any type of property).
#define ibPG_ATTR_UNITS                     wxS("Units")

// When set, will be shown as 'greyed' text in property's value cell when
// the actual displayed value is blank.
#define ibPG_ATTR_HINT                      wxS("Hint")

// Universal, wxArrayString. Set to enable auto-completion in any
// ibTextCtrl-based property editor.
#define ibPG_ATTR_AUTOCOMPLETE              wxS("AutoComplete")

// ibBoolProperty and ibFlagsProperty specific. Value type is bool.
// Default value is False.
// When set to True, bool property will use check box instead of a
// combo box as its editor control. If you set this attribute
// for a ibFlagsProperty, it is automatically applied to child
// bool properties.
#define ibPG_BOOL_USE_CHECKBOX              wxS("UseCheckbox")

// ibBoolProperty and ibFlagsProperty specific. Value type is bool.
// Default value is False.
// Set to True for the bool property to cycle value on double click
// (instead of showing the popup listbox). If you set this attribute
// for a ibFlagsProperty, it is automatically applied to child
// bool properties.
#define ibPG_BOOL_USE_DOUBLE_CLICK_CYCLING  wxS("UseDClickCycling")

// ibFloatProperty (and similar) specific, int, default -1.
// Sets the (max) precision used when floating point value is rendered as
// text. The default -1 means infinite precision.
#define ibPG_FLOAT_PRECISION                wxS("Precision")

// The text will be echoed as asterisks (wxTE_PASSWORD will be passed
// to textctrl etc.).
#define ibPG_STRING_PASSWORD                wxS("Password")

// Define base used by a ibUIntProperty. Valid constants are
// ibPG_BASE_OCT, ibPG_BASE_DEC, ibPG_BASE_HEX and ibPG_BASE_HEXL
// (lowercase characters).
#define ibPG_UINT_BASE                      wxS("Base")

// Define prefix rendered to ibUIntProperty. Accepted constants
// ibPG_PREFIX_NONE, ibPG_PREFIX_0x, and ibPG_PREFIX_DOLLAR_SIGN.
// Note:
// Only ibPG_PREFIX_NONE works with Decimal and Octal numbers.
#define ibPG_UINT_PREFIX                    wxS("Prefix")

// Specific to ibEditorDialogProperty and derivatives, wxString, default is empty.
// Sets a specific title for the editor dialog.
#define ibPG_DIALOG_TITLE                   wxS("DialogTitle")

// ibFileProperty/ibImageFileProperty specific, wxChar*, default is
// detected/varies.
// Sets the wildcard used in the triggered wxFileDialog. Format is the same.
#define ibPG_FILE_WILDCARD                  wxS("Wildcard")

// ibFileProperty/ibImageFileProperty specific, int, default 1.
// When 0, only the file name is shown (i.e. drive and directory are hidden).
#define ibPG_FILE_SHOW_FULL_PATH            wxS("ShowFullPath")

// Specific to ibFileProperty and derived properties, wxString, default empty.
// If set, then the filename is shown relative to the given path string.
#define ibPG_FILE_SHOW_RELATIVE_PATH        wxS("ShowRelativePath")

// Specific to ibFileProperty and derived properties, wxString,
// default is empty.
// Sets the initial path of where to look for files.
#define ibPG_FILE_INITIAL_PATH              wxS("InitialPath")

// Specific to ibFileProperty and derivatives, long, default is 0.
// Sets a specific wxFileDialog style for the file dialog, e.g. ::wxFD_SAVE.
#define ibPG_FILE_DIALOG_STYLE              wxS("DialogStyle")

// ibArrayStringProperty's string delimiter character. If this is
// a quotation mark or hyphen, then strings will be quoted instead
// (with given character).
// Default delimiter is quotation mark.
#define ibPG_ARRAY_DELIMITER                wxS("Delimiter")

// Sets displayed date format for ibDateProperty.
#define ibPG_DATE_FORMAT                    wxS("DateFormat")

// Sets ibDatePickerCtrl window style used with ibDateProperty. Default
// is wxDP_DEFAULT | wxDP_SHOWCENTURY. Using wxDP_ALLOWNONE will enable
// better unspecified value support in the editor
#define ibPG_DATE_PICKER_STYLE              wxS("PickerStyle")

#if wxUSE_SPINBTN
// SpinCtrl editor, int or double. How much number changes when button is
// pressed (or up/down on keyboard).
#define ibPG_ATTR_SPINCTRL_STEP             wxS("Step")

// SpinCtrl editor, bool. If true, value wraps at Min/Max.
#define ibPG_ATTR_SPINCTRL_WRAP             wxS("Wrap")

// SpinCtrl editor, bool. If true, moving mouse when one of the spin
//    buttons is depressed rapidly changing "spin" value.
#define ibPG_ATTR_SPINCTRL_MOTION           wxS("MotionSpin")
#endif  // wxUSE_SPINBTN

// ibMultiChoiceProperty, int.
// If 0, no user strings allowed. If 1, user strings appear before list
// strings. If 2, user strings appear after list string.
#define ibPG_ATTR_MULTICHOICE_USERSTRINGMODE    wxS("UserStringMode")

// ibColourProperty and its kind, int, default 1.
// Setting this attribute to 0 hides custom colour from property's list of
// choices.
#define ibPG_COLOUR_ALLOW_CUSTOM            wxS("AllowCustom")

// ibColourProperty and its kind: Set to True in order to support editing
// alpha colour component.
#define ibPG_COLOUR_HAS_ALPHA               wxS("HasAlpha")

// -----------------------------------------------------------------------

// Data of a single ibPGChoices choice.
class DESKGUI_API ibPGChoiceEntry : public ibPGCell
{
public:
    ibPGChoiceEntry();
    ibPGChoiceEntry(const ibPGChoiceEntry& other);
    ibPGChoiceEntry(const wxString& label, int value = ibPG_INVALID_VALUE);

    virtual ~ibPGChoiceEntry() = default;

    void SetValue( int value ) { m_value = value; }
    int GetValue() const { return m_value; }

    ibPGChoiceEntry& operator=(const ibPGChoiceEntry& other);

protected:
    int m_value;
};


typedef void* ibPGChoicesId;

class DESKGUI_API ibPGChoicesData : public wxObjectRefData
{
    friend class ibPGChoices;
public:
    // Constructor sets m_refCount to 1.
    ibPGChoicesData() = default;

    void CopyDataFrom( ibPGChoicesData* data );

    ibPGChoiceEntry& Insert( int index, const ibPGChoiceEntry& item );

    // Delete all entries
    void Clear();

    unsigned int GetCount() const
    {
        return (unsigned int) m_items.size();
    }

    const ibPGChoiceEntry& Item( unsigned int i ) const
    {
        wxASSERT_MSG( i < GetCount(), wxS("invalid index") );
        return m_items[i];
    }

    ibPGChoiceEntry& Item( unsigned int i )
    {
        wxASSERT_MSG( i < GetCount(), wxS("invalid index") );
        return m_items[i];
    }

private:
    std::vector<ibPGChoiceEntry> m_items;

protected:
    virtual ~ibPGChoicesData();
};

constexpr ibPGChoicesData* ibPGChoicesEmptyData = nullptr;


// Helper class for managing choices of ibPropertyGrid properties.
// Each entry can have label, value, bitmap, text colour, and background
// colour.
// ibPGChoices uses reference counting, similar to other wxWidgets classes.
// This means that assignment operator and copy constructor only copy the
// reference and not the actual data. Use Copy() member function to create
// a real copy.
// If you do not specify value for entry, index is used.
class DESKGUI_API ibPGChoices
{
public:
    typedef long ValArrItem;

    // Default constructor.
    ibPGChoices()
    {
        Init();
    }

    // Copy constructor, uses reference counting. To create a real copy,
    // use Copy() member function instead.
    ibPGChoices( const ibPGChoices& a )
    {
        if ( a.m_data != ibPGChoicesEmptyData )
        {
            m_data = a.m_data;
            m_data->IncRef();
        }
        else
        {
            Init();
        }
    }

    // Constructor.
    // count - Number of labels.
    // labels - Labels themselves.
    // values - Values for choices. If nullptr, indexes are used.
    ibPGChoices(size_t count, const wxString* labels, const long* values = nullptr)
    {
        Init();
        Add(count, labels, values);
    }

    // Constructor overload taking wxChar strings, provided mostly for
    // compatibility.
    // labels - Labels for choices, nullptr-terminated.
    // values - Values for choices. If nullptr, indexes are used.
    ibPGChoices( const wxChar* const* labels, const long* values = nullptr )
    {
        Init();
        Add(labels,values);
    }

    // Constructor.
    // labels - Labels for choices.
    // values - Values for choices. If empty, indexes are used.
    ibPGChoices( const wxArrayString& labels,
                 const wxArrayInt& values = wxArrayInt() )
    {
        Init();
        Add(labels,values);
    }

    // Simple interface constructor.
    ibPGChoices( ibPGChoicesData* data )
        : m_data(data)
    {
        wxCHECK_RET(data, "Data pointer cannot be null");
        m_data->IncRef();
    }

    // Destructor.
    ~ibPGChoices()
    {
        Free();
    }

    // Adds to current.
    // If did not have own copies, creates them now. If was empty, identical
    // to set except that creates copies.
    void Add(size_t count, const wxString* labels, const long* values = nullptr);

    // Overload taking wxChar strings, provided mostly for compatibility.
    // labels - Labels for added choices, nullptr-terminated.
    // values - Values for added choices. If empty, relevant entry indexes are used.
    void Add( const wxChar* const* labels, const ValArrItem* values = nullptr );

    // Version that works with wxArrayString and wxArrayInt.
    void Add( const wxArrayString& arr, const wxArrayInt& arrint = wxArrayInt() );

    // Adds a single choice.
    // label - Label for added choice.
    // value - Value for added choice. If unspecified, index is used.
    ibPGChoiceEntry& Add( const wxString& label,
                          int value = ibPG_INVALID_VALUE );

    // Adds a single item, with bitmap.
    ibPGChoiceEntry& Add( const wxString& label,
                          const wxBitmapBundle& bitmap,
                          int value = ibPG_INVALID_VALUE );

    // Adds a single item with full entry information.
    ibPGChoiceEntry& Add( const ibPGChoiceEntry& entry )
    {
        return Insert(entry, -1);
    }

    // Adds a single item, sorted.
    ibPGChoiceEntry& AddAsSorted( const wxString& label,
                                  int value = ibPG_INVALID_VALUE );

    // Assigns choices data, using reference counting. To create a real copy,
    // use Copy() member function instead.
    void Assign( const ibPGChoices& a )
    {
        AssignData(a.m_data);
    }

    // Assigns data from another set of choices.
    void AssignData( ibPGChoicesData* data );

    // Delete all choices.
    void Clear();

    // Returns a real copy of the choices.
    ibPGChoices Copy() const
    {
        ibPGChoices dst;
        dst.EnsureData();
        dst.m_data->CopyDataFrom(m_data);
        return dst;
    }

    void EnsureData()
    {
        if ( m_data == ibPGChoicesEmptyData )
            m_data = new ibPGChoicesData();
    }

    // Gets a unsigned number identifying this list.
    ibPGChoicesId GetId() const { return (ibPGChoicesId) m_data; }

    // Returns label of item.
    const wxString& GetLabel( unsigned int ind ) const
    {
        return Item(ind).GetText();
    }

    // Returns number of items.
    unsigned int GetCount () const
    {
        return m_data ? m_data->GetCount() : 0;
    }

    // Returns value of item.
    int GetValue( unsigned int ind ) const { return Item(ind).GetValue(); }

    // Returns array of values matching the given strings. Unmatching strings
    // result in ibPG_INVALID_VALUE entry in array.
    wxArrayInt GetValuesForStrings( const wxArrayString& strings ) const;

    // Returns array of indices matching given strings. Unmatching strings
    // are added to 'unmatched', if not null.
    wxArrayInt GetIndicesForStrings( const wxArrayString& strings,
                                     wxArrayString* unmatched = nullptr ) const;

    // Returns index of item with given label.
    int Index( const wxString& str ) const;
    // Returns index of item with given value.
    int Index( int val ) const;

    // Inserts a single item.
    ibPGChoiceEntry& Insert( const wxString& label,
                             int index,
                             int value = ibPG_INVALID_VALUE );

    // Inserts a single item with full entry information.
    ibPGChoiceEntry& Insert( const ibPGChoiceEntry& entry, int index );

    // Returns false if this is a constant empty set of choices,
    // which should not be modified.
    bool IsOk() const
    {
        return ( m_data != ibPGChoicesEmptyData );
    }

    const ibPGChoiceEntry& Item( unsigned int i ) const
    {
        wxASSERT( IsOk() );
        return m_data->Item(i);
    }

    // Returns item at given index.
    ibPGChoiceEntry& Item( unsigned int i )
    {
        wxASSERT( IsOk() );
        return m_data->Item(i);
    }

    // Removes count items starting at position nIndex.
    void RemoveAt(size_t nIndex, size_t count = 1);

    // Sets contents from lists of strings and values.
    // Does not create copies for itself.
    // TODO: Deprecate.
    void Set(size_t count, const wxString* labels, const long* values = nullptr)
    {
        Free();
        Add(count, labels, values);
    }

    void Set( const wxChar* const* labels, const long* values = nullptr )
    {
        Free();
        Add(labels,values);
    }

    // Sets contents from lists of strings and values.
    // Version that works with wxArrayString and wxArrayInt.
    void Set( const wxArrayString& labels,
              const wxArrayInt& values = wxArrayInt() )
    {
        Free();
        Add(labels,values);
    }

    // Creates exclusive copy of current choices
    void AllocExclusive();

    // Returns data, increases refcount.
    ibPGChoicesData* GetData()
    {
        wxASSERT( m_data->GetRefCount() != -1 );
        m_data->IncRef();
        return m_data;
    }

    // Returns plain data ptr - no refcounting stuff is done.
    ibPGChoicesData* GetDataPtr() const { return m_data; }

    // Changes ownership of data to you.
    ibPGChoicesData* ExtractData()
    {
        ibPGChoicesData* data = m_data;
        m_data = ibPGChoicesEmptyData;
        return data;
    }

    // Returns array of choice labels.
    wxArrayString GetLabels() const;

    void operator= (const ibPGChoices& a)
    {
        if (this != &a)
            AssignData(a.m_data);
    }

    ibPGChoiceEntry& operator[](unsigned int i)
    {
        return Item(i);
    }

    const ibPGChoiceEntry& operator[](unsigned int i) const
    {
        return Item(i);
    }

protected:
    ibPGChoicesData*    m_data;

    void Init();
    void Free();
};

// -----------------------------------------------------------------------

// ibPGProperty is base class for all ibPropertyGrid properties.
class DESKGUI_API ibPGProperty : public wxObject
{
    friend class ibPropertyGrid;
    friend class ibPropertyGridInterface;
    friend class ibPropertyGridPageState;
    friend class ibPropertyGridPopulator;

    wxDECLARE_ABSTRACT_CLASS(ibPGProperty);
public:

    // Virtual destructor.
    // It is customary for derived properties to implement this.
    virtual ~ibPGProperty();

    // This virtual function is called after m_value has been set.
    // Remarks:
    // - If m_value was set to Null variant (i.e. unspecified value),
    //   OnSetValue() will not be called.
    // - m_value may be of any variant type. Typically properties internally
    //   support only one variant type, and as such OnSetValue() provides a
    //   good opportunity to convert
    //   supported values into internal type.
    // - Default implementation does nothing.
    virtual void OnSetValue();

    // Override this to return something else than m_value as the value.
    virtual ibVariant DoGetValue() const { return m_value; }

    // Implement this function in derived class to check the value.
    // Return true if it is ok. Returning false prevents property change
    // events from occurring.
    // Remark: Default implementation always returns true.
    virtual bool ValidateValue( ibVariant& value,
                                ibPGValidationInfo& validationInfo ) const;

    // Converts text into ibVariant value appropriate for this property.
    // Parameters:
    // variant - On function entry this is the old value (should not be
    //   null ibVariant in normal cases). Translated value must be assigned
    //   back to it.
    // text - Text to be translated into variant.
    // flags - If ibPGPropValFormatFlags::FullValue is set, returns complete, storable value instead
    //   of displayable one (they may be different).
    //   If ibPGPropValFormatFlags::CompositeFragment is set, text is interpreted as a part of
    //   composite property string value (as generated by ValueToString()
    //   called with this same flag).
    // Returns true if resulting ibVariant value was different.
    // Default implementation converts semicolon delimited tokens into
    // child values. Only works for properties with children.
    // You might want to take into account that m_value is Null variant
    // if property value is unspecified (which is usually only case if
    // you explicitly enabled that sort behaviour).
    virtual bool StringToValue(ibVariant& variant, const wxString& text,
                               ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const;

    // Converts integer (possibly a choice selection) into ibVariant value
    // appropriate for this property.
    // Parameters:
    // variant - On function entry this is the old value (should not be null ibVariant
    //   in normal cases). Translated value must be assigned back to it.
    // number - Integer to be translated into variant.
    // flags - If ibPGPropValFormatFlags::FullValue is set, returns complete, storable value
    //   instead of displayable one.
    // Returns true if resulting ibVariant value was different.
    // Remarks
    // - If property is not supposed to use choice or spinctrl or other editor
    //   with int-based value, it is not necessary to implement this method.
    // - Default implementation simply assign given int to m_value.
    // - If property uses choice control, and displays a dialog on some choice
    //   items, then it is preferred to display that dialog in IntToValue
    //   instead of OnEvent.
    // - You might want to take into account that m_value is Null variant
    //   if property value is unspecified (which is usually only case if
    //   you explicitly enabled that sort behaviour).
    virtual bool IntToValue(ibVariant& value, int number,
                            ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const;

    // Converts property value into a text representation.
    // Parameters:
    // value - Value to be converted.
    // flags - If ibPGPropValFormatFlags::Null (default value), then displayed string is returned.
    //   If ibPGPropValFormatFlags::FullValue is set, returns complete, storable string value
    //   instead of displayable. If ibPGPropValFormatFlags::EditableValue is set, returns
    //   string value that must be editable in textctrl. If
    //   ibPGPropValFormatFlags::CompositeFragment is set, returns text that is appropriate to
    //   display as a part of string property's composite text
    //   representation.
    // Default implementation calls GenerateComposedValue().
    virtual wxString ValueToString(ibVariant& value, ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const;

    // Converts string to a value, and if successful, calls SetValue() on it.
    // Default behaviour is to do nothing.
    // Returns true if value was changed.
    bool SetValueFromString(const wxString& text, ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::ProgrammaticValue);

    // Converts integer to a value, and if successful, calls SetValue() on it.
    // Default behaviour is to do nothing.
    // Parameters:
    //   value - Int to get the value from.
    //   flags - If has ibPGPropValFormatFlags::FullValue, then the value given is a actual value
    //     and not an index.
    // Returns true if value was changed.
    bool SetValueFromInt(long value, ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null);

    // Returns size of the custom painted image in front of property.
    // This method must be overridden to return non-default value if
    // OnCustomPaint is to be called.
    // item - Normally -1, but can be an index to the property's list of items.
    // Remarks:
    // - Default behaviour is to return wxSize(0,0), which means no image.
    // - Default image width or height is indicated with dimension -1.
    // - You can also return ibPG_DEFAULT_IMAGE_SIZE, i.e. wxDefaultSize.
    virtual wxSize OnMeasureImage( int item = -1 ) const;

    // Events received by editor widgets are processed here.
    // Note that editor class usually processes most events. Some, such as
    // button press events of TextCtrlAndButton class, can be handled here.
    // Also, if custom handling for regular events is desired, then that can
    // also be done (for example, ibSystemColourProperty custom handles
    // wxEVT_CHOICE to display colour picker dialog when
    // 'custom' selection is made).
    // If the event causes value to be changed, SetValueInEvent()
    // should be called to set the new value.
    // event - Associated wxEvent.
    // Should return true if any changes in value should be reported.
    // If property uses choice control, and displays a dialog on some choice
    // items, then it is preferred to display that dialog in IntToValue
    // instead of OnEvent.
    virtual bool OnEvent( ibPropertyGrid* propgrid,
                          wxWindow* wnd_primary,
                          wxEvent& event );

    // Called after value of a child property has been altered. Must return
    // new value of the whole property (after any alterations warranted by
    // child's new value).
    // Note that this function is usually called at the time that value of
    // this property, or given child property, is still pending for change,
    // and as such, result of GetValue() or m_value should not be relied
    // on.
    // Parameters:
    //   thisValue - Value of this property. Changed value should be returned
    //     (in previous versions of ibPropertyGrid it was only necessary to
    //     write value back to this argument).
    //   childIndex - Index of child changed (you can use Item(childIndex)
    //     to get child property).
    //   childValue - (Pending) value of the child property.
    // Returns modified value of the whole property.
    virtual ibVariant ChildChanged( ibVariant& thisValue,
                                    int childIndex,
                                    ibVariant& childValue ) const;

    // Returns pointer to an instance of used editor.
    virtual const ibPGEditor* DoGetEditorClass() const;

    // Returns pointer to the wxValidator that should be used
    // with the editor of this property (nullptr for no validator).
    // Setting validator explicitly via SetPropertyValidator
    // will override this.
    // You can get common filename validator by returning
    // ibFileProperty::GetClassValidator(). ibDirProperty,
    // for example, uses it.
    virtual wxValidator* DoGetValidator () const;

    // Override to paint an image in front of the property value text or
    // drop-down list item (but only if ibPGProperty::OnMeasureImage is
    // overridden as well).
    // If property's OnMeasureImage() returns size that has height != 0 but
    // less than row height ( < 0 has special meanings), ibPropertyGrid calls
    // this method to draw a custom image in a limited area in front of the
    // editor control or value text/graphics, and if control has drop-down
    // list, then the image is drawn there as well (even in the case
    // OnMeasureImage() returned higher height than row height).
    // NOTE: Following applies when OnMeasureImage() returns a "flexible"
    // height ( using ibPG_FLEXIBLE_SIZE(W,H) macro), which implies variable
    // height items: If rect.x is < 0, then this is a measure item call, which
    // means that dc is invalid and only thing that should be done is to set
    // paintdata.m_drawnHeight to the height of the image of item at index
    // paintdata.m_choiceItem. This call may be done even as often as once
    // every drop-down popup show.
    // Parameters:
    //   dc - wxDC to paint on.
    //   rect - Box reserved for custom graphics. Includes surrounding rectangle,
    //     if any. If x is < 0, then this is a measure item call (see above).
    //   paintdata - ibPGPaintData structure with much useful data.
    // Remarks:
    // - You can actually exceed rect width, but if you do so then
    //   paintdata.m_drawnWidth must be set to the full width drawn in
    //   pixels.
    // - Due to technical reasons, rect's height will be default even if
    //   custom height was reported during measure call.
    // - Brush is guaranteed to be default background colour. It has been
    //   already used to clear the background of area being painted. It
    //   can be modified.
    // - Pen is guaranteed to be 1-wide 'black' (or whatever is the proper
    //   colour) pen for drawing framing rectangle. It can be changed as
    //   well.
    // See ValueToString()
    virtual void OnCustomPaint( wxDC& dc,
                                const wxRect& rect,
                                ibPGPaintData& paintdata );

    // Returns used ibPGCellRenderer instance for given property column
    // (label=0, value=1).
    // Default implementation returns editor's renderer for all columns.
    virtual ibPGCellRenderer* GetCellRenderer( int column ) const;

    // Returns which choice is currently selected. Only applies to properties
    // which have choices.
    // Needs to be reimplemented in derived class if property value does not
    // map directly to a choice. Integer as index, bool, and string usually do.
    virtual int GetChoiceSelection() const;

    // Refresh values of child properties.
    // Automatically called after value is set.
    virtual void RefreshChildren();

    // Reimplement this member function to add special handling for
    // attributes of this property.
    // Return false to have the attribute automatically stored in
    // m_attributes. Default implementation simply does that and
    // nothing else.
    // To actually set property attribute values from the
    // application, use ibPGProperty::SetAttribute() instead.
    virtual bool DoSetAttribute( const wxString& name, ibVariant& value );

    // Returns value of an attribute.
    // Override if custom handling of attributes is needed.
    // Default implementation simply return nullptr variant.
    virtual ibVariant DoGetAttribute( const wxString& name ) const;

    // Returns instance of a new ibPGEditorDialogAdapter instance, which is
    // used when user presses the (optional) button next to the editor control;
    // Default implementation returns nullptr (ie. no action is generated when
    // button is pressed).
    virtual ibPGEditorDialogAdapter* GetEditorDialog() const;

    // Called whenever validation has failed with given pending value.
    // If you implement this in your custom property class, please
    // remember to call the baser implementation as well, since they
    // may use it to revert property into pre-change state.
    virtual void OnValidationFailure( ibVariant& pendingValue );

    // Append a new choice to property's list of choices.
    int AddChoice( const wxString& label, int value = ibPG_INVALID_VALUE )
    {
        return InsertChoice(label, wxNOT_FOUND, value);
    }

    // Returns true if children of this property are component values (for
    // instance, points size, face name, and is_underlined are component
    // values of a font).
    bool AreChildrenComponents() const
    {
        return !!(m_flags & (ibPGFlags::ComposedValue|ibPGFlags::Aggregate));
    }

    // Deletes children of the property.
    void DeleteChildren();

    // Removes entry from property's ibPGChoices and editor control (if it is
    // active).
    // If selected item is deleted, then the value is set to unspecified.
    void DeleteChoice( int index );

    // Enables or disables the property. Disabled property usually appears
    // as having grey text.
    // See ibPropertyGridInterface::EnableProperty()
    void Enable( bool enable = true );

    // Call to enable or disable usage of common value (integer value that can
    // be selected for properties instead of their normal values) for this
    // property.
    // Common values are disabled by the default for all properties.
    void EnableCommonValue( bool enable = true )
    {
        ChangeFlag(ibPGFlags::UsesCommonValue, enable);
    }

    // Composes text from values of child properties.
    wxString GenerateComposedValue() const
    {
        wxString s;
        DoGenerateComposedValue(s);
        return s;
    }

    // Returns property's label.
    const wxString& GetLabel() const { return m_label; }

    // Returns property's name with all (non-category, non-root) parents.
    wxString GetName() const;

    // Returns property's base name (i.e. parent's name is not added
    // in any case).
    const wxString& GetBaseName() const { return m_name; }

    // Returns read-only reference to property's list of choices.
    const ibPGChoices& GetChoices() const
    {
        return m_choices;
    }

    // Returns coordinate to the top y of the property. Note that the
    // position of scrollbars is not taken into account.
    int GetY() const;

    // Returns property's value.
    ibVariant GetValue() const
    {
        return DoGetValue();
    }

    // Returns reference to the internal stored value. GetValue is preferred
    // way to get the actual value, since GetValueRef ignores DoGetValue,
    // which may override stored value.
    ibVariant& GetValueRef()
    {
        return m_value;
    }

    const ibVariant& GetValueRef() const
    {
        return m_value;
    }

    // Helper function (for wxPython bindings and such) for settings protected
    // m_value.
    ibVariant GetValuePlain() const
    {
        return m_value;
    }

    // Returns text representation of property's value.
    // flags - If ibPGPropValFormatFlags::Null (default value), then displayed string is returned.
    //   If ibPGPropValFormatFlags::FullValue is set, returns complete, storable string value
    //   instead of displayable. If ibPGPropValFormatFlags::EditableValue is set, returns
    //   string value that must be editable in textctrl. If
    //   ibPGPropValFormatFlags::CompositeFragment is set, returns text that is appropriate to
    //   display as a part of string property's composite text
    //   representation.
    // In older versions, this function used to be overridden to convert
    // property's value into a string representation. This function is
    // now handled by ValueToString(), and overriding this function now
    // will result in run-time assertion failure.
    virtual wxString GetValueAsString(ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const;

    // Returns ibPGCell of given column.
    // Const version of this member function returns 'default'
    // ibPGCell object if the property itself didn't hold
    // cell data.
    const ibPGCell& GetCell( unsigned int column ) const;

    // Returns ibPGCell of given column, creating one if necessary.
    ibPGCell& GetCell( unsigned int column )
    {
        return GetOrCreateCell(column);
    }

    // Returns ibPGCell of given column, creating one if necessary.
    ibPGCell& GetOrCreateCell( unsigned int column );

    // Return number of displayed common values for this property.
    int GetDisplayedCommonValueCount() const;

    // Returns property's displayed text.
    wxString GetDisplayedString() const
    {
        return GetValueAsString(ibPGPropValFormatFlags::Null);
    }

    // Returns property's hint text (shown in empty value cell).
    wxString GetHintText() const;

    // Returns property grid where property lies.
    ibPropertyGrid* GetGrid() const;

    // Returns owner ibPropertyGrid, but only if one is currently
    // on a page displaying this property.
    ibPropertyGrid* GetGridIfDisplayed() const;

    // Returns highest level non-category, non-root parent. Useful when you
    // have nested properties with children.
    // Thus, if immediate parent is root or category, this will return the
    // property itself.
    ibPGProperty* GetMainParent() const;

    // Return parent of property.
    ibPGProperty* GetParent() const { return m_parent; }

    // Returns true if property has editable ibTextCtrl when selected.
    // Although disabled properties do not displayed editor, they still
    // Returns true here as being disabled is considered a temporary
    // condition (unlike being read-only or having limited editing enabled).
    bool IsTextEditable() const;

    // Returns true if property's value is considered unspecified.
    // This usually means that value is Null variant.
    bool IsValueUnspecified() const
    {
        return m_value.GetType() == ibValueTypes::TYPE_EMPTY;
    }

    // Returns true if property has given flag set.
    bool HasFlag(ibPGFlags flag) const
    {
        return !!(m_flags & flag);
    }

    // Returns true if property has all given flags set.
    bool HasFlagsExact(ibPGFlags flags) const
    {
        return (m_flags & flags) == flags;
    }

    // Returns comma-delimited string of property attributes.
    const ibPGAttributeStorage& GetAttributes() const
    {
        return m_attributes;
    }

    // Returns m_attributes as list ibVariant.
    ibVariant GetAttributesAsList() const;

    // Returns ibPGEditor that will be used and created when
    // property becomes selected. Returns more accurate value
    // than DoGetEditorClass().
    const ibPGEditor* GetEditorClass() const;

    // Returns value type used by this property.
    wxString GetValueType() const
    {
        return m_value.GetClassName();
    }

    // Returns editor used for given column. nullptr for no editor.
    const ibPGEditor* GetColumnEditor( int column ) const
    {
        return column == 1 ? GetEditorClass() : nullptr;
    }

    // Returns common value selected for this property. -1 for none.
    int GetCommonValue() const
    {
        return m_commonValue;
    }

    // Returns true if property has even one visible child.
    bool HasVisibleChildren() const;

    // Use this member function to add independent (i.e. regular) children to
    // a property.
    // Returns inserted childProperty.
    // ibPropertyGrid is not automatically refreshed by this function.
    ibPGProperty* InsertChild( int index, ibPGProperty* childProperty );

    // Inserts a new choice to property's list of choices.
    int InsertChoice( const wxString& label, int index, int value = ibPG_INVALID_VALUE );

    // Returns true if this property is actually a ibPGCategoryProperty.
    bool IsCategory() const { return !!(m_flags & ibPGFlags::Category); }

    // Returns true if this property is actually a ibRootProperty.
    bool IsRoot() const { return (m_parent == nullptr); }

    // Returns true if this is a sub-property.
    bool IsSubProperty() const
    {
        ibPGProperty* parent = m_parent;
        if ( parent && !parent->IsCategory() )
            return true;
        return false;
    }

    // Returns last visible sub-property, recursively.
    const ibPGProperty* GetLastVisibleSubItem() const;

    // Returns property's default value. If property's value type is not
    // a built-in one, and "DefaultValue" attribute is not defined, then
    // this function usually returns Null variant.
    ibVariant GetDefaultValue() const;

    // Returns maximum allowed length of the text the user can enter in
    // the property text editor.
    int GetMaxLength() const
    {
        return m_maxLen;
    }

    // Determines, recursively, if all children are not unspecified.
    // pendingList - Assumes members in this ibVariant list as pending
    //   replacement values.
    bool AreAllChildrenSpecified( const ibVariant* pendingList = nullptr ) const;

    // Updates composed values of parent non-category properties, recursively.
    // Returns topmost property updated.
    // Must not call SetValue() (as can be called in it).
    ibPGProperty* UpdateParentValues();

    // Returns true if containing grid uses ibPG_EX_AUTO_UNSPECIFIED_VALUES.
    bool UsesAutoUnspecified() const
    {
        return !!(m_flags & ibPGFlags::AutoUnspecified);
    }

    // Returns bitmap that appears next to value text. Only returns non-null
    // bitmap if one was set with SetValueImage().
    wxBitmap* GetValueImage() const;

    // Returns property attribute value, null variant if not found.
    ibVariant GetAttribute( const wxString& name ) const;

    // Returns named attribute, as string, if found.
    // Otherwise defVal is returned.
    wxString GetAttribute( const wxString& name, const wxString& defVal ) const;

    // Returns named attribute, as long, if found.
    // Otherwise defVal is returned.
    long GetAttributeAsLong( const wxString& name, long defVal ) const;

    // Returns named attribute, as double, if found.
    // Otherwise defVal is returned.
    double GetAttributeAsDouble( const wxString& name, double defVal ) const;

    unsigned int GetDepth() const { return (unsigned int)m_depth; }

    // Gets flags as a'|' delimited string. Note that flag names are not
    // prepended with 'ibPGFlags'.
    // flagmask - String will only be made to include flags combined by this parameter.
    wxString GetFlagsAsString(ibPGFlags flagsMask) const;

    // Returns position in parent's array.
    unsigned int GetIndexInParent() const
    {
        return m_arrIndex;
    }

    // Hides or reveals the property.
    // hide - true for hide, false for reveal.
    // flags - By default changes are applied recursively. Set this
    //   parameter to ibPGPropertyValuesFlags::DontRecurse to prevent this.
    bool Hide(bool hide, ibPGPropertyValuesFlags = ibPGPropertyValuesFlags::Recurse);

    // Returns true if property has visible children.
    bool IsExpanded() const
        { return (!(m_flags & ibPGFlags::Collapsed) && HasAnyChild()); }

    // Returns true if all parents expanded.
    bool IsVisible() const;

    // Returns true if property is enabled.
    bool IsEnabled() const { return !(m_flags & ibPGFlags::Disabled); }

    // If property's editor is created this forces its recreation.
    // Useful in SetAttribute etc. Returns true if actually did anything.
    bool RecreateEditor();

    // If property's editor is active, then update its value.
    void RefreshEditor();

    // Sets an attribute for this property.
    // name - Text identifier of attribute. See @ref propgrid_property_attributes.
    // value - Value of attribute.
    // Setting attribute's value to Null variant will simply remove it
    // from property's set of attributes.
    void SetAttribute( const wxString& name, ibVariant value );

    void SetAttributes( const ibPGAttributeStorage& attributes );

    // Set if user can change the property's value to unspecified by
    // modifying the value of the editor control (usually by clearing
    // it).  Currently, this can work with following properties:
    // ibIntProperty, ibUIntProperty, ibFloatProperty, ibEditEnumProperty.
    // enable - Whether to enable or disable this behaviour (it is disabled
    // by default).
    void SetAutoUnspecified( bool enable = true )
    {
        ChangeFlag(ibPGFlags::AutoUnspecified, enable);
    }

    // Sets property's background colour.
    // colour - Background colour to use.
    // flags - Default is ibPGPropertyValuesFlags::Recurse which causes colour to be set recursively.
    //   Omit this flag to only set colour for the property in question
    //   and not any of its children.
    void SetBackgroundColour(const wxColour& colour,
                             ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::Recurse);

    // Sets property's text colour.
    // colour - Text colour to use.
    // flags - Default is ibPGPropertyValuesFlags::Recurse which causes colour to be set recursively.
    // Omit this flag to only set colour for the property in question
    // and not any of its children.
    void SetTextColour(const wxColour& colour,
                       ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::Recurse);

    // Sets property's default text and background colours.
    // flags - Default is ibPGPropertyValuesFlags::Recurse
    //   which causes colours to be set recursively.
    //   Omit this flag to only set colours for the property in question
    //   and not any of its children.
    void SetDefaultColours(ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::Recurse);

    // Set default value of a property. Synonymous to
    // SetAttribute("DefaultValue", value);
    void SetDefaultValue( ibVariant& value );

    // Sets editor for a property.
    // editor - For builtin editors, use ibPGEditor_X, where X is builtin editor's
    //   name (TextCtrl, Choice, etc. see ibPGEditor documentation for full
    //   list).
    // For custom editors, use pointer you received from
    // ibPropertyGrid::RegisterEditorClass().
    void SetEditor( const ibPGEditor* editor )
    {
        m_customEditor = editor;
    }

    // Sets editor for a property, , by editor name.
    void SetEditor( const wxString& editorName );

    // Sets cell information for given column.
    void SetCell( int column, const ibPGCell& cell );

    // Sets common value selected for this property. -1 for none.
    void SetCommonValue( int commonValue )
    {
        m_commonValue = commonValue;
    }

    // Sets flags from a '|' delimited string. Note that flag names are not
    // prepended with 'ibPGFlags'.
    void SetFlagsFromString( const wxString& str );

    // Sets property's "is it modified?" flag. Affects children recursively.
    void SetModifiedStatus( bool modified )
    {
        SetFlagRecursively(ibPGFlags::Modified, modified);
    }

    // Call in OnEvent(), OnButtonClick() etc. to change the property value
    // based on user input.
    // This method is const since it doesn't actually modify value, but posts
    // given variant as pending value, stored in ibPropertyGrid.
    void SetValueInEvent( const ibVariant& value ) const;

    // Call this to set value of the property.
    // Unlike methods in ibPropertyGrid, this does not automatically update
    // the display.
    // Use ibPropertyGrid::ChangePropertyValue() instead if you need to run
    // through validation process and send property change event.
    // If you need to change property value in event, based on user input, use
    // SetValueInEvent() instead.
    // pList - Pointer to list variant that contains child values. Used to
    //   indicate which children should be marked as modified.
    // flags - Various flags (for instance, ibPGSetValueFlags::RefreshEditor,
    //  which is enabled by default).
    void SetValue(ibVariant value, ibVariant* pList = nullptr,
                  ibPGSetValueFlags flags = ibPGSetValueFlags::RefreshEditor);

    // Set wxBitmap in front of the value. This bitmap may be ignored
    // by custom cell renderers.
    void SetValueImage( const wxBitmapBundle& bmp );

    // Sets selected choice and changes property value.
    // Tries to retain value type, although currently if it is not string,
    // then it is forced to integer.
    void SetChoiceSelection( int newValue );

    void SetExpanded( bool expanded )
    {
        ChangeFlag(ibPGFlags::Collapsed, !expanded);
    }

    // Sets or clears given property flag. Mainly for internal use.
    // Setting a property flag never has any side-effect, and is
    // intended almost exclusively for internal use. So, for
    // example, if you want to disable a property, call
    // Enable(false) instead of setting ibPGFlags::Disabled flag.
    void ChangeFlag( ibPGFlags flag, bool set )
    {
        if ( set )
            m_flags |= flag;
        else
            m_flags &= ~flag;
    }

    // Sets or clears given property flag, recursively. This function is
    // primarily intended for internal use.
    void SetFlagRecursively( ibPGFlags flag, bool set );

    // Sets property's help string, which is shown, for example, in
    // ibPropertyGridManager's description text box.
    void SetHelpString( const wxString& helpString )
    {
        m_helpString = helpString;
    }

    // Sets property's label.
    // Properties under same parent may have same labels. However,
    // property names must still remain unique.
    void SetLabel( const wxString& label );

    // Sets new (base) name for property.
    void SetName( const wxString& newName );

    // Changes what sort of parent this property is for its children.
    // flag - Use one of the following values: ibPGFlags::MiscParent (for
    //   generic parents), ibPGFlags::Category (for categories), or
    //   ibPGFlags::Aggregate (for derived property classes with private
    //   children).
    // You generally do not need to call this function.
    void SetParentalType(ibPGFlags flag)
    {
        m_flags &= ~(ibPGFlags::Property | ibPGFlags::ParentalFlags);
        m_flags |= flag;
    }

    // Sets property's value to unspecified (i.e. Null variant).
    void SetValueToUnspecified()
    {
        ibVariant val;  // Create null variant
        SetValue(val, nullptr, ibPGSetValueFlags::RefreshEditor);
    }

    // Helper function (for wxPython bindings and such) for settings protected
    // m_value.
    void SetValuePlain( const ibVariant& value )
    {
        m_value = value;
    }

#if wxUSE_VALIDATORS
    // Sets wxValidator for a property.
    void SetValidator( const wxValidator& validator )
    {
        m_validator = wxDynamicCast(validator.Clone(),wxValidator);
    }

    // Gets assignable version of property's validator.
    wxValidator* GetValidator() const
    {
        return  m_validator ? m_validator : DoGetValidator();
    }
#endif // wxUSE_VALIDATORS

    // Returns client data (void*) of a property.
    void* GetClientData() const
    {
        return m_clientData;
    }

    // Sets client data (void*) of a property.
    // This untyped client data has to be deleted manually.
    void SetClientData( void* clientData )
    {
        m_clientData = clientData;
    }

    // Sets client object of a property.
    void SetClientObject(wxClientData* clientObject)
    {
        delete m_clientObject;
        m_clientObject = clientObject;
    }

    // Gets managed client object of a property.
    wxClientData *GetClientObject() const { return m_clientObject; }

    // Sets new set of choices for the property.
    // This operation deselects the property and clears its
    // value.
    bool SetChoices( const ibPGChoices& choices );

    // Set max length of text in text editor.
    bool SetMaxLength( int maxLen );

    // Call with 'false' in OnSetValue to cancel value changes after all
    // (i.e. cancel 'true' returned by StringToValue() or IntToValue()).
    void SetWasModified( bool set = true )
    {
        ChangeFlag(ibPGFlags::WasModified, set);
    }

    // Returns property's help or description text.
    const wxString& GetHelpString() const
    {
        return m_helpString;
    }

    // Returns true if candidateParent is some parent of this property.
    // Use, for example, to detect if item is inside collapsed section.
    bool IsSomeParent( ibPGProperty* candidate_parent ) const;

    // Adapts list variant into proper value using consecutive
    // ChildChanged-calls.
    void AdaptListToValue( ibVariant& list, ibVariant* value ) const;

    // Adds a private child property. If you use this instead of
    // ibPropertyGridInterface::Insert() or
    // ibPropertyGridInterface::AppendIn(), then property's parental
    // type will automatically be set up to ibPGFlags::Aggregate. In other
    // words, all properties of this property will become private.
    void AddPrivateChild( ibPGProperty* prop );

    // Use this member function to add independent (i.e. regular) children to
    // a property.
    // ibPropertyGrid is not automatically refreshed by this function.
    ibPGProperty* AppendChild( ibPGProperty* prop )
    {
        return InsertChild(-1, prop);
    }

    // Returns height of children, recursively, and
    // by taking expanded/collapsed status into account.
    // lh - Line height. Pass result of GetGrid()->GetRowHeight() here.
    // iMax - Only used (internally) when finding property y-positions.
    int GetChildrenHeight( int lh, int iMax = -1 ) const;

    // Returns number of child properties.
    unsigned int GetChildCount() const
    {
        return (unsigned int) m_children.size();
    }

    // Checks if contains any child property.
    bool HasAnyChild() const
    {
        return !m_children.empty();
    }

    // Returns sub-property at index i.
    ibPGProperty* Item( unsigned int i ) const
        { return m_children[i]; }

    // Returns last sub-property.
    ibPGProperty* Last() const { return m_children.back(); }

    // Returns index of given child property. wxNOT_FOUND if
    // given property is not child of this.
    int Index( const ibPGProperty* p ) const;

    // Puts correct indexes to children
    void FixIndicesOfChildren( unsigned int starthere = 0 );

    // Converts image width into full image offset, with margins.
    int GetImageOffset( int imageWidth ) const;

    // Returns ibPropertyGridPageState in which this property resides.
    ibPropertyGridPageState* GetParentState() const { return m_parentState; }

    ibPGProperty* GetItemAtY( unsigned int y,
                              unsigned int lh,
                              unsigned int* nextItemY ) const;

    // Returns property at given virtual y coordinate.
    ibPGProperty* GetItemAtY( unsigned int y ) const;

    // Returns (direct) child property with given name (or nullptr if not found).
    ibPGProperty* GetPropertyByName( const wxString& name ) const;

    // Returns various display-related information for given column
    // This function can return modified (customized) cell object.
    void GetDisplayInfo( unsigned int column,
                         int choiceIndex,
                         int flags,
                         wxString* pString,
                         ibPGCell* pCell );


    const static wxString       sm_labelItem;

    // This member is public so scripting language bindings
    // wrapper code can access it freely.
    void*                       m_clientData;

protected:

    // Ctors are ptotected because ibPGProperty is only a base class
    // for all property classes and shouldn't be instantiated directly.
    ibPGProperty();
    // All non-abstract property classes should have a constructor with
    // the same first two arguments as this one.
    ibPGProperty(const wxString& label, const wxString& name);

    // Sets property cell in fashion that reduces number of exclusive
    // copies of cell data. Used when setting, for instance, same
    // background colour for a number of properties.
    // firstCol - First column to affect.
    // lastCol- Last column to affect.
    // preparedCell - Pre-prepared cell that is used for those which cell data
    //   before this matched unmodCellData.
    // srcData - If unmodCellData did not match, valid cell data from this
    //   is merged into cell (usually generating new exclusive copy
    //   of cell's data).
    // unmodCellData - If cell's cell data matches this, its cell is now set to
    //   preparedCell.
    // ignoreWithFlags - Properties with any one of these flags are skipped.
    // recursively - If true, apply this operation recursively in child properties.
    void AdaptiveSetCell(unsigned int firstCol,
                         unsigned int lastCol,
                         const ibPGCell& preparedCell,
                         const ibPGCell& srcData,
                         ibPGCellData* unmodCellData,
                         ibPGFlags ignoreWithFlags,
                         bool recursively);

    // Clear cells associated with property.
    // recursively - If true, apply this operation recursively in child properties.
    void ClearCells(ibPGFlags ignoreWithFlags, bool recursively);

    // Makes sure m_cells has size of column+1 (or more).
    void EnsureCells( unsigned int column );

    // Returns (direct) child property with given name (or nullptr if not found),
    // with hint index.
    // hintIndex - Start looking for the child at this index.
    // Does not support scope (i.e. Parent.Child notation).
    ibPGProperty* GetPropertyByNameWH( const wxString& name,
                                       unsigned int hintIndex ) const;

    // This is used by Insert etc.
    void DoAddChild( ibPGProperty* prop,
                     int index = -1,
                     bool correct_mode = true );

    void DoGenerateComposedValue(wxString& text,
                                 ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::ValueIsCurrent,
                                 const ibPGVariantList* valueOverrides = nullptr,
                                 ibPGHashMapS2S* childResults = nullptr) const;

    bool DoHide( bool hide, ibPGPropertyValuesFlags flags );

    void DoSetName(const wxString& str) { m_name = str; }

    // Deletes all sub-properties.
    void Empty();

    bool HasCell( unsigned int column ) const
    {
        return m_cells.size() > column;
    }

    void InitAfterAdded( ibPropertyGridPageState* pageState,
                         ibPropertyGrid* propgrid );

    // Returns true if child property is selected.
    bool IsChildSelected( bool recursive = false ) const;

    // Removes child property with given pointer. Does not delete it.
    void RemoveChild( ibPGProperty* p );

    // Removes child property at given index. Does not delete it.
    void RemoveChild(unsigned int index);

    // Sorts children using specified comparison function.
    void SortChildren(bool (*fCmp)(ibPGProperty*, ibPGProperty*));

    void DoEnable( bool enable );

    void DoPreAddChild( int index, ibPGProperty* prop );

    void SetParentState( ibPropertyGridPageState* pstate )
        { m_parentState = pstate; }

    void SetFlag( ibPGFlags flag )
    {
        //
        // NB: While using ibPGFlags here makes it difficult to
        //     combine different flags, it usefully prevents user from
        //     using incorrect flags (say, wxWindow styles).
        m_flags |= flag;
    }

    void ClearFlag(ibPGFlags flag)
    {
        m_flags &= ~(flag);
    }

    // Called when the property is being removed from the grid and/or
    // page state (but *not* when it is also deleted).
    void OnDetached(ibPropertyGridPageState* state,
                    ibPropertyGrid* propgrid);

    // Call after fixed sub-properties added/removed after creation.
    // if oldSelInd >= 0 and < new max items, then selection is
    // moved to it.
    void SubPropsChanged( int oldSelInd = -1 );

    int GetY2( int lh ) const;

    wxString                    m_label;
    wxString                    m_name;
    ibPGProperty*               m_parent;
    ibPropertyGridPageState*    m_parentState;

    wxClientData*               m_clientObject;

    // Overrides editor returned by property class
    const ibPGEditor*           m_customEditor;
#if wxUSE_VALIDATORS
    // Editor is going to get this validator
    wxValidator*                m_validator;
#endif
    // Show this in front of the value
    //
    // TODO: Can bitmap be implemented with ibPGCell?
    wxBitmapBundle              m_valueBitmapBundle;
    mutable wxBitmap            m_valueBitmap;

    ibVariant                   m_value;
    ibPGAttributeStorage        m_attributes;
    std::vector<ibPGProperty*>  m_children;

    // Extended cell information
    std::vector<ibPGCell>       m_cells;

    // Choices shown in drop-down list of editor control.
    ibPGChoices                 m_choices;

    // Help shown in statusbar or help box.
    wxString                    m_helpString;

    // Index in parent's property array.
    unsigned int                m_arrIndex;

    // If not -1, then overrides m_value
    int                         m_commonValue;

    ibPGFlags                   m_flags;

    // Maximum length (for string properties). Could be in some sort of
    // ibBaseStringProperty, but currently, for maximum flexibility and
    // compatibility, we'll stick it here.
    int                         m_maxLen;

    // Root has 0, categories etc. at that level 1, etc.
    unsigned char               m_depth;

    // m_depthBgCol indicates width of background colour between margin and item
    // (essentially this is category's depth, if none then equals m_depth).
    unsigned char               m_depthBgCol;

private:
    // Called in constructors.
    void Init();
    void Init( const wxString& label, const wxString& name );
};

// -----------------------------------------------------------------------

//
// Property class declaration helper macros
// (ibPGRootPropertyClass and ibPGCategoryProperty require this).
//

#define IB_PG_DECLARE_DOGETEDITORCLASS \
    virtual const ibPGEditor* DoGetEditorClass() const override;

#ifndef IB_PG_DECLARE_PROPERTY_CLASS
    #define IB_PG_DECLARE_PROPERTY_CLASS(CLASSNAME) \
        public: \
            wxDECLARE_DYNAMIC_CLASS(CLASSNAME); \
            IB_PG_DECLARE_DOGETEDITORCLASS \
        private:
#endif

// Implements sans constructor function. Also, first arg is class name, not
// property name.
#define ibPG_IMPLEMENT_PROPERTY_CLASS_PLAIN(PROPNAME, EDITOR) \
const ibPGEditor* PROPNAME::DoGetEditorClass() const \
{ \
    return ibPGEditor_##EDITOR; \
}

// -----------------------------------------------------------------------

// Root parent property.
class DESKGUI_API ibPGRootProperty : public ibPGProperty
{
public:
    IB_PG_DECLARE_PROPERTY_CLASS(ibPGRootProperty)
public:

    // Constructor.
    ibPGRootProperty( const wxString& name = wxS("<Root>") );
    virtual ~ibPGRootProperty() = default;

    virtual bool StringToValue( ibVariant&, const wxString&, ibPGPropValFormatFlags ) const override
    {
        return false;
    }

protected:
};

// -----------------------------------------------------------------------

// Category (caption) property.
class DESKGUI_API ibPGCategoryProperty : public ibPGProperty
{
    friend class ibPropertyGridPageState;
    IB_PG_DECLARE_PROPERTY_CLASS(ibPGCategoryProperty)
public:

    // Default constructor is only used in special cases.
    ibPGCategoryProperty();

    ibPGCategoryProperty( const wxString& label,
                        const wxString& name = ibPG_LABEL );
    virtual ~ibPGCategoryProperty() = default;

    int GetTextExtent( const wxWindow* wnd, const wxFont& font ) const;

    virtual wxString ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const override;
    virtual wxString GetValueAsString(ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

protected:
    void SetTextColIndex( unsigned int colInd )
        { m_capFgColIndex = (wxByte) colInd; }
    unsigned int GetTextColIndex() const
        { return (unsigned int) m_capFgColIndex; }

    void CalculateTextExtent(const wxWindow* wnd, const wxFont& font);

    int     m_textExtent;  // pre-calculated length of text
    wxByte  m_capFgColIndex;  // caption text colour index

private:
    void Init();
};

// -----------------------------------------------------------------------

#endif // _DESKGUI_UNIV_PROPGRID_PROPERTY_H_
