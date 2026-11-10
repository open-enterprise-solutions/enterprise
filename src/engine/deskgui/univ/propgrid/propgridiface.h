// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, include/wx/propgrid/propgridiface.h): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/propgrid/propgridiface.h
// Purpose:     ibPropertyGridInterface class
// Author:      Jaakko Salli
// Created:     2008-08-24
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_PROPGRID_PROPGRIDIFACE_H_
#define _DESKGUI_UNIV_PROPGRID_PROPGRIDIFACE_H_

#include "wx/defs.h"

#if wxUSE_PROPGRID

#include "deskgui/univ/propgrid/property.h"
#include "deskgui/univ/propgrid/propgridpagestate.h"

// -----------------------------------------------------------------------
// ibPropertyGrid Validation Failure behaviour Flags
enum class ibPGVFBFlags : int
{
    // No flags
    Null                   = 0,
    // Prevents user from leaving property unless value is valid. If this
    // behaviour flag is not used, then value change is instead cancelled.
    StayInProperty         = 0x0001,
    // Calls wxBell() on validation failure.
    Beep                   = 0x0002,
    // Cell with invalid value will be marked (with red colour).
    MarkCell               = 0x0004,
    // Display a text message explaining the situation.
    // To customize the way the message is displayed, you need to
    // reimplement ibPropertyGrid::DoShowPropertyError() in a
    // derived class. Default behaviour is to display the text on
    // the top-level frame's status bar, if present, and otherwise
    // using wxMessageBox.
    ShowMessage            = 0x0008,
    // Similar to SHOW_MESSAGE, except always displays the
    // message using wxMessageBox.
    ShowMessageBox         = 0x0010,
    // Similar to SHOW_MESSAGE, except always displays the
    // message on the status bar (when present - you can reimplement
    // ibPropertyGrid::GetStatusBar() in a derived class to specify
    // this yourself).
    ShowMessageOnStatusBar = 0x0020,
    // Defaults.
    Default = MarkCell | ShowMessageBox,
    // Only used internally.
    Undefined              = 0x0040
};

