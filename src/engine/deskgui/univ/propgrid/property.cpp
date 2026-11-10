// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, src/propgrid/property.cpp): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/propgrid/property.cpp
// Purpose:     ibPGProperty and related support classes
// Author:      Jaakko Salli
// Created:     2008-08-23
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"


#if wxUSE_PROPGRID

#ifndef WX_PRECOMP
    #include "wx/dc.h"
    #include "wx/log.h"
#endif

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/property.h"
#include "deskgui/univ/propgrid/props.h"
#include "deskgui/univ/propgrid/editors.h"
#include "deskgui/univ/propgrid/private.h"

#include "deskgui/univ/theme.h"          // ibThemeEngine — the focus rectangle: the theme's renderer
#include "deskgui/univ/renderer.h"

#include <array>

#define PWC_CHILD_SUMMARY_LIMIT         16 // Maximum number of children summarized in a parent property's
                                           // value field.

#define PWC_CHILD_SUMMARY_CHAR_LIMIT    64 // Character limit of summary field when not editing

// -----------------------------------------------------------------------

static void ibPGDrawFocusRect(wxWindow *win, wxDC& dc,
                              int x, int y, int w, int h)
{
    wxRect rect(x, y+((h-dc.GetCharHeight())/2), w, h);
    wxASSERT_MSG( win, wxS("Invalid window to be drawn") );
    ibThemeEngine::Get()->GetRenderer()->DrawFocusRect(win, dc, rect);
}

// -----------------------------------------------------------------------
// ibPGCellRenderer
// -----------------------------------------------------------------------

wxSize ibPGCellRenderer::GetImageSize( const ibPGProperty* WXUNUSED(property),
                                       int WXUNUSED(column),
                                       int WXUNUSED(item) ) const
{
     return wxSize(0, 0);
}

void ibPGCellRenderer::DrawText( wxDC& dc, const wxRect& rect,
                                 int xOffset, const wxString& text ) const
{
    dc.DrawText( text,
                 rect.x+xOffset+ibPG_XBEFORETEXT,
                 rect.y+((rect.height-dc.GetCharHeight())/2) );
}

void ibPGCellRenderer::DrawEditorValue( wxDC& dc, const wxRect& rect,
                                        int xOffset, const wxString& text,
                                        ibPGProperty* property,
                                        const ibPGEditor* editor ) const
{
    int yOffset = ((rect.height-dc.GetCharHeight())/2);

    if ( editor )
    {
        wxRect rect2(rect);
        rect2.Offset(xOffset, yOffset);
        rect2.height -= yOffset * 2;
        editor->DrawValue( dc, rect2, property, text );
    }
    else
    {
        dc.DrawText( text,
                     rect.x+xOffset+ibPG_XBEFORETEXT,
                     rect.y+yOffset );
    }
}

void ibPGCellRenderer::DrawCaptionSelectionRect(wxWindow* win, wxDC& dc,
                                                int x, int y, int w, int h) const
{
    ibPGDrawFocusRect(win, dc, x, y, w, h);
}

int ibPGCellRenderer::PreDrawCell( wxDC& dc, const wxRect& rect, const ibPropertyGrid* propGrid, const ibPGCell& cell, int flags ) const
{
    int imageWidth = 0;

    // If possible, use cell colours
    if ( !(flags & DontUseCellBgCol) )
    {
        const wxColour& bgCol = cell.GetBgCol();
        dc.SetPen(bgCol);
        dc.SetBrush(bgCol);
    }

    if ( !(flags & DontUseCellFgCol) )
    {
        dc.SetTextForeground(cell.GetFgCol());
    }

    // Draw Background, but only if not rendering in control
    // (as control already has rendered correct background).
    if ( !(flags & (Control|ChoicePopup)) )
        dc.DrawRectangle(rect);

    // Use cell font, if provided
    const wxFont& font = cell.GetFont();
    if ( font.IsOk() )
        dc.SetFont(font);

    wxBitmap bmp = cell.GetBitmap().GetBitmapFor(propGrid);
    if ( bmp.IsOk() )
    {
        int hMax = rect.height - ibPG_CUSTOM_IMAGE_SPACINGY;
        int yOfs;
        if ( bmp.GetHeight() <= hMax )
        {
            yOfs = (hMax - bmp.GetHeight()) / 2;
        }
        else
        {
            int w = wxRound(bmp.GetWidth() * (double)hMax / bmp.GetHeight());
            wxBitmap::Rescale(bmp, wxSize(w, hMax));
            yOfs = 0;
        }

        dc.DrawBitmap( bmp,
                       rect.x + ibPG_CONTROL_MARGIN + ibCC_CUSTOM_IMAGE_MARGIN1,
                       rect.y + ibPG_CUSTOM_IMAGE_SPACINGY + yOfs,
                       true );
        imageWidth = bmp.GetWidth();
    }

    return imageWidth;
}

void ibPGCellRenderer::PostDrawCell( wxDC& dc,
                                     const ibPropertyGrid* propGrid,
                                     const ibPGCell& cell,
                                     int WXUNUSED(flags) ) const
{
    // Revert font
    const wxFont& font = cell.GetFont();
    if ( font.IsOk() )
        dc.SetFont(propGrid->GetFont());
}

// -----------------------------------------------------------------------
// ibPGDefaultRenderer
// -----------------------------------------------------------------------

bool ibPGDefaultRenderer::Render( wxDC& dc, const wxRect& rect,
                                  const ibPropertyGrid* propertyGrid, ibPGProperty* property,
                                  int column, int item, int flags ) const
{
    const ibPGEditor* editor = nullptr;

    wxString text;
    bool isUnspecified = property->IsValueUnspecified();
    int selItem = item;

    if ( column == 1 && item == -1 )
    {
        int cmnVal = property->GetCommonValue();
        if ( cmnVal >= 0 )
        {
            // Common Value
            if ( !isUnspecified )
            {
                text = propertyGrid->GetCommonValueLabel(cmnVal);
                DrawText( dc, rect, 0, text );
                if ( !text.empty() )
                    return true;
            }
            return false;
        }
        // We need to know the current selection to override default
        // cell settings (colours, etc.) with custom cell settings
        // which can be defined separately for any single choice item.
        selItem = property->GetChoiceSelection();
    }

    int imageWidth = 0;
    int preDrawFlags = flags;
    bool res = false;
    ibPGCell cell;

    property->GetDisplayInfo(column, selItem, flags, &text, &cell);

    // Property image takes precedence over cell image
    if ( column == 1 && !isUnspecified && property->GetValueImage() )
        cell.SetBitmap(wxBitmapBundle());

    imageWidth = PreDrawCell( dc, rect, propertyGrid, cell, preDrawFlags );

    if ( column == 1 )
    {
        editor = property->GetColumnEditor(column);

        if ( !isUnspecified )
        {
            // Regular property value

            wxSize imageSize = propertyGrid->GetImageSize(property, item);

            ibPGPaintData paintdata;
            paintdata.m_parent = propertyGrid;
            paintdata.m_choiceItem = item;

            if ( imageSize.x > 0 )
            {
                wxRect imageRect(rect.x + ibPG_CONTROL_MARGIN + ibCC_CUSTOM_IMAGE_MARGIN1,
                                 rect.y+ibPG_CUSTOM_IMAGE_SPACINGY,
                                 ibPG_CUSTOM_IMAGE_WIDTH,
                                 rect.height-(ibPG_CUSTOM_IMAGE_SPACINGY*2));

                dc.SetPen( wxPen(propertyGrid->GetCellTextColour(), 1, wxPENSTYLE_SOLID) );

                paintdata.m_drawnWidth = imageSize.x;
                paintdata.m_drawnHeight = imageSize.y;

                property->OnCustomPaint( dc, imageRect, paintdata );

                imageWidth = paintdata.m_drawnWidth;
            }

            text = property->GetValueAsString();

            // Add units string?
            if ( propertyGrid->GetColumnCount() <= 2 )
            {
                wxString unitsString = property->GetAttribute(ibPG_ATTR_UNITS, wxString());
                if ( !unitsString.empty() )
                    text = wxString::Format(wxS("%s %s"), text, unitsString );
            }
        }

        if ( text.empty() )
        {
            text = property->GetHintText();
            if ( !text.empty() )
            {
                res = true;

                const wxColour& hCol =
                    propertyGrid->GetCellDisabledTextColour();
                dc.SetTextForeground(hCol);

                // Must make the editor nullptr to override its own rendering
                // code.
                editor = nullptr;
            }
        }
        else
        {
            res = true;
        }
    }

    int imageOffset = property->GetImageOffset(imageWidth);

    DrawEditorValue( dc, rect, imageOffset, text, property, editor );

    // active caption gets nice dotted rectangle
    if ( property->IsCategory() && column == 0 )
    {
        if ( flags & Selected )
        {
            if ( imageOffset > 0 )
            {
                imageOffset -= DEFAULT_IMAGE_OFFSET_INCREMENT;
                imageOffset += ibCC_CUSTOM_IMAGE_MARGIN2 + 4;
            }

            const wxRect rectCaption( rect.x+ibPG_XBEFORETEXT-ibPG_CAPRECTXMARGIN+imageOffset,
                                      rect.y-ibPG_CAPRECTYMARGIN+1,
                                      ((ibPGCategoryProperty*)property)->GetTextExtent(propertyGrid,
                                                                                     propertyGrid->GetCaptionFont())
                                      +(ibPG_CAPRECTXMARGIN*2),
                                      propertyGrid->GetFontHeight()+(ibPG_CAPRECTYMARGIN*2) );

            DrawCaptionSelectionRect( const_cast<ibPropertyGrid*>(propertyGrid), dc,
                                      rectCaption.x, rectCaption.y,
                                      rectCaption.width, rectCaption.height );
        }
    }

    PostDrawCell(dc, propertyGrid, cell, preDrawFlags);

    return res;
}

wxSize ibPGDefaultRenderer::GetImageSize( const ibPGProperty* property,
                                          int column,
                                          int item ) const
{
    if ( property && column == 1 )
    {
        if ( item == -1 )
        {
            wxBitmap* bmp = property->GetValueImage();

            if ( bmp && bmp->IsOk() )
                return bmp->GetSize();
        }
    }
    return wxSize(0,0);
}

// -----------------------------------------------------------------------
// ibPGCellData
// -----------------------------------------------------------------------

ibPGCellData::ibPGCellData()
    : wxObjectRefData()
    , m_hasValidText(false)
{
}

// -----------------------------------------------------------------------
// ibPGCell
// -----------------------------------------------------------------------

ibPGCell::ibPGCell( const wxString& text,
                    const wxBitmapBundle& bitmap,
                    const wxColour& fgCol,
                    const wxColour& bgCol )
    : wxObject()
{
    ibPGCellData* data = new ibPGCellData();
    m_refData = data;
    data->m_text = text;
    data->m_bitmapBundle = bitmap;
    data->m_fgCol = fgCol;
    data->m_bgCol = bgCol;
    data->m_hasValidText = true;
}

