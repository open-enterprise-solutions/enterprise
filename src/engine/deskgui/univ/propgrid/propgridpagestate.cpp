// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, src/propgrid/propgridpagestate.cpp): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/propgrid/propgridpagestate.cpp
// Purpose:     ibPropertyGridPageState class
// Author:      Jaakko Salli
// Created:     2008-08-24
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"


#if wxUSE_PROPGRID

#ifndef WX_PRECOMP
    #include "wx/bitmap.h"
    #include "wx/event.h"
    #include "wx/font.h"
    #include "wx/log.h"
    #include "wx/validate.h"
#endif

#include "deskgui/univ/propgrid/propgridpagestate.h"
#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/private.h"

#include <numeric>

#define ibPG_DEFAULT_SPLITTERX      110

// -----------------------------------------------------------------------
// ibPropertyGridIterator
// -----------------------------------------------------------------------

void ibPropertyGridIteratorBase::Init( ibPropertyGridPageState* state, int flags, ibPGProperty* property, int dir  )
{
    wxASSERT( dir == 1 || dir == -1 );

    m_state = state;
    m_baseParent = state->DoGetRoot();
    if ( !property && m_baseParent->HasAnyChild() )
        property = m_baseParent->Item(0);

    m_property = property;

    ibPGCreateIteratorMasks(flags, m_itemExMask, m_parentExMask);

    // Need to skip first?
    if ( property && property->HasFlag(m_itemExMask) )
    {
        if ( dir == 1 )
            Next();
        else
            Prev();
    }
}

void ibPropertyGridIteratorBase::Init( ibPropertyGridPageState* state, int flags, int startPos, int dir  )
{
    ibPGProperty* property = nullptr;

    if ( startPos == wxTOP )
    {
        if ( dir == 0 )
            dir = 1;
    }
    else if ( startPos == wxBOTTOM )
    {
        property = state->GetLastItem(flags);
        if ( dir == 0 )
            dir = -1;
    }
    else
    {
        wxFAIL_MSG("Only supported starting positions are wxTOP and wxBOTTOM");
    }

    Init( state, flags, property, dir );
}

void ibPropertyGridIteratorBase::Assign( const ibPropertyGridIteratorBase& it )
{
    m_property = it.m_property;
    m_state = it.m_state;
    m_baseParent = it.m_baseParent;
    m_itemExMask = it.m_itemExMask;
    m_parentExMask = it.m_parentExMask;
}

void ibPropertyGridIteratorBase::Prev()
{
    ibPGProperty* property = m_property;
    if ( !property )
        return;

    ibPGProperty* parent = property->GetParent();
    wxASSERT( parent );
    unsigned int index = property->GetIndexInParent();

    if ( index > 0 )
    {
        // Previous sibling
        index--;

        property = parent->Item(index);

        // Go to last child if children of property should be iterated through)
        if ( property->HasAnyChild() && !property->HasFlag(m_parentExMask) )
        {
            // First child
            property = property->Last();
        }
    }
    else
    {
        // Up to a parent
        if ( parent == m_baseParent )
        {
            m_property = nullptr;
            return;
        }
        else
        {
            property = parent;
        }
    }

    m_property = property;

    // If property does not match our criteria, skip it
    if ( property->HasFlag(m_itemExMask) )
        Prev();
}

void ibPropertyGridIteratorBase::Next( bool iterateChildren )
{
    ibPGProperty* property = m_property;
    if ( !property )
        return;

    // Go to first child if children of property should be iterated through
    if ( property->HasAnyChild() && !property->HasFlag(m_parentExMask) &&
         iterateChildren )
    {
        // First child
        property = property->Item(0);
    }
    else
    {
        ibPGProperty* parent = property->GetParent();
        wxASSERT( parent );
        unsigned int index = property->GetIndexInParent() + 1;

        if ( index < parent->GetChildCount() )
        {
            // Next sibling
            property = parent->Item(index);
        }
        else
        {
            // Next sibling of parent
            if ( parent == m_baseParent )
            {
                m_property = nullptr;
            }
            else
            {
                m_property = parent;
                Next(false);
            }
            return;
        }
    }

    m_property = property;

    // If property does not match our criteria, skip it
    if ( property->HasFlag(m_itemExMask) )
        Next();
}

// -----------------------------------------------------------------------
// ibPropertyGridPageState
// -----------------------------------------------------------------------

ibPropertyGridPageState::ibPropertyGridPageState()
    : m_pPropGrid(nullptr)
    , m_properties(&m_regularArray)
    , m_abcArray(nullptr)
    , m_colWidths{ibPG_DEFAULT_SPLITTERX, ibPG_DEFAULT_SPLITTERX}
    , m_editableColumns{1} // By default, we only have the 'value' column editable
    , m_columnProportions{1, 1}
    , m_fSplitterX(ibPG_DEFAULT_SPLITTERX)
    , m_currentCategory(nullptr)
    , m_width(0)
    , m_virtualHeight(0)
    , m_itemsAdded(false)
    , m_anyModified(false)
    , m_vhCalcPending(false)
    , m_isSplitterPreSet(false)
    , m_dontCenterSplitter(false)
{
    m_regularArray.SetParentState(this);
}

// -----------------------------------------------------------------------

ibPropertyGridPageState::~ibPropertyGridPageState()
{
    delete m_abcArray;
}

// -----------------------------------------------------------------------

void ibPropertyGridPageState::InitNonCatMode()
{
    // Initialize and populate array holding properties in alphabetic (non-categoric)
    // mode only once.
    if (m_abcArray)
        return;

    m_abcArray = new ibPGRootProperty(wxS("<Root_NonCat>"));
    m_abcArray->SetParentState(this);
    m_abcArray->SetFlag(ibPGFlags::ChildrenAreCopies);

    // Must be called when state::m_properties still points to regularArray.
    ibPGProperty* oldProperties = m_properties;

    // Must use temp value in state::m_properties for item iteration loop
    // to run as expected.
    m_properties = &m_regularArray;

    if ( m_properties->HasAnyChild() )
    {
        //
        // Prepare m_abcArray
        ibPropertyGridIterator it( this, ibPG_ITERATE_PROPERTIES );

        for ( ; !it.AtEnd(); it.Next() )
        {
            ibPGProperty* p = it.GetProperty();
            ibPGProperty* parent = p->GetParent();
            if ( parent->IsCategory() || parent->IsRoot() )
            {
                m_abcArray->DoAddChild(p);
                // Restore parent modified in AddChild()
                p->m_parent = parent;
            }
        }
    }

    m_properties = oldProperties;
}

// -----------------------------------------------------------------------