constexpr ibPGVFBFlags operator|(ibPGVFBFlags a, ibPGVFBFlags b)
{
    return static_cast<ibPGVFBFlags>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr ibPGVFBFlags operator&(ibPGVFBFlags a, ibPGVFBFlags b)
{
    return static_cast<ibPGVFBFlags>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr ibPGVFBFlags operator~(ibPGVFBFlags a)
{
    return static_cast<ibPGVFBFlags>(~static_cast<int>(a));
}

constexpr bool operator!(ibPGVFBFlags a)
{
    return static_cast<int>(a) == 0;
}

// -----------------------------------------------------------------------

// Most property grid functions have this type as their argument, as it can
// convey a property by either a pointer or name.
class DESKGUI_API ibPGPropArgCls
{
public:
    ibPGPropArgCls( const ibPGProperty* property )
    {
        m_property = const_cast<ibPGProperty*>(property);
        m_isProperty = true;
    }
    ibPGPropArgCls( const wxString& str )
    {
        m_name = str;
        m_property = nullptr;
        m_isProperty = false;
    }
    ibPGPropArgCls( const ibPGPropArgCls& id )
    {
        m_isProperty = id.m_isProperty;
        m_property = id.m_property;
        m_name = id.m_name;
    }
    // This is only needed for wxPython bindings.
    ibPGPropArgCls( wxString* str, bool WXUNUSED(deallocPtr) )
    {
        m_name = *str;
        delete str; // we own this string
        m_property = nullptr;
        m_isProperty = false;
    }
    ibPGProperty* GetPtr() const
    {
        wxCHECK( m_isProperty, nullptr );
        return m_property;
    }
#ifndef wxNO_IMPLICIT_WXSTRING_ENCODING
    ibPGPropArgCls( const char* str )
    {
        m_name = str;
        m_property = nullptr;
        m_isProperty = false;
    }
#endif // !wxNO_IMPLICIT_WXSTRING_ENCODING
    ibPGPropArgCls( const wchar_t* str )
    {
        m_name = str;
        m_property = nullptr;
        m_isProperty = false;
    }
    // This constructor is required for null.
    ibPGPropArgCls( int )
    {
        m_property = nullptr;
        m_isProperty = true;
    }
    ibPGProperty* GetPtr( ibPropertyGridInterface* iface ) const;
    ibPGProperty* GetPtr( const ibPropertyGridInterface* iface ) const
    {
        return GetPtr(const_cast<ibPropertyGridInterface*>(iface));
    }
    ibPGProperty* GetPtr0() const { return m_property; }
    bool HasName() const { return !m_isProperty; }
    const wxString& GetName() const { return m_name; }

private:
    bool m_isProperty;
    ibPGProperty* m_property;
    wxString m_name;
};

typedef const ibPGPropArgCls& ibPGPropArg;

// -----------------------------------------------------------------------

DESKGUI_API
void ibPGTypeOperationFailed( const ibPGProperty* p,
                              const wxString& typestr,
                              const wxString& op );
DESKGUI_API
void ibPGGetFailed( const ibPGProperty* p, const wxString& typestr );

// -----------------------------------------------------------------------

// Helper macro that does necessary preparations when calling
// some ibPGProperty's member function.
#define ibPG_PROP_ARG_CALL_PROLOG_0(PROPERTY) \
    PROPERTY *p = (PROPERTY*)id.GetPtr(this); \
    if ( !p ) return;

#define ibPG_PROP_ARG_CALL_PROLOG_RETVAL_0(PROPERTY, RETVAL) \
    PROPERTY *p = (PROPERTY*)id.GetPtr(this); \
    if ( !p ) return RETVAL;

#define ibPG_PROP_ARG_CALL_PROLOG() \
    ibPG_PROP_ARG_CALL_PROLOG_0(ibPGProperty)

#define ibPG_PROP_ARG_CALL_PROLOG_RETVAL(RVAL) \
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL_0(ibPGProperty, RVAL)

#define ibPG_PROP_ID_CONST_CALL_PROLOG() \
    ibPG_PROP_ARG_CALL_PROLOG_0(const ibPGProperty)

#define ibPG_PROP_ID_CONST_CALL_PROLOG_RETVAL(RVAL) \
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL_0(const ibPGProperty, RVAL)

// -----------------------------------------------------------------------


// Most of the shared property manipulation interface shared by ibPropertyGrid,
// ibPropertyGridPage, and ibPropertyGridManager is defined in this class.
// In separate ibPropertyGrid component this class was known as
// ibPropertyContainerMethods.
// ibPropertyGridInterface's property operation member functions all accept
// a special ibPGPropArg id argument, using which you can refer to properties
// either by their pointer (for performance) or by their name (for conveniency).
class DESKGUI_API ibPropertyGridInterface
{
public:

    // Destructor.
    virtual ~ibPropertyGridInterface() = default;

    // Appends property to the list.
    // ibPropertyGrid assumes ownership of the object.
    // Becomes child of most recently added category.
    // ibPropertyGrid takes the ownership of the property pointer.
    // If appending a category with name identical to a category already in
    // the ibPropertyGrid, then newly created category is deleted, and most
    // recently added category (under which properties are appended) is set
    // to the one with same name. This allows easier adding of items to same
    // categories in multiple passes.
    // Does not automatically redraw the control, so you may need to call
    // Refresh when calling this function after control has been shown for
    // the first time.
    // This functions deselects selected property, if any. Validation
    // failure option ibPGVFBFlags::StayInProperty is not respected, ie.
    // selection is cleared even if editor had invalid value.
    ibPGProperty* Append( ibPGProperty* property );

    // Same as Append(), but appends under given parent property.
    ibPGProperty* AppendIn( ibPGPropArg id, ibPGProperty* newproperty );

    // In order to add new items into a property with fixed children (for
    // instance, ibFlagsProperty), you need to call this method. After
    // populating has been finished, you need to call EndAddChildren.
    void BeginAddChildren( ibPGPropArg id );

    // Deletes all properties.
    virtual void Clear() = 0;

    // Clears current selection, if any.
    // validation - If set to false, deselecting the property will always work,
    //   even if its editor had invalid value in it.
    // Returns true if successful or if there was no selection. May
    // fail if validation was enabled and active editor had invalid
    // value.
    bool ClearSelection( bool validation = false );

    // Resets modified status of all properties.
    void ClearModifiedStatus();

    // Collapses given category or property with children.
    // Returns true if actually collapses.
    bool Collapse( ibPGPropArg id );

    // Collapses all items that can be collapsed.
    // Return false if failed (may fail if editor value cannot be validated).
    bool CollapseAll() { return ExpandAll(false); }

    // Changes value of a property, as if from an editor.
    // Use this instead of SetPropertyValue() if you need the value to run
    // through validation process, and also send the property change event.
    // Returns true if value was successfully changed.
    bool ChangePropertyValue( ibPGPropArg id, ibVariant newValue );

    // Removes and deletes a property and any children.
    // id - Pointer or name of a property.
    // If you delete a property in a ibPropertyGrid event
    // handler, the actual deletion is postponed until the next
    // idle event.
    // This functions deselects selected property, if any.
    // Validation failure option ibPGVFBFlags::StayInProperty is not
    // respected, ie. selection is cleared even if editor had
    // invalid value.
    void DeleteProperty( ibPGPropArg id );

    // Removes a property. Does not delete the property object, but
    // instead returns it.
    // id - Pointer or name of a property.
    // Removed property cannot have any children.
    // Also, if you remove property in a ibPropertyGrid event
    // handler, the actual removal is postponed until the next
    // idle event.
    ibPGProperty* RemoveProperty( ibPGPropArg id );

    // Disables a property.
    bool DisableProperty( ibPGPropArg id ) { return EnableProperty(id,false); }

    // Returns true if all property grid data changes have been committed.
    // Usually only returns false if value in active editor has been
    // invalidated by a wxValidator.
    bool EditorValidate();

    // Enables or disables property, depending on whether enable is true or
    // false. Disabled property usually appears as having grey text.
    // id - Name or pointer to a property.
    // enable - If false, property is disabled instead.
    bool EnableProperty( ibPGPropArg id, bool enable = true );

    // Called after population of property with fixed children has finished.
    void EndAddChildren( ibPGPropArg id );

    // Expands given category or property with children.
    // Returns true if actually expands.
    bool Expand( ibPGPropArg id );

    // Expands all items that can be expanded.
    bool ExpandAll( bool expand = true );

    // Returns id of first child of given property.
    // Does not return sub-properties!
    ibPGProperty* GetFirstChild( ibPGPropArg id )
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(ibNullProperty)

        if ( !p->HasAnyChild() || p->HasFlag(ibPGFlags::Aggregate) )
            return ibNullProperty;

        return p->Item(0);
    }

    // Returns iterator class instance.
    // flags - See ibPG_ITERATOR_FLAGS. Value ibPG_ITERATE_DEFAULT causes
    //   iteration over everything except private child properties.
    // firstProp - Property to start iteration from. If nullptr, then first
    //   child of root is used.
    ibPropertyGridIterator GetIterator( int flags = ibPG_ITERATE_DEFAULT,
                                        ibPGProperty* firstProp = nullptr )
    {
        return ibPropertyGridIterator( m_pState, flags, firstProp );
    }

    ibPropertyGridConstIterator
    GetIterator( int flags = ibPG_ITERATE_DEFAULT,
                 ibPGProperty* firstProp = nullptr ) const
    {
        return ibPropertyGridConstIterator( m_pState, flags, firstProp );
    }

    // Returns iterator class instance.
    // flags - See ibPG_ITERATOR_FLAGS. Value ibPG_ITERATE_DEFAULT causes
    //   iteration over everything except private child properties.
    // startPos - Either wxTOP or wxBOTTOM. wxTOP will indicate that iterations start
    //   from the first property from the top, and wxBOTTOM means that the
    //   iteration will instead begin from bottommost valid item.
    ibPropertyGridIterator GetIterator( int flags, int startPos )
    {
        return ibPropertyGridIterator( m_pState, flags, startPos );
    }

    ibPropertyGridConstIterator GetIterator( int flags, int startPos ) const
    {
        return ibPropertyGridConstIterator( m_pState, flags, startPos );
    }

    // Returns id of first item that matches given criteria.
    ibPGProperty* GetFirst( int flags = ibPG_ITERATE_ALL )
    {
        ibPropertyGridIterator it( m_pState, flags, ibNullProperty, 1 );
        return *it;
    }

    const ibPGProperty* GetFirst( int flags = ibPG_ITERATE_ALL ) const
    {
        return const_cast<ibPropertyGridInterface*>(this)->GetFirst(flags);
    }

    // Returns pointer to a property with given name (case-sensitive).
    // If there is no property with such name, null pointer is returned.
    // Properties which have non-category, non-root parent
    // cannot be accessed globally by their name. Instead, use
    // "<property>.<subproperty>" instead of "<subproperty>".
    ibPGProperty* GetProperty( const wxString& name ) const
    {
        return GetPropertyByName(name);
    }

    // Returns map-like storage of property's attributes.
    // Note that if extra style ibPG_EX_WRITEONLY_BUILTIN_ATTRIBUTES is set,
    // then builtin-attributes are not included in the storage.
    const ibPGAttributeStorage& GetPropertyAttributes( ibPGPropArg id ) const
    {
        // If 'id' refers to invalid property, then we will return dummy
        // attributes (i.e. root property's attributes, which contents
        // should always be empty and of no consequence).
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(m_pState->DoGetRoot()->GetAttributes())
        return p->GetAttributes();
    }

    // Adds to 'targetArr' pointers to properties that have given
    // flags 'flags' set. However, if 'inverse' is set to true, then
    // only properties without given flags are stored.
    // flags - Property flags to use.
    // iterFlags - Iterator flags to use. Default is everything expect private children.
    void GetPropertiesWithFlag(ibArrayPGProperty* targetArr,
                               ibPGFlags flags,
                               bool inverse = false,
                               int iterFlags = ibPG_ITERATE_PROPERTIES |
                                               ibPG_ITERATE_HIDDEN |
                                               ibPG_ITERATE_CATEGORIES) const;

    // Returns value of given attribute. If none found, returns null ibVariant.
    ibVariant GetPropertyAttribute( ibPGPropArg id,
                                    const wxString& attrName ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(ibVariant())
        return p->GetAttribute(attrName);
    }

    // Returns pointer of property's nearest parent category. If no category
    // found, returns nullptr.
    ibPGCategoryProperty* GetPropertyCategory( ibPGPropArg id ) const
    {
        ibPG_PROP_ID_CONST_CALL_PROLOG_RETVAL(nullptr)
        return m_pState->GetPropertyCategory(p);
    }

    // Returns client data (void*) of a property.
    void* GetPropertyClientData( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(nullptr)
        return p->GetClientData();
    }

    // Returns first property which label matches given string.
    // nullptr if none found. Note that this operation is extremely slow when
    // compared to GetPropertyByName().
    ibPGProperty* GetPropertyByLabel( const wxString& label ) const;

    // Returns pointer to a property with given name (case-sensitive).
    // If there is no property with such name, null pointer is returned.
    ibPGProperty* GetPropertyByName( const wxString& name ) const;

    // Returns child property 'subname' of property 'name'. Same as
    // calling GetPropertyByName("name.subname"), albeit slightly faster.
    ibPGProperty* GetPropertyByName( const wxString& name,
                                     const wxString& subname ) const;

    // Returns property's editor.
    const ibPGEditor* GetPropertyEditor( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(nullptr)
        return p->GetEditorClass();
    }

    // Returns help string associated with a property.
    wxString GetPropertyHelpString( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(wxString())
        return p->GetHelpString();
    }

    // Returns property's custom value image (nullptr if none).
    wxBitmap* GetPropertyImage( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(nullptr)
        return p->GetValueImage();
    }

    // Returns label of a property.
    const wxString& GetPropertyLabel( ibPGPropArg id )
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(m_emptyString)
        return p->GetLabel();
    }

    // Returns name of a property, by which it is globally accessible.
    wxString GetPropertyName( ibPGProperty* property )
    {
        return property->GetName();
    }

    // Returns parent item of a property.
    ibPGProperty* GetPropertyParent( ibPGPropArg id )
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(ibNullProperty)
        return p->GetParent();
    }