wxObjectRefData *ibPGCell::CloneRefData( const wxObjectRefData *data ) const
{
    ibPGCellData* c = new ibPGCellData();
    const ibPGCellData* o = (const ibPGCellData*) data;
    c->m_text = o->m_text;
    c->m_bitmapBundle = o->m_bitmapBundle;
    c->m_fgCol = o->m_fgCol;
    c->m_bgCol = o->m_bgCol;
    c->m_hasValidText = o->m_hasValidText;
    return c;
}

void ibPGCell::SetText( const wxString& text )
{
    AllocExclusive();

    GetData()->SetText(text);
}

void ibPGCell::SetBitmap( const wxBitmapBundle& bitmap )
{
    AllocExclusive();

    GetData()->SetBitmap(bitmap);
}

void ibPGCell::SetFgCol( const wxColour& col )
{
    AllocExclusive();

    GetData()->SetFgCol(col);
}

void ibPGCell::SetFont( const wxFont& font )
{
    AllocExclusive();

    GetData()->SetFont(font);
}

void ibPGCell::SetBgCol( const wxColour& col )
{
    AllocExclusive();

    GetData()->SetBgCol(col);
}

void ibPGCell::MergeFrom( const ibPGCell& srcCell )
{
    AllocExclusive();

    ibPGCellData* data = GetData();

    if ( srcCell.HasText() )
        data->SetText(srcCell.GetText());

    if ( srcCell.GetFgCol().IsOk() )
        data->SetFgCol(srcCell.GetFgCol());

    if ( srcCell.GetBgCol().IsOk() )
        data->SetBgCol(srcCell.GetBgCol());

    if ( srcCell.GetBitmap().IsOk() )
        data->SetBitmap(srcCell.GetBitmap());
}

void ibPGCell::SetEmptyData()
{
    AllocExclusive();
}

// -----------------------------------------------------------------------
// ibPGChoiceEntry
// -----------------------------------------------------------------------

ibPGChoiceEntry::ibPGChoiceEntry()
    : ibPGCell()
    , m_value(ibPG_INVALID_VALUE)
{
}

ibPGChoiceEntry::ibPGChoiceEntry(const ibPGChoiceEntry& other)
    : ibPGCell(other)
    , m_value(other.m_value)
{
}

ibPGChoiceEntry::ibPGChoiceEntry(const wxString& label, int value)
    : ibPGCell()
    , m_value(value)
{
    SetText(label);
}

ibPGChoiceEntry& ibPGChoiceEntry::operator=(const ibPGChoiceEntry& other)
{
    if ( this != &other )
    {
        Ref(other);
    }
    m_value = other.m_value;
    return *this;
}

// -----------------------------------------------------------------------
// ibPGChoicesData
// -----------------------------------------------------------------------

ibPGChoicesData::~ibPGChoicesData()
{
    Clear();
}

void ibPGChoicesData::Clear()
{
    m_items.clear();
}

void ibPGChoicesData::CopyDataFrom(ibPGChoicesData* data)
{
    wxASSERT(m_items.empty());

    m_items = data->m_items;
}

ibPGChoiceEntry& ibPGChoicesData::Insert(int index,
    const ibPGChoiceEntry& item)
{
    std::vector<ibPGChoiceEntry>::iterator it;
    if ( index == -1 )
    {
        it = m_items.end();
        index = (int)m_items.size();
    }
    else
    {
        it = m_items.begin() + index;
    }

    m_items.insert(it, item);

    ibPGChoiceEntry& ownEntry = m_items[index];

    // Need to fix value?
    if ( ownEntry.GetValue() == ibPG_INVALID_VALUE )
        ownEntry.SetValue(index);

    return ownEntry;
}

// -----------------------------------------------------------------------
// ibPGProperty
// -----------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibPGProperty, wxObject);

const wxString ibPGProperty::sm_labelItem("@!");

void ibPGProperty::Init()
{
    m_commonValue = -1;
    m_arrIndex = 0xFFFF;
    m_parent = nullptr;

    m_parentState = nullptr;

    m_clientData = nullptr;
    m_clientObject = nullptr;

    m_customEditor = nullptr;
#if wxUSE_VALIDATORS
    m_validator = nullptr;
#endif

    m_maxLen = 0; // infinite maximum length

    m_flags = ibPGFlags::Property;

    m_depth = 1;

    SetExpanded(true);
}


void ibPGProperty::Init( const wxString& label, const wxString& name )
{
    if ( label != ibPG_LABEL )
        m_label = label;

    if ( name != ibPG_LABEL )
        DoSetName( name );
    else
        DoSetName( m_label );

    Init();
}

void ibPGProperty::InitAfterAdded( ibPropertyGridPageState* pageState,
                                   ibPropertyGrid* propgrid )
{
    //
    // Called after property has been added to grid or page
    // (so propgrid can be null, too).

    ibPGProperty* parent = m_parent;
    bool parentIsRoot = wxDynamicCast(parent, ibPGRootProperty) != nullptr;

    //
    // Convert invalid cells to default ones in this grid
    for ( ibPGCell& cell : m_cells )
    {
        if ( cell.IsInvalid() )
        {
            cell = IsCategory() ? propgrid->GetCategoryDefaultCell()
                                : propgrid->GetPropertyDefaultCell();
        }
    }

    m_parentState = pageState;

    if ( !parentIsRoot && !parent->IsCategory() )
    {
        m_cells = parent->m_cells;
    }

    // If in hideable adding mode, or if assigned parent is hideable, then
    // make this one hideable.
    if (
         ( !parentIsRoot && parent->HasFlag(ibPGFlags::Hidden) ) ||
         ( propgrid && (propgrid->HasInternalFlag(ibPropertyGrid::ibPG_FL_ADDING_HIDEABLES)) )
       )
        SetFlag(ibPGFlags::Hidden);

    // Set custom image flag.
    int custImgHeight = OnMeasureImage().y;
    if ( custImgHeight == wxDefaultCoord )
    {
        SetFlag(ibPGFlags::CustomImage);
    }

    if ( propgrid && (propgrid->HasFlag(ibPG_LIMITED_EDITING)) )
        SetFlag(ibPGFlags::NoEditor);

    // Make sure parent has some parental flags
    if ( !parent->HasFlag(ibPGFlags::ParentalFlags) )
        parent->SetParentalType(ibPGFlags::MiscParent);

    if ( !IsCategory() )
    {
        // This is not a category.

        // Depth.
        //
        unsigned char depth = 1;
        if ( !parentIsRoot )
        {
            depth = parent->GetDepth();
            if ( !parent->IsCategory() )
                depth++;
        }
        m_depth = depth;
        unsigned char greyDepth = depth;

        if ( !parentIsRoot )
        {
            ibPGCategoryProperty* pc;

            if ( parent->IsCategory() )
                pc = (ibPGCategoryProperty* ) parent;
            else
                // This conditional compile is necessary to
                // bypass some compiler bug.
                pc = pageState->GetPropertyCategory(parent);

            if ( pc )
                greyDepth = pc->GetDepth();
            else
                greyDepth = parent->m_depthBgCol;
        }

        m_depthBgCol = greyDepth;
    }
    else
    {
        // This is a category.

        // depth
        unsigned char depth = 1;
        if ( !parentIsRoot )
        {
            depth = parent->GetDepth() + 1;
        }
        m_depth = depth;
        m_depthBgCol = depth;
    }

    //
    // Has initial children
    if ( HasAnyChild() )
    {
        // Check parental flags
        wxASSERT_MSG( ((m_flags & ibPGFlags::ParentalFlags) ==
                            ibPGFlags::Aggregate) ||
                      ((m_flags & ibPGFlags::ParentalFlags) ==
                            ibPGFlags::MiscParent),
                      wxS("ibPGProperty parental flags set incorrectly at ")
                      wxS("this time") );

        if ( HasFlag(ibPGFlags::Aggregate) )
        {
            // Properties with private children are not expanded by default.
            SetExpanded(false);
        }
        else if ( propgrid && propgrid->HasFlag(ibPG_HIDE_MARGIN) )
        {
            // ...unless it cannot be expanded by user and therefore must
            // remain visible at all times
            SetExpanded(true);
        }

        //
        // Prepare children recursively
        for ( ibPGProperty* child : m_children )
        {
            child->InitAfterAdded(pageState, pageState->GetGrid());
        }

        if ( propgrid && propgrid->HasExtraStyle(ibPG_EX_AUTO_UNSPECIFIED_VALUES) )
            SetFlagRecursively(ibPGFlags::AutoUnspecified, true);
    }
}

void ibPGProperty::OnDetached(ibPropertyGridPageState* WXUNUSED(state),
                              ibPropertyGrid* propgrid)
{
    if ( propgrid )
    {
        const ibPGCell& propDefCell = propgrid->GetPropertyDefaultCell();
        const ibPGCell& catDefCell = propgrid->GetCategoryDefaultCell();

        // Make default cells invalid
        for ( ibPGCell& cell : m_cells )
        {
            if ( cell.IsSameAs(propDefCell) ||
                 cell.IsSameAs(catDefCell) )
            {
                cell.UnRef();
            }
        }
    }
}

ibPGProperty::ibPGProperty()
    : wxObject()
{
    Init();
}


ibPGProperty::ibPGProperty( const wxString& label, const wxString& name )
    : wxObject()
{
    Init( label, name );
}


ibPGProperty::~ibPGProperty()
{
    delete m_clientObject;

    Empty();  // this deletes items

#if wxUSE_VALIDATORS
    delete m_validator;
#endif

    // This makes it easier for us to detect dangling pointers
    m_parent = nullptr;
}


bool ibPGProperty::IsSomeParent( ibPGProperty* candidate ) const
{
    ibPGProperty* parent = m_parent;
    do
    {
        if ( parent == candidate )
            return true;
        parent = parent->m_parent;
    } while ( parent );
    return false;
}

void ibPGProperty::SetLabel(const wxString& label)
{
    m_label = label;
    // Update cell text if possible
    if ( HasCell(0) )
    {
        ibPGCell& cell = GetCell(0);
        if ( cell.HasText() )
        {
            cell.SetText(label);
        }
    }
}

void ibPGProperty::SetName( const wxString& newName )
{
    ibPropertyGrid* pg = GetGrid();

    if ( pg )
        pg->SetPropertyName(this, newName);
    else
        DoSetName(newName);
}

wxString ibPGProperty::GetName() const
{
    ibPGProperty* parent = GetParent();

    if ( m_name.empty() || !parent || parent->IsCategory() || parent->IsRoot() )
        return m_name;

    return m_parent->GetName() + wxS(".") + m_name;
}

ibPropertyGrid* ibPGProperty::GetGrid() const
{
    return m_parentState ? m_parentState->GetGrid() : nullptr;
}

int ibPGProperty::Index( const ibPGProperty* p ) const
{
    auto it = std::find(m_children.begin(), m_children.end(), p);
    return it != m_children.end() ? (int)(it - m_children.begin()) : wxNOT_FOUND;
}

bool ibPGProperty::ValidateValue( ibVariant& WXUNUSED(value), ibPGValidationInfo& WXUNUSED(validationInfo) ) const
{
    return true;
}

