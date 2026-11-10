// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, include/wx/propgrid/propgridpagestate.h): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/propgrid/propgridpagestate.h
// Purpose:     ibPropertyGridPageState class
// Author:      Jaakko Salli
// Created:     2008-08-24
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_PROPGRID_PROPGRIDPAGESTATE_H_
#define _DESKGUI_UNIV_PROPGRID_PROPGRIDPAGESTATE_H_

#include "wx/defs.h"

#if wxUSE_PROPGRID

#include "deskgui/univ/propgrid/property.h"

#include <set>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------

enum class ibPGSelectPropertyFlags : int
{
    // No flags
    Null          = 0,
    // Focuses to created editor
    Focus         = 0x0001,
    // Forces deletion and recreation of editor
    Force         = 0x0002,
    // For example, doesn't cause EnsureVisible
    Nonvisible    = 0x0004,
    // Do not validate editor's value before selecting
    NoValidate    = 0x0008,
    // Property being deselected is about to be deleted
    Deleting      = 0x0010,
    // Property's values was set to unspecified by the user
    SetUnspec     = 0x0020,
    // Property's event handler changed the value
    DialogVal     = 0x0040,
    // Set to disable sending of ibEVT_PG_SELECTED event
    DontSendEvent = 0x0080,
    // Don't make any graphics updates
    NoRefresh     = 0x0100
};