#if wxUSE_VALIDATORS
    // Returns validator of a property as a reference, which you
    // can pass to any number of SetPropertyValidator.
    wxValidator* GetPropertyValidator( ibPGPropArg id )
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(nullptr)
        return p->GetValidator();
    }
#endif

    // Returns value as ibVariant. To get wxObject pointer from it,
    // you will have to use IB_PG_VARIANT_TO_WXOBJECT(VARIANT,CLASSNAME) macro.
    // If property value is unspecified, null ibVariant is returned.
    ibVariant GetPropertyValue(ibPGPropArg id);

    wxString GetPropertyValueAsString( ibPGPropArg id ) const;
    long GetPropertyValueAsLong( ibPGPropArg id ) const;
    unsigned long GetPropertyValueAsULong( ibPGPropArg id ) const
    {
        return (unsigned long) GetPropertyValueAsLong(id);
    }
    int GetPropertyValueAsInt( ibPGPropArg id ) const
        { return (int)GetPropertyValueAsLong(id); }
    bool GetPropertyValueAsBool( ibPGPropArg id ) const;
    double GetPropertyValueAsDouble( ibPGPropArg id ) const;

    wxArrayString GetPropertyValueAsArrayString(ibPGPropArg id) const;

    wxLongLong_t GetPropertyValueAsLongLong(ibPGPropArg id) const;

    wxULongLong_t GetPropertyValueAsULongLong(ibPGPropArg id) const;

    wxArrayInt GetPropertyValueAsArrayInt(ibPGPropArg id) const;