void ibPGProperty::OnSetValue()
{
}

void ibPGProperty::RefreshChildren ()
{
}

void ibPGProperty::OnValidationFailure( ibVariant& WXUNUSED(pendingValue) )
{
}

void ibPGProperty::GetDisplayInfo( unsigned int column,
                                   int choiceIndex,
                                   int flags,
                                   wxString* pString,
                                   ibPGCell* pCell )
{
    wxCHECK_RET( GetGrid(),
                 wxS("Cannot obtain display info for detached property") );

    // Get default cell
    ibPGCell cell = GetCell(column);

    if ( !(flags & ibPGCellRenderer::ChoicePopup) )
    {
        // Not painting list of choice popups, so get text from property
        if ( column != 1 || !IsValueUnspecified() || IsCategory() )
        {
            // Override default cell settings with
            // custom settings defined for choice item.
            if (column == 1 && !IsValueUnspecified() && choiceIndex != wxNOT_FOUND)
            {
                const ibPGChoiceEntry& entry = m_choices[choiceIndex];
                cell.MergeFrom(entry);
            }
        }
        else
        {
            // Use special unspecified value cell
            cell.MergeFrom(GetGrid()->GetUnspecifiedValueAppearance());
        }

        if ( cell.HasText() )
        {
            *pString = cell.GetText();
        }
        else
        {
            if ( column == 0 )
                *pString = GetLabel();
            else if ( column == 1 )
                *pString = GetDisplayedString();
            else if ( column == 2 )
                *pString = GetAttribute(ibPG_ATTR_UNITS, wxString());
        }
    }
    else
    {
        wxASSERT( column == 1 );

        if ( choiceIndex != wxNOT_FOUND )
        {
            // Override default cell settings with
            // custom settings defined for choice item.
            const ibPGChoiceEntry& entry = m_choices[choiceIndex];
            cell.MergeFrom(entry);

            *pString = m_choices.GetLabel(choiceIndex);
        }
    }

    wxASSERT_MSG( cell.GetData(),
                  wxString::Format(wxS("Invalid cell for property %s"),
                                   GetName()) );

    // We need to return customized cell object.
    if (pCell)
    {
        *pCell = cell;
    }
}

/*
wxString ibPGProperty::GetColumnText( unsigned int col, int choiceIndex ) const
{

    if ( col != 1 || choiceIndex == wxNOT_FOUND )
    {
        const ibPGCell& cell = GetCell(col);
        if ( cell->HasText() )
        {
            return cell->GetText();
        }
        else
        {
            if ( col == 0 )
                return GetLabel();
            else if ( col == 1 )
                return GetDisplayedString();
            else if ( col == 2 )
                return GetAttribute(ibPG_ATTR_UNITS, wxString());
        }
    }
    else
    {
        // Use choice
        return m_choices.GetLabel(choiceIndex);
    }

    return wxString();
}
*/

void ibPGProperty::DoGenerateComposedValue( wxString& text,
                                            ibPGPropValFormatFlags flags,
                                            const ibPGVariantList* valueOverrides,
                                            ibPGHashMapS2S* childResults ) const
{
    size_t iMax = m_children.size();

    text.clear();
    if ( iMax == 0 )
        return;

    if ( iMax > PWC_CHILD_SUMMARY_LIMIT &&
         !(flags & ibPGPropValFormatFlags::FullValue) )
        iMax = PWC_CHILD_SUMMARY_LIMIT;

    size_t iMaxMinusOne = iMax-1;

    if ( !IsTextEditable() )
        flags |= ibPGPropValFormatFlags::UneditableCompositeFragment;

    ibPGProperty* curChild = m_children[0];

    bool overridesLeft = false;
    wxString overrideName;
    ibVariant overrideValue;
    ibPGVariantList::const_iterator node;

    if ( valueOverrides )
    {
        node = valueOverrides->begin();
        if ( node != valueOverrides->end() )
        {
            overrideName = node->m_name;
            overrideValue = node->m_value;
            overridesLeft = true;
        }
    }

    size_t i;
    for ( i = 0; i < iMax; i++ )
    {
        ibVariant childValue;

        wxString childLabel = curChild->GetLabel();

        // Check for value override
        if ( overridesLeft && overrideName == childLabel )
        {
            if ( overrideValue.GetType() != ibValueTypes::TYPE_EMPTY )
                childValue = overrideValue;
            else
                childValue = curChild->GetValue();
            ++node;
            if ( node != valueOverrides->end() )
            {
                overrideName = node->m_name;
                overrideValue = node->m_value;
            }
            else
                overridesLeft = false;
        }
        else
        {
            childValue = curChild->GetValue();
        }

        wxString s;
        if ( childValue.GetType() != ibValueTypes::TYPE_EMPTY )
        {
            const ibPGVariantList* childList = childValue.ConvertToType<ibPGVariantList>();
            if ( overridesLeft &&
                 curChild->HasFlag(ibPGFlags::ComposedValue) &&
                 childList != nullptr )
            {
                DoGenerateComposedValue(s, flags|ibPGPropValFormatFlags::CompositeFragment,
                                        childList, childResults);
            }
            else
            {
                s = curChild->ValueToString(childValue,
                                            flags|ibPGPropValFormatFlags::CompositeFragment);
            }
        }

        if ( childResults && curChild->HasAnyChild() )
            (*childResults)[curChild->GetName()] = s;

        bool skip = false;
        if ( !!(flags & ibPGPropValFormatFlags::UneditableCompositeFragment) && s.empty() )
            skip = true;

        if ( !curChild->HasAnyChild() || skip )
            text += s;
        else
            text += wxS("[") + s + wxS("]");

        if ( i < iMaxMinusOne )
        {
            if ( text.length() > PWC_CHILD_SUMMARY_CHAR_LIMIT &&
                 !(flags & ibPGPropValFormatFlags::EditableValue) &&
                 !(flags & ibPGPropValFormatFlags::FullValue) )
                break;

            if ( !skip )
            {
                if ( !curChild->HasAnyChild() )
                    text += wxS("; ");
                else
                    text += wxS(" ");
            }

            curChild = m_children[i+1];
        }
    }

    if ( i < m_children.size() )
    {
        if ( !text.EndsWith(wxS("; ")) )
            text += wxS("; ...");
        else
            text += wxS("...");
    }
}

wxString ibPGProperty::ValueToString( ibVariant& WXUNUSED(value),
                                      ibPGPropValFormatFlags flags ) const
{
    wxCHECK_MSG( HasAnyChild(),
                 wxString(),
                 wxS("If user property does not have any children, it must ")
                 wxS("override GetValueAsString") );

    // FIXME: Currently code below only works if value is actually m_value
    wxASSERT_MSG( !!(flags & ibPGPropValFormatFlags::ValueIsCurrent),
                  wxS("Sorry, currently default ibPGProperty::ValueToString() ")
                  wxS("implementation only works if value is m_value.") );

    wxString text;
    DoGenerateComposedValue(text, flags);
    return text;
}

wxString ibPGProperty::GetValueAsString(ibPGPropValFormatFlags flags) const
{
    ibPropertyGrid* pg = GetGrid();
    wxCHECK_MSG( pg, wxString(),
                 wxS("Cannot get valid value for detached property") );

    if ( IsValueUnspecified() )
        return pg->GetUnspecifiedValueText(flags);

    if ( m_commonValue == -1 )
    {
        ibVariant value(GetValue());
        return ValueToString(value, flags|ibPGPropValFormatFlags::ValueIsCurrent);
    }

    //
    // Return common value's string representation
    const ibPGCommonValue* cv = pg->GetCommonValue(m_commonValue);

    if ( !!(flags & ibPGPropValFormatFlags::FullValue) )
    {
        return cv->GetLabel();
    }
    else if ( !!(flags & ibPGPropValFormatFlags::EditableValue) )
    {
        return cv->GetEditableText();
    }
    else
    {
        return cv->GetLabel();
    }
}

bool ibPGProperty::IntToValue( ibVariant& variant, int number, ibPGPropValFormatFlags WXUNUSED(flags) ) const
{
    variant = (long)number;
    return true;
}

// Convert semicolon delimited tokens into child values.
bool ibPGProperty::StringToValue( ibVariant& v, const wxString& text, ibPGPropValFormatFlags flags ) const
{
    if ( !HasAnyChild() )
        return false;

    unsigned int curChild = 0;

    unsigned int iMax = m_children.size();

    if ( iMax > PWC_CHILD_SUMMARY_LIMIT &&
         !(flags & ibPGPropValFormatFlags::FullValue) )
        iMax = PWC_CHILD_SUMMARY_LIMIT;

    bool changed = false;

    wxString token;
    size_t pos = 0;

    // It is best only to add non-empty group items
    bool addOnlyIfNotEmpty = false;
    const wxChar delimiter = wxS(';');

    size_t tokenStart = 0xFFFFFF;

    ibPGVariantList* const list = new ibPGVariantList();
    const ibVariant listValue(list);

    ibPGPropValFormatFlags propagatedFlags = flags & (ibPGPropValFormatFlags::ReportError|ibPGPropValFormatFlags::ProgrammaticValue);

    wxLogTrace("propgrid",
               wxS(">> %s.StringToValue('%s')"), GetLabel(), text);

    wxString::const_iterator it = text.begin();
    wxUniChar a = ( it != text.end() ) ? *it : wxUniChar(0);

    for ( ;; )
    {
        // How many units we iterate string forward at the end of loop?
        // We need to keep track of this or risk going to negative
        // with it-- operation.
        unsigned int strPosIncrement = 1;

        if ( tokenStart != 0xFFFFFF )
        {
            // Token is running
            if ( a == delimiter || a == 0 )
            {
                token = text.substr(tokenStart,pos-tokenStart);
                token.Trim(true);
                size_t len = token.length();

                if ( !addOnlyIfNotEmpty || len > 0 )
                {
                    const ibPGProperty* child = Item(curChild);
                    ibVariant variant(child->GetValue());
                    wxString childName = child->GetBaseName();

                    wxLogTrace("propgrid",
                               wxS("token = '%s', child = %s"),
                               token, childName);

                    // Add only if editable or setting programmatically
                    if ( !!(flags & ibPGPropValFormatFlags::ProgrammaticValue) ||
                         (!child->HasFlag(ibPGFlags::Disabled) &&
                          !child->HasFlag(ibPGFlags::ReadOnly)) )
                    {
                        if ( len > 0 )
                        {
                            if ( child->StringToValue(variant, token,
                                                      propagatedFlags | ibPGPropValFormatFlags::CompositeFragment) )
                            {
                                // the child's name is its entry's, not the
                                // value's (wx's value carried it, and lost it
                                // to an assignment)
                                list->Append(childName, variant);

                                changed = true;
                            }
                        }
                        else
                        {
                            // Empty, becomes unspecified
                            variant.Reset();
                            list->Append(childName, variant);
                            changed = true;
                        }
                    }

                    curChild++;
                    if ( curChild >= iMax )
                        break;
                }

                tokenStart = 0xFFFFFF;
            }
        }
        else
        {
            // Token is not running
            if ( a != wxS(' ') )
            {

                addOnlyIfNotEmpty = false;

                // Is this a group of tokens?
                if ( a == wxS('[') )
                {
                    int depth = 1;

                    if ( it != text.end() ) ++it;
                    pos++;
                    size_t startPos = pos;

                    // Group item - find end
                    while ( it != text.end() && depth > 0 )
                    {
                        a = *it;
                        ++it;
                        pos++;

                        if ( a == wxS(']') )
                            depth--;
                        else if ( a == wxS('[') )
                            depth++;
                    }

                    token = text.substr(startPos,pos-startPos-1);

                    if ( token.empty() )
                        break;

                    const ibPGProperty* child = Item(curChild);

                    ibVariant oldChildValue = child->GetValue();
                    ibVariant variant(oldChildValue);

                    if ( !!(flags & ibPGPropValFormatFlags::ProgrammaticValue) ||
                         (!child->HasFlag(ibPGFlags::Disabled) &&
                          !child->HasFlag(ibPGFlags::ReadOnly)) )
                    {
                        wxString childName = child->GetBaseName();

                        bool stvRes = child->StringToValue(variant, token,
                                                           propagatedFlags);
                        if ( stvRes || (variant != oldChildValue) )
                        {
                            list->Append(childName, variant);

                            changed = true;
                        }
                        else
                        {
                            // No changes...
                        }
                    }

                    curChild++;
                    if ( curChild >= iMax )
                        break;

                    addOnlyIfNotEmpty = true;

                    tokenStart = 0xFFFFFF;
                }
                else
                {
                    tokenStart = pos;

                    if ( a == delimiter )
                        strPosIncrement -= 1;
                }
            }
        }

        if ( a == 0 )
            break;

        it += strPosIncrement;

        a = ( it != text.end() ) ? *it : wxUniChar(0);

        pos += strPosIncrement;
    }

    if ( changed )
        v = listValue;

    return changed;
}