constexpr ibPGSelectPropertyFlags operator|(ibPGSelectPropertyFlags a, ibPGSelectPropertyFlags b)
{
    return static_cast<ibPGSelectPropertyFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline ibPGSelectPropertyFlags operator|=(ibPGSelectPropertyFlags& a, ibPGSelectPropertyFlags b)
{
    return a = a | b;
}

constexpr ibPGSelectPropertyFlags operator&(ibPGSelectPropertyFlags a, ibPGSelectPropertyFlags b)
{
    return static_cast<ibPGSelectPropertyFlags>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr bool operator!(ibPGSelectPropertyFlags a)
{
    return static_cast<int>(a) == 0;
}

// -----------------------------------------------------------------------

// DoSetSplitter() flags

enum class ibPGSplitterPositionFlags : int
{
    Null           = 0,
    Refresh        = 0x0001,
    AllPages       = 0x0002,
    FromEvent      = 0x0004,
    FromAutoCenter = 0x0008
};

constexpr ibPGSplitterPositionFlags operator&(ibPGSplitterPositionFlags a, ibPGSplitterPositionFlags b)
{
    return static_cast<ibPGSplitterPositionFlags>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr ibPGSplitterPositionFlags operator|(ibPGSplitterPositionFlags a, ibPGSplitterPositionFlags b)
{
    return static_cast<ibPGSplitterPositionFlags>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr bool operator!(ibPGSplitterPositionFlags a)
{
    return static_cast<int>(a) == 0;
}

// -----------------------------------------------------------------------

// A return value from ibPropertyGrid::HitTest(),
// contains all you need to know about an arbitrary location on the grid.
class DESKGUI_API ibPropertyGridHitTestResult
{
    friend class ibPropertyGridPageState;
public:
    ibPropertyGridHitTestResult()
        : m_property(nullptr)
        , m_column(-1)
        , m_splitter(-1)
        , m_splitterHitOffset(0)
    {
    }

    ~ibPropertyGridHitTestResult() = default;

    // Returns column hit. -1 for margin.
    int GetColumn() const { return m_column; }

    // Returns property hit. nullptr if empty space below
    // properties was hit instead.
    ibPGProperty* GetProperty() const
    {
        return m_property;
    }

    // Returns index of splitter hit, -1 for none.
    int GetSplitter() const { return m_splitter; }

    // If splitter hit, then this member function
    // returns offset to the exact splitter position.
    int GetSplitterHitOffset() const { return m_splitterHitOffset; }

private:
    // Property. nullptr if empty space below properties was hit.
    ibPGProperty*   m_property;

    // Column. -1 for margin.
    int             m_column;

    // Index of splitter hit, -1 for none.
    int             m_splitter;

    // If splitter hit, offset to that.
    int             m_splitterHitOffset;
};

// -----------------------------------------------------------------------

#define ibPG_IT_CHILDREN(A)         ((A)<<16)

// NOTES: At lower 16-bits, there are flags to check if item will be included.
// At higher 16-bits, there are same flags, but to instead check if children
// will be included.
enum ibPG_ITERATOR_FLAGS
{

// Iterate through 'normal' property items (does not include children of
// aggregate or hidden items by default).
ibPG_ITERATE_PROPERTIES = ibPGFlags::Property |
                          ibPGFlags::MiscParent |
                          ibPGFlags::Aggregate |
                          ibPGFlags::Collapsed |
                          ibPG_IT_CHILDREN(ibPGFlags::MiscParent) |
                          ibPG_IT_CHILDREN(ibPGFlags::Category),

// Iterate children of collapsed parents, and individual items that are hidden.
ibPG_ITERATE_HIDDEN = ibPGFlags::Hidden |
                      ibPG_IT_CHILDREN(ibPGFlags::Collapsed),

// Iterate children of parent that is an aggregate property (ie has fixed
// children).
ibPG_ITERATE_FIXED_CHILDREN = ibPG_IT_CHILDREN(ibPGFlags::Aggregate) |
                              ibPG_ITERATE_PROPERTIES,

// Iterate categories.
// Note that even without this flag, children of categories are still iterated
// through.
ibPG_ITERATE_CATEGORIES = ibPGFlags::Category |
                          ibPG_IT_CHILDREN(ibPGFlags::Category) |
                          ibPGFlags::Collapsed,

ibPG_ITERATE_ALL_PARENTS = static_cast<int>(ibPGFlags::MiscParent |
                           ibPGFlags::Aggregate |
                           ibPGFlags::Category),

ibPG_ITERATE_ALL_PARENTS_RECURSIVELY = ibPG_ITERATE_ALL_PARENTS |
                                       ibPG_IT_CHILDREN(
                                                ibPG_ITERATE_ALL_PARENTS),

ibPG_ITERATOR_FLAGS_ALL = static_cast<int>(ibPGFlags::Property |
                          ibPGFlags::MiscParent |
                          ibPGFlags::Aggregate |
                          ibPGFlags::Hidden |
                          ibPGFlags::Category |
                          ibPGFlags::Collapsed),

ibPG_ITERATOR_MASK_OP_ITEM = ibPG_ITERATOR_FLAGS_ALL,

// (ibPGFlags::MiscParent|ibPGFlags::Aggregate|ibPGFlags::Category)
ibPG_ITERATOR_MASK_OP_PARENT = ibPG_ITERATOR_FLAGS_ALL,

// Combines all flags needed to iterate through visible properties
// (ie. hidden properties and children of collapsed parents are skipped).
ibPG_ITERATE_VISIBLE = ibPG_ITERATE_PROPERTIES |
                       ibPGFlags::Category |
                       ibPG_IT_CHILDREN(ibPGFlags::Aggregate),

// Iterate all items.
ibPG_ITERATE_ALL = ibPG_ITERATE_VISIBLE |
                   ibPG_ITERATE_HIDDEN,

// Iterate through individual properties (ie categories and children of
// aggregate properties are skipped).
ibPG_ITERATE_NORMAL = ibPG_ITERATE_PROPERTIES |
                      ibPG_ITERATE_HIDDEN,

// Default iterator flags.
ibPG_ITERATE_DEFAULT = ibPG_ITERATE_NORMAL

};

inline void ibPGCreateIteratorMasks(int flags, ibPGFlags& itemExMask, ibPGFlags& parentExMask)
{
    itemExMask = static_cast<ibPGFlags>((flags ^ ibPG_ITERATOR_MASK_OP_ITEM) &
        ibPG_ITERATOR_MASK_OP_ITEM & 0xFFFF);
    parentExMask = static_cast<ibPGFlags>(((flags >> 16) ^ ibPG_ITERATOR_MASK_OP_PARENT) &
        ibPG_ITERATOR_MASK_OP_PARENT & 0xFFFF);
}

// Base for ibPropertyGridIterator classes.
class DESKGUI_API ibPropertyGridIteratorBase
{
protected:
    ibPropertyGridIteratorBase() = default;

public:
    void Assign( const ibPropertyGridIteratorBase& it );

    bool AtEnd() const { return m_property == nullptr; }

    // Get current property.
    ibPGProperty* GetProperty() const { return m_property; }

    void Init( ibPropertyGridPageState* state,
               int flags,
               ibPGProperty* property,
               int dir = 1 );

    void Init( ibPropertyGridPageState* state,
               int flags,
               int startPos = wxTOP,
               int dir = 0 );

    // Iterate to the next property.
    void Next( bool iterateChildren = true );

    // Iterate to the previous property.
    void Prev();

    // Set base parent, i.e. a property when, in which iteration returns, it
    // ends.
    // Default base parent is the root of the used ibPropertyGridPageState.
    void SetBaseParent( ibPGProperty* baseParent )
        { m_baseParent = baseParent; }

protected:

    ibPGProperty*               m_property;

private:
    ibPropertyGridPageState*        m_state;
    ibPGProperty*               m_baseParent;

    // Masks are used to quickly exclude items
    ibPGFlags           m_itemExMask;
    ibPGFlags           m_parentExMask;
};

template <typename PROPERTY, typename STATE>
class ibPGIterator : public ibPropertyGridIteratorBase
{
public:
    ibPGIterator(STATE* state, int flags = ibPG_ITERATE_DEFAULT,
                 PROPERTY* property = nullptr, int dir = 1)
        : ibPropertyGridIteratorBase()
    {
        Init(const_cast<ibPropertyGridPageState*>(state), flags, const_cast<ibPGProperty*>(property), dir);
    }
    ibPGIterator(STATE* state, int flags, int startPos, int dir = 0)
        : ibPropertyGridIteratorBase()
    {
        Init(const_cast<ibPropertyGridPageState*>(state), flags, startPos, dir);
    }
    ibPGIterator()
        : ibPropertyGridIteratorBase()
    {
        m_property = nullptr;
    }
    ibPGIterator(const ibPGIterator& it)
        : ibPropertyGridIteratorBase()
    {
        Assign(it);
    }
    ~ibPGIterator() = default;

    ibPGIterator& operator=(const ibPGIterator& it)
    {
        if ( this != &it )
            Assign(it);
        return *this;
    }

    ibPGIterator& operator++() { Next(); return *this; }
    ibPGIterator operator++(int) { ibPGIterator it = *this; Next(); return it; }
    ibPGIterator& operator--() { Prev(); return *this; }
    ibPGIterator operator--(int) { ibPGIterator it = *this; Prev(); return it; }
    PROPERTY* operator *() const { return const_cast<PROPERTY*>(m_property); }
    static PROPERTY* OneStep(STATE* state, int flags = ibPG_ITERATE_DEFAULT,
                             PROPERTY* property = nullptr, int dir = 1)
    {
        ibPGIterator it(state, flags, property, dir);
        if ( property )
        {
            if ( dir == 1 )
                it.Next();
            else
                it.Prev();
        }
        return *it;
    }
};

// Preferable way to iterate through contents of ibPropertyGrid,
// ibPropertyGridManager, and ibPropertyGridPage.
// See ibPropertyGridInterface::GetIterator() for more information about usage.
typedef ibPGIterator<ibPGProperty, ibPropertyGridPageState> ibPropertyGridIterator;
typedef ibPGIterator<const ibPGProperty, const ibPropertyGridPageState> ibPropertyGridConstIterator;

// -----------------------------------------------------------------------

// Base class to derive new viterators.
class DESKGUI_API ibPGVIteratorBase : public wxObjectRefData
{
    friend class ibPGVIterator;
public:
    ibPGVIteratorBase() = default;
    virtual void Next() = 0;
    virtual bool AtEnd() const = 0;
protected:
    virtual ~ibPGVIteratorBase() = default;

    ibPropertyGridIterator  m_it;
};

// Abstract implementation of a simple iterator. Can only be used
// to iterate in forward order, and only through the entire container.
// Used to have functions dealing with all properties work with both
// ibPropertyGrid and ibPropertyGridManager.
class DESKGUI_API ibPGVIterator
{
public:
    ibPGVIterator() { m_pIt = nullptr; }
    ibPGVIterator( ibPGVIteratorBase* obj ) { m_pIt = obj; }
    ~ibPGVIterator() { UnRef(); }
    void UnRef() { if (m_pIt) m_pIt->DecRef(); }
    ibPGVIterator( const ibPGVIterator& it )
    {
        m_pIt = it.m_pIt;
        m_pIt->IncRef();
    }
    const ibPGVIterator& operator=( const ibPGVIterator& it )
    {
        if (this != &it)
        {
            UnRef();
            m_pIt = it.m_pIt;
            m_pIt->IncRef();
        }
        return *this;
    }
    void Next() { m_pIt->Next(); }
    bool AtEnd() const { return m_pIt->AtEnd(); }
    ibPGProperty* GetProperty() const { return m_pIt->m_it.GetProperty(); }
protected:
    ibPGVIteratorBase*  m_pIt;
};

// -----------------------------------------------------------------------

// Contains low-level property page information (properties, column widths,
// etc.) of a single ibPropertyGrid or single ibPropertyGridPage. Generally you
// should not use this class directly, but instead member functions in
// ibPropertyGridInterface, ibPropertyGrid, ibPropertyGridPage, and
// ibPropertyGridManager.
// - Currently this class is not implemented in wxPython.
class DESKGUI_API ibPropertyGridPageState
{
    friend class ibPropertyGrid;
    friend class ibPropertyGridInterface;
    friend class ibPropertyGridPage;
    friend class ibPropertyGridManager;
    friend class ibPGProperty;
    friend class ibFlagsProperty;
    friend class ibPropertyGridIteratorBase;
    friend class ibPropertyGridXmlHandler;
public:

    // Default constructor.
    ibPropertyGridPageState();

    // Destructor.
    virtual ~ibPropertyGridPageState();

    // Makes sure all columns have minimum width.
    void CheckColumnWidths( int widthChange = 0 );

    // Override this member function to add custom behaviour on property
    // deletion.
    virtual void DoDelete( ibPGProperty* item, bool doDelete = true );

    // Override this member function to add custom behaviour on property
    // insertion.
    virtual ibPGProperty* DoInsert( ibPGProperty* parent,
                                    int index,
                                    ibPGProperty* property );

    // This needs to be overridden in grid used the manager so that splitter
    // changes can be propagated to other pages.
    virtual void DoSetSplitter(int pos,
                               int splitterColumn = 0,
                               ibPGSplitterPositionFlags
                                flags = ibPGSplitterPositionFlags::Null);

    bool EnableCategories( bool enable );

    // Make sure virtual height is up-to-date.
    void EnsureVirtualHeight()
    {
        if ( m_vhCalcPending )
        {
            RecalculateVirtualHeight();
            m_vhCalcPending = false;
        }
    }

    // Returns (precalculated) height of contained visible properties.
    unsigned int GetVirtualHeight() const
    {
        wxASSERT( !m_vhCalcPending );
        return m_virtualHeight;
    }

    // Returns (precalculated) height of contained visible properties.
    unsigned int GetVirtualHeight()
    {
        EnsureVirtualHeight();
        return m_virtualHeight;
    }

    // Returns actual height of contained visible properties.
    // Mostly used for internal diagnostic purposes.
    unsigned int GetActualVirtualHeight() const;

    unsigned int GetColumnCount() const
    {
        return (unsigned int) m_colWidths.size();
    }

    int GetColumnMinWidth( int column ) const;

    int GetColumnWidth( unsigned int column ) const
    {
        return m_colWidths[column];
    }

    ibPropertyGrid* GetGrid() const { return m_pPropGrid; }

    // Returns last item which could be iterated using given flags.
    ibPGProperty* GetLastItem( int flags = ibPG_ITERATE_DEFAULT );

    const ibPGProperty* GetLastItem( int flags = ibPG_ITERATE_DEFAULT ) const
    {
        return const_cast<ibPropertyGridPageState*>(this)->GetLastItem(flags);
    }

    // Returns currently selected property.
    ibPGProperty* GetSelection() const
    {
        return m_selection.empty()? nullptr: m_selection[0];
    }

    ibPGCategoryProperty* GetPropertyCategory( const ibPGProperty* p ) const;

    // Returns combined width of margin and all the columns
    int GetVirtualWidth() const
    {
        return m_width;
    }

    int GetColumnFullWidth(ibPGProperty* p, unsigned int col) const;

    // Returns information about arbitrary position in the grid.
    // pt - Logical coordinates in the virtual grid space. Use
    //   wxScrolled<T>::CalcUnscrolledPosition() if you need to
    //   translate a scrolled position into a logical one.
    ibPropertyGridHitTestResult HitTest( const wxPoint& pt ) const;

    // Returns true if page is visibly displayed.
    bool IsDisplayed() const;

    bool IsInNonCatMode() const { return m_properties == m_abcArray; }

    // Called after virtual height needs to be recalculated.
    void VirtualHeightChanged()
    {
        m_vhCalcPending = true;
    }

protected:
    wxSize DoFitColumns(bool allowGridResize = false);

    ibPGProperty* DoGetItemAtY(int y) const;

    void DoSetSelection(ibPGProperty* prop)
    {
        m_selection.clear();
        if (prop)
            m_selection.push_back(prop);
    }

    bool DoClearSelection()
    {
        return DoSelectProperty(nullptr);
    }

    void DoRemoveFromSelection(ibPGProperty* prop);

    void DoSetColumnProportion(unsigned int column, int proportion);

    int DoGetColumnProportion(unsigned int column) const
    {
        return m_columnProportions[column];
    }

    ibVariant DoGetPropertyValues(const wxString& listname, ibPGProperty* baseparent,
                                  ibPGPropertyValuesFlags flags) const;

    ibPGProperty* DoGetRoot() const { return m_properties; }

    void DoSetPropertyName(ibPGProperty* p, const wxString& newName);

    // Utility to check if two properties are visibly next to each other
    bool ArePropertiesAdjacent( ibPGProperty* prop1,
                                ibPGProperty* prop2,
                                int iterFlags = ibPG_ITERATE_VISIBLE ) const;

    int DoGetSplitterPosition( int splitterIndex = 0 ) const;

    void DoLimitPropertyEditing(ibPGProperty* p, bool limit = true)
    {
        p->SetFlagRecursively(ibPGFlags::NoEditor, limit);
    }

    bool DoSelectProperty(ibPGProperty* p, ibPGSelectPropertyFlags flags = ibPGSelectPropertyFlags::Null);

    // Base append.
    ibPGProperty* DoAppend(ibPGProperty* property);

    // Called in, for example, ibPropertyGrid::Clear.
    void DoClear();

    bool DoIsPropertySelected(ibPGProperty* prop) const;

    bool DoCollapse(ibPGProperty* p);

    bool DoExpand(ibPGProperty* p);

    // Returns column at x coordinate (in GetGrid()->GetPanel()).
    // pSplitterHit - Give pointer to int that receives index to splitter that is at x.
    // pSplitterHitOffset - Distance from said splitter.
    int HitTestH( int x, int* pSplitterHit, int* pSplitterHitOffset ) const;

    bool PrepareToAddItem( ibPGProperty* property,
                           ibPGProperty* scheduledParent );

    // Returns property by its label.
    ibPGProperty* BaseGetPropertyByLabel( const wxString& label,
                                      const ibPGProperty* parent = nullptr ) const;

    // Unselect sub-properties.
    void DoRemoveChildrenFromSelection(ibPGProperty* p, bool recursive,
                                       ibPGSelectPropertyFlags selFlags);

    // Mark sub-properties as being deleted.
    void DoMarkChildrenAsDeleted(ibPGProperty* p, bool recursive);

    // Rename the property
    // so it won't remain in the way of the user code.
    void DoInvalidatePropertyName(ibPGProperty* p);

    // Rename sub-properties
    // so it won't remain in the way of the user code.
    void DoInvalidateChildrenNames(ibPGProperty* p, bool recursive);

    bool DoHideProperty(ibPGProperty* p, bool hide, ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::Recurse);

    bool DoSetPropertyValueString(ibPGProperty* p, const wxString& value);

    bool DoSetPropertyValue(ibPGProperty* p, ibVariant& value);

    void DoSetPropertyValues(const ibPGVariantList& list,
                             ibPGProperty* default_category);

    void DoSortChildren(ibPGProperty* p, ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::DontRecurse);
    void DoSort(ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::DontRecurse);

    // widthChange is non-client.
    void OnClientWidthChange(int newWidth, int widthChange, bool fromOnResize = false);

    // Check if property contains given sub-category.
    bool IsChildCategory(ibPGProperty* p,
                         ibPGCategoryProperty* cat, bool recursive);

    void PropagateColSizeDec(int column, int decrease, int dir);

    void CalculateFontAndBitmapStuff(int vspacing);

    // Returns property by its name.
    ibPGProperty* BaseGetPropertyByName(const wxString& name) const;

    // Returns minimal width for given column so that all images and texts
    // will fit entirely.
    // Used by SetSplitterLeft() and DoFitColumns().
    int GetColumnFitWidth(const ibPGProperty* p, unsigned int col, bool subProps) const;

    void SetSplitterLeft(bool subProps = false);

    void SetColumnCount(int colCount);

    void ResetColumnSizes(ibPGSplitterPositionFlags setSplitterFlags);

    bool PrepareAfterItemsAdded();

    // Recalculates m_virtualHeight.
    void RecalculateVirtualHeight()
    {
        m_virtualHeight = GetActualVirtualHeight();
    }

    // Set virtual width for this particular page.
    void SetVirtualWidth(int width);

    // If visible, then this is pointer to ibPropertyGrid.
    // This shall *never* be null to indicate that this state is not visible.
    ibPropertyGrid*             m_pPropGrid;

    // Pointer to currently used array.
    ibPGProperty*               m_properties;

    // Array for categoric mode.
    ibPGRootProperty            m_regularArray;

    // Array for root of non-categoric mode.
    ibPGRootProperty*           m_abcArray;

    // Dictionary for name-based access.
    std::unordered_map<wxString, ibPGProperty*> m_dictName;

    // List of column widths (first column does not include margin).
    std::vector<int>            m_colWidths;

    // List of indices of columns the user can edit by clicking it.
    std::set<unsigned int>      m_editableColumns;

    // Column proportions.
    std::vector<int>            m_columnProportions;

    double                      m_fSplitterX;

    // Most recently added category.
    ibPGCategoryProperty*         m_currentCategory;

    // Array of selected property.
    ibArrayPGProperty           m_selection;

    // Virtual width.
    int                         m_width;

    // Indicates total virtual height of visible properties.
    unsigned int                m_virtualHeight;

    // True: items appended/inserted, so stuff needs to be done before drawing;
    // If m_virtualHeight == 0, then calcylatey's must be done.
    // Otherwise just sort.
    bool                        m_itemsAdded;

    // True if any value is modified.
    bool                        m_anyModified;

    bool                        m_vhCalcPending;

    // True if splitter has been pre-set by the application.
    bool                        m_isSplitterPreSet;

    // Used to (temporarily) disable splitter centering.
    bool                        m_dontCenterSplitter;

private:
    void InitNonCatMode();
};

// -----------------------------------------------------------------------

#endif // wxUSE_PROPGRID

#endif // _DESKGUI_UNIV_PROPGRID_PROPGRIDPAGESTATE_H_