#if wxUSE_DATETIME
    wxDateTime GetPropertyValueAsDateTime(ibPGPropArg id) const;
#endif

    // Returns a ibVariant list containing ibVariant versions of all
    // property values. Order is not guaranteed.
    // flags - Use ibPGPropertyValuesFlags::KeepStructure to retain category structure; each sub
    // category will be its own ibPGVariantList.
    // Use ibPGPropertyValuesFlags::IncAttributes to include property attributes as well.
    // Each attribute will be stored as list variant named
    // "@<propname>@attr."
    ibVariant GetPropertyValues(const wxString& listname = wxString(),
                                ibPGProperty* baseparent = nullptr,
                                ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::DontRecurse) const
    {
        return m_pState->DoGetPropertyValues(listname, baseparent, flags);
    }

    // Returns currently selected property. nullptr if none.
    // When ibPG_EX_MULTIPLE_SELECTION extra style is used, this
    // member function returns the focused property, that is the
    // one which can have active editor.
    ibPGProperty* GetSelection() const;

    // Returns list of currently selected properties.
    // ibArrayPGProperty should be compatible with std::vector API.
    const ibArrayPGProperty& GetSelectedProperties() const
    {
        return m_pState->m_selection;
    }

    ibPropertyGridPageState* GetState() const { return m_pState; }

    // Similar to GetIterator(), but instead returns ibPGVIterator instance,
    // which can be useful for forward-iterating through arbitrary property
    // containers.
    // flags - See ibPG_ITERATOR_FLAGS.
    virtual ibPGVIterator GetVIterator( int flags ) const;

    // Hides or reveals a property.
    // hide - If true, hides property, otherwise reveals it.
    // flags - By default changes are applied recursively. Set this parameter
    //   ibPGPropertyValuesFlags::DontRecurse to prevent this.
    bool HideProperty(ibPGPropArg id, bool hide = true,
                      ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::Recurse);

#if ibPG_INCLUDE_ADVPROPS
    // Initializes *all* property types. Causes references to most object
    // files in the library, so calling this may cause significant increase
    // in executable size when linking with static library.
    static void InitAllTypeHandlers();
#else
    static void InitAllTypeHandlers() { }
