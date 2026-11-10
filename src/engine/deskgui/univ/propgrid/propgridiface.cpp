// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, src/propgrid/propgridiface.cpp): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/propgrid/propgridiface.cpp
// Purpose:     ibPropertyGridInterface class
// Author:      Jaakko Salli
// Created:     2008-08-24
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"


#ifndef WX_PRECOMP
    #include "wx/bitmap.h"
    #include "wx/colour.h"
    #include "wx/font.h"
    #include "wx/log.h"
    #include "wx/textctrl.h"
    #include "wx/validate.h"
#endif

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/ctrl/textCtrl.h"   // ibTextCtrl — the editor whose length SetPropertyMaxLength limits

// ----------------------------------------------------------------------------
// VariantDatas
// ----------------------------------------------------------------------------

IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED(wxPoint, DESKGUI_API)
IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED(wxSize, DESKGUI_API)
IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_DUMMY_EQ(wxArrayInt, DESKGUI_API)
IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED(wxArrayString, DESKGUI_API)
IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED(wxFont, DESKGUI_API)
IB_PG_IMPLEMENT_VARIANT_DATA_EXPORTED(wxColour, DESKGUI_API)

// ----------------------------------------------------------------------------
// ibPGVariantList
// ----------------------------------------------------------------------------

// The same when each entry is: its child's name and its value, in order.
bool ibPGVariantList::CompareValueEQ( const ibVariant& data ) const
{
    const ibPGVariantList* other = nullptr;
    if ( !data.ConvertToValue(other) || other->m_entries.size() != m_entries.size() )
        return false;

    for ( size_t i = 0; i < m_entries.size(); i++ )
    {
        if ( m_entries[i].m_name != other->m_entries[i].m_name ||
             m_entries[i].m_value != other->m_entries[i].m_value )
            return false;
    }
    return true;
}

// -----------------------------------------------------------------------
// ibPGPropArgCls
// -----------------------------------------------------------------------

ibPGProperty* ibPGPropArgCls::GetPtr( ibPropertyGridInterface* iface ) const
{
    if ( m_isProperty )
    {
        wxASSERT_MSG( m_property, "invalid property ptr" );
        return m_property;
    }

    return iface->GetPropertyByNameA(m_name);
}

// -----------------------------------------------------------------------
// ibPropertyGridInterface
// -----------------------------------------------------------------------