bool ibPGProperty::SetValueFromString( const wxString& text, ibPGPropValFormatFlags flags )
{
    ibVariant variant(m_value);
    bool res = StringToValue(variant, text, flags);
    if ( res )
        SetValue(variant);
    return res;
}

bool ibPGProperty::SetValueFromInt( long number, ibPGPropValFormatFlags flags )
{
    ibVariant variant(m_value);
    bool res = IntToValue(variant, number, flags);
    if ( res )
        SetValue(variant);
    return res;
}

wxSize ibPGProperty::OnMeasureImage( int WXUNUSED(item) ) const
{
    if ( m_valueBitmapBundle.IsOk() )
    {
        ibPropertyGrid* pg = GetGrid();
        wxBitmap bmp;
        double scale = 1.0;
        if ( pg )
        {
            bmp = m_valueBitmapBundle.GetBitmapFor(pg);
            int hMax = pg->GetImageSize().GetHeight();
            if ( bmp.GetHeight() > hMax )
            {
                scale = (double)hMax / bmp.GetHeight();
            }
        }
        else
        {
            bmp = m_valueBitmapBundle.GetBitmap(m_valueBitmapBundle.GetDefaultSize());
        }

        return wxSize(wxRound(scale*bmp.GetWidth()), wxDefaultCoord);
    }

    return wxSize(0,0);
}

int ibPGProperty::GetImageOffset( int imageWidth ) const
{
    int imageOffset = 0;

    if ( imageWidth )
    {
        // Do not increment offset too much for wide images
        if ( imageWidth <= (ibPG_CUSTOM_IMAGE_WIDTH+5) )
            imageOffset = imageWidth + DEFAULT_IMAGE_OFFSET_INCREMENT;
        else
            imageOffset = imageWidth + 1;
    }

    return imageOffset;
}

ibPGCellRenderer* ibPGProperty::GetCellRenderer( int WXUNUSED(column) ) const
{
    return ibPGGlobalVars->m_defaultRenderer;
}

void ibPGProperty::OnCustomPaint( wxDC& dc,
                                  const wxRect& rect,
                                  ibPGPaintData& paintData)
{
    wxCHECK_RET( m_valueBitmapBundle.IsOk(), wxS("invalid bitmap bundle") );

    wxBitmap bmp = m_valueBitmapBundle.GetBitmapFor(paintData.m_parent);
    int yOfs;
    if ( bmp.GetHeight() <= rect.height )
    {
        yOfs = (rect.height - bmp.GetHeight()) / 2;
    }
    else
    {
        int w = wxRound(bmp.GetWidth() * (double)rect.height / bmp.GetHeight());
        wxBitmap::Rescale(bmp, wxSize(w, rect.height));
        yOfs = 0;
    }

    dc.DrawBitmap(bmp, rect.x, rect.y + yOfs);
}

const ibPGEditor* ibPGProperty::DoGetEditorClass() const
{
    return ibPGEditor_TextCtrl;
}

// Default extra property event handling - that is, none at all.
bool ibPGProperty::OnEvent( ibPropertyGrid*, wxWindow*, wxEvent& )
{
    return false;
}


void ibPGProperty::SetValue( ibVariant value, ibVariant* pList, ibPGSetValueFlags flags )
{
    // If auto unspecified values are not wanted (via window or property style),
    // then get default value instead of null ibVariant.
    if ( value.GetType() == ibValueTypes::TYPE_EMPTY && !!(flags & ibPGSetValueFlags::ByUser) &&
         !UsesAutoUnspecified() )
    {
        value = GetDefaultValue();
    }

    if ( value.GetType() != ibValueTypes::TYPE_EMPTY )
    {
        ibVariant tempListVariant;

        SetCommonValue(-1);
        // List variants are reserved a special purpose
        // as intermediate containers for child values
        // of properties with children.
        if ( value.GetClassName() == ibPGVariantList::s_className )
        {
            //
            // However, situation is different for composed string properties
            if ( HasFlag(ibPGFlags::ComposedValue) )
            {
                tempListVariant = value;
                pList = &tempListVariant;
            }

            ibVariant newValue;
            AdaptListToValue(value, &newValue);
            value = newValue;
            //wxLogDebug(wxS(">> %s.SetValue() adapted list value to type '%s'"),GetName(),value.GetType());
        }

        if ( HasFlag(ibPGFlags::Aggregate) )
            flags |= ibPGSetValueFlags::Aggregated;

        if ( pList && pList->GetType() != ibValueTypes::TYPE_EMPTY )
        {
            const ibPGVariantList* list = pList->ConvertToType<ibPGVariantList>();
            wxASSERT( list != nullptr );
            wxASSERT( HasAnyChild() );
            wxASSERT( !IsCategory() );

            unsigned int i = 0;

            //wxLogDebug(wxS(">> %s.SetValue() pList parsing"),GetName());

            // Children in list can be in any order, but we will give hint to
            // GetPropertyByNameWH(). This optimizes for full list parsing.
            for ( const ibPGVariantList::Entry& node : *list )
            {
                ibVariant childValue = node.m_value;
                ibPGProperty* child = GetPropertyByNameWH(node.m_name, i);
                if ( child )
                {
                    //wxLogDebug(wxS("%i: child = %s, childValue.GetType()=%s"),i,child->GetBaseName(),childValue.GetType());
                    if ( childValue.GetClassName() == ibPGVariantList::s_className )
                    {
                        if ( child->HasFlag(ibPGFlags::Aggregate) && !(flags & ibPGSetValueFlags::Aggregated) )
                        {
                            ibVariant listRefCopy = childValue;
                            child->SetValue(childValue, &listRefCopy, flags| ibPGSetValueFlags::FromParent);
                        }
                        else
                        {
                            ibVariant oldVal = child->GetValue();
                            child->SetValue(oldVal, &childValue, flags| ibPGSetValueFlags::FromParent);
                        }
                    }
                    else if ( child->GetValue() != childValue )
                    {
                        // For aggregate properties, we will trust RefreshChildren()
                        // to update child values.
                        if ( !HasFlag(ibPGFlags::Aggregate) )
                            child->SetValue(childValue, nullptr, flags| ibPGSetValueFlags::FromParent);
                        if ( !!(flags & ibPGSetValueFlags::ByUser) )
                            child->SetFlag(ibPGFlags::Modified);
                    }
                }
                i++;
            }

            // Always call OnSetValue() for a parent property (do not call it
            // here if the value is non-null because it will then be called
            // below)
            if ( value.GetType() == ibValueTypes::TYPE_EMPTY )
                OnSetValue();
        }

        if ( value.GetType() != ibValueTypes::TYPE_EMPTY )
        {
            m_value = value;
            OnSetValue();
        }

        if ( !!(flags & ibPGSetValueFlags::ByUser) )
            SetFlag(ibPGFlags::Modified);

        if ( HasFlag(ibPGFlags::Aggregate) )
            RefreshChildren();
    }
    else
    {
        if ( m_commonValue != -1 )
        {
            ibPropertyGrid* pg = GetGrid();
            if ( !pg || m_commonValue != pg->GetUnspecifiedCommonValue() )
                SetCommonValue(-1);
        }

        m_value = value;

        // Set children to unspecified, but only if aggregate or
        // value is <composed>
        if ( AreChildrenComponents() )
        {
            for ( ibPGProperty* child : m_children )
                child->SetValue(value, nullptr, flags| ibPGSetValueFlags::FromParent);
        }
    }

    if ( !(flags & ibPGSetValueFlags::FromParent) )
        UpdateParentValues();

    //
    // Update editor control.
    if ( !!(flags & ibPGSetValueFlags::RefreshEditor) )
    {
        ibPropertyGrid* pg = GetGridIfDisplayed();
        if ( pg )
        {
            ibPGProperty* selected = pg->GetSelectedProperty();

            // Only refresh the control if this was selected, or
            // this was some parent of selected, or vice versa)
            if ( selected && (selected == this ||
                              selected->IsSomeParent(this) ||
                              this->IsSomeParent(selected)) )
                RefreshEditor();

            pg->DrawItemAndValueRelated(this);
        }
    }
}


void ibPGProperty::SetValueInEvent( const ibVariant& value ) const
{
    wxCHECK_RET( GetGrid(),
                 wxS("Cannot store pending value for detached property"));
    GetGrid()->ValueChangeInEvent(value);
}

void ibPGProperty::SetFlagRecursively( ibPGFlags flag, bool set )
{
    ChangeFlag(flag, set);

    for ( ibPGProperty* child : m_children )
        child->SetFlagRecursively(flag, set);
}

void ibPGProperty::RefreshEditor()
{
    if ( !m_parent )
        return;

    ibPropertyGrid* pg = GetGrid();
    if ( pg && pg->GetSelectedProperty() == this )
        pg->RefreshEditor();
}