#endif

    // Inserts property to the property container.
    // priorThis - New property is inserted just prior to this. Available only
    //   in the first variant. There are two versions of this function
    //   to allow this parameter to be either an id or name to
    //   a property.
    // newproperty - Pointer to the inserted property. ibPropertyGrid will take
    //   ownership of this object.
    // Returns id for the property,
    // ibPropertyGrid takes the ownership of the property pointer.
    // While Append may be faster way to add items, make note that when
    // both types of data storage (categoric and
    // non-categoric) are active, Insert becomes even more slow. This is
    // especially true if current mode is non-categoric.
    // Example of use:
    //  // append category
    //  ibPGProperty* my_cat_id = propertygrid->Append(
    //     new ibPGCategoryProperty("My Category") );
    //  ...
    //  // insert into category - using second variant
    //  ibPGProperty* my_item_id_1 = propertygrid->Insert(
    //     my_cat_id, 0, new ibStringProperty("My String 1") );
    // // insert before to first item - using first variant
    //  ibPGProperty* my_item_id_2 = propertygrid->Insert(
    //     my_item_id, new ibStringProperty("My String 2") );
    ibPGProperty* Insert( ibPGPropArg priorThis, ibPGProperty* newproperty );

    // Inserts property to the property container.
    //See the other overload for more details.
    // parent - New property is inserted under this category. Available only
    //   in the second variant. There are two versions of this function
    //   to allow this parameter to be either an id or name to
    //   a property.
    // index - Index under category. Available only in the second variant.
    //   If index is < 0, property is appended in category.
    // newproperty - Pointer to the inserted property. ibPropertyGrid will take
    // ownership of this object.
    // Returns id for the property.
    ibPGProperty* Insert( ibPGPropArg parent,
                          int index,
                          ibPGProperty* newproperty );

    // Returns true if property is a category.
    bool IsPropertyCategory( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return p->IsCategory();
    }

    // Returns true if property is enabled.
    bool IsPropertyEnabled( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return !p->HasFlag(ibPGFlags::Disabled);
    }

    // Returns true if given property is expanded.
    // Naturally, always returns false for properties that cannot be expanded.
    bool IsPropertyExpanded( ibPGPropArg id ) const;

    // Returns true if property has been modified after value set or modify
    // flag clear by software.
    bool IsPropertyModified( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return p->HasFlag(ibPGFlags::Modified);
    }

    // Returns true if property is selected.
    bool IsPropertySelected( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return m_pState->DoIsPropertySelected(p);
    }

    // Returns true if property is shown (i.e. HideProperty with true not
    // called for it).
    bool IsPropertyShown( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return !p->HasFlag(ibPGFlags::Hidden);
    }

    // Returns true if property value is set to unspecified.
    bool IsPropertyValueUnspecified( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return p->IsValueUnspecified();
    }

    // Disables (limit = true) or enables (limit = false) ibTextCtrl editor
    // of a property, if it is not the sole mean to edit the value.
    void LimitPropertyEditing( ibPGPropArg id, bool limit = true );

    // If state is shown in its grid, refresh it now.
    virtual void RefreshGrid( ibPropertyGridPageState* state = nullptr );

#if ibPG_INCLUDE_ADVPROPS
    // Initializes additional property editors (SpinCtrl etc.). Causes
    // references to most object files in the library, so calling this may
    // cause significant increase in executable size when linking with static
    // library.
    static void RegisterAdditionalEditors();
#else
    static void RegisterAdditionalEditors() { }