void ibPropertyGridPageState::DoClear()
{
    if ( m_pPropGrid && IsDisplayed() )
    {
        m_pPropGrid->ClearSelection(false);
    }
    else
    {
        m_selection.clear();
    }

    // If handling ibPG event then every property item must be
    // deleted individually (and with deferral).
    if ( m_pPropGrid && m_pPropGrid->m_processedEvent )
    {
        for (unsigned int i = 0; i < m_regularArray.GetChildCount(); i++)
        {
            ibPGProperty* p = m_regularArray.Item(i);
            DoDelete(p, true);
        }
    }
    else
    {
        // Properties which will be deleted immediately
        // should be removed from the lists of pending deletions.
        for (unsigned int i = 0; i < m_regularArray.GetChildCount(); i++)
        {
            ibPGProperty* p = m_regularArray.Item(i);
            m_pPropGrid->m_deletedProperties.erase(p);
            m_pPropGrid->m_removedProperties.erase(p);
        }

        m_regularArray.Empty();
        if ( m_abcArray )
            m_abcArray->Empty();

        m_dictName.clear();

        // We need to clear parent grid's m_propHover
        if ( m_pPropGrid )
            m_pPropGrid->m_propHover = nullptr;

        m_currentCategory = nullptr;
        m_itemsAdded = false;

        m_virtualHeight = 0;
        m_vhCalcPending = false;
    }
}

// -----------------------------------------------------------------------

void ibPropertyGridPageState::CalculateFontAndBitmapStuff( int WXUNUSED(vspacing) )
{
    ibPropertyGrid* propGrid = GetGrid();

    VirtualHeightChanged();

    // Recalculate caption text extents.
    for ( unsigned int i = 0; i < m_regularArray.GetChildCount();i++ )
    {
        ibPGProperty* p =m_regularArray.Item(i);

        if ( p->IsCategory() )
            ((ibPGCategoryProperty*)p)->CalculateTextExtent(propGrid, propGrid->GetCaptionFont());
    }
}

// -----------------------------------------------------------------------

void ibPropertyGridPageState::SetVirtualWidth( int width )
{
    // Sometimes width less than 0 is offered. Let's make things easy for
    // everybody and deal with it here.
    if ( width < 0 )
        width = 0;

    ibPropertyGrid* pg = GetGrid();
    int gw = pg->GetClientSize().x;
    if ( width < gw )
        width = gw;

    m_width = width;
}

// -----------------------------------------------------------------------

void ibPropertyGridPageState::OnClientWidthChange( int newWidth, int widthChange, bool fromOnResize )
{
    ibPropertyGrid* pg = GetGrid();

    if ( pg->HasVirtualWidth() )
    {
        if ( m_width < newWidth )
            SetVirtualWidth( newWidth );

        CheckColumnWidths(widthChange);
    }
    else
    {
        SetVirtualWidth( newWidth );

        // This should be done before splitter auto centering
        // NOTE: Splitter auto-centering is done in this function.
        if ( !fromOnResize )
            widthChange = 0;
        CheckColumnWidths(widthChange);

        if ( !m_isSplitterPreSet )
        {
            if ( m_dontCenterSplitter )
            {
                SetSplitterLeft( false );
                m_isSplitterPreSet = false;
            }
            else
            {
                DoSetSplitter( newWidth / 2, 0, ibPGSplitterPositionFlags::FromAutoCenter );
            }
        }
    }

    if ( IsDisplayed() )
    {
        pg->SendEvent(ibEVT_PG_COLS_RESIZED, ibNullProperty);
    }
}

// -----------------------------------------------------------------------
// ibPropertyGridPageState item iteration methods
// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridPageState::GetLastItem( int flags )
{
    if ( !m_properties->HasAnyChild() )
        return nullptr;

    ibPGFlags itemExMask;
    ibPGFlags parentExMask;
    ibPGCreateIteratorMasks(flags, itemExMask, parentExMask);

    // First, get last child of last parent if children of 'pwc' should be iterated through
    ibPGProperty* pwc = m_properties->Last();
    while ( pwc->HasAnyChild() && !pwc->HasFlag(parentExMask) )
        pwc = pwc->Last();

    // Then, if it doesn't fit our criteria, back up until we find something that does
    if ( pwc->HasFlag(itemExMask) )
    {
        ibPropertyGridIterator it( this, flags, pwc );
        for ( ; !it.AtEnd(); it.Prev() )
            ;
        pwc = it.GetProperty();
    }

    return pwc;
}

ibPGCategoryProperty* ibPropertyGridPageState::GetPropertyCategory( const ibPGProperty* p ) const
{
    const ibPGProperty* parent = p;
    const ibPGProperty* grandparent = parent->GetParent();
    do
    {
        parent = grandparent;
        grandparent = parent->GetParent();
        if ( parent->IsCategory() && grandparent )
            return const_cast<ibPGCategoryProperty*>(static_cast<const ibPGCategoryProperty*>(parent));
    } while ( grandparent );

    return nullptr;
}