ibVariant ibPGProperty::GetDefaultValue() const
{
    ibVariant defVal = GetAttribute(ibPG_ATTR_DEFAULT_VALUE);
    if ( defVal.GetType() != ibValueTypes::TYPE_EMPTY )
        return defVal;

    ibVariant value = GetValue();

    if ( value.GetType() != ibValueTypes::TYPE_EMPTY )
    {
        // a long, a double, a long long and an unsigned one are each the core's number
        if ( value.GetType() == ibValueTypes::TYPE_NUMBER )
            return ibVariant(0);
        if ( value.GetType() == ibValueTypes::TYPE_STRING )
            return ibVariant(wxString());
        if ( value.GetType() == ibValueTypes::TYPE_BOOLEAN )
            return ibVariant(false);
        if ( value.GetClassName() == wxArrayStringVariantData::s_className )
            return IBVARIANT(wxArrayString());
        if ( value.GetClassName() == wxColourVariantData::s_className )
            return IBVARIANT(*wxBLACK);
#if wxUSE_DATETIME
        if ( value.GetType() == ibValueTypes::TYPE_DATE )
            return IBVARIANT(wxDateTime::Now());
#endif
        if ( value.GetClassName() == wxFontVariantData::s_className )
            return IBVARIANT(*wxNORMAL_FONT);
        if ( value.GetClassName() == wxPointVariantData::s_className )
            return IBVARIANT(wxPoint(0, 0));
        if ( value.GetClassName() == wxSizeVariantData::s_className )
            return IBVARIANT(wxSize(0, 0));
    }

    return ibVariant();
}

void ibPGProperty::Enable( bool enable )
{
    ibPropertyGrid* pg = GetGrid();

    // Preferably call the version in the owning ibPropertyGrid,
    // since it handles the editor de-activation.
    if ( pg )
        pg->EnableProperty(this, enable);
    else
        DoEnable(enable);
}

void ibPGProperty::DoEnable( bool enable )
{
    ChangeFlag(ibPGFlags::Disabled, !enable);

    // Apply same to sub-properties as well
    for ( ibPGProperty* child : m_children )
        child->DoEnable( enable );
}

void ibPGProperty::EnsureCells( unsigned int column )
{
    if ( column >= m_cells.size() )
    {
        // Fill empty slots with default cells
        ibPropertyGrid* pg = GetGrid();
        ibPGCell defaultCell;

        if ( pg )
        {
            defaultCell = IsCategory() ? pg->GetCategoryDefaultCell()
                                       : pg->GetPropertyDefaultCell();
        }

        // Alloc new default cells.
        m_cells.resize(column+1, defaultCell);
    }
}

void ibPGProperty::SetCell( int column,
                            const ibPGCell& cell )
{
    EnsureCells(column);

    m_cells[column] = cell;
}

void ibPGProperty::AdaptiveSetCell( unsigned int firstCol,
                                    unsigned int lastCol,
                                    const ibPGCell& cell,
                                    const ibPGCell& srcData,
                                    ibPGCellData* unmodCellData,
                                    ibPGFlags ignoreWithFlags,
                                    bool recursively )
{
    //
    // Sets cell in memory optimizing fashion. That is, if
    // current cell data matches unmodCellData, we will
    // simply get reference to data from cell. Otherwise,
    // cell information from srcData is merged into current.
    //

    if ( !(m_flags & ignoreWithFlags) && !IsRoot() )
    {
        EnsureCells(lastCol);

        for ( unsigned int col=firstCol; col<=lastCol; col++ )
        {
            if ( m_cells[col].GetData() == unmodCellData )
            {
                // Data matches... use cell directly
                m_cells[col] = cell;
            }
            else
            {
                // Data did not match... merge valid information
                m_cells[col].MergeFrom(srcData);
            }
        }
    }

    if ( recursively )
    {
        for ( ibPGProperty* child : m_children )
            child->AdaptiveSetCell( firstCol,
                                    lastCol,
                                    cell,
                                    srcData,
                                    unmodCellData,
                                    ignoreWithFlags,
                                    recursively );
    }
}

void ibPGProperty::ClearCells(ibPGFlags ignoreWithFlags, bool recursively)
{
    if ( !(m_flags & ignoreWithFlags) && !IsRoot() )
    {
        m_cells.clear();
    }

    if ( recursively )
    {
        for ( ibPGProperty* child : m_children )
        {
            child->ClearCells(ignoreWithFlags, recursively);
        }
    }
}

const ibPGCell& ibPGProperty::GetCell( unsigned int column ) const
{
    if ( m_cells.size() > column )
        return m_cells[column];

    ibPropertyGrid* pg = GetGrid();
    wxASSERT_MSG( pg,
                  wxS("Cannot get cell for detached property") );
    if ( !pg )
    {
        static const ibPGCell invalidCell;
        return invalidCell;
    }

    return IsCategory() ? pg->GetCategoryDefaultCell()
                        : pg->GetPropertyDefaultCell();
}

ibPGCell& ibPGProperty::GetOrCreateCell( unsigned int column )
{
    EnsureCells(column);
    return m_cells[column];
}

void ibPGProperty::SetBackgroundColour(const wxColour& colour, ibPGPropertyValuesFlags flags)
{
    ibPGProperty* firstProp = this;
    bool recursively = !!(flags & ibPGPropertyValuesFlags::Recurse);

    //
    // If category is tried to set recursively, skip it and only
    // affect the children.
    if ( recursively )
    {
        while ( firstProp->IsCategory() )
        {
            if ( !firstProp->HasAnyChild() )
                return;
            firstProp = firstProp->Item(0);
        }
    }

    ibPGCell& firstCell = firstProp->GetCell(0);
    ibPGCellData* firstCellData = firstCell.GetData();

    ibPGCell newCell(firstCell);
    newCell.SetBgCol(colour);
    ibPGCell srcCell;
    srcCell.SetBgCol(colour);

    AdaptiveSetCell( 0,
                     GetParentState()->GetColumnCount()-1,
                     newCell,
                     srcCell,
                     firstCellData,
                     recursively ? ibPGFlags::Category : ibPGFlags::Null,
                     recursively );
}

void ibPGProperty::SetTextColour(const wxColour& colour, ibPGPropertyValuesFlags flags)
{
    ibPGProperty* firstProp = this;
    bool recursively = !!(flags & ibPGPropertyValuesFlags::Recurse);

    //
    // If category is tried to set recursively, skip it and only
    // affect the children.
    if ( recursively )
    {
        while ( firstProp->IsCategory() )
        {
            if ( !firstProp->HasAnyChild() )
                return;
            firstProp = firstProp->Item(0);
        }
    }

    ibPGCell& firstCell = firstProp->GetCell(0);
    ibPGCellData* firstCellData = firstCell.GetData();

    ibPGCell newCell(firstCell);
    newCell.SetFgCol(colour);
    ibPGCell srcCell;
    srcCell.SetFgCol(colour);

    AdaptiveSetCell( 0,
                     GetParentState()->GetColumnCount()-1,
                     newCell,
                     srcCell,
                     firstCellData,
                     recursively ? ibPGFlags::Category : ibPGFlags::Null,
                     recursively );
}

void ibPGProperty::SetDefaultColours(ibPGPropertyValuesFlags flags)
{
    ibPGProperty* firstProp = this;
    bool recursively = !!(flags & ibPGPropertyValuesFlags::Recurse);

    // If category is tried to set recursively, skip it and only
    // affect the children.
    if ( recursively )
    {
        while ( firstProp->IsCategory() )
        {
            if ( !firstProp->HasAnyChild() )
                return;
            firstProp = firstProp->Item(0);
        }
    }

    ClearCells(recursively ? ibPGFlags::Category : ibPGFlags::Null,
               recursively);
}

ibPGEditorDialogAdapter* ibPGProperty::GetEditorDialog() const
{
    return nullptr;
}

bool ibPGProperty::DoSetAttribute( const wxString& WXUNUSED(name), ibVariant& WXUNUSED(value) )
{
    return false;
}

void ibPGProperty::SetAttribute( const wxString& name, ibVariant value )
{
    if ( !DoSetAttribute(name, value) || !ibPGGlobalVars->HasExtraStyle(ibPG_EX_WRITEONLY_BUILTIN_ATTRIBUTES) )
    {
        m_attributes.Set(name, value);
    }
    // Because changing some attributes (like ibPG_FLOAT_PRECISION, ibPG_UINT_BASE,
    // ibPG_UINT_PREFIX, ibPG_ATTR_UNITS) affect displayed form of the property value
    // so we have to redraw property to be sure that displayed value is in sync
    // with current attributes.
    ibPropertyGrid* pg = GetGridIfDisplayed();
    if ( pg )
    {
        pg->RefreshProperty(this);
    }
}

void ibPGProperty::SetAttributes( const ibPGAttributeStorage& attributes )
{
    ibPGAttributeStorage::const_iterator it = attributes.StartIteration();
    wxString name;
    ibVariant variant;

    while ( attributes.GetNext(it, name, variant) )
        SetAttribute( name, variant );
}

ibVariant ibPGProperty::DoGetAttribute( const wxString& WXUNUSED(name) ) const
{
    return ibVariant();
}


ibVariant ibPGProperty::GetAttribute( const wxString& name ) const
{
    ibVariant value = DoGetAttribute(name);
    return value.GetType() != ibValueTypes::TYPE_EMPTY ? value : m_attributes.FindValue(name);
}

wxString ibPGProperty::GetAttribute( const wxString& name, const wxString& defVal ) const
{
    ibVariant variant = m_attributes.FindValue(name);

    return variant.GetType() == ibValueTypes::TYPE_EMPTY ? defVal : variant.GetString().ToWxString();
}

long ibPGProperty::GetAttributeAsLong( const wxString& name, long defVal ) const
{
    ibVariant variant = m_attributes.FindValue(name);

    return variant.GetType() == ibValueTypes::TYPE_EMPTY ? defVal : variant.GetInteger();
}

double ibPGProperty::GetAttributeAsDouble( const wxString& name, double defVal ) const
{
    ibVariant variant = m_attributes.FindValue(name);

    return variant.GetType() == ibValueTypes::TYPE_EMPTY ? defVal : variant.GetDouble();
}

ibVariant ibPGProperty::GetAttributesAsList() const
{
    // named "@<name>@attr" by the list it is appended to (GetPropertyValues): a list's name is its entry's
    ibPGVariantList* v = new ibPGVariantList();

    ibPGAttributeStorage::const_iterator it = m_attributes.StartIteration();
    wxString name;
    ibVariant variant;

    while ( m_attributes.GetNext(it, name, variant) )
        v->Append(name, variant);

    return ibVariant(v);
}

// Utility flags are excluded.
// Store the literals in the internal representation for better performance.
static const std::array<std::pair<ibPGFlags, const wxStringCharType*>, 4> gs_propFlagToString
{ {
  { ibPGFlags::Disabled,  wxS("DISABLED")  },
  { ibPGFlags::Hidden,    wxS("HIDDEN")    },
  { ibPGFlags::NoEditor,  wxS("NOEDITOR")  },
  { ibPGFlags::Collapsed, wxS("COLLAPSED") }
} };