#endif

    // Replaces property with id with newly created property. For example,
    // this code replaces existing property named "Flags" with one that
    // will have different set of items:
    //   pg->ReplaceProperty("Flags",
    //            ibFlagsProperty("Flags", ibPG_LABEL, newItems))
    // For more info, see ibPropertyGrid::Insert.
    ibPGProperty* ReplaceProperty( ibPGPropArg id, ibPGProperty* property );

    // Flags for ibPropertyGridInterface::SaveEditableState()
    // and ibPropertyGridInterface::RestoreEditableState().
    enum EditableStateFlags
    {
        // Include selected property.
        SelectionState   = 0x01,
        // Include expanded/collapsed property information.
        ExpandedState    = 0x02,
        // Include scrolled position.
        ScrollPosState   = 0x04,
        // Include selected page information.
        // Only applies to ibPropertyGridManager.
        PageState        = 0x08,
        // Include splitter position. Stored for each page.
        SplitterPosState = 0x10,
        // Include description box size.
        // Only applies to ibPropertyGridManager.
        DescBoxState     = 0x20,

        // Include all supported user editable state information.
        // This is usually the default value.
        AllStates        = SelectionState |
                           ExpandedState |
                           ScrollPosState |
                           PageState |
                           SplitterPosState |
                           DescBoxState
    };

    // Restores user-editable state.
    // See also ibPropertyGridInterface::SaveEditableState().
    // src - String generated by SaveEditableState.
    // restoreStates - Which parts to restore from source string. See @ref
    //   propgridinterface_editablestate_flags "list of editable state
    //   flags".
    // Returns false if there was problem reading the string.
    // If some parts of state (such as scrolled or splitter position) fail to
    // restore correctly, please make sure that you call this function after
    // ibPropertyGrid size has been set (this may sometimes be tricky when
    // sizers are used).
    bool RestoreEditableState( const wxString& src,
                               int restoreStates = AllStates );

    // Used to acquire user-editable state (selected property, expanded
    // properties, scrolled position, splitter positions).
    // includedStates - Which parts of state to include. See EditableStateFlags.
    wxString SaveEditableState( int includedStates = AllStates ) const;

    // Lets user set the strings listed in the choice dropdown of a
    // ibBoolProperty. Defaults are "True" and "False", so changing them to,
    // say, "Yes" and "No" may be useful in some less technical applications.
    static void SetBoolChoices( const wxString& trueChoice,
                                const wxString& falseChoice );

    // Set proportion of a auto-stretchable column. ibPG_SPLITTER_AUTO_CENTER
    // window style needs to be used to indicate that columns are auto-
    // resizable.
    // Returns false on failure.
    // You should call this for individual pages of ibPropertyGridManager
    // (if used).
    bool SetColumnProportion( unsigned int column, int proportion );

    // Returns auto-resize proportion of the given column.
    int GetColumnProportion( unsigned int column ) const
    {
        return m_pState->DoGetColumnProportion(column);
    }

    // Sets an attribute for this property.
    // name - Text identifier of attribute. See @ref propgrid_property_attributes.
    // value - Value of attribute.
    // flags - Optional. Use ibPGPropertyValuesFlags::Recurse to set the attribute to child
    //   properties recursively.
    // Setting attribute's value to null ibVariant will simply remove it
    // from property's set of attributes.
    void SetPropertyAttribute(ibPGPropArg id, const wxString& attrName, ibVariant value,
                              ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::DontRecurse)
    {
        DoSetPropertyAttribute(id, attrName, value, flags);
    }
    // Sets property attribute for all applicable properties.
    // Be sure to use this method only after all properties have been
    // added to the grid.
    void SetPropertyAttributeAll( const wxString& attrName, ibVariant value );

    // Sets background colour of a property.
    // id - Property name or pointer.
    // colour - New background colour.
    // flags - Default is ibPGPropertyValuesFlags::Recurse which causes colour to be set recursively.
    //   Omit this flag to only set colour for the property in question
    //   and not any of its children.
    void SetPropertyBackgroundColour(ibPGPropArg id, const wxColour& colour,
                                     ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::Recurse);

    // Resets text and background colours of given property.
    // id - Property name or pointer.
    // flags - Default is ibPGPropertyValuesFlags::DontRecurse which causes colour to be reset
    //   only for the property in question (for backward compatibility).
    void SetPropertyColoursToDefault(ibPGPropArg id, ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::DontRecurse);

    // Sets text colour of a property.
    // id - Property name or pointer.
    // colour - New background colour.
    // flags - Default is ibPGPropertyValuesFlags::Recurse which causes colour to be set recursively.
    //   Omit this flag to only set colour for the property in question
    //   and not any of its children.
    void SetPropertyTextColour(ibPGPropArg id, const wxColour& col,
                               ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::Recurse);

    // Returns background colour of first cell of a property.
    wxColour GetPropertyBackgroundColour( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(wxColour())
        return p->GetCell(0).GetBgCol();
    }

    // Returns text colour of first cell of a property.
    wxColour GetPropertyTextColour( ibPGPropArg id ) const
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(wxColour())
        return p->GetCell(0).GetFgCol();
    }

    // Sets text, bitmap, and colours for given column's cell.
    // You can set label cell by setting column to 0.
    // You can use ibPG_LABEL as text to use default text for column.
    void SetPropertyCell( ibPGPropArg id,
                          int column,
                          const wxString& text = wxString(),
                          const wxBitmapBundle& bitmap = wxBitmapBundle(),
                          const wxColour& fgCol = wxNullColour,
                          const wxColour& bgCol = wxNullColour );

    // Sets client data (void*) of a property.
    // This untyped client data has to be deleted manually.
    void SetPropertyClientData( ibPGPropArg id, void* clientData )
    {
        ibPG_PROP_ARG_CALL_PROLOG()
        p->SetClientData(clientData);
    }

    // Sets editor for a property.
    // editor - For builtin editors, use ibPGEditor_X, where X is builtin editor's
    // name (TextCtrl, Choice, etc. see ibPGEditor documentation for full
    // list).
    // For custom editors, use pointer you received from
    // ibPropertyGrid::RegisterEditorClass().
    void SetPropertyEditor( ibPGPropArg id, const ibPGEditor* editor )
    {
        ibPG_PROP_ARG_CALL_PROLOG()
        wxCHECK_RET( editor, wxS("unknown/null editor") );
        p->SetEditor(editor);
        RefreshProperty(p);
    }

    // Sets editor control of a property. As editor argument, use
    // editor name string, such as "TextCtrl" or "Choice".
    void SetPropertyEditor( ibPGPropArg id, const wxString& editorName )
    {
        SetPropertyEditor(id,GetEditorByName(editorName));
    }

    // Sets label of a property.
    // Properties under same parent may have same labels. However,
    // property names must still remain unique.
    void SetPropertyLabel( ibPGPropArg id, const wxString& newproplabel );

    // Sets name of a property.
    // id - Name or pointer of property which name to change.
    // newName - New name for property.
    void SetPropertyName( ibPGPropArg id, const wxString& newName )
    {
        ibPG_PROP_ARG_CALL_PROLOG()
        m_pState->DoSetPropertyName( p, newName );
    }

    // Sets property (and, recursively, its children) to have read-only value.
    // In other words, user cannot change the value in the editor, but they
    // can still copy it.
    // This is mainly for use with textctrl editor. Not all other editors fully
    // support it.
    // By default changes are applied recursively. Set parameter "flags"
    // to ibPGPropertyValuesFlags::DontRecurse to prevent this.
    void SetPropertyReadOnly(ibPGPropArg id, bool set = true,
                             ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::Recurse);

    // Sets property's value to unspecified.
    // If it has children (it may be category), then the same thing is done to
    // them.
    void SetPropertyValueUnspecified( ibPGPropArg id )
    {
        ibPG_PROP_ARG_CALL_PROLOG()
        p->SetValueToUnspecified();
    }

    // Sets property values from a list of values, each under its property's name.
    void SetPropertyValues( const ibPGVariantList& list,
                            ibPGPropArg defaultCategory = ibNullProperty )
    {
        ibPGProperty* p = defaultCategory.HasName() ?
                          defaultCategory.GetPtr(this) : defaultCategory.GetPtr0();
        m_pState->DoSetPropertyValues(list, p);
    }

    // Sets property values from a value that holds such a list.
    void SetPropertyValues( const ibVariant& list,
                            ibPGPropArg defaultCategory = ibNullProperty )
    {
        const ibPGVariantList* entries = list.ConvertToType<ibPGVariantList>();
        wxCHECK_RET( entries, wxS("the value holds no list") );
        SetPropertyValues(*entries,defaultCategory);
    }

    // Associates the help string with property.
    // By default, text is shown either in the manager's "description"
    // text box or in the status bar. If extra window style
    // ibPG_EX_HELP_AS_TOOLTIPS is used, then the text will appear as a
    // tooltip.
    void SetPropertyHelpString( ibPGPropArg id, const wxString& helpString )
    {
        ibPG_PROP_ARG_CALL_PROLOG()
        p->SetHelpString(helpString);
    }

    // Set wxBitmap in front of the value.
    // Bitmap will be scaled to a size returned by
    // ibPropertyGrid::GetImageSize();
    void SetPropertyImage( ibPGPropArg id, const wxBitmapBundle& bmp )
    {
        ibPG_PROP_ARG_CALL_PROLOG()
        p->SetValueImage(bmp);
        RefreshProperty(p);
    }

    // Sets max length of property's text.
    bool SetPropertyMaxLength( ibPGPropArg id, int maxLen );

