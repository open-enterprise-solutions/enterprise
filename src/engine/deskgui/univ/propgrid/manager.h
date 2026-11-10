// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, include/wx/propgrid/manager.h): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/propgrid/manager.h
// Purpose:     ibPropertyGridManager
// Author:      Jaakko Salli
// Created:     2005-01-14
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_PROPGRID_MANAGER_H_
#define _DESKGUI_UNIV_PROPGRID_MANAGER_H_

#include "wx/defs.h"

#if wxUSE_PROPGRID

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/panel.h"           // ibPanel — the manager's window, the library's (wx's wxPanel)

#include <vector>

// -----------------------------------------------------------------------

#ifndef SWIG
extern DESKGUI_API const char ibPropertyGridManagerNameStr[];
#endif

// Holder of property grid page information. You can subclass this and
// give instance in ibPropertyGridManager::AddPage. It inherits from
// wxEvtHandler and can be used to process events specific to this
// page (id of events will still be same as manager's). If you don't
// want to use it to process all events of the page, you need to
// return false in the derived ibPropertyGridPage::IsHandlingAllEvents.
//
// Please note that ibPropertyGridPage lacks many non-const property
// manipulation functions found in ibPropertyGridManager. Please use
// parent manager (m_manager member variable) when needed.
//
// Please note that most member functions are inherited and as such not
// documented on this page. This means you will probably also want to read
// ibPropertyGridInterface class reference.
//
// ibPropertyGridPage receives events emitted by its ibPropertyGridManager, but
// only those events that are specific to that page. If
// ibPropertyGridPage::IsHandlingAllEvents returns false, then unhandled
// events are sent to the manager's parent, as usual.
class DESKGUI_API ibPropertyGridPage : public wxEvtHandler,
                                                public ibPropertyGridInterface,
                                                public ibPropertyGridPageState
{
    friend class ibPropertyGridManager;
    wxDECLARE_CLASS(ibPropertyGridPage);
public:

    ibPropertyGridPage();
    virtual ~ibPropertyGridPage() = default;

    // Deletes all properties on page.
    virtual void Clear() override;

    // Reduces column sizes to minimum possible that contents are still
    // visibly (naturally some margin space will be applied as well).
    // Returns minimum size for the page to still display everything.
    // This function only works properly if size of containing grid was
    // already fairly large.
    // Note that you can also get calculated column widths by calling
    // GetColumnWidth() immediately after this function returns.
    wxSize FitColumns();

    // Returns page index in manager;
    inline int GetIndex() const;

    // Returns x-coordinate position of splitter on a page.
    int GetSplitterPosition( int col = 0 ) const
        { return GetStatePtr()->DoGetSplitterPosition(col); }

    // Returns "root property". It does not have name, etc. and it is not
    // visible. It is only useful for accessing its children.
    ibPGProperty* GetRoot() const { return GetStatePtr()->DoGetRoot(); }

    // Returns pointer to contained property grid state.
    ibPropertyGridPageState* GetStatePtr()
    {
        return this;
    }

    // Returns pointer to contained property grid state.
    const ibPropertyGridPageState* GetStatePtr() const
    {
        return this;
    }

    // Returns id of the tool bar item that represents this page on
    // ibPropertyGridManager's ibToolBar.
    int GetToolId() const
    {
        return m_toolId;
    }

    // Do any member initialization in this method.
    // Notes:
    // - Called every time the page is added into a manager.
    // - You can add properties to the page here.
    virtual void Init() {}

    // Return false here to indicate unhandled events should be
    // propagated to manager's parent, as normal.
    virtual bool IsHandlingAllEvents() const { return true; }

    // Called every time page is about to be shown.
    // Useful, for instance, creating properties just-in-time.
    virtual void OnShow();

    //  Refreshes given property on page.
    virtual void RefreshProperty( ibPGProperty* p ) override;

    // Sets splitter position on page.
    // Splitter position cannot exceed grid size, and therefore setting it
    // during form creation may fail as initial grid size is often smaller
    // than desired splitter position, especially when sizers are being used.
    void SetSplitterPosition( int splitterPos, int col = 0 );

protected:

    // Propagate to other pages
    virtual void DoSetSplitter(int pos,
                               int splitterColumn,
                               ibPGSplitterPositionFlags flags) override;

    // Page label (may be referred as name in some parts of documentation).
    // Can be set in constructor, or passed in
    // ibPropertyGridManager::AddPage(), but *not* in both.
    wxString                m_label;

    //virtual bool ProcessEvent( wxEvent& event );

    ibPropertyGridManager*  m_manager;

    // Toolbar tool id. Note that this is only valid when the tool bar
    // exists.
    int                     m_toolId;

private:
    bool                    m_isDefault; // is this base page object?

    wxDECLARE_EVENT_TABLE();
};