wxString ibPGProperty::GetFlagsAsString(ibPGFlags flagsMask) const
{
    wxString s;
    const ibPGFlags relevantFlags = m_flags & flagsMask & ibPGFlags::StringStoredFlags;

    for ( auto& item : gs_propFlagToString )
    {
        if ( !!(relevantFlags & item.first) )
        {
            if ( !s.empty() )
            {
                s.append(wxS("|"));
            }
            s.append(item.second);
        }
    }

    return s;
}

void ibPGProperty::SetFlagsFromString( const wxString& str )
{
    ibPGFlags flags = ibPGFlags::Null;

    IB_PG_TOKENIZER1_BEGIN(str, wxS('|'))
        for ( auto& item : gs_propFlagToString )
        {
            if ( token == item.second )
            {
                flags |= item.first;
                break;
            }
        }
    IB_PG_TOKENIZER1_END()

    m_flags = (m_flags & ~ibPGFlags::StringStoredFlags) | flags;
}

wxValidator* ibPGProperty::DoGetValidator() const
{
    return nullptr;
}

int ibPGProperty::InsertChoice( const wxString& label, int index, int value )
{
    ibPropertyGrid* pg = GetGrid();
    const int sel = GetChoiceSelection();

    int newSel = (sel == wxNOT_FOUND) ? 0 : sel;

    const int numChoices = m_choices.GetCount();
    if ( index == wxNOT_FOUND )
        index = numChoices;

    if ( numChoices > 0 && index <= sel )
        newSel++;

    m_choices.Insert(label, index, value);
    // Set new selection if it was modified
    // or if the first element was added.
    if ( sel != newSel || numChoices == 0 )
        SetChoiceSelection(newSel);

    if ( pg && this == pg->GetSelection() )
        GetEditorClass()->InsertItem(pg->GetEditorControl(),label,index);

    return index;
}


void ibPGProperty::DeleteChoice( int index )
{
    ibPropertyGrid* pg = GetGrid();

    int sel = GetChoiceSelection();
    int newSel = sel;

    // Adjust current value
    if ( sel == index )
    {
        SetValueToUnspecified();
        newSel = 0;
    }
    else if ( index < sel )
    {
        newSel--;
    }

    m_choices.RemoveAt(index);

    if ( sel != newSel )
        SetChoiceSelection(newSel);

    if ( pg && this == pg->GetSelection() )
        GetEditorClass()->DeleteItem(pg->GetEditorControl(), index);
}

int ibPGProperty::GetChoiceSelection() const
{
    ibVariant value = GetValue();
    const ibValueTypes valueType = value.GetType();
    int index = wxNOT_FOUND;

    if ( IsValueUnspecified() || m_choices.GetCount() == 0 )
        return wxNOT_FOUND;

    if ( valueType == ibValueTypes::TYPE_NUMBER )
    {
        index = m_choices.Index(value.GetInteger());
    }
    else if ( valueType == ibValueTypes::TYPE_STRING )
    {
        index = m_choices.Index(value.GetString());
    }
    else if ( valueType == ibValueTypes::TYPE_BOOLEAN )
    {
        index = value.GetBoolean()? 1 : 0;
    }

    return index;
}

void ibPGProperty::SetChoiceSelection( int newValue )
{
    wxCHECK_RET( m_choices.IsOk(), wxS("invalid choiceinfo") );

    // Allow setting the value of -1 to reset the selection, for consistency
    // with wxChoice::SetSelection().
    if ( newValue == wxNOT_FOUND )
    {
        SetValueToUnspecified();
        return;
    }

    wxCHECK_RET( newValue >= 0 && newValue < (int)m_choices.GetCount(),
                 wxS("New index is out of range") );

    // Changes value of a property with choices, but only
    // works if the value type is long or string.
    const ibValueTypes valueType = GetValue().GetType();

    if ( valueType == ibValueTypes::TYPE_STRING )
    {
        SetValue( m_choices.GetLabel(newValue) );
    }
    else  // if ( valueType == ibValueTypes::TYPE_NUMBER )
    {
        SetValue( m_choices.GetValue(newValue) );
    }
}

bool ibPGProperty::SetChoices( const ibPGChoices& choices )
{
    // Property must be de-selected first (otherwise choices in
    // the control would be de-synced with true choices)
    ibPropertyGrid* pg = GetGrid();
    bool isSelected = pg && pg->GetSelection() == this;
    if ( isSelected )
    {
        pg->ClearSelection();
    }

    m_choices.Assign(choices);
    if ( isSelected )
    {
        wxWindow* ctrl = pg->GetEditorControl();
        if ( ctrl )
            GetEditorClass()->SetItems(ctrl, m_choices.GetLabels());
    }

    // This may be needed to trigger some initialization
    // (but don't do it if property is somewhat uninitialized)
    ibVariant defVal = GetDefaultValue();
    if ( defVal.GetType() != ibValueTypes::TYPE_EMPTY )
        SetValue(defVal);

    if ( isSelected )
    {
        // Recreate editor
        pg->DoSelectProperty(this, ibPGSelectPropertyFlags::Force);
    }

    return true;
}


const ibPGEditor* ibPGProperty::GetEditorClass() const
{
    const ibPGEditor* editor = m_customEditor ? m_customEditor : DoGetEditorClass();

    // Maybe override editor if common value specified
    if ( GetDisplayedCommonValueCount() )
    {
        // TextCtrlAndButton -> ComboBoxAndButton
        if ( wxDynamicCast(editor, ibPGTextCtrlAndButtonEditor) )
            editor = ibPGEditor_ChoiceAndButton;

        // TextCtrl -> ComboBox
        else if ( wxDynamicCast(editor, ibPGTextCtrlEditor) )
            editor = ibPGEditor_ComboBox;
    }

    return editor;
}

bool ibPGProperty::Hide( bool hide, ibPGPropertyValuesFlags flags )
{
    ibPropertyGrid* pg = GetGrid();
    if ( pg )
        return pg->HideProperty(this, hide, flags);

    return DoHide( hide, flags );
}

bool ibPGProperty::DoHide( bool hide, ibPGPropertyValuesFlags flags )
{
    ChangeFlag(ibPGFlags::Hidden, hide);

    if ( !!(flags & ibPGPropertyValuesFlags::Recurse) )
    {
        for ( ibPGProperty* child : m_children )
            child->DoHide(hide, flags | ibPGPropertyValuesFlags::RecurseStarts);
    }

    return true;
}

bool ibPGProperty::HasVisibleChildren() const
{
    for ( ibPGProperty* child : m_children )
    {
        if ( !child->HasFlag(ibPGFlags::Hidden) )
            return true;
    }

    return false;
}

bool ibPGProperty::RecreateEditor()
{
    ibPropertyGrid* pg = GetGrid();
    wxCHECK_MSG(pg, false, "Cannot recreate editor for detached property");

    ibPGProperty* selected = pg->GetSelection();
    if ( this == selected )
    {
        pg->DoSelectProperty(this, ibPGSelectPropertyFlags::Force);
        return true;
    }
    return false;
}


void ibPGProperty::SetValueImage( const wxBitmapBundle& bmp )
{
    wxCHECK_RET( GetGrid(),
                 wxS("Cannot set image for detached property") );

    if ( bmp.IsOk() )
    {
        m_valueBitmapBundle = bmp;
        m_flags |= ibPGFlags::CustomImage;
    }
    else
    {
        m_valueBitmapBundle = wxBitmapBundle();
        m_flags &= ~(ibPGFlags::CustomImage);
    }
}


ibPGProperty* ibPGProperty::GetMainParent() const
{
    ibPGProperty* curChild = const_cast<ibPGProperty*>(this);
    ibPGProperty* curParent = m_parent;

    while ( !curParent->IsRoot() && !curParent->IsCategory() )
    {
        curChild = curParent;
        curParent = curParent->m_parent;
    }

    return curChild;
}


const ibPGProperty* ibPGProperty::GetLastVisibleSubItem() const
{
    //
    // Returns last visible sub-item, recursively.
    if ( !IsExpanded() || !HasAnyChild() )
        return this;

    return Last()->GetLastVisibleSubItem();
}


bool ibPGProperty::IsVisible() const
{
    if ( HasFlag(ibPGFlags::Hidden) )
        return false;

    for (const ibPGProperty* parent = GetParent(); parent != nullptr; parent = parent->GetParent() )
    {
        if ( !parent->IsExpanded() || parent->HasFlag(ibPGFlags::Hidden) )
            return false;
    }

    return true;
}

ibPropertyGrid* ibPGProperty::GetGridIfDisplayed() const
{
    ibPropertyGridPageState* state = GetParentState();
    if ( !state )
        return nullptr;
    ibPropertyGrid* propGrid = state->GetGrid();
    return state == propGrid->GetState() ? propGrid : nullptr;
}

int ibPGProperty::GetY2( int lh ) const
{
    const ibPGProperty* parent;
    const ibPGProperty* child = this;

    int y = 0;

    for ( parent = GetParent(); parent != nullptr; parent = child->GetParent() )
    {
        if ( !parent->IsExpanded() )
            return parent->GetY2(lh);
        y += parent->GetChildrenHeight(lh, child->GetIndexInParent());
        y += lh;
        child = parent;
    }

    y -= lh;  // need to reduce one level

    return y;
}


int ibPGProperty::GetY() const
{
    ibPropertyGrid *pg = GetGrid();
    wxCHECK_MSG( pg, 0,
                 wxS("Cannot obtain coordinates of detached property") );
    return GetY2(pg->GetRowHeight());
}

// This is used by Insert etc.
void ibPGProperty::DoAddChild( ibPGProperty* prop, int index,
                               bool correct_mode )
{
    if ( index < 0 || (size_t)index >= m_children.size() )
    {
        if ( correct_mode ) prop->m_arrIndex = m_children.size();
        m_children.push_back( prop );
    }
    else
    {
        m_children.insert( m_children.begin()+index, prop);
        if ( correct_mode ) FixIndicesOfChildren( index );
    }

    prop->m_parent = this;
}

void ibPGProperty::DoPreAddChild( int index, ibPGProperty* prop )
{
    wxASSERT_MSG( !prop->GetBaseName().empty(),
                  wxS("Property's children must have unique, non-empty ")
                  wxS("names within their scope") );

    prop->m_arrIndex = index;
    m_children.insert( m_children.begin()+index,
                       prop );

    int custImgHeight = prop->OnMeasureImage().y;
    if ( custImgHeight == wxDefaultCoord /*|| custImgHeight > 1*/ )
        prop->m_flags |= ibPGFlags::CustomImage;

    prop->m_parent = this;
}

void ibPGProperty::AddPrivateChild( ibPGProperty* prop )
{
    if ( !(m_flags & ibPGFlags::ParentalFlags) )
        SetParentalType(ibPGFlags::Aggregate);

    wxASSERT_MSG( (m_flags & ibPGFlags::ParentalFlags) == ibPGFlags::Aggregate,
                  wxS("Do not mix up AddPrivateChild() calls with other ")
                  wxS("property adders.") );

    DoPreAddChild( m_children.size(), prop );
}