#if wxUSE_VALIDATORS
    // Sets validator of a property.
    void SetPropertyValidator( ibPGPropArg id, const wxValidator& validator )
    {
        ibPG_PROP_ARG_CALL_PROLOG()
        p->SetValidator(validator);
    }
#endif

    // Sets value (long integer) of a property.
    void SetPropertyValue( ibPGPropArg id, long value )
    {
        ibVariant v(value);
        SetPropVal( id, v );
    }

    // Sets value (integer) of a property.
    void SetPropertyValue( ibPGPropArg id, int value )
    {
        ibVariant v((long)value);
        SetPropVal( id, v );
    }

    // Sets value (floating point) of a property.
    void SetPropertyValue( ibPGPropArg id, double value )
    {
        ibVariant v(value);
        SetPropVal( id, v );
    }

    // Sets value (bool) of a property.
    void SetPropertyValue( ibPGPropArg id, bool value )
    {
        ibVariant v(value);
        SetPropVal( id, v );
    }

    // Sets value (wchar_t*) of a property.
    void SetPropertyValue( ibPGPropArg id, const wchar_t* value )
    {
        SetPropertyValueString( id, wxString(value) );
    }

#ifndef wxNO_IMPLICIT_WXSTRING_ENCODING
    // Sets value (char*) of a property.
    void SetPropertyValue( ibPGPropArg id, const char* value )
    {
        SetPropertyValueString( id, wxString(value) );
    }
#endif // !wxNO_IMPLICIT_WXSTRING_ENCODING

    // Sets value (string) of a property.
    void SetPropertyValue( ibPGPropArg id, const wxString& value )
    {
        SetPropertyValueString( id, value );
    }

    // Sets value (wxArrayString) of a property.
    void SetPropertyValue( ibPGPropArg id, const wxArrayString& value )
    {
        ibVariant v = IBVARIANT(value);
        SetPropVal( id, v );
    }

#if wxUSE_DATETIME
    // Sets value (wxDateTime) of a property.
    void SetPropertyValue( ibPGPropArg id, const wxDateTime& value )
    {
        ibVariant v = IBVARIANT(value);
        SetPropVal( id, v );
    }