void ibPropertyGridInterface::RefreshGrid( ibPropertyGridPageState* state )
{
    if ( !state )
        state = m_pState;

    ibPropertyGrid* grid = state->GetGrid();
    if ( grid->GetState() == state && !grid->IsFrozen() )
    {
        grid->Refresh();
    }
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridInterface::Append( ibPGProperty* property )
{
    ibPGProperty* retp = m_pState->DoAppend(property);

    ibPropertyGrid* grid = m_pState->GetGrid();
    if ( grid )
        grid->RefreshGrid();

    return retp;
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridInterface::AppendIn( ibPGPropArg id, ibPGProperty* newproperty )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(ibNullProperty)
    ibPGProperty* retp = m_pState->DoInsert(p, p->GetChildCount(), newproperty);
    return retp;
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridInterface::Insert( ibPGPropArg id, ibPGProperty* property )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(ibNullProperty)
    ibPGProperty* retp = m_pState->DoInsert(p->GetParent(), p->GetIndexInParent(), property);
    RefreshGrid();
    return retp;
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridInterface::Insert( ibPGPropArg id, int index, ibPGProperty* newproperty )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(ibNullProperty)
    ibPGProperty* retp = m_pState->DoInsert(p,index,newproperty);
    RefreshGrid();
    return retp;
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::DeleteProperty( ibPGPropArg id )
{
    ibPG_PROP_ARG_CALL_PROLOG()

    ibPropertyGridPageState* state = p->GetParentState();

    state->DoDelete( p, true );

    RefreshGrid(state);
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridInterface::RemoveProperty( ibPGPropArg id )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(ibNullProperty)

    wxCHECK( !p->HasAnyChild() || p->HasFlag(ibPGFlags::Aggregate),
             ibNullProperty);

    ibPropertyGridPageState* state = p->GetParentState();

    state->DoDelete( p, false );

    RefreshGrid(state);

    return p;
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridInterface::ReplaceProperty( ibPGPropArg id, ibPGProperty* property )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(ibNullProperty)

    ibPGProperty* replaced = p;
    wxCHECK_MSG( replaced && property,
                 ibNullProperty,
                 wxS("null property") );
    wxCHECK_MSG( !replaced->IsCategory(),
                 ibNullProperty,
                 wxS("cannot replace this type of property") );
    wxCHECK_MSG( !m_pState->IsInNonCatMode(),
                 ibNullProperty,
                 wxS("cannot replace properties in alphabetic mode") );

    // Get address to the slot
    ibPGProperty* parent = replaced->GetParent();
    int ind = replaced->GetIndexInParent();

    ibPropertyGridPageState* state = replaced->GetParentState();
    DeleteProperty(replaced); // Must use generic Delete
    state->DoInsert(parent,ind,property);

    return property;
}

// -----------------------------------------------------------------------
// ibPropertyGridInterface property operations
// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridInterface::GetSelection() const
{
    return m_pState->GetSelection();
}

// -----------------------------------------------------------------------

bool ibPropertyGridInterface::ClearSelection( bool validation )
{
    bool res = DoClearSelection(validation, ibPGSelectPropertyFlags::DontSendEvent);
    ibPropertyGrid* pg = GetPropertyGrid();
    if ( pg )
        pg->Refresh();
    return res;
}

// -----------------------------------------------------------------------

bool ibPropertyGridInterface::DoClearSelection( bool validation,
                                                ibPGSelectPropertyFlags selFlags )
{
    if ( !validation )
        selFlags |= ibPGSelectPropertyFlags::NoValidate;

    ibPropertyGridPageState* state = m_pState;

    if ( state )
    {
        ibPropertyGrid* pg = state->GetGrid();
        if ( pg->GetState() == state )
            return pg->DoSelectProperty(nullptr, selFlags);
        else
            state->DoSetSelection(nullptr);
    }

    return true;
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::LimitPropertyEditing( ibPGPropArg id, bool limit )
{
    ibPG_PROP_ARG_CALL_PROLOG()

    m_pState->DoLimitPropertyEditing(p, limit);
    RefreshProperty(p);
}

// -----------------------------------------------------------------------

bool ibPropertyGridInterface::EnableProperty( ibPGPropArg id, bool enable )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)

    ibPropertyGridPageState* state = p->GetParentState();
    ibPropertyGrid* grid = state->GetGrid();

    if ( enable )
    {
        if ( !p->HasFlag(ibPGFlags::Disabled) )
            return false;

        // If active, Set active Editor.
        if ( grid && grid->GetState() == state && p == grid->GetSelection() )
            grid->DoSelectProperty( p, ibPGSelectPropertyFlags::Force );
    }
    else
    {
        if ( p->HasFlag(ibPGFlags::Disabled) )
            return false;

        // If active, Disable as active Editor.
        if ( grid && grid->GetState() == state && p == grid->GetSelection() )
            grid->DoSelectProperty( p, ibPGSelectPropertyFlags::Force );
    }

    p->DoEnable(enable);

    RefreshProperty( p );

    return true;
}

void ibPropertyGridInterface::SetPropertyReadOnly( ibPGPropArg id, bool set, ibPGPropertyValuesFlags flags)
{
    ibPG_PROP_ARG_CALL_PROLOG()

    if ( !!(flags & ibPGPropertyValuesFlags::Recurse) )
    {
        p->SetFlagRecursively(ibPGFlags::ReadOnly, set);
    }
    else
    {
        // Do nothing if flag is already set as required.
        if ( set && p->HasFlag(ibPGFlags::ReadOnly) )
            return;
        if ( !set && !p->HasFlag(ibPGFlags::ReadOnly) )
            return;

        p->ChangeFlag(ibPGFlags::ReadOnly, set);
    }

    ibPropertyGridPageState* state = p->GetParentState();
    if( state )
    {
        // If property is attached to the property grid
        // then refresh the view.
        RefreshProperty( p );
    }
}

// -----------------------------------------------------------------------

bool ibPropertyGridInterface::ExpandAll( bool doExpand )
{
    ibPropertyGridPageState* state = m_pState;

    if ( !state->DoGetRoot()->HasAnyChild() )
        return true;

    ibPropertyGrid* pg = state->GetGrid();

    if ( GetSelection() && GetSelection() != state->DoGetRoot() &&
         !doExpand )
    {
        pg->DoClearSelection();
    }

    ibPGVIterator it;

    for ( it = GetVIterator( ibPG_ITERATE_ALL ); !it.AtEnd(); it.Next() )
    {
        ibPGProperty* p = it.GetProperty();
        if ( p->HasAnyChild() )
        {
            if ( doExpand )
            {
                if ( !p->IsExpanded() )
                {
                    state->DoExpand(p);
                }
            }
            else
            {
                if ( p->IsExpanded() )
                {
                    state->DoCollapse(p);
                }
            }
        }
    }

    pg->RecalculateVirtualSize();

    RefreshGrid();

    return true;
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::ClearModifiedStatus()
{
    unsigned int pageIndex = 0;

    ibPropertyGridPageState* page;
    while ( (page = GetPageState(pageIndex)) != nullptr )
    {
        page->DoGetRoot()->SetFlagRecursively(ibPGFlags::Modified, false);
        page->m_anyModified = false;

        pageIndex++;
    }

    // Update active editor control, if any
    GetPropertyGrid()->RefreshEditor();
}

bool ibPropertyGridInterface::SetColumnProportion( unsigned int column,
                                                   int proportion )
{
    wxCHECK(m_pState, false);
    ibPropertyGrid* pg = m_pState->GetGrid();
    wxCHECK(pg, false);
    wxCHECK(pg->HasFlag(ibPG_SPLITTER_AUTO_CENTER), false);
    m_pState->DoSetColumnProportion(column, proportion);
    return true;
}

// -----------------------------------------------------------------------
// ibPropertyGridInterface property value setting and getting
// -----------------------------------------------------------------------

void ibPGGetFailed( const ibPGProperty* p, const wxString& typestr )
{
    ibPGTypeOperationFailed(p, typestr, wxS("Get"));
}

// -----------------------------------------------------------------------

void ibPGTypeOperationFailed( const ibPGProperty* p,
                              const wxString& typestr,
                              const wxString& op )
{
    wxASSERT( p != nullptr );
    wxLogError( _("Type operation \"%s\" failed: Property labeled \"%s\" is of type \"%s\", NOT \"%s\"."),
        op, p->GetLabel(), p->GetValueType(), typestr );
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::SetPropVal( ibPGPropArg id, ibVariant& value )
{
    ibPG_PROP_ARG_CALL_PROLOG()

    if ( p )
        p->SetValue(value);
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::SetPropertyValueString( ibPGPropArg id, const wxString& value )
{
    ibPG_PROP_ARG_CALL_PROLOG()

    if ( p )
        m_pState->DoSetPropertyValueString(p, value);
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::SetValidationFailureBehavior( ibPGVFBFlags vfbFlags )
{
    GetPropertyGrid()->m_permanentValidationFailureBehavior = vfbFlags;
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridInterface::GetPropertyByNameA( const wxString& name ) const
{
    ibPGProperty* p = GetPropertyByName(name);
    wxASSERT_MSG(p,wxString::Format(wxS("no property with name '%s'"),name));
    return p;
}

// ----------------------------------------------------------------------------

ibPGProperty* ibPropertyGridInterface::GetPropertyByLabel( const wxString& label ) const
{
    return m_pState->BaseGetPropertyByLabel(label, nullptr);
}

// ----------------------------------------------------------------------------

void ibPropertyGridInterface::DoSetPropertyAttribute( ibPGPropArg id, const wxString& name,
                                                      ibVariant& value, ibPGPropertyValuesFlags flags )
{
    ibPG_PROP_ARG_CALL_PROLOG()

    p->SetAttribute( name, value ); // property is also refreshed here

    if ( !!(flags & ibPGPropertyValuesFlags::Recurse) )
    {
        for ( unsigned int i = 0; i < p->GetChildCount(); i++ )
            DoSetPropertyAttribute(p->Item(i), name, value, flags);
    }
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::SetPropertyAttributeAll( const wxString& attrName,
                                                       ibVariant value )
{
    unsigned int pageIndex = 0;

    ibPropertyGridPageState* page;
    while ( (page = GetPageState(pageIndex)) != nullptr )
    {
        DoSetPropertyAttribute(page->DoGetRoot(), attrName, value, ibPGPropertyValuesFlags::Recurse);

        pageIndex++;
    }
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::GetPropertiesWithFlag( ibArrayPGProperty* targetArr,
                                                     ibPGFlags flags,
                                                     bool inverse,
                                                     int iterFlags ) const
{
    wxASSERT( targetArr );
    ibPGVIterator it = GetVIterator( iterFlags );

    for ( ;
          !it.AtEnd();
          it.Next() )
    {
        ibPGProperty* property = it.GetProperty();

        if ( !inverse )
        {
            if ( property->HasFlagsExact(flags) )
                targetArr->push_back(property);
        }
        else
        {
            if ( !property->HasFlagsExact(flags) )
                targetArr->push_back(property);
        }
    }
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::SetBoolChoices( const wxString& trueChoice,
                                                 const wxString& falseChoice )
{
    ibPGGlobalVars->m_boolChoices[0] = falseChoice;
    ibPGGlobalVars->m_boolChoices[1] = trueChoice;
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridInterface::DoGetPropertyByName( const wxString& name ) const
{
    return m_pState->BaseGetPropertyByName(name);
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridInterface::GetPropertyByName( const wxString& name,
                                                             const wxString& subname ) const
{
    ibPGProperty* p = DoGetPropertyByName(name);
    if ( !p || !p->HasAnyChild() )
        return ibNullProperty;

    return p->GetPropertyByName(subname);
}

// -----------------------------------------------------------------------

// Since GetPropertyByName is used *a lot*, this makes sense
// since non-virtual method can be called with less code.
ibPGProperty* ibPropertyGridInterface::GetPropertyByName( const wxString& name ) const
{
    ibPGProperty* p = DoGetPropertyByName(name);
    if ( p )
        return p;

    // Check if it is "Property.SubProperty" format
    int pos = name.Find(wxS('.'));
    if ( pos <= 0 )
        return nullptr;

    return GetPropertyByName(name.substr(0,pos),
                             name.substr(pos+1,name.length()-pos-1));
}

// -----------------------------------------------------------------------

bool ibPropertyGridInterface::HideProperty(ibPGPropArg id, bool hide, ibPGPropertyValuesFlags flags)
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)

    // Do nothing if single property is already hidden/visible as requested.
    if ( !(flags & ibPGPropertyValuesFlags::Recurse) )
    {
        if ( hide && p->HasFlag(ibPGFlags::Hidden) )
            return false;
        if ( !hide && !p->HasFlag(ibPGFlags::Hidden) )
            return false;
    }

    ibPropertyGrid* pg = m_pState->GetGrid();

    if ( pg == p->GetGrid() )
        return pg->DoHideProperty(p, hide, flags);
    else
        m_pState->DoHideProperty(p, hide, flags);

    return true;
}

// -----------------------------------------------------------------------

bool ibPropertyGridInterface::Collapse( ibPGPropArg id )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
    ibPropertyGrid* pg = p->GetGridIfDisplayed();
    if ( pg )
        return pg->DoCollapse(p);

    return p->GetParentState()->DoCollapse(p);
}

// -----------------------------------------------------------------------

bool ibPropertyGridInterface::Expand( ibPGPropArg id )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
    ibPropertyGrid* pg = p->GetGridIfDisplayed();
    if ( pg )
        return pg->DoExpand(p);

    return p->GetParentState()->DoExpand(p);
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::Sort(ibPGPropertyValuesFlags flags)
{
    ibPropertyGrid* pg = GetPropertyGrid();

    unsigned int pageIndex = 0;

    ibPropertyGridPageState* page;
    while( (page = GetPageState(pageIndex)) != nullptr )
    {
        page->DoSort(flags);
        pageIndex++;
    }

    // Fix positions of any open editor controls
    if ( pg )
        pg->CorrectEditorWidgetPosY();
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::SetPropertyLabel( ibPGPropArg id, const wxString& newproplabel )
{
    ibPG_PROP_ARG_CALL_PROLOG()

    if ( p->GetLabel() == newproplabel )
        return;

    p->SetLabel( newproplabel );

    ibPropertyGridPageState* state = p->GetParentState();
    ibPropertyGrid* pg = state->GetGrid();

    if ( pg->HasFlag(ibPG_AUTO_SORT) )
        pg->SortChildren(p->GetParent());

    if ( pg->GetState() == state )
    {
        if ( pg->HasFlag(ibPG_AUTO_SORT) )
        {
            pg->Refresh();
            // If any property is selected it has to
            // be refreshed in the new location.
            if ( pg == p->GetGrid() && pg->GetSelectedProperty() )
            {
                RefreshProperty(pg->GetSelectedProperty());
            }
        }
        else
        {
            pg->DrawItem( p );
        }
    }
}

// -----------------------------------------------------------------------

bool ibPropertyGridInterface::SetPropertyMaxLength( ibPGPropArg id, int maxLen )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)

    if ( !p->SetMaxLength(maxLen) )
        return false;

    ibPropertyGrid* pg = m_pState->GetGrid();

    // Adjust control if selected currently
    if ( pg == p->GetGrid() && p == m_pState->GetSelection() )
    {
        wxWindow* wnd = pg->GetEditorControl();
        ibTextCtrl* tc = wxDynamicCast(wnd, ibTextCtrl);
        wxCHECK_MSG(tc, false, "Text ctrl is expected here");
        tc->SetMaxLength(maxLen);
    }

    return true;
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::SetPropertyBackgroundColour(ibPGPropArg id, const wxColour& colour,
                                                          ibPGPropertyValuesFlags flags )
{
    ibPG_PROP_ARG_CALL_PROLOG()
    p->SetBackgroundColour(colour, flags);

    // Redraw the control
    ibPropertyGrid* pg = m_pState->GetGrid();
    if ( pg == p->GetGrid() )
    {
        if ( !!(flags & ibPGPropertyValuesFlags::Recurse) )
            pg->DrawItemAndChildren(p);
        else
            pg->DrawItem(p);
    }
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::SetPropertyTextColour(ibPGPropArg id, const wxColour& colour,
                                                    ibPGPropertyValuesFlags flags)
{
    ibPG_PROP_ARG_CALL_PROLOG()
    p->SetTextColour(colour, flags);

    // Redraw the control
    ibPropertyGrid* pg = m_pState->GetGrid();
    if ( pg == p->GetGrid() )
    {
        if ( !!(flags & ibPGPropertyValuesFlags::Recurse) )
            pg->DrawItemAndChildren(p);
        else
            pg->DrawItem(p);
    }
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::SetPropertyColoursToDefault(ibPGPropArg id, ibPGPropertyValuesFlags flags)
{
    ibPG_PROP_ARG_CALL_PROLOG()
    p->SetDefaultColours(flags);

    // Redraw the control
    ibPropertyGrid* pg = m_pState->GetGrid();
    if ( pg == p->GetGrid() )
    {
        if ( !!(flags & ibPGPropertyValuesFlags::Recurse) )
            pg->DrawItemAndChildren(p);
        else
            pg->DrawItem(p);
    }
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::SetPropertyCell( ibPGPropArg id,
                                               int column,
                                               const wxString& text,
                                               const wxBitmapBundle& bitmap,
                                               const wxColour& fgCol,
                                               const wxColour& bgCol )
{
    ibPG_PROP_ARG_CALL_PROLOG()

    ibPGCell& cell = p->GetCell(column);
    if ( !text.empty() && text != ibPG_LABEL )
        cell.SetText(text);
    if ( bitmap.IsOk() )
        cell.SetBitmap(bitmap);
    if ( fgCol != wxNullColour )
        cell.SetFgCol(fgCol);
    if ( bgCol != wxNullColour )
        cell.SetBgCol(bgCol);
}

// -----------------------------------------------------------------------
// GetPropertyValueAsXXX methods

ibVariant ibPropertyGridInterface::GetPropertyValue(ibPGPropArg id)
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(ibVariant())
    return p->GetValue();
}

// String is different from others.
wxString ibPropertyGridInterface::GetPropertyValueAsString( ibPGPropArg id ) const
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(wxString())
    return p->GetValueAsString(ibPGPropValFormatFlags::FullValue);
}

bool ibPropertyGridInterface::GetPropertyValueAsBool( ibPGPropArg id ) const
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
    ibVariant value = p->GetValue();
    if ( value.GetType() == ibValueTypes::TYPE_BOOLEAN )
    {
        return value.GetBoolean();
    }
    if ( value.GetType() == ibValueTypes::TYPE_NUMBER )
    {
        return value.GetInteger()?true:false;
    }
    ibPGGetFailed(p, wxS("bool"));
    return false;
}

// the value asked by what it is (IsKind - its kind, or the class it holds), and named by PGTypeName when it is not
#define ibPG_PROP_ID_GETPROPVAL_CALL_PROLOG_RETVAL(IsKind, PGTypeName, DEFVAL) \
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(DEFVAL) \
    ibVariant value = p->GetValue(); \
    if ( !(IsKind) ) \
    { \
        ibPGGetFailed(p, PGTypeName); \
        return DEFVAL; \
    }

long ibPropertyGridInterface::GetPropertyValueAsLong(ibPGPropArg id) const
{
    ibPG_PROP_ID_GETPROPVAL_CALL_PROLOG_RETVAL(value.GetType() == ibValueTypes::TYPE_NUMBER, wxS("long"), 0L)
    return value.GetInteger();
}

double ibPropertyGridInterface::GetPropertyValueAsDouble(ibPGPropArg id) const
{
    ibPG_PROP_ID_GETPROPVAL_CALL_PROLOG_RETVAL(value.GetType() == ibValueTypes::TYPE_NUMBER, wxS("double"), 0.0)
    return value.GetDouble();
}

wxArrayString ibPropertyGridInterface::GetPropertyValueAsArrayString(ibPGPropArg id) const
{
    ibPG_PROP_ID_GETPROPVAL_CALL_PROLOG_RETVAL(value.GetClassName() == wxArrayStringVariantData::s_className,
                                               wxS("wxArrayString"), wxArrayString())
    return wxArrayStringRefFromVariant(value);
}

wxLongLong_t ibPropertyGridInterface::GetPropertyValueAsLongLong(ibPGPropArg id) const
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(0)
    long long result = 0;
    p->GetValue().GetNumber().ToInt(result);
    return result;
}

wxULongLong_t ibPropertyGridInterface::GetPropertyValueAsULongLong(ibPGPropArg id) const
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(0)
    unsigned long long result = 0;
    p->GetValue().GetNumber().ToInt(result);
    return result;
}

wxArrayInt ibPropertyGridInterface::GetPropertyValueAsArrayInt(ibPGPropArg id) const
{
    ibPG_PROP_ID_GETPROPVAL_CALL_PROLOG_RETVAL(value.GetClassName() == wxArrayIntVariantData::s_className,
                                               wxS("wxArrayInt"), wxArrayInt())
    wxArrayInt arr;
    arr << value;
    return arr;
}

#if wxUSE_DATETIME
wxDateTime ibPropertyGridInterface::GetPropertyValueAsDateTime(ibPGPropArg id) const
{
    ibPG_PROP_ID_GETPROPVAL_CALL_PROLOG_RETVAL(value.GetType() == ibValueTypes::TYPE_DATE, wxS("datetime"), wxDateTime())
    return typeConv::DateTimeToWxDateTime(value.GetDate());
}
#endif

bool ibPropertyGridInterface::IsPropertyExpanded( ibPGPropArg id ) const
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
    return p->IsExpanded();
}

// -----------------------------------------------------------------------
// ibPropertyGridInterface wrappers
// -----------------------------------------------------------------------

bool ibPropertyGridInterface::ChangePropertyValue( ibPGPropArg id, ibVariant newValue )
{
    return GetPropertyGrid()->ChangePropertyValue(id, newValue);
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::BeginAddChildren( ibPGPropArg id )
{
    ibPG_PROP_ARG_CALL_PROLOG()
    wxCHECK_RET( p->HasFlag(ibPGFlags::Aggregate), wxS("only call on properties with fixed children") );
    p->ClearFlag(ibPGFlags::Aggregate);
    p->SetFlag(ibPGFlags::MiscParent);
}

// -----------------------------------------------------------------------

bool ibPropertyGridInterface::EditorValidate()
{
    return GetPropertyGrid()->DoEditorValidate();
}

// -----------------------------------------------------------------------

void ibPropertyGridInterface::EndAddChildren( ibPGPropArg id )
{
    ibPG_PROP_ARG_CALL_PROLOG()
    wxCHECK_RET( p->HasFlag(ibPGFlags::MiscParent), wxS("only call on properties for which BeginAddChildren was called prior") );
    p->ClearFlag(ibPGFlags::MiscParent);
    p->SetFlag(ibPGFlags::Aggregate);
}

// -----------------------------------------------------------------------
// ibPGVIterator_State
// -----------------------------------------------------------------------

// Default returned by ibPropertyGridInterface::GetVIterator().
class ibPGVIteratorBase_State : public ibPGVIteratorBase
{
public:
    ibPGVIteratorBase_State( ibPropertyGridPageState* state, int flags )
    {
        m_it.Init( state, flags );
    }
    virtual ~ibPGVIteratorBase_State() = default;
    virtual void Next() override { m_it.Next(); }
    virtual bool AtEnd() const override { return m_it.AtEnd(); }
};

ibPGVIterator ibPropertyGridInterface::GetVIterator( int flags ) const
{
    return ibPGVIterator( new ibPGVIteratorBase_State( m_pState, flags ) );
}

// -----------------------------------------------------------------------
// ibPGEditableState related functions
// -----------------------------------------------------------------------

// EscapeDelimiters() changes ";" into "\;" and "|" into "\|"
// in the input string.  This is an internal functions which is
// used for saving states
// NB: Similar function exists in aui/framemanager.cpp
static wxString EscapeDelimiters(const wxString& s)
{
    wxString result;
    result.reserve(s.length());

    for ( wxStringCharType ch : s )
    {
        if ( ch == wxS(';') || ch == wxS('|') || ch == wxS(',') )
            result += wxS('\\');
        result += ch;
    }

    return result;
}

wxString ibPropertyGridInterface::SaveEditableState( int includedStates ) const
{
    wxString result;

    //
    // Save state on page basis
    std::vector<ibPropertyGridPageState*> pageStates;
    unsigned int pageIndex = 0;
    ibPropertyGridPageState* page;
    while ( (page = GetPageState(pageIndex)) != nullptr )
    {
        pageStates.push_back(page);
        pageIndex++;
    }

    for( ibPropertyGridPageState* pageState : pageStates )
    {
        if ( includedStates & SelectionState )
        {
            wxString sel;
            if ( pageState->GetSelection() )
                sel = pageState->GetSelection()->GetName();
            result += wxS("selection=");
            result += EscapeDelimiters(sel);
            result += wxS(";");
        }
        if ( includedStates & ExpandedState )
        {
            ibPropertyGridConstIterator it =
                ibPropertyGridConstIterator( pageState,
                                             ibPG_ITERATE_ALL_PARENTS_RECURSIVELY|ibPG_ITERATE_HIDDEN,
                                             ibNullProperty );

            result += wxS("expanded=");

            for ( ;
                  !it.AtEnd();
                  it.Next() )
            {
                const ibPGProperty* p = it.GetProperty();

                if ( !p->HasFlag(ibPGFlags::Collapsed) )
                    result += EscapeDelimiters(p->GetName());
                result += wxS(",");

            }

            if ( result.Last() == wxS(',') )
                result.RemoveLast();

            result += wxS(";");
        }
        if ( includedStates & ScrollPosState )
        {
            int x, y;
            GetPropertyGrid()->GetViewStart(&x,&y);
            result += wxString::Format(wxS("scrollpos=%i,%i;"), x, y);
        }
        if ( includedStates & SplitterPosState )
        {
            result += wxS("splitterpos=");

            for ( size_t i=0; i<pageState->GetColumnCount(); i++ )
                result += wxString::Format(wxS("%i,"), pageState->DoGetSplitterPosition(i));

            result.RemoveLast();  // Remove last comma
            result += wxS(";");
        }
        if ( includedStates & PageState )
        {
            result += wxS("ispageselected=");

            if ( GetPageState(-1) == pageState )
                result += wxS("1;");
            else
                result += wxS("0;");
        }
        if ( includedStates & DescBoxState )
        {
            ibVariant v = GetEditableStateItem(wxS("descboxheight"));
            if ( v.GetType() != ibValueTypes::TYPE_EMPTY )
                result += wxString::Format(wxS("descboxheight=%i;"), (int)v.GetInteger());
        }
        result.RemoveLast();  // Remove last semicolon
        result += wxS("|");
    }

    // Remove last '|'
    if ( !result.empty() )
        result.RemoveLast();

    return result;
}

bool ibPropertyGridInterface::RestoreEditableState( const wxString& src, int restoreStates )
{
    ibPropertyGrid* pg = GetPropertyGrid();
    ibPGProperty* newSelection = nullptr;
    size_t pageIndex;
    long vx = -1;
    long vy = -1;
    long selectedPage = -1;
    bool pgSelectionSet = false;
    bool res = true;

    pg->Freeze();
    wxArrayString pageStrings = ::wxSplit(src, wxS('|'), wxS('\\'));

    for ( pageIndex=0; pageIndex<pageStrings.size(); pageIndex++ )
    {
        ibPropertyGridPageState* pageState = GetPageState(pageIndex);
        if ( !pageState )
            break;

        wxArrayString kvpairStrings = ::wxSplit(pageStrings[pageIndex], wxS(';'), wxS('\\'));

        for( const wxString& kvs : kvpairStrings )
        {
            int eq_pos = kvs.Find(wxS('='));
            if ( eq_pos != wxNOT_FOUND )
            {
                wxString key = kvs.substr(0, eq_pos);
                wxString value = kvs.substr(eq_pos+1);

                // Further split value by commas
                wxArrayString values = ::wxSplit(value, wxS(','), wxS('\\'));

                if ( key == wxS("expanded") )
                {
                    if ( restoreStates & ExpandedState )
                    {
                        ibPropertyGridIterator it =
                            ibPropertyGridIterator( pageState,
                                                    ibPG_ITERATE_ALL,
                                                    ibNullProperty );

                        // First collapse all
                        for ( ; !it.AtEnd(); it.Next() )
                        {
                            ibPGProperty* p = it.GetProperty();
                            pageState->DoCollapse(p);
                        }

                        // Then expand those which names are in values
                        for( const wxString& name : values )
                        {
                            ibPGProperty* prop = GetPropertyByName(name);
                            if ( prop )
                                pageState->DoExpand(prop);
                        }
                    }
                }
                else if ( key == wxS("scrollpos") )
                {
                    if ( restoreStates & ScrollPosState )
                    {
                        if ( values.size() == 2 )
                        {
                            values[0].ToLong(&vx);
                            values[1].ToLong(&vy);
                        }
                        else
                        {
                            res = false;
                        }
                    }
                }
                else if ( key == wxS("splitterpos") )
                {
                    if ( restoreStates & SplitterPosState )
                    {
                        for ( size_t n=0; n<values.size(); n++ )
                        {
                            long pos = 0;
                            values[n].ToLong(&pos);
                            if ( pos > 0 )
                                pageState->DoSetSplitter(pos, n);
                        }
                    }
                }
                else if ( key == wxS("selection") )
                {
                    if ( restoreStates & SelectionState )
                    {
                        if ( !values.empty() )
                        {
                            if ( pageState->IsDisplayed() )
                            {
                                if ( !values[0].empty() )
                                    newSelection = GetPropertyByName(value);
                                pgSelectionSet = true;
                            }
                            else
                            {
                                if ( !values[0].empty() )
                                    pageState->DoSetSelection(GetPropertyByName(value));
                                else
                                    pageState->DoClearSelection();
                            }
                        }
                    }
                }
                else if ( key == wxS("ispageselected") )
                {
                    if ( restoreStates & PageState )
                    {
                        long pageSelStatus;
                        if ( values.size() == 1 && values[0].ToLong(&pageSelStatus) )
                        {
                            if ( pageSelStatus )
                                selectedPage = pageIndex;
                        }
                        else
                        {
                            res = false;
                        }
                    }
                }
                else if ( key == wxS("descboxheight") )
                {
                    if ( restoreStates & DescBoxState )
                    {
                        long descBoxHeight;
                        if ( values.size() == 1 && values[0].ToLong(&descBoxHeight) )
                        {
                            SetEditableStateItem(wxS("descboxheight"), descBoxHeight);
                        }
                        else
                        {
                            res = false;
                        }
                    }
                }
                else
                {
                    res = false;
                }
            }
        }
    }

    //
    // Force recalculation of virtual heights of all pages
    // (may be needed on unclean source string).
    pageIndex = 0;
    ibPropertyGridPageState* pageState;
    while ( (pageState = GetPageState(pageIndex)) != nullptr )
    {
        pageState->VirtualHeightChanged();
        pageIndex++;
    }

    pg->Thaw();

    //
    // Selection of visible grid page must be set after Thaw() call
    if ( pgSelectionSet )
    {
        if ( newSelection )
            pg->DoSelectProperty(newSelection);
        else
            pg->DoClearSelection();
    }

    if ( selectedPage != -1 )
    {
        ibPropertyGridPageState* curPageState = GetPageState(-1);
        DoSelectPage(selectedPage);
        if ( GetPageState(-1) != curPageState )
        {
            pg->SendEvent(ibEVT_PG_SELECTED, pg->GetSelectedProperty());
        }
    }

    if ( vx >= 0 )
    {
        pg->Scroll(vx, vy);
    }

    return res;
}