ibPGProperty* ibPGProperty::InsertChild( int index,
                                         ibPGProperty* childProperty )
{
    if ( index < 0 )
        index = m_children.size();

    if ( m_parentState )
    {
        m_parentState->DoInsert(this, index, childProperty);
    }
    else
    {
        if ( !(m_flags & ibPGFlags::ParentalFlags) )
            SetParentalType(ibPGFlags::MiscParent);

        wxASSERT_MSG( (m_flags & ibPGFlags::ParentalFlags) == ibPGFlags::MiscParent,
                      wxS("Do not mix up AddPrivateChild() calls with other ")
                      wxS("property adders.") );

        DoPreAddChild( index, childProperty );
    }

    return childProperty;
}

void ibPGProperty::RemoveChild( ibPGProperty* p )
{
    auto it = std::find(m_children.begin(), m_children.end(), p);
    if ( it != m_children.end() )
    {
        m_children.erase(it);
    }
}

void ibPGProperty::RemoveChild(unsigned int index)
{
    m_children.erase(m_children.begin()+index);
}

void ibPGProperty::SortChildren(bool (*fCmp)(ibPGProperty*, ibPGProperty*))
{
    std::sort(m_children.begin(), m_children.end(), fCmp);
}

void ibPGProperty::AdaptListToValue( ibVariant& list, ibVariant* value ) const
{
    wxASSERT( HasAnyChild() );
    wxASSERT( !IsCategory() );

    *value = GetValue();

    const ibPGVariantList* entries = list.ConvertToType<ibPGVariantList>();
    if ( entries == nullptr || entries->GetCount() == 0 )
        return;

    wxASSERT( GetChildCount() >= (unsigned int)entries->GetCount() );

    bool allChildrenSpecified;

    // Don't fully update aggregate properties unless all children have
    // specified value
    if ( HasFlag(ibPGFlags::Aggregate) )
        allChildrenSpecified = AreAllChildrenSpecified(&list);
    else
        allChildrenSpecified = true;

    size_t n = 0;
    wxString childName = (*entries)[n].m_name;
    ibVariant childValue = (*entries)[n].m_value;

    //wxLogDebug(wxS(">> %s.AdaptListToValue()"),GetBaseName());

    for ( unsigned int i = 0; i < GetChildCount(); i++ )
    {
        const ibPGProperty* child = Item(i);
        if ( childName == child->GetBaseName() )
        {
            //wxLogDebug(wxS("  %s(n=%i), %s"),childValue.GetName(),n,childValue.GetType());

            if ( childValue.GetClassName() == ibPGVariantList::s_className )
            {
                ibVariant cv2(child->GetValue());
                child->AdaptListToValue(childValue, &cv2);
                childValue = cv2;
            }

            if ( allChildrenSpecified )
            {
                *value = ChildChanged(*value, i, childValue);
            }

            n++;
            if ( n == entries->GetCount() )
                break;
            childName = (*entries)[n].m_name;
            childValue = (*entries)[n].m_value;
        }
    }
}


void ibPGProperty::FixIndicesOfChildren( unsigned int starthere )
{
    for ( unsigned int i = starthere; i < GetChildCount(); i++)
        Item(i)->m_arrIndex = i;
}


// Returns (direct) child property with given name (or nullptr if not found)
ibPGProperty* ibPGProperty::GetPropertyByName( const wxString& name ) const
{
    for ( ibPGProperty* p : m_children )
    {
        if ( p->m_name == name )
            return p;
    }

    // Does it have point, then?
    int pos = name.Find(wxS('.'));
    if ( pos <= 0 )
        return nullptr;

    ibPGProperty* p = GetPropertyByName(name. substr(0,pos));

    if ( !p || !p->HasAnyChild() )
        return nullptr;

    return p->GetPropertyByName(name.substr(pos+1,name.length()-pos-1));
}

ibPGProperty* ibPGProperty::GetPropertyByNameWH( const wxString& name, unsigned int hintIndex ) const
{
    unsigned int i = hintIndex >= GetChildCount() ? 0 : hintIndex;
    unsigned int lastIndex = i - 1;

    if ( lastIndex >= GetChildCount() )
        lastIndex = GetChildCount() - 1;

    for (;;)
    {
        ibPGProperty* p = Item(i);
        if ( p->m_name == name )
            return p;

        if ( i == lastIndex )
            break;

        i++;
        if ( i == GetChildCount() )
            i = 0;
    }

    return nullptr;
}

int ibPGProperty::GetChildrenHeight( int lh, int iMax ) const
{
    // Returns height of children, recursively, and
    // by taking expanded/collapsed status into account.
    //
    // iMax is used when finding property y-positions.
    //

    unsigned int _iMax = iMax == -1 ? GetChildCount() : iMax;
    wxASSERT( _iMax <= GetChildCount() );

    if ( !IsExpanded() && GetParent() )
        return 0;

    int h = 0;
    for ( unsigned int i = 0; i < _iMax; i++ )
    {
        ibPGProperty* pwc = Item(i);

        if ( !pwc->HasFlag(ibPGFlags::Hidden) )
        {
            if ( !pwc->IsExpanded() ||
                 !pwc->HasAnyChild() )
                h += lh;
            else
                h += pwc->GetChildrenHeight(lh) + lh;
        }
    }

    return h;
}

ibPGProperty* ibPGProperty::GetItemAtY( unsigned int y,
                                        unsigned int lh,
                                        unsigned int* nextItemY ) const
{
    wxCHECK( nextItemY, nullptr );

    // Linear search at the moment
    //
    // nextItemY = y of next visible property, final value will be written back.
    ibPGProperty* result = nullptr;
    ibPGProperty* current = nullptr;
    unsigned int iy = *nextItemY;

    for ( ibPGProperty* pwc : m_children )
    {
        if ( !pwc->HasFlag(ibPGFlags::Hidden) )
        {
            // Found?
            if ( y < iy )
            {
                result = current;
                break;
            }

            iy += lh;

            if ( pwc->IsExpanded() &&
                 pwc->HasAnyChild() )
            {
                result = pwc->GetItemAtY( y, lh, &iy );
                if ( result )
                    break;
            }

            current = pwc;
        }
    }

    // Found?
    if ( !result && y < iy )
        result = current;

    *nextItemY = iy;

    /*
    if ( current )
    {
        wxLogDebug(wxS("%s::GetItemAtY(%i) -> %s"),this->GetLabel(),y,current->GetLabel());
    }
    else
    {
        wxLogDebug(wxS("%s::GetItemAtY(%i) -> NULL"),this->GetLabel(),y);
    }
    */

    return result;
}

void ibPGProperty::Empty()
{
    if ( !HasFlag(ibPGFlags::ChildrenAreCopies) )
    {
        for ( ibPGProperty* child : m_children )
        {
            delete child;
        }
    }

    m_children.clear();
}

ibPGProperty* ibPGProperty::GetItemAtY( unsigned int y ) const
{
    ibPropertyGrid *pg = GetGrid();
    wxCHECK_MSG( pg, nullptr,
                 wxS("Cannot obtain property item for detached property") );

    unsigned int nextItem = 0;
    return GetItemAtY(y, pg->GetRowHeight(), &nextItem);
}

void ibPGProperty::DeleteChildren()
{
    ibPropertyGridPageState* state = m_parentState;

    if ( !HasAnyChild() )
        return;

    // Because deletion is sometimes deferred, we have to use
    // this sort of code for enumerating the child properties.
    unsigned int i = GetChildCount();
    while ( i > 0 )
    {
        i--;
        state->DoDelete(Item(i), true);
    }
}

bool ibPGProperty::IsChildSelected( bool recursive ) const
{
    for ( ibPGProperty* child : m_children )
    {
        // Test child
        if ( m_parentState->DoIsPropertySelected( child ) )
            return true;

        // Test sub-children
        if ( recursive && child->IsChildSelected( recursive ) )
            return true;
    }

    return false;
}

ibVariant ibPGProperty::ChildChanged( ibVariant& WXUNUSED(thisValue),
                                      int WXUNUSED(childIndex),
                                      ibVariant& WXUNUSED(childValue) ) const
{
    return ibVariant();
}

bool ibPGProperty::AreAllChildrenSpecified( const ibVariant* pendingList ) const
{
    const ibPGVariantList* pList = nullptr;
    ibPGVariantList::const_iterator node;

    if ( pendingList )
    {
        pList = pendingList->ConvertToType<ibPGVariantList>();
        wxASSERT( pList != nullptr );
        if ( pList )
            node = pList->begin();
    }

    for ( ibPGProperty* child : m_children )
    {
        const ibVariant* listValue = nullptr;
        ibVariant value;

        if ( pList )
        {
            const wxString& childName = child->GetBaseName();

            for ( ; node != pList->end(); ++node )
            {
                const ibPGVariantList::Entry& item = *node;
                if ( item.m_name == childName )
                {
                    listValue = &item.m_value;
                    value = item.m_value;
                    break;
                }
            }
        }

        if ( !listValue )
            value = child->GetValue();

        if ( value.GetType() == ibValueTypes::TYPE_EMPTY )
            return false;

        // Check recursively
        if ( child->HasAnyChild() )
        {
            const ibVariant* childList = nullptr;

            if ( listValue && listValue->GetClassName() == ibPGVariantList::s_className )
                childList = listValue;

            if ( !child->AreAllChildrenSpecified(childList) )
                return false;
        }
    }

    return true;
}

ibPGProperty* ibPGProperty::UpdateParentValues()
{
    ibPGProperty* parent = m_parent;
    if ( parent && parent->HasFlag(ibPGFlags::ComposedValue) &&
         !parent->IsCategory() && !parent->IsRoot() )
    {
        wxString s;
        parent->DoGenerateComposedValue(s);
        parent->m_value = s;
        return parent->UpdateParentValues();
    }
    return this;
}

bool ibPGProperty::IsTextEditable() const
{
    if ( HasFlag(ibPGFlags::ReadOnly) )
        return false;

    if ( HasFlag(ibPGFlags::NoEditor) &&
         (HasAnyChild() ||
          wxString(GetEditorClass()->GetClassInfo()->GetClassName()).EndsWith(wxS("Button")))
       )
        return false;

    return true;
}

// Call after fixed sub-properties added/removed after creation.
// if oldSelInd >= 0 and < new max items, then selection is
// moved to it. Note: oldSelInd -2 indicates that this property
// should be selected.
void ibPGProperty::SubPropsChanged( int oldSelInd )
{
    ibPropertyGridPageState* state = GetParentState();
    ibPropertyGrid* grid = state->GetGrid();

    //
    // Re-repare children (recursively)
    for ( ibPGProperty* child : m_children )
    {
        child->InitAfterAdded(state, grid);
    }

    ibPGProperty* sel = nullptr;
    if ( oldSelInd >= (int)m_children.size() )
        oldSelInd = (int)m_children.size() - 1;

    if ( oldSelInd >= 0 )
        sel = m_children[oldSelInd];
    else if ( oldSelInd == -2 )
        sel = this;

    if ( sel )
        state->DoSelectProperty(sel);

    if ( state == grid->GetState() )
    {
        grid->GetPanel()->Refresh();
    }
}