#endif

    // (wx's SetPropertyValue(wxObject*) and (wxObject&) are not here: the core's value holds no pointer of another
    // class - its rule, core/variant.h)

    // Sets value (native 64-bit int) of a property.
    void SetPropertyValue(ibPGPropArg id, wxLongLong_t value)
    {
        ibVariant v = IBVARIANT(wxLongLong(value));
        SetPropVal(id, v);
    }
    // Sets value (wxLongLong) of a property.
    void SetPropertyValue( ibPGPropArg id, wxLongLong value )
    {
        ibVariant v = IBVARIANT(value);
        SetPropVal( id, v );
    }
    // Sets value (native 64-bit unsigned int) of a property.
    void SetPropertyValue(ibPGPropArg id, wxULongLong_t value)
    {
        ibVariant v = IBVARIANT(wxULongLong(value));
        SetPropVal(id, v);
    }
    // Sets value (wxULongLong) of a property.
    void SetPropertyValue( ibPGPropArg id, wxULongLong value )
    {
        ibVariant v = IBVARIANT(value);
        SetPropVal( id, v );
    }

    // Sets value (wxArrayInt&) of a property.
    void SetPropertyValue( ibPGPropArg id, const wxArrayInt& value )
    {
        ibVariant v = IBVARIANT(value);
        SetPropVal( id, v );
    }

    // Sets value (wxString) of a property.
    // This method uses ibPGProperty::SetValueFromString, which all properties
    // should implement. This means that there should not be a type error,
    // and instead the string is converted to property's actual value type.
    void SetPropertyValueString( ibPGPropArg id, const wxString& value );

    // Sets value (ibVariant) of a property.
    // Use ibPropertyGrid::ChangePropertyValue() instead if you need to run
    // through validation process and send property change event.
    void SetPropertyValue( ibPGPropArg id, ibVariant value )
    {
        SetPropVal( id, value );
    }

    // Sets value (ibVariant&) of a property. Same as SetPropertyValue,
    // but accepts reference.
    void SetPropVal( ibPGPropArg id, ibVariant& value );

    // Adjusts how ibPropertyGrid behaves when invalid value is entered
    // in a property.
    // vfbFlags - See ibPG_VALIDATION_FAILURE_BEHAVIOR_FLAGS for possible values.
    void SetValidationFailureBehavior(ibPGVFBFlags vfbFlags);

    // Sorts all properties recursively.
    // flags - This can contain any of the following options:
    //    ibPGPropertyValuesFlags::SortTopLevelOnly: Only sort categories and their
    //    immediate children. Sorting done by ibPG_AUTO_SORT option
    //    uses this.
    // See SortChildren, ibPropertyGrid::SetSortFunction
    void Sort(ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::DontRecurse);

    // Sorts children of a property.
    // id - Name or pointer to a property.
    // flags - This can contain any of the following options:
    //   ibPGPropertyValuesFlags::Recurse: Sorts recursively.
    // See Sort, ibPropertyGrid::SetSortFunction
    void SortChildren(ibPGPropArg id, ibPGPropertyValuesFlags flags = ibPGPropertyValuesFlags::DontRecurse)
    {
        ibPG_PROP_ARG_CALL_PROLOG()
        m_pState->DoSortChildren(p, flags);
    }

    // GetPropertyByName() with nice assertion error message.
    ibPGProperty* GetPropertyByNameA( const wxString& name ) const;

    // Returns editor pointer of editor with given name.
    static ibPGEditor* GetEditorByName( const wxString& editorName );

    // NOTE: This function reselects the property and may cause
    //       excess flicker, so to just call Refresh() on a rect
    //       of single property, call DrawItem() instead.
    virtual void RefreshProperty( ibPGProperty* p ) = 0;

protected:

    bool DoClearSelection( bool validation = false,
                           ibPGSelectPropertyFlags selFlags = ibPGSelectPropertyFlags::Null );

    // In derived class, implement to set editable state component with
    // given name to given value.
    virtual bool SetEditableStateItem( const wxString& name, ibVariant value )
    {
        wxUnusedVar(name);
        wxUnusedVar(value);
        return false;
    }

    // In derived class, implement to return editable state component with
    // given name.
    virtual ibVariant GetEditableStateItem( const wxString& name ) const
    {
        wxUnusedVar(name);
        return ibVariant();
    }

    // Returns page state data for given (sub) page (-1 means current page).
    virtual ibPropertyGridPageState* GetPageState( int pageIndex ) const
    {
        return pageIndex <= 0 ? m_pState : nullptr;
    }

    virtual bool DoSelectPage( int WXUNUSED(index) ) { return true; }

    // Default call's m_pState's BaseGetPropertyByName
    virtual ibPGProperty* DoGetPropertyByName( const wxString& name ) const;

    // Deriving classes must set this (it must be only or current page).
    ibPropertyGridPageState*         m_pState;

    // Intermediate version needed due to ibVariant copying inefficiency
    void DoSetPropertyAttribute( ibPGPropArg id,
                                 const wxString& name,
                                 ibVariant& value, ibPGPropertyValuesFlags flags );

    // Empty string object to return from member functions returning const
    // wxString&.
    wxString                    m_emptyString;

private:
    // Cannot be GetGrid() due to ambiguity issues.
    ibPropertyGrid* GetPropertyGrid()
    {
        return m_pState ? m_pState->GetGrid() : nullptr;
    }

    // Cannot be GetGrid() due to ambiguity issues.
    const ibPropertyGrid* GetPropertyGrid() const
    {
        return m_pState ? m_pState->GetGrid() : nullptr;
    }
};

#endif // wxUSE_PROPGRID

#endif // _DESKGUI_UNIV_PROPGRID_PROPGRIDIFACE_H_