// -----------------------------------------------------------------------

#if wxUSE_TOOLBAR
class ibToolBar;
#endif
#if wxUSE_HEADERCTRL
class ibPGHeaderCtrl;
#endif
class ibStaticText;

// ibPropertyGridManager is an efficient multi-page version of ibPropertyGrid,
// which can optionally have toolbar for mode and page selection, and help
// text box.
// Use window flags to select components to include.
class DESKGUI_API
    ibPropertyGridManager : public ibPanel, public ibPropertyGridInterface
{
    wxDECLARE_CLASS(ibPropertyGridManager);
public:

#ifndef SWIG
    // Two step constructor.
    // Call Create when this constructor is called to build up the
    // ibPropertyGridManager.
    ibPropertyGridManager();
#endif

    // The default constructor. The styles to be used are styles valid for
    // the wxWindow.
    ibPropertyGridManager( wxWindow *parent, wxWindowID id = wxID_ANY,
                           const wxPoint& pos = wxDefaultPosition,
                           const wxSize& size = wxDefaultSize,
                           long style = ibPGMAN_DEFAULT_STYLE,
                           const wxString& name = wxASCII_STR(ibPropertyGridManagerNameStr) );

    // Destructor.
    virtual ~ibPropertyGridManager();

    // Creates new property page. Note that the first page is not created
    // automatically.
    // label - A label for the page. This may be shown as a toolbar tooltip etc.
    // bmp - Bitmap bundle image for toolbar. If it's null then a built-in
    //   default bitmap bundle is used.
    // pageObj - ibPropertyGridPage instance. Manager will take ownership of this object.
    // nullptr indicates that a default page instance should be created.
    // Returns pointer to created page.
    // If toolbar is used, it is highly recommended that the pages are
    // added when the toolbar is not turned off using window style flag
    // switching.
    ibPropertyGridPage* AddPage( const wxString& label = wxString(),
                                 const wxBitmapBundle& bmp = wxBitmapBundle(),
                                 ibPropertyGridPage* pageObj = nullptr )
    {
        return InsertPage(-1, label, bmp, pageObj);
    }

    // Deletes all properties and all pages.
    virtual void Clear() override;

    // Deletes all properties on given page.
    void ClearPage( int page );

    // Forces updating the value of property from the editor control.
    // Returns true if DoPropertyChanged was actually called.
    bool CommitChangesFromEditor(ibPGSelectPropertyFlags flags = ibPGSelectPropertyFlags::Null)
    {
        return m_pPropGrid->CommitChangesFromEditor(flags);
    }

    // Two step creation.
    // Whenever the control is created without any parameters, use Create to
    // actually create it. Don't access the control's public methods before
    // this is called.
    bool Create( wxWindow *parent, wxWindowID id = wxID_ANY,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 long style = ibPGMAN_DEFAULT_STYLE,
                 const wxString& name = wxASCII_STR(ibPropertyGridManagerNameStr) );

    // Enables or disables (shows/hides) categories according to parameter
    // enable.
    // Calling this may not properly update toolbar buttons.
    bool EnableCategories( bool enable )
    {
        long fl = m_windowStyle | ibPG_HIDE_CATEGORIES;
        if ( enable ) fl = m_windowStyle & ~(ibPG_HIDE_CATEGORIES);
        SetWindowStyleFlag(fl);
        return true;
    }

    // Selects page, scrolls and/or expands items to ensure that the
    // given item is visible. Returns true if something was actually done.
    bool EnsureVisible( ibPGPropArg id );

    // Returns number of columns on given page. By the default,
    // returns number of columns on current page.
    int GetColumnCount( int page = -1 ) const;

    // Returns height of the description text box.
    int GetDescBoxHeight() const;

    // Returns pointer to the contained ibPropertyGrid. This does not change
    // after ibPropertyGridManager has been created, so you can safely obtain
    // pointer once and use it for the entire lifetime of the manager instance.
    ibPropertyGrid* GetGrid()
    {
        wxASSERT(m_pPropGrid);
        return m_pPropGrid;
    }

    const ibPropertyGrid* GetGrid() const
    {
        wxASSERT(m_pPropGrid);
        return m_pPropGrid;
    }

    // Returns iterator class instance.
    // Calling this method in ibPropertyGridManager causes run-time assertion
    // failure. Please only iterate through individual pages or use
    // CreateVIterator().
    ibPropertyGridIterator GetIterator( int flags = ibPG_ITERATE_DEFAULT,
                                        ibPGProperty* firstProp = nullptr )
    {
        wxFAIL_MSG( wxS("Please only iterate through individual pages ")
                    wxS("or use CreateVIterator()") );
        return ibPropertyGridInterface::GetIterator( flags, firstProp );
    }

    ibPropertyGridConstIterator
    GetIterator(int flags = ibPG_ITERATE_DEFAULT,
                ibPGProperty* firstProp = nullptr) const
    {
        wxFAIL_MSG( wxS("Please only iterate through individual pages ")
                    wxS("or use CreateVIterator()") );
        return ibPropertyGridInterface::GetIterator( flags, firstProp );
    }

    // Returns iterator class instance.
    // Calling this method in ibPropertyGridManager causes run-time assertion
    // failure. Please only iterate through individual pages or use
    // CreateVIterator().
    ibPropertyGridIterator GetIterator( int flags, int startPos )
    {
        wxFAIL_MSG( wxS("Please only iterate through individual pages ")
                    wxS("or use CreateVIterator()") );

        return ibPropertyGridInterface::GetIterator( flags, startPos );
    }

    ibPropertyGridConstIterator GetIterator( int flags, int startPos ) const
    {
        wxFAIL_MSG( wxS("Please only iterate through individual pages ")
                    wxS("or use CreateVIterator()") );
        return ibPropertyGridInterface::GetIterator( flags, startPos );
    }

    // Similar to GetIterator, but instead returns ibPGVIterator instance,
    // which can be useful for forward-iterating through arbitrary property
    // containers.
    virtual ibPGVIterator GetVIterator( int flags ) const override;

    // Returns currently selected page.
    ibPropertyGridPage* GetCurrentPage() const
    {
        return GetPage(m_selPage);
    }

    // Returns page object for given page index.
    ibPropertyGridPage* GetPage( unsigned int ind ) const
    {
        return m_arrPages[ind];
    }

    // Returns page object for given page name.
    ibPropertyGridPage* GetPage( const wxString& name ) const
    {
        return GetPage(GetPageByName(name));
    }

    // Returns index for a page name.
    // If no match is found, wxNOT_FOUND is returned.
    int GetPageByName( const wxString& name ) const;

    // Returns index for a relevant propertygrid state.
    // If no match is found, wxNOT_FOUND is returned.
    int GetPageByState( const ibPropertyGridPageState* pstate ) const;

protected:
    // Returns ibPropertyGridPageState of given page, current page's for -1.
    virtual ibPropertyGridPageState* GetPageState( int page ) const override;

public:
    // Returns number of managed pages.
    size_t GetPageCount() const;

    // Returns name of given page.
    const wxString& GetPageName( int index ) const;

    // Returns "root property" of the given page. It does not have name, etc.
    // and it is not visible. It is only useful for accessing its children.
    ibPGProperty* GetPageRoot( int index ) const;

    // Returns index to currently selected page.
    int GetSelectedPage() const { return m_selPage; }

    // Alias for GetSelection().
    ibPGProperty* GetSelectedProperty() const
    {
        return GetSelection();
    }

    // Shortcut for GetGrid()->GetSelection().
    ibPGProperty* GetSelection() const
    {
        return m_pPropGrid->GetSelection();
    }

#if wxUSE_TOOLBAR
    // Returns a pointer to the toolbar currently associated with the
    // ibPropertyGridManager (if any).
    ibToolBar* GetToolBar() const { return m_pToolbar; }
#endif // wxUSE_TOOLBAR

    // Creates new property page. Note that the first page is not created
    // automatically.
    // index - Add to this position. -1 will add as the last item.
    // label - A label for the page. This may be shown as a toolbar tooltip etc.
    // bmp - Bitmap bundle for toolbar. If it's null, then a built-in
    //   default bitmap bundle is used.
    // pageObj - ibPropertyGridPage instance. Manager will take ownership of this object.
    //   If nullptr, default page object is constructed.
    // Returns pointer to created page.
    virtual ibPropertyGridPage* InsertPage( int index,
                                            const wxString& label,
                                            const wxBitmapBundle& bmp = wxBitmapBundle(),
                                            ibPropertyGridPage* pageObj = nullptr );

    // Returns true if any property on any page has been modified by the user.
    bool IsAnyModified() const;

    // Returns true if any property on given page has been modified by the
    // user.
    bool IsPageModified( size_t index ) const;

    // Returns true if property is selected. Since selection is page
    // based, this function checks every page in the manager.
    virtual bool IsPropertySelected( ibPGPropArg id ) const;

    // Removes a page.
    // Returns false if it was not possible to remove page in question.
    virtual bool RemovePage( int page );

    // Select and displays a given page.
    // index - Index of page being selected. Can be -1 to select nothing.
    void SelectPage( int index );

    // Select and displays a given page (by label).
    void SelectPage( const wxString& label )
    {
        int index = GetPageByName(label);
        wxCHECK_RET( index >= 0, wxS("No page with such name") );
        SelectPage( index );
    }

    // Select and displays a given page.
    void SelectPage( ibPropertyGridPage* ptr )
    {
        SelectPage( GetPageByState(ptr) );
    }

    // Select a property.
    bool SelectProperty( ibPGPropArg id, bool focus = false )
    {
        ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        ibPGSelectPropertyFlags flags = ibPGSelectPropertyFlags::DontSendEvent;
        if ( focus )
            flags |= ibPGSelectPropertyFlags::Focus;

        return p->GetParentState()->DoSelectProperty(p, flags);
    }

#if wxUSE_HEADERCTRL
    // Sets a column title. Default title for column 0 is "Property",
    // and "Value" for column 1.
    // If header is not shown yet, then calling this
    // member function will make it visible.
    void SetColumnTitle( int idx, const wxString& title );
#endif // wxUSE_HEADERCTRL

    // Sets number of columns on given page (default is current page).
    // If you use header, then you should always use this
    // member function to set the column count, instead of
    // ones present in ibPropertyGrid or ibPropertyGridPage.
    void SetColumnCount( int colCount, int page = -1 );

    // Sets label and text in description box.
    void SetDescription( const wxString& label, const wxString& content );

    // Sets y coordinate of the description box splitter.
    void SetDescBoxHeight( int ht, bool refresh = true );

    // Moves splitter as left as possible, while still allowing all
    // labels to be shown in full.
    // subProps - If false, will still allow sub-properties (ie. properties which
    //   parent is not root or category) to be cropped.
    // allPages - If true, takes labels on all pages into account.
    void SetSplitterLeft( bool subProps = false, bool allPages = true );

    // Moves splitter as left as possible on an individual page, while still allowing all
    // labels to be shown in full.
    void SetPageSplitterLeft(int page, bool subProps = false);

    // Sets splitter position on individual page.
    // If you use header, then you should always use this
    // member function to set the splitter position, instead of
    // ones present in ibPropertyGrid or ibPropertyGridPage.
    void SetPageSplitterPosition( int page, int pos, int column = 0 );

    // Sets splitter position for all pages.
    // Splitter position cannot exceed grid size, and therefore
    // setting it during form creation may fail as initial grid
    // size is often smaller than desired splitter position,
    // especially when sizers are being used.
    // If you use header, then you should always use this
    // member function to set the splitter position, instead of
    // ones present in ibPropertyGrid or ibPropertyGridPage.
    void SetSplitterPosition( int pos, int column = 0 );

#if wxUSE_HEADERCTRL
    // Show or hide the property grid header control. It is hidden
    // by the default.
    // Grid may look better if you use ibPG_NO_INTERNAL_BORDER
    // window style when showing a header.
    void ShowHeader(bool show = true);
#endif

protected:

    //
    // Subclassing helpers
    //

    // Creates property grid for the manager. Reimplement in derived class to
    // use subclassed ibPropertyGrid. However, if you do this then you
    // must also use the two-step construction (i.e. default constructor and
    // Create() instead of constructor with arguments) when creating the
    // manager.
    virtual ibPropertyGrid* CreatePropertyGrid() const;

public:
    virtual void RefreshProperty( ibPGProperty* p ) override;

    //
    // Overridden functions - no documentation required.
    //

    void SetId( wxWindowID winid ) override;
    virtual void SetExtraStyle ( long exStyle ) override;
    virtual bool SetFont ( const wxFont& font ) override;
    virtual void SetWindowStyleFlag ( long style ) override;
    virtual bool Reparent( wxWindowBase *newParent ) override;

protected:
    virtual wxSize DoGetBestSize() const override;

    //
    // Event handlers
    //
    void OnMouseMove( wxMouseEvent &event );
    void OnMouseClick( wxMouseEvent &event );
    void OnMouseUp( wxMouseEvent &event );
    void OnMouseEntry( wxMouseEvent &event );

    void OnPaint( wxPaintEvent &event );

#if wxUSE_TOOLBAR
    void OnToolbarClick( wxCommandEvent &event );
#endif
    void OnResize( wxSizeEvent& event );
    void OnPropertyGridSelect( ibPropertyGridEvent& event );
    void OnPGScrollH(ibPropertyGridEvent& evt);
    void OnColWidthsChanged(ibPropertyGridEvent& evt);


    ibPropertyGrid* m_pPropGrid;

    std::vector<ibPropertyGridPage*> m_arrPages;

#if wxUSE_TOOLBAR
    ibToolBar*      m_pToolbar;
#endif
#if wxUSE_HEADERCTRL
    ibPGHeaderCtrl* m_pHeaderCtrl;
#endif
    ibStaticText*   m_pTxtHelpCaption;
    ibStaticText*   m_pTxtHelpContent;

    ibPropertyGridPage*     m_emptyPage;

    long            m_iFlags;

    // Selected page index.
    int             m_selPage;

    int             m_width;

    int             m_height;

    int             m_extraHeight;

    int             m_splitterY;

    int             m_splitterHeight;

    int             m_dragOffset;

    wxCursor        m_cursorSizeNS;

    int             m_nextDescBoxSize;

    // Toolbar tool ids for categorized and alphabetic mode selectors.
    int             m_categorizedModeToolId;
    int             m_alphabeticModeToolId;

    unsigned char   m_dragStatus;

    bool            m_onSplitter;

    bool            m_showHeader;

    virtual ibPGProperty* DoGetPropertyByName( const wxString& name ) const override;

    // Select and displays a given page.
    virtual bool DoSelectPage( int index ) override;

    // Sets some members to defaults.
    void Init1();

    // Initializes some members.
    void Init2( int style );

    virtual bool ProcessEvent( wxEvent& event ) override;

    // Recalculates new positions for components, according to the
    // given size.
    void RecalculatePositions( int width, int height );

    // (Re)creates/destroys controls, according to the window style bits.
    void RecreateControls();

    void UpdateDescriptionBox( int new_splittery, int new_width, int new_height );

    void RepaintDescBoxDecorations( wxDC& dc,
                                    int newSplitterY,
                                    int newWidth,
                                    int newHeight );

    void SetDescribedProperty( ibPGProperty* p );

    // Reimplement these to handle "descboxheight" state item
    virtual bool SetEditableStateItem( const wxString& name, ibVariant value ) override;
    virtual ibVariant GetEditableStateItem( const wxString& name ) const override;

    // Reconnect propgrid event handlers.
    void ReconnectEventHandlers(wxWindowID oldId, wxWindowID newId);
private:
    wxDECLARE_EVENT_TABLE();
};

// -----------------------------------------------------------------------

inline int ibPropertyGridPage::GetIndex() const
{
    return m_manager ? m_manager->GetPageByState(this) : wxNOT_FOUND;
}

// -----------------------------------------------------------------------

#endif // wxUSE_PROPGRID

#endif // _DESKGUI_UNIV_PROPGRID_MANAGER_H_