// -----------------------------------------------------------------------
// ibPropertyGridPageState GetPropertyXXX methods
// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridPageState::BaseGetPropertyByLabel
                        ( const wxString& label, const ibPGProperty* parent ) const
{
    if ( !parent )
    {
        parent = &m_regularArray;
    }

    for ( size_t i=0; i<parent->GetChildCount(); i++ )
    {
        ibPGProperty* p = parent->Item(i);
        if ( p->GetLabel() == label )
            return p;
        // Check children recursively.
        if ( p->HasAnyChild() )
        {
            p = BaseGetPropertyByLabel(label, p);
            if ( p )
                return p;
        }
    }

    return nullptr;
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridPageState::BaseGetPropertyByName( const wxString& name ) const
{
    auto it = m_dictName.find(name);
    return it != m_dictName.end() ? it->second : nullptr;
}

// -----------------------------------------------------------------------

void ibPropertyGridPageState::DoSetPropertyName( ibPGProperty* p,
                                                 const wxString& newName )
{
    wxCHECK_RET( p, wxS("invalid property id") );

    ibPGProperty* parent = p->GetParent();

    if ( parent->IsCategory() || parent->IsRoot() )
    {
        if ( !p->GetBaseName().empty() )
            m_dictName.erase( p->GetBaseName() );
        if ( !newName.empty() )
            m_dictName[newName] = p;
    }

    p->DoSetName(newName);
}

// -----------------------------------------------------------------------
// ibPropertyGridPageState global operations
// -----------------------------------------------------------------------

bool ibPropertyGridPageState::EnableCategories( bool enable )
{
    //
    // NB: We can't use ibPropertyGridIterator in this
    //     function, since it depends on m_arrIndexes,
    //     which, among other things, is being fixed here.
    //

    if ( enable )
    {
        // Enable categories
        if ( !IsInNonCatMode() )
            return false;

        m_properties = &m_regularArray;
    }
    else
    {
        // Disable categories
        if ( IsInNonCatMode() )
            return false;

        // Create array, if necessary.
        if ( !m_abcArray )
            InitNonCatMode();

        m_properties = m_abcArray;
    }

    // fix parents, indexes, and depths
    ibPGProperty* parent = m_properties;
    unsigned int i = 0;
    do
    {
        unsigned int iMax = parent->GetChildCount();
        while ( i < iMax )
        {
            ibPGProperty* p = parent->Item(i);

            p->m_arrIndex = i;
            p->m_parent = parent;
            if ( enable )
            {
                // If parent was category, and this is not,
                // then the depth stays the same.
                if ( parent->IsCategory() &&
                    !p->IsCategory() )
                    p->m_depth = parent->GetDepth();
                else
                    p->m_depth = parent->GetDepth() + 1;
            }
            else
            {
                p->m_depth = parent->GetDepth() + 1;
            }

            if ( p->HasAnyChild() )
            {
                i = 0;
                parent = p;
                iMax = parent->GetChildCount();
            }
            else
            {
                i++;
            }
        }

        i = parent->GetIndexInParent() + 1;
        parent = parent->GetParent();
    } while ( parent != nullptr );

    VirtualHeightChanged();

    if ( IsDisplayed() )
        m_pPropGrid->RecalculateVirtualSize();

    return true;
}

// -----------------------------------------------------------------------
static bool ibPG_SortFunc_ByFunction(ibPGProperty* p1, ibPGProperty* p2)
{
    ibPropertyGrid* pg = p1->GetGrid();
    ibPGSortCallback sortFunction = pg->GetSortFunction();
    return sortFunction(pg, p1, p2) < 0;
}

static bool ibPG_SortFunc_ByLabel(ibPGProperty* p1, ibPGProperty* p2)
{
    return p1->GetLabel().CmpNoCase( p2->GetLabel() ) < 0;
}

void ibPropertyGridPageState::DoSortChildren(ibPGProperty* p, ibPGPropertyValuesFlags flags)
{
    if ( !p )
        p = m_properties;

    // Can only sort items with children
    if ( !p->HasAnyChild() )
        return;

    // Never sort children of aggregate properties
    if ( p->HasFlag(ibPGFlags::Aggregate) )
        return;

    if ( !!(flags & ibPGPropertyValuesFlags::SortTopLevelOnly)
         && !p->IsCategory() && !p->IsRoot() )
        return;

    if ( GetGrid()->GetSortFunction() )
        p->SortChildren(ibPG_SortFunc_ByFunction);
    else
        p->SortChildren(ibPG_SortFunc_ByLabel);

    // Fix indices
    p->FixIndicesOfChildren();

    if ( !!(flags & ibPGPropertyValuesFlags::Recurse) )
    {
        // Apply sort recursively
        for ( unsigned int i=0; i<p->GetChildCount(); i++ )
            DoSortChildren(p->Item(i), flags);
    }
}

// -----------------------------------------------------------------------

void ibPropertyGridPageState::DoSort(ibPGPropertyValuesFlags flags)
{
    DoSortChildren( m_properties, flags | ibPGPropertyValuesFlags::Recurse );

    // We used to sort categories as well here also if in non-categorized
    // mode, but doing would naturally cause child indices to become
    // corrupted.
}

// -----------------------------------------------------------------------

bool ibPropertyGridPageState::PrepareAfterItemsAdded()
{
    if ( !m_itemsAdded ) return false;

    ibPropertyGrid* pg = GetGrid();

    m_itemsAdded = false;

    if ( pg->HasFlag(ibPG_AUTO_SORT) )
        DoSort(ibPGPropertyValuesFlags::SortTopLevelOnly);

    return true;
}

// -----------------------------------------------------------------------
// ibPropertyGridPageState splitter, column and hittest functions
// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridPageState::DoGetItemAtY( int y ) const
{
    // Outside?
    if ( y < 0 )
        return nullptr;

    unsigned int a = 0;
    return m_properties->GetItemAtY(y, GetGrid()->GetRowHeight(), &a);
}

// -----------------------------------------------------------------------

ibPropertyGridHitTestResult
ibPropertyGridPageState::HitTest( const wxPoint&pt ) const
{
    ibPropertyGridHitTestResult result;
    result.m_column = HitTestH( pt.x, &result.m_splitter,
                                &result.m_splitterHitOffset );
    result.m_property = DoGetItemAtY( pt.y );
    return result;
}

// -----------------------------------------------------------------------

// Used by SetSplitterLeft() and DotFitColumns()

int ibPropertyGridPageState::GetColumnFitWidth(const ibPGProperty* p, unsigned int col, bool subProps) const
{
    const ibPropertyGrid* pg = GetGrid();
    int maxW = 0;

    for ( unsigned int i = 0; i < p->GetChildCount(); i++ )
    {
        int w;
        ibPGProperty* pc = p->Item(i);
        if ( !pc->IsCategory() )
        {
            wxString text;
            pc->GetDisplayInfo(col, -1, 0, &text, (ibPGCell*)nullptr);
            pg->GetTextExtent(text, &w, nullptr);
            if ( col == 0 )
                w += ((pc->GetDepth() - 1) * pg->m_subgroup_extramargin);

            // account for the bitmap
            if ( col == 1 )
                w += pc->GetImageOffset(pg->GetImageRect(pc, -1).GetWidth());

            w += (ibPG_XBEFORETEXT * 2);

            if ( w > maxW )
                maxW = w;
        }

        if ( pc->HasAnyChild() && (subProps || pc->IsCategory()) )
        {
            w = GetColumnFitWidth(pc, col, subProps);

            if ( w > maxW )
                maxW = w;
        }
    }

    return maxW;
}

int ibPropertyGridPageState::GetColumnFullWidth(ibPGProperty* p, unsigned int col) const
{
    if ( p->IsCategory() )
        return 0;

    wxString text;
    p->GetDisplayInfo(col, -1, 0, &text, (ibPGCell*)nullptr);
    int w = GetGrid()->GetTextExtent(text).x;

    if ( col == 0 )
        w += p->GetDepth() * GetGrid()->m_subgroup_extramargin;

    // account for the bitmap
    if ( col == 1 )
        w += p->GetImageOffset(GetGrid()->GetImageRect(p, -1).GetWidth());

    w += (ibPG_XBEFORETEXT * 2);
    return w;
}

int ibPropertyGridPageState::DoGetSplitterPosition( int splitterColumn ) const
{
    int n = GetGrid()->GetMarginWidth();
    for ( int i = 0; i <= splitterColumn; i++ )
        n += m_colWidths[i];
    return n;
}

int ibPropertyGridPageState::GetColumnMinWidth( int WXUNUSED(column) ) const
{
    return m_pPropGrid->FromDIP(ibPG_DRAG_MARGIN);
}

void ibPropertyGridPageState::PropagateColSizeDec( int column,
                                                   int decrease,
                                                   int dir )
{
    wxASSERT( decrease >= 0 );
    wxASSERT( dir == 1 || dir == -1 );

    int col = column;
    while(decrease > 0 && col >= 0 && col < (int)m_colWidths.size())
    {
        const int origWidth = m_colWidths[col];
        const int min = GetColumnMinWidth(col);
        m_colWidths[col] -= decrease;
        if ( m_colWidths[col] < min )
        {
            m_colWidths[col] = min;
        }
        decrease -= (origWidth - m_colWidths[col]);
        col += dir;
    }
    // As a last resort, if change of width was not fully absorbed
    // on the requested side we try to do this on the other side.
    col = column;
    dir *= -1;
    while(decrease > 0 && col >= 0 && col < (int)m_colWidths.size())
    {
        const int origWidth = m_colWidths[col];
        const int min = GetColumnMinWidth(col);
        m_colWidths[col] -= decrease;
        if ( m_colWidths[col] < min )
        {
            m_colWidths[col] = min;
        }
        decrease -= (origWidth - m_colWidths[col]);
        col += dir;
    }

    wxASSERT( decrease == 0 );
}

void ibPropertyGridPageState::DoSetSplitter(int newXPos, int splitterColumn,
                                            ibPGSplitterPositionFlags flags )
{
    int adjust = newXPos - DoGetSplitterPosition(splitterColumn);
    int otherColumn = splitterColumn + 1;
    if ( otherColumn == (int)m_colWidths.size() )
        otherColumn = 0;
    if ( adjust > 0 )
    {
        m_colWidths[splitterColumn] += adjust;
        PropagateColSizeDec( otherColumn, adjust, 1 );
    }
    else if ( adjust < 0 )
    {
        m_colWidths[otherColumn] -= adjust;
        PropagateColSizeDec( splitterColumn, -adjust, -1 );
    }
    // Actual adjustment can be different from demanded.
    newXPos = DoGetSplitterPosition(splitterColumn);

    if ( splitterColumn == 0 )
        m_fSplitterX = (double) newXPos;

    if ( !(flags & ibPGSplitterPositionFlags::FromAutoCenter) )
    {
        // Don't allow initial splitter auto-positioning after this.
        m_isSplitterPreSet = true;

        CheckColumnWidths();
    }
}

// Moves splitter so that all labels are visible, but just.
void ibPropertyGridPageState::SetSplitterLeft( bool subProps )
{
    ibPropertyGrid* pg = GetGrid();

    int maxW = GetColumnFitWidth(m_properties, 0, subProps);

    if ( maxW > 0 )
    {
        maxW += pg->GetMarginWidth();
        DoSetSplitter( maxW );
    }

    m_dontCenterSplitter = true;
}

wxSize ibPropertyGridPageState::DoFitColumns( bool WXUNUSED(allowGridResize) )
{
    ibPropertyGrid* pg = GetGrid();

    int marginWidth = pg->GetMarginWidth();
    int accWid = marginWidth;
    int maxColWidth = 500;

    for ( unsigned int col=0; col < GetColumnCount(); col++ )
    {
        int fitWid = GetColumnFitWidth(m_properties, col, true);
        int colMinWidth = GetColumnMinWidth(col);
        if ( fitWid < colMinWidth )
            fitWid = colMinWidth;
        else if ( fitWid > maxColWidth )
            fitWid = maxColWidth;

        m_colWidths[col] = fitWid;

        accWid += fitWid;
    }

    // Expand last one to fill the width
    int remaining = m_width - accWid;
    m_colWidths.back() += remaining;

    m_dontCenterSplitter = true;

    int firstSplitterX = marginWidth + m_colWidths[0];
    m_fSplitterX = (double) firstSplitterX;

    // Don't allow initial splitter auto-positioning after this.
    if ( IsDisplayed() )
    {
        pg->SetSplitterPosition(firstSplitterX, false);
        pg->Refresh();
    }

    int x, y;
    pg->GetVirtualSize(&x, &y);

    return wxSize(accWid, y);
}

void ibPropertyGridPageState::CheckColumnWidths( int widthChange )
{
    if ( m_width == 0 )
        return;

    ibPropertyGrid* pg = GetGrid();

    int clientWidth = pg->GetClientSize().x;

    wxLogTrace("propgrid",
               wxS("ColumnWidthCheck (virtualWidth: %i, clientWidth: %i)"),
               m_width, clientWidth);

    int colsWidth = std::accumulate(m_colWidths.begin(), m_colWidths.end(), pg->GetMarginWidth());

    wxLogTrace("propgrid",
               wxS("  HasVirtualWidth: %i  colsWidth: %i"),
               (int)pg->HasVirtualWidth(), colsWidth);

    // Then mode-based requirement
    if ( !pg->HasVirtualWidth() )
    {
        m_width = clientWidth;

        int widthHigher = m_width - colsWidth;

        // Adapt colsWidth to width
        if ( colsWidth < m_width )
        {
            // Increase column
            wxLogTrace("propgrid",
                       wxS("  Adjust last column to %i"),
                       m_colWidths.back() + widthHigher);
            m_colWidths.back() += widthHigher;
        }
        else if ( colsWidth > m_width )
        {
            widthHigher = -widthHigher;
            // Always reduce the last column that is larger than minimum size
            // (looks nicer, even with auto-centering enabled).
            for (int reduceCol = (int)m_colWidths.size() - 1; reduceCol >= 0 && widthHigher > 0; reduceCol--)
            {
                // Reduce column, if possible.
                if ( m_colWidths[reduceCol] > GetColumnMinWidth(reduceCol) )
                {
                    int d = wxMin(m_colWidths[reduceCol] - GetColumnMinWidth(reduceCol), widthHigher);
                    wxLogTrace("propgrid", wxS("  Reduce column %i (by %i)"), reduceCol, d);

                    m_colWidths[reduceCol] -= d;
                    colsWidth -= d;
                    widthHigher -= d;
                }
            }
            m_width = colsWidth;
        }
    }
    else
    {
        // Only adapt colsWidth to width
        if ( colsWidth < m_width )
        {
            m_colWidths.back() += (m_width-colsWidth);
        }

        // If width changed, recalculate virtual size
        if ( IsDisplayed() )
            pg->RecalculateVirtualSize();
    }

    for (size_t i=0; i<m_colWidths.size(); i++)
    {
        wxLogTrace("propgrid", wxS("col%zu: %i"), i, m_colWidths[i]);
    }

    // Auto center splitter
    if ( !m_dontCenterSplitter )
    {
        if ( m_colWidths.size() == 2 &&
             m_columnProportions[0] == m_columnProportions[1] )
        {
            //
            // When we have two columns of equal proportion, then use this
            // code. It will look nicer when the scrollbar visibility is
            // toggled on and off.
            //
            // TODO: Adapt this to generic recenter code.
            //
            double centerX = pg->m_width / 2.0;
            double splitterX;

            if ( m_fSplitterX < 0.0 )
            {
                splitterX = centerX;
            }
            else if ( widthChange )
            {
                //float centerX = float(pg->GetSize().x) * 0.5;

                // Recenter?
                splitterX = m_fSplitterX + (widthChange * 0.5);
                double deviation = fabs(centerX - splitterX);

                // If deviating from center, adjust towards it
                if ( deviation > 20.0 )
                {
                    if ( splitterX > centerX)
                        splitterX -= 2;
                    else
                        splitterX += 2;
                }
            }
            else
            {
                // No width change, just keep sure we keep splitter position intact
                splitterX = m_fSplitterX;
                double deviation = fabs(centerX - splitterX);
                if ( deviation > 50.0 )
                {
                    splitterX = centerX;
                }
            }

            DoSetSplitter((int)splitterX, 0,
                          ibPGSplitterPositionFlags::FromAutoCenter);

            m_fSplitterX = splitterX; // needed to retain accuracy
        }
        else
        {
            //
            // Generic re-center code
            //
            ResetColumnSizes(ibPGSplitterPositionFlags::FromAutoCenter);
        }
    }
}

void ibPropertyGridPageState::ResetColumnSizes(ibPGSplitterPositionFlags setSplitterFlags)
{
    // Calculate sum of proportions
    int psum = std::accumulate(m_columnProportions.begin(), m_columnProportions.end(), 0);
    int puwid = (m_pPropGrid->m_width*256) / psum;
    int cpos = 0;

    // Convert proportion to splitter positions
    for (size_t i=0; i<(m_colWidths.size() - 1); i++)
    {
        int cwid = (puwid*m_columnProportions[i]) / 256;
        cpos += cwid;
        DoSetSplitter(cpos, (int)i, setSplitterFlags);
    }
}

void ibPropertyGridPageState::SetColumnCount( int colCount )
{
    wxASSERT( colCount >= 2 );
    m_colWidths.resize(colCount, m_pPropGrid->FromDIP(ibPG_DRAG_MARGIN));
    m_columnProportions.resize(colCount, 1);

    CheckColumnWidths();
    if ( IsDisplayed() )
        m_pPropGrid->RecalculateVirtualSize();
}

void ibPropertyGridPageState::DoSetColumnProportion( unsigned int column,
                                                 int proportion )
{
    wxASSERT_MSG( proportion >= 1,
                  wxS("Column proportion must 1 or higher") );

    if ( proportion < 1 )
        proportion = 1;

    while ( m_columnProportions.size() <= column )
        m_columnProportions.push_back(1);

    m_columnProportions[column] = proportion;
}

// Returns column index, -1 for margin
int ibPropertyGridPageState::HitTestH( int x, int* pSplitterHit, int* pSplitterHitOffset ) const
{
    int cx = GetGrid()->GetMarginWidth();
    int col = -1;
    int prevSplitter = -1;

    while ( x > cx )
    {
        col++;
        if ( col >= (int)m_colWidths.size() )
        {
            *pSplitterHit = -1;
            return col;
        }
        prevSplitter = cx;
        cx += m_colWidths[col];
    }

    // Near prev. splitter
    if ( col >= 1 )
    {
        int diff = x - prevSplitter;
        if ( abs(diff) < ibPG_SPLITTERX_DETECTMARGIN1 )
        {
            *pSplitterHit = col - 1;
            *pSplitterHitOffset = diff;
            return col;
        }
    }

    // Near next splitter
    int nextSplitter = cx;
    if ( col < (int)(m_colWidths.size()-1) )
    {
        int diff = x - nextSplitter;
        if ( abs(diff) < ibPG_SPLITTERX_DETECTMARGIN1 )
        {
            *pSplitterHit = col;
            *pSplitterHitOffset = diff;
            return col;
        }
    }

    *pSplitterHit = -1;
    return col;
}

bool ibPropertyGridPageState::ArePropertiesAdjacent( ibPGProperty* prop1,
                                                     ibPGProperty* prop2,
                                                     int iterFlags ) const
{
    const ibPGProperty* ap1 =
        ibPropertyGridConstIterator::OneStep(this,
                                             iterFlags,
                                             prop1,
                                             1);
    if ( ap1 && ap1 == prop2 )
        return true;

    const ibPGProperty* ap2 =
        ibPropertyGridConstIterator::OneStep(this,
                                             iterFlags,
                                             prop1,
                                             -1);
    if ( ap2 && ap2 == prop2 )
        return true;

    return false;
}

// -----------------------------------------------------------------------
// ibPropertyGridPageState property value setting and getting
// -----------------------------------------------------------------------

bool ibPropertyGridPageState::DoSetPropertyValueString( ibPGProperty* p, const wxString& value )
{
    if ( p )
    {
        constexpr ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::ReportError|ibPGPropValFormatFlags::FullValue|ibPGPropValFormatFlags::ProgrammaticValue;

        ibVariant variant = p->GetValueRef();

        if ( p->StringToValue(variant, value, flags) )
        {
            p->SetValue(variant);
            if ( p == m_pPropGrid->GetSelection() && IsDisplayed() )
                m_pPropGrid->RefreshEditor();
        }

        return true;
    }
    return false;
}

// -----------------------------------------------------------------------

bool ibPropertyGridPageState::DoSetPropertyValue( ibPGProperty* p, ibVariant& value )
{
    if ( p )
    {
        p->SetValue(value);
        if ( p == m_pPropGrid->GetSelection() && IsDisplayed() )
            m_pPropGrid->RefreshEditor();

        return true;
    }
    return false;
}

// -----------------------------------------------------------------------

// -----------------------------------------------------------------------
// ibPropertyGridPageState property operations
// -----------------------------------------------------------------------

bool ibPropertyGridPageState::DoIsPropertySelected( ibPGProperty* prop ) const
{
    return m_selection.Index(prop) != wxNOT_FOUND;
}

// -----------------------------------------------------------------------

void ibPropertyGridPageState::DoRemoveFromSelection( ibPGProperty* prop )
{
    for ( unsigned int i=0; i<m_selection.size(); i++ )
    {
        if ( m_selection[i] == prop )
        {
            ibPropertyGrid* pg = m_pPropGrid;
            if ( i == 0 && IsDisplayed() )
            {
                // If first item (i.e. one with the active editor) was
                // deselected, then we need to take some extra measures.
                ibArrayPGProperty sel(m_selection.begin() + 1, m_selection.end());

                ibPGProperty* newFirst = sel.empty()? nullptr: sel[0];

                pg->DoSelectProperty(newFirst,
                                     ibPGSelectPropertyFlags::DontSendEvent);

                m_selection = sel;

                pg->Refresh();
            }
            else
            {
                m_selection.erase( m_selection.begin() + i );
            }
            return;
        }
    }
}

// -----------------------------------------------------------------------

bool ibPropertyGridPageState::DoCollapse( ibPGProperty* p )
{
    wxCHECK_MSG( p, false, wxS("invalid property id") );

    if ( !p->HasAnyChild() ) return false;

    if ( !p->IsExpanded() ) return false;

    p->SetExpanded(false);

    VirtualHeightChanged();

    return true;
}

// -----------------------------------------------------------------------

bool ibPropertyGridPageState::DoExpand( ibPGProperty* p )
{
    wxCHECK_MSG( p, false, wxS("invalid property id") );

    if ( !p->HasAnyChild() ) return false;

    if ( p->IsExpanded() ) return false;

    p->SetExpanded(true);

    VirtualHeightChanged();

    return true;
}

// -----------------------------------------------------------------------

bool ibPropertyGridPageState::DoSelectProperty( ibPGProperty* p, ibPGSelectPropertyFlags flags )
{
    if ( IsDisplayed() )
        return m_pPropGrid->DoSelectProperty( p, flags );

    DoSetSelection(p);
    return true;
}

// -----------------------------------------------------------------------

bool ibPropertyGridPageState::DoHideProperty(ibPGProperty* p, bool hide, ibPGPropertyValuesFlags flags)
{
    p->DoHide(hide, flags);
    VirtualHeightChanged();

    return true;
}

// -----------------------------------------------------------------------
// ibPropertyGridPageState ibVariant related routines
// -----------------------------------------------------------------------

// Returns list of ibVariant objects (non-categories and non-sub-properties only).
// Never includes sub-properties (unless they are parented by ibParentProperty).
ibVariant ibPropertyGridPageState::DoGetPropertyValues(const wxString& WXUNUSED(listname),
                                                    ibPGProperty* baseparent,
                                                    ibPGPropertyValuesFlags flags) const
{
    ibPGProperty* pwc = baseparent;

    // Root is the default base-parent.
    if ( !pwc )
        pwc = m_properties;

    // the list's name (listname) is the name of its entry where it is appended - in a list above it (below) or
    // in a caller's: wx's list carried it in itself
    ibPGVariantList* const v = new ibPGVariantList();
    const ibVariant list(v);

    if ( pwc->HasAnyChild() )
    {
        if ( !!(flags & ibPGPropertyValuesFlags::KeepStructure) )
        {
            wxASSERT( !pwc->HasFlag(ibPGFlags::Aggregate) );

            for ( unsigned int i = 0; i < pwc->GetChildCount(); i++ )
            {
                ibPGProperty* p = pwc->Item(i);
                if ( !p->HasAnyChild() || p->HasFlag(ibPGFlags::Aggregate) )
                {
                    v->Append( p->GetBaseName(), p->GetValue() );
                }
                else
                {
                    v->Append( p->GetBaseName(),
                               DoGetPropertyValues(p->GetBaseName(),p,flags|ibPGPropertyValuesFlags::KeepStructure) );
                }
                if ( !!(flags & ibPGPropertyValuesFlags::IncAttributes) && p->GetAttributes().GetCount() )
                    v->Append( wxString::Format(wxS("@%s@attr"), p->GetBaseName()), p->GetAttributesAsList() );
            }
        }
        else
        {
            ibPropertyGridConstIterator it( this, ibPG_ITERATE_DEFAULT, pwc->Item(0) );
            it.SetBaseParent( pwc );

            for ( ; !it.AtEnd(); it.Next() )
            {
                const ibPGProperty* p = it.GetProperty();

                // Use a trick to ignore ibParentProperty itself, but not its sub-properties.
                if ( !p->HasAnyChild() || p->HasFlag(ibPGFlags::Aggregate) )
                {
                    v->Append( p->GetName(), p->GetValue() );
                    if ( !!(flags & ibPGPropertyValuesFlags::IncAttributes) && p->GetAttributes().GetCount() )
                        v->Append( wxString::Format(wxS("@%s@attr"), p->GetBaseName()), p->GetAttributesAsList() );
                }
            }
        }
    }

    return list;
}

// -----------------------------------------------------------------------

void ibPropertyGridPageState::DoSetPropertyValues( const ibPGVariantList& list, ibPGProperty* defaultCategory )
{
    bool origFrozen = true;

    if ( IsDisplayed() )
    {
        origFrozen = m_pPropGrid->IsFrozen();
        if ( !origFrozen ) m_pPropGrid->Freeze();
    }

    const auto asCategory = [](ibPGProperty* p) -> ibPGCategoryProperty*
    {
        return p && p->IsCategory() ? static_cast<ibPGCategoryProperty*>(p) : nullptr;
    };

    ibPGCategoryProperty* use_category = asCategory(defaultCategory);

    if ( !use_category )
        use_category = asCategory(m_properties);

    // Let's iterate over the list of variants.
    int numSpecialEntries = 0;

    //
    // Second pass for special entries
    for ( const ibPGVariantList::Entry& current : list )
    {
        const wxString& name = current.m_name;
        if ( !name.empty() )
        {
            //
            // '@' signified a special entry
            if ( name[0] == wxS('@') )
            {
                numSpecialEntries++;
            }
            else
            {
                const ibPGVariantList* currentList = current.m_value.ConvertToType<ibPGVariantList>();
                ibPGProperty* foundProp = BaseGetPropertyByName(name);
                if ( foundProp )
                {
                    ibPGProperty* p = foundProp;

                    // If it was a list, we still have to go through it.
                    if ( currentList != nullptr )
                    {
                        DoSetPropertyValues( *currentList,
                                p->IsCategory()?p:(nullptr)
                            );
                    }
                    else
                    {
                        wxASSERT_LEVEL_2_MSG(
                            current.m_value.GetClassName() == p->GetValue().GetClassName(),
                            wxString::Format(
                                wxS("setting value of property \"%s\" from variant"),
                                p->GetName())
                        );

                        p->SetValue(current.m_value);
                    }
                }
                else
                {
                    // Is it list?
                    if ( currentList == nullptr )
                    {
                        // Not.
                    }
                    else
                    {
                        // Yes, it is; create a sub category and append contents there.
                        ibPGProperty* newCat = DoInsert(use_category,-1,new ibPGCategoryProperty(name,ibPG_LABEL));
                        DoSetPropertyValues( *currentList, newCat );
                    }
                }
            }
        }
    }

    if ( numSpecialEntries )
    {
        for ( const ibPGVariantList::Entry& current : list )
        {
            const wxString& name = current.m_name;
            if ( !name.empty() )
            {
                //
                // '@' signified a special entry
                if ( name[0] == wxS('@') )
                {
                    numSpecialEntries--;

                    size_t pos2 = name.rfind(wxS('@'));
                    if ( pos2 > 0 && pos2 < (name.size()-1) )
                    {
                        wxString propName = name.substr(1, pos2-1);
                        wxString entryType = name.substr(pos2+1, wxString::npos);

                        if ( entryType == wxS("attr") )
                        {
                            //
                            // List of attributes
                            ibPGProperty* foundProp = BaseGetPropertyByName(propName);
                            if ( foundProp )
                            {
                                const ibPGVariantList* list2 = current.m_value.ConvertToType<ibPGVariantList>();
                                wxASSERT( list2 != nullptr );

                                if ( list2 )
                                {
                                    for ( const ibPGVariantList::Entry& attr : *list2 )
                                    {
                                        foundProp->SetAttribute( attr.m_name, attr.m_value );
                                    }
                                }
                            }
                            else
                            {
                                // ERROR: No such property: 'propName'
                            }
                        }
                    }
                    else
                    {
                        // ERROR: Special entry requires name of format @<propname>@<entrytype>
                    }
                }
            }

            if ( !numSpecialEntries )
                break;
        }
    }

    if ( !origFrozen )
    {
        m_pPropGrid->Thaw();

        if ( IsDisplayed() )
            m_pPropGrid->RefreshEditor();
    }

}

// -----------------------------------------------------------------------
// ibPropertyGridPageState property adding and removal
// -----------------------------------------------------------------------

bool ibPropertyGridPageState::PrepareToAddItem( ibPGProperty* property,
                                                ibPGProperty* scheduledParent )
{
    ibPropertyGrid* propGrid = m_pPropGrid;

    // This will allow better behaviour.
    if ( scheduledParent == m_properties )
        scheduledParent = nullptr;

    if ( scheduledParent && !scheduledParent->IsCategory() )
    {
        wxASSERT_MSG( !property->GetBaseName().empty(),
                      wxS("Property's children must have unique, non-empty names within their scope") );
    }

    property->m_parentState = this;

    if ( property->IsCategory() )
    {

        // Parent of a category must be either root or another category
        // (otherwise Bad Things might happen).
        wxASSERT_MSG( scheduledParent == nullptr ||
                      scheduledParent == m_properties ||
                      scheduledParent->IsCategory(),
                 wxS("Parent of a category must be either root or another category."));

        // If we already have category with same name, delete given property
        // and use it instead as most recent caption item.
        ibPGProperty* found_id = BaseGetPropertyByName( property->GetBaseName() );
        if ( found_id )
        {
            ibPGCategoryProperty* pwc = (ibPGCategoryProperty*) found_id;
            if ( pwc->IsCategory() ) // Must be a category.
            {
                delete property;
                m_currentCategory = pwc;
                return false;
            }
        }
    }

#if wxDEBUG_LEVEL
    // Warn for identical names in debug mode.
    if ( BaseGetPropertyByName(property->GetName()) &&
         (!scheduledParent || scheduledParent->IsCategory()) )
    {
        wxFAIL_MSG(wxString::Format(
            wxS("ibPropertyGrid item with name \"%s\" already exists"),
            property->GetName()));

        ibPGGlobalVars->m_warnings++;
    }
#endif // wxDEBUG_LEVEL

    // null parent == root parent
    if ( !scheduledParent )
        scheduledParent = DoGetRoot();

    property->m_parent = scheduledParent;

    property->InitAfterAdded(this, propGrid);

    if ( property->IsCategory() )
    {
        ibPGCategoryProperty* pc = wxStaticCast(property, ibPGCategoryProperty);

        m_currentCategory = pc;

        // Calculate text extent for category caption
        if ( propGrid )
            pc->CalculateTextExtent(propGrid, propGrid->GetCaptionFont());
    }

    return true;
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridPageState::DoAppend( ibPGProperty* property )
{
    ibPGCategoryProperty* cur_cat = property->IsCategory() ? nullptr : m_currentCategory;

    return DoInsert( cur_cat, -1, property );
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridPageState::DoInsert( ibPGProperty* parent, int index, ibPGProperty* property )
{
    if ( !parent )
        parent = m_properties;

    wxCHECK_MSG( !parent->HasFlag(ibPGFlags::Aggregate),
                 ibNullProperty,
                 wxS("when adding properties to fixed parents, use BeginAddChildren and EndAddChildren.") );

    bool res = PrepareToAddItem( property, parent );

    // PrepareToAddItem() may just decide to use current category
    // instead of adding new one.
    if ( !res )
        return m_currentCategory;

    bool parentIsRoot = parent->IsRoot();
    bool parentIsCategory = parent->IsCategory();

    // Note that item must be added into current mode later.

    // If parent is ibParentProperty, just stick it in...
    // If parent is root (m_properties), then...
    //   In categoric mode: Add as last item in m_abcArray (if not category).
    //                      Add to given index in m_regularArray.
    //   In non-cat mode:   Add as last item in m_regularArray.
    //                      Add to given index in m_abcArray.
    // If parent is category, then...
    //   1) Add to given category in given index.
    //   2) Add as last item in m_abcArray.

    if ( m_properties == &m_regularArray )
    {
        // We are currently in Categorized mode

        // Only add non-categories to m_abcArray.
        if ( m_abcArray && !property->IsCategory() &&
             (parentIsCategory || parentIsRoot) )
        {
            m_abcArray->DoAddChild( property, -1, false );
        }

        // Add to current mode.
        parent->DoAddChild( property, index, true );
    }
    else
    {
        // We are currently in Non-categorized/Alphabetic mode

        if ( parentIsCategory )
            // Parent is category.
            parent->DoAddChild( property, index, false );
        else if ( parentIsRoot )
            // Parent is root.
            m_regularArray.DoAddChild( property, -1, false );
        else
            parent->DoAddChild(property, index, true);

        // Add to current mode
        if ( !property->IsCategory() && (parentIsCategory || parentIsRoot) )
            m_abcArray->DoAddChild( property, index, true );
    }

    // Only add name to hashmap if parent is root or category
    if ( !property->GetBaseName().empty() &&
        (parentIsCategory || parentIsRoot) )
        m_dictName[property->GetBaseName()] = property;

    VirtualHeightChanged();

    // Update values of all parents if they are containers of composed values.
    property->UpdateParentValues();

    // Update editor controls of all parents if they are containers of composed values.
    for( ibPGProperty *p = property->GetParent();
         p && !p->IsRoot() && !p->IsCategory() && p->HasFlag(ibPGFlags::ComposedValue);
         p = p->GetParent() )
    {
        p->RefreshEditor();
    }

    m_itemsAdded = true;

    return property;
}

// -----------------------------------------------------------------------

void ibPropertyGridPageState::DoRemoveChildrenFromSelection(ibPGProperty* p,
                                                            bool recursive,
                                                            ibPGSelectPropertyFlags selFlags)
{
    ibPropertyGrid* pg = GetGrid();

    for( unsigned int i = 0; i < p->GetChildCount(); i++ )
    {
        ibPGProperty* child = p->Item(i);
        if ( DoIsPropertySelected(child) )
        {
            if ( pg && IsDisplayed() )
            {
                pg->DoRemoveFromSelection(child, selFlags);
            }
            else
            {
                DoRemoveFromSelection(child);
            }
        }

        if ( recursive )
        {
            DoRemoveChildrenFromSelection(child, recursive, selFlags);
        }
    }
}

void ibPropertyGridPageState::DoMarkChildrenAsDeleted(ibPGProperty* p,
                                                      bool recursive)
{
    for( unsigned int i = 0; i < p->GetChildCount(); i++ )
    {
        ibPGProperty* child = p->Item(i);

        child->SetFlag(ibPGFlags::BeingDeleted);

        if ( recursive )
        {
            DoMarkChildrenAsDeleted(child, recursive);
        }
    }
}

void ibPropertyGridPageState::DoInvalidatePropertyName(ibPGProperty* p)
{
    // Let's trust that no sane property uses prefix like
    // this. It would be anyway fairly inconvenient (in
    // current code) to check whether a new name is used
    // by another property with parent (due to the child
    // name notation).
    wxString newName = wxS("_&/_%$") + p->GetBaseName();
    DoSetPropertyName(p, newName);
}

void ibPropertyGridPageState::DoInvalidateChildrenNames(ibPGProperty* p,
                                                        bool recursive)
{
    if (p->IsCategory())
    {
        for( unsigned int i = 0; i < p->GetChildCount(); i++ )
        {
            ibPGProperty* child = p->Item(i);
            DoInvalidatePropertyName(child);

            if ( recursive )
            {
                DoInvalidateChildrenNames(child, recursive);
            }
        }
    }
}

bool ibPropertyGridPageState::IsChildCategory(ibPGProperty* p,
                                              ibPGCategoryProperty* cat,
                                              bool recursive)
{
    if (p->IsCategory())
    {
        for( unsigned int i = 0; i < p->GetChildCount(); i++ )
        {
            ibPGProperty* child = p->Item(i);

            if (child->IsCategory() && child == cat)
            {
                return true;
            }

            if ( recursive && IsChildCategory(child, cat, recursive) )
            {
                return true;
            }
        }
    }

    return false;
}

namespace
{
static ibPGProperty* FindCategoryForProperty(ibPGProperty* cat, ibPGProperty* prop, int& foundAtIndex)
{
    wxASSERT(cat->IsCategory() || cat->IsRoot());

    int idx = cat->Index(prop);
    if ( idx != wxNOT_FOUND )
    {
        foundAtIndex = idx;
        return cat;
    }

    for ( unsigned int i = 0; i < cat->GetChildCount(); i++ )
    {
        ibPGProperty* p = cat->Item(i);
        if ( p->IsCategory() )
        {
            ibPGProperty* foundCat = FindCategoryForProperty(p, prop, foundAtIndex);
            if ( foundCat )
            {
                return foundCat;
            }
        }
    }

    return nullptr;
}
};

void ibPropertyGridPageState::DoDelete( ibPGProperty* item, bool doDelete )
{
    wxCHECK_RET(item != &m_regularArray && item != m_abcArray,
        wxS("ibPropertyGrid: Do not attempt to remove the root item."));

    wxCHECK_RET( item->GetParent(),
        wxS("ibPropertyGrid: This property was already deleted.") );

    ibPGProperty* parent = item->GetParent();

    wxCHECK_RET( !parent->HasFlag(ibPGFlags::Aggregate),
        wxS("ibPropertyGrid: Do not attempt to remove sub-properties.") );

    wxASSERT( item->GetParentState() == this );

    ibPropertyGrid* pg = GetGrid();

    // Try to unselect property and its sub-properties.
    if ( DoIsPropertySelected(item) )
    {
        if ( pg && IsDisplayed() )
        {
            pg->DoRemoveFromSelection(item,
                ibPGSelectPropertyFlags::Deleting|ibPGSelectPropertyFlags::NoValidate);
        }
        else
        {
            DoRemoveFromSelection(item);
        }
    }

    if ( item->IsChildSelected(true) )
    {
        DoRemoveChildrenFromSelection(item, true,
            ibPGSelectPropertyFlags::Deleting|ibPGSelectPropertyFlags::NoValidate);
    }

    // If deleted category or its sub-category is
    // a current category then reset current category marker.
    if ( item->IsCategory() )
    {
        if (item == m_currentCategory || IsChildCategory(item, m_currentCategory, true))
        {
            m_currentCategory = nullptr;
        }
    }

    // Must defer deletion? Yes, if handling a ibPG event.
    if ( pg && pg->m_processedEvent )
    {
        // Prevent adding duplicates to the lists.
        if ( doDelete )
        {
            pg->m_deletedProperties.insert(item);
        }
        else
        {
            pg->m_removedProperties.insert(item);
        }

        // Rename the property and its children so it won't remain in the way
        // of the user code.
        DoInvalidatePropertyName(item);
        DoInvalidateChildrenNames(item, true);

        return;
    }

    // Property has to be unselected prior deleting.
    // Otherwise crash can happen.
    wxASSERT_MSG( !DoIsPropertySelected(item) && !item->IsChildSelected(true),
                  wxS("Failed to unselect deleted property") );
    // Don't attempt to delete current category.
    wxASSERT_MSG( !item->IsCategory() || item != m_currentCategory,
                  wxS("Current category cannot be deleted") );

    // Prevent property and its children from being re-selected
    item->SetFlag(ibPGFlags::BeingDeleted);
    DoMarkChildrenAsDeleted(item, true);

    unsigned int indinparent = item->GetIndexInParent();

    // Delete children
    if ( item->HasAnyChild() && !item->HasFlag(ibPGFlags::Aggregate) )
    {
        // deleting a category
        item->DeleteChildren();
    }

    if ( !IsInNonCatMode() )
    {
        // categorized mode - non-categorized array

        // Remove from non-cat array
        if ( !item->IsCategory() &&
             (parent->IsCategory() || parent->IsRoot()) )
        {
            if ( m_abcArray )
                m_abcArray->RemoveChild(item);
        }

        // categorized mode - categorized array
        parent->RemoveChild(indinparent);
        parent->FixIndicesOfChildren();
    }
    else
    {
        // non-categorized mode - categorized array

        // We need to find location of item.
        int cat_index;
        ibPGProperty* cat_parent = FindCategoryForProperty(&m_regularArray, item, cat_index);
        if ( cat_parent )
        {
            cat_parent->RemoveChild(cat_index);
        }

        // non-categorized mode - non-categorized array
        if ( !item->IsCategory() )
        {
            parent->RemoveChild(indinparent);
            parent->FixIndicesOfChildren(indinparent);
        }
    }

    if ( !item->GetBaseName().empty() &&
         (parent->IsCategory() || parent->IsRoot()) )
        m_dictName.erase(item->GetBaseName());

    // We need to clear parent grid's m_propHover, if it matches item
    if ( pg && pg->m_propHover == item )
        pg->m_propHover = nullptr;

    // Mark the property as 'unattached'
    item->m_parentState = nullptr;
    item->m_parent = nullptr;

    // We can actually delete it now
    if ( doDelete )
    {
        // Remove the item from both lists of pending operations.
        // (Deleted item cannot be also the subject of further removal.)
        pg->m_deletedProperties.erase(item);
        pg->m_removedProperties.erase(item);
        delete item;
    }
    else
    {
        // Remove the item from the list of pending removals.
        pg->m_removedProperties.erase(item);
        item->OnDetached(this, pg);
    }

    m_itemsAdded = true; // Not a logical assignment (but required nonetheless).

    VirtualHeightChanged();
}

bool ibPropertyGridPageState::IsDisplayed() const
{
    return (this == m_pPropGrid->GetState());
}

unsigned int ibPropertyGridPageState::GetActualVirtualHeight() const
{
    return DoGetRoot()->GetChildrenHeight(GetGrid()->GetRowHeight());
}

// -----------------------------------------------------------------------

#endif  // wxUSE_PROPGRID