wxString ibPGProperty::GetHintText() const
{
    ibVariant vHintText = GetAttribute(ibPG_ATTR_HINT);

    return vHintText.GetType() == ibValueTypes::TYPE_EMPTY ? wxString() : vHintText.GetString().ToWxString();
}

int ibPGProperty::GetDisplayedCommonValueCount() const
{
    if ( HasFlag(ibPGFlags::UsesCommonValue) )
    {
        ibPropertyGrid* pg = GetGrid();
        if ( pg )
            return (int)pg->GetCommonValueCount();
    }
    return 0;
}

void ibPGProperty::SetDefaultValue(ibVariant& value)
{
    SetAttribute(ibPG_ATTR_DEFAULT_VALUE, value);
}

void ibPGProperty::SetEditor(const wxString& editorName)
{
    m_customEditor = ibPropertyGridInterface::GetEditorByName(editorName);
}

bool ibPGProperty::SetMaxLength(int maxLen)
{
    const ibPGEditor* editorClass = GetEditorClass();
    if ( editorClass != ibPGEditor_TextCtrl &&
         editorClass != ibPGEditor_TextCtrlAndButton )
        return false;

    m_maxLen = wxMax(maxLen, 0); // shouldn't be a nagative value
    return true;
}

wxBitmap* ibPGProperty::GetValueImage() const
{
    if ( !m_valueBitmapBundle.IsOk() )
        return nullptr;

    ibPropertyGrid* pg = GetGrid();
    if ( pg )
        m_valueBitmap = m_valueBitmapBundle.GetBitmapFor(pg);
    else
        m_valueBitmap = m_valueBitmapBundle.GetBitmap(m_valueBitmapBundle.GetDefaultSize());

    return &m_valueBitmap;
}

// -----------------------------------------------------------------------
// ibPGRootProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGRootProperty, ibPGProperty, TextCtrl)

ibPGRootProperty::ibPGRootProperty( const wxString& name )
    : ibPGProperty()
{
    m_name = name;
    m_label = m_name;
    SetParentalType(ibPGFlags::Null);
    m_depth = 0;
}

// -----------------------------------------------------------------------
// ibPGCategoryProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGCategoryProperty, ibPGProperty, TextCtrl)

void ibPGCategoryProperty::Init()
{
    // don't set colour - prepareadditem method should do this
    SetParentalType(ibPGFlags::Category);
    m_capFgColIndex = 1;
    m_textExtent = -1;
}

ibPGCategoryProperty::ibPGCategoryProperty()
    : ibPGProperty()
{
    Init();
}


ibPGCategoryProperty::ibPGCategoryProperty( const wxString &label, const wxString& name )
    : ibPGProperty(label,name)
{
    Init();
}

wxString ibPGCategoryProperty::ValueToString( ibVariant& WXUNUSED(value),
                                            ibPGPropValFormatFlags WXUNUSED(flags) ) const
{
    return m_value.GetType() == ibValueTypes::TYPE_STRING ? m_value.GetString().ToWxString() : wxString();
}

wxString ibPGCategoryProperty::GetValueAsString(ibPGPropValFormatFlags flags) const
{
    // Unspecified value is always empty string
    return IsValueUnspecified() ? wxString() : ibPGProperty::GetValueAsString(flags);
}

static int DoGetTextExtent(const wxWindow* wnd, const wxString& label, const wxFont& font)
{
    int x;
    wnd->GetTextExtent(label, &x, nullptr, nullptr, nullptr, &font);
    return x;
}

int ibPGCategoryProperty::GetTextExtent( const wxWindow* wnd, const wxFont& font ) const
{
    return (m_textExtent > 0) ? m_textExtent : DoGetTextExtent(wnd, m_label, font);
}

void ibPGCategoryProperty::CalculateTextExtent(const wxWindow* wnd, const wxFont& font)
{
    m_textExtent = DoGetTextExtent(wnd, m_label, font);
}

// -----------------------------------------------------------------------
// ibPGChoices
// -----------------------------------------------------------------------

ibPGChoiceEntry& ibPGChoices::Add( const wxString& label, int value )
{
    AllocExclusive();

    ibPGChoiceEntry entry(label, value);
    return m_data->Insert( -1, entry );
}

// -----------------------------------------------------------------------

ibPGChoiceEntry& ibPGChoices::Add( const wxString& label, const wxBitmapBundle& bitmap, int value )
{
    AllocExclusive();

    ibPGChoiceEntry entry(label, value);
    entry.SetBitmap(bitmap);
    return m_data->Insert( -1, entry );
}

// -----------------------------------------------------------------------

ibPGChoiceEntry& ibPGChoices::Insert( const ibPGChoiceEntry& entry, int index )
{
    AllocExclusive();

    return m_data->Insert( index, entry );
}

// -----------------------------------------------------------------------

ibPGChoiceEntry& ibPGChoices::Insert( const wxString& label, int index, int value )
{
    AllocExclusive();

    ibPGChoiceEntry entry(label, value);
    return m_data->Insert( index, entry );
}

// -----------------------------------------------------------------------

ibPGChoiceEntry& ibPGChoices::AddAsSorted( const wxString& label, int value )
{
    AllocExclusive();

    size_t index = 0;

    while ( index < GetCount() )
    {
        int cmpRes = GetLabel(index).Cmp(label);
        if ( cmpRes > 0 )
            break;
        index++;
    }

    ibPGChoiceEntry entry(label, value);
    return m_data->Insert( index, entry );
}

// -----------------------------------------------------------------------

void ibPGChoices::Add(size_t count, const wxString* labels, const long* values)
{
    AllocExclusive();

    for ( size_t i = 0; i < count; ++i )
    {
        const int value = values ? values[i] : i;
        ibPGChoiceEntry entry(labels[i], value);
        m_data->Insert( i, entry );
    }
}

// -----------------------------------------------------------------------

void ibPGChoices::Add( const wxChar* const* labels, const ValArrItem* values )
{
    AllocExclusive();

    for ( unsigned int i = 0; *labels; labels++, i++ )
    {
        const int value = values ? values[i] : i;
        ibPGChoiceEntry entry(*labels, value);
        m_data->Insert( i, entry );
    }
}

// -----------------------------------------------------------------------

void ibPGChoices::Add( const wxArrayString& arr, const wxArrayInt& arrint )
{
    AllocExclusive();

    const unsigned int itemcount = arr.size();
    const unsigned int valcount = arrint.size();
    wxASSERT_MSG( valcount >= itemcount || valcount == 0,
                  wxS("Insufficient number of values in the array") );

    for ( unsigned int i = 0; i < itemcount; i++ )
    {
        const int value = (i < valcount) ? arrint[i] : i;
        ibPGChoiceEntry entry(arr[i], value);
        m_data->Insert( i, entry );
    }
}

// -----------------------------------------------------------------------

void ibPGChoices::RemoveAt(size_t nIndex, size_t count)
{
    AllocExclusive();

    wxASSERT( m_data->GetRefCount() != -1 );
    m_data->m_items.erase(m_data->m_items.begin()+nIndex,
                          m_data->m_items.begin()+nIndex+count);
}

// -----------------------------------------------------------------------

void ibPGChoices::Clear()
{
    if ( m_data != ibPGChoicesEmptyData )
    {
        AllocExclusive();
        m_data->Clear();
    }
}

// -----------------------------------------------------------------------

int ibPGChoices::Index( const wxString& str ) const
{
    if ( IsOk() )
    {
        for ( unsigned int i = 0; i < m_data->GetCount(); i++ )
        {
            const ibPGChoiceEntry& entry = m_data->Item(i);
            if ( entry.HasText() && entry.GetText() == str )
                return i;
        }
    }
    return -1;
}

// -----------------------------------------------------------------------

int ibPGChoices::Index( int val ) const
{
    if ( IsOk() )
    {
        for ( unsigned int i = 0; i < m_data->GetCount(); i++ )
        {
            if ( GetValue(i) == val )
                return i;
        }
    }
    return -1;
}

// -----------------------------------------------------------------------

wxArrayString ibPGChoices::GetLabels() const
{
    wxArrayString arr;

    if ( IsOk() )
        for ( unsigned int i = 0; i < GetCount(); i++ )
            arr.push_back(GetLabel(i));

    return arr;
}

// -----------------------------------------------------------------------

wxArrayInt ibPGChoices::GetValuesForStrings( const wxArrayString& strings ) const
{
    wxArrayInt arr;

    if ( IsOk() )
    {
        for( const wxString& str : strings )
        {
            int index = Index(str);
            if ( index >= 0 )
                arr.Add(GetValue(index));
            else
                arr.Add(ibPG_INVALID_VALUE);
        }
    }

    return arr;
}

// -----------------------------------------------------------------------

wxArrayInt ibPGChoices::GetIndicesForStrings( const wxArrayString& strings,
                                              wxArrayString* unmatched ) const
{
    wxArrayInt arr;

    if ( IsOk() )
    {
        for( const wxString& str : strings )
        {
            int index = Index(str);
            if ( index >= 0 )
                arr.Add(index);
            else if ( unmatched )
                unmatched->Add(str);
        }
    }

    return arr;
}

// -----------------------------------------------------------------------

void ibPGChoices::AllocExclusive()
{
    EnsureData();

    if ( m_data->GetRefCount() != 1 )
    {
        ibPGChoicesData* data = new ibPGChoicesData();
        data->CopyDataFrom(m_data);
        Free();
        m_data = data;
    }
}

// -----------------------------------------------------------------------

void ibPGChoices::AssignData( ibPGChoicesData* data )
{
    Free();

    if ( data != ibPGChoicesEmptyData )
    {
        m_data = data;
        data->IncRef();
    }
}

// -----------------------------------------------------------------------

void ibPGChoices::Init()
{
    m_data = ibPGChoicesEmptyData;
}

// -----------------------------------------------------------------------

void ibPGChoices::Free()
{
    if ( m_data != ibPGChoicesEmptyData )
    {
        m_data->DecRef();
        m_data = ibPGChoicesEmptyData;
    }
}

// -----------------------------------------------------------------------
// ibPGAttributeStorage
// -----------------------------------------------------------------------

void ibPGAttributeStorage::Set(const wxString& name, const ibVariant& value)
{
    // If unspecified value, just remove from set
    if ( value.GetType() == ibValueTypes::TYPE_EMPTY )
    {
        m_map.erase(name);
        return;
    }

    m_map[name] = value;
}

ibVariant ibPGAttributeStorage::FindValue(const wxString& name) const
{
    auto it = m_map.find(name);
    if ( it != m_map.end() )
        return it->second;
    return ibVariant();
}

ibPGAttributeStorage::const_iterator ibPGAttributeStorage::StartIteration() const
{
    return m_map.begin();
}

bool ibPGAttributeStorage::GetNext(const_iterator& it, wxString& name, ibVariant& variant) const
{
    if ( it == m_map.end() )
        return false;

    name = it->first;
    variant = it->second;
    ++it;
    return true;
}

#endif  // wxUSE_PROPGRID
