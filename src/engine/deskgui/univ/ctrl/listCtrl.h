// FORKED from the wx GENERIC list control (wxGenericListCtrl -> ibListCtrl,
// mechanical ib prefixes). The generic list is already custom-drawn - the
// port is the usual deskgui seam work: ibControl base, theme colours.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/listctrl.h
// Purpose:     Generic list control
// Author:      Robert Roebling
// Created:     01/02/97
// Copyright:   (c) 1998 Robert Roebling and Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_LISTCTRL_H_
#define _DESKGUI_UNIV_LISTCTRL_H_

#include "deskgui/deskgui.h"

#if wxUSE_LISTCTRL

#include <wx/containr.h>
#include <wx/scrolwin.h>
#include <wx/listctrl.h>      // wxListItem, wxListEvent, wxLC_* styles, wxListCtrlNameStr
#include <wx/withimages.h>

#include "deskgui/univ/ctrl/control.h"
#include "deskgui/univ/ctrl/textCtrl.h"

#if wxUSE_DRAG_AND_DROP
class WXDLLIMPEXP_FWD_CORE wxDropTarget;
#endif

// ----------------------------------------------------------------------------
// ibListCtrlBase: the base class for the main control itself.
// ----------------------------------------------------------------------------

// Unlike other base classes, this class doesn't currently define the API of
// the real control class but is just used for implementation convenience. We
// should define the public class functions as pure virtual here in the future
// however.
//
// SEAM vs wx: wxListCtrlBase (wx/listbase.h) rides the native wxControl chain
// (wxSystemThemedControl<wxControl>); this is its re-declaration on the ib
// chain, so the generic implementation keeps all its overrides valid. The data
// it hands about - wxListItem, wxItemAttr, wxListEvent and its wxEVT_LIST_*,
// the wxLC_* styles - stay wx's.
class DESKGUI_API ibListCtrlBase : public ibControl
{
public:
    ibListCtrlBase() = default;

    // Image-related methods.
    // ----------------------

    // Preferred way of specifying the images is by using the SetXXXImages(),
    // methods using wxImageList below still work, but don't allow specifying
    // images in different resolutions, which is necessary for good appearance
    // on high DPI displays.

    // "Normal" images are used only in icon view, the "report" view uses
    // "small" images.
    void SetNormalImages(const wxVector<wxBitmapBundle>& images);
    void SetSmallImages(const wxVector<wxBitmapBundle>& images);

    // Associate the given (possibly null to indicate that no images will be
    // used) image list with the control. The ownership of the image list
    // passes to the control, i.e. it will be deleted when the control itself
    // is destroyed.
    //
    // The value of "which" must be one of wxIMAGE_LIST_{NORMAL,SMALL,STATE}.
    virtual void AssignImageList(wxImageList* imageList, int which);

    // Same as AssignImageList() but the control does not delete the image list
    // so it can be shared among several controls.
    virtual void SetImageList(wxImageList* imageList, int which);

    // Return the currently used image list, may be null.
    virtual wxImageList* GetImageList(int which) const;


    // Column-related methods.
    // -----------------------

    // All these methods can only be used in report view mode.

    // Appends a new column.
    //
    // Returns the index of the newly inserted column or -1 on error.
    long AppendColumn(const wxString& heading,
                      wxListColumnFormat format = wxLIST_FORMAT_LEFT,
                      int width = -1);

    // Add a new column to the control at the position "col".
    //
    // Returns the index of the newly inserted column or -1 on error.
    long InsertColumn(long col, const wxListItem& info);
    long InsertColumn(long col,
                      const wxString& heading,
                      int format = wxLIST_FORMAT_LEFT,
                      int width = wxLIST_AUTOSIZE);

    // Delete the given or all columns.
    virtual bool DeleteColumn(int col) = 0;
    virtual bool DeleteAllColumns() = 0;

    // Return the current number of items.
    virtual int GetItemCount() const = 0;

    // Check if the control is empty, i.e. doesn't contain any items.
    bool IsEmpty() const { return GetItemCount() == 0; }

    // Return the current number of columns.
    virtual int GetColumnCount() const = 0;

    // Get or update information about the given column. Set item mask to
    // indicate the fields to retrieve or change.
    //
    // Returns false on error, e.g. if the column index is invalid.
    virtual bool GetColumn(int col, wxListItem& item) const = 0;
    virtual bool SetColumn(int col, const wxListItem& item) = 0;

    // Convenient wrappers for the above methods which get or update just the
    // column width.
    virtual int GetColumnWidth(int col) const = 0;
    virtual bool SetColumnWidth(int col, int width) = 0;

    // Column ordering functions
    virtual int GetColumnOrder(int col) const = 0;
    virtual int GetColumnIndexFromOrder(int order) const = 0;

    virtual wxArrayInt GetColumnsOrder() const = 0;
    virtual bool SetColumnsOrder(const wxArrayInt& orders) = 0;


    // Other miscellaneous accessors.
    // ------------------------------

    // Convenient functions for testing the list control mode:
    bool InReportView() const { return HasFlag(wxLC_REPORT); }
    bool IsVirtual() const { return HasFlag(wxLC_VIRTUAL); }

    // Check if the item is visible
    virtual bool IsVisible(long WXUNUSED(item)) const { return false; }

    // Enable or disable beep when incremental match doesn't find any item.
    // Only implemented in the generic version currently.
    virtual void EnableBellOnNoMatch(bool WXUNUSED(on) = true) { }

    void EnableAlternateRowColours(bool enable = true);
    void SetAlternateRowColour(const wxColour& colour);
    wxColour GetAlternateRowColour() const { return m_alternateRowColour.GetBackgroundColour(); }

    virtual void ExtendRulesAndAlternateColour(bool WXUNUSED(extend) = true) { }

    // Header attributes support: only implemented in wxMSW currently.
    virtual bool SetHeaderAttr(const wxItemAttr& WXUNUSED(attr)) { return false; }

    // Checkboxes support.
    virtual bool HasCheckBoxes() const { return false; }
    virtual bool EnableCheckBoxes(bool WXUNUSED(enable) = true) { return false; }
    virtual bool IsItemChecked(long WXUNUSED(item)) const { return false; }
    virtual void CheckItem(long WXUNUSED(item), bool WXUNUSED(check)) { }

    // Sort indicator in header.
    virtual void ShowSortIndicator(int WXUNUSED(col), bool WXUNUSED(ascending) = true) { }
    void RemoveSortIndicator() { ShowSortIndicator(-1); }
    virtual int GetSortIndicator() const { return -1; }
    virtual bool IsAscendingSortIndicator() const { return true; }
    bool GetUpdatedAscendingSortIndicator(int col) const
    {
        // If clicking on the same column by which we already sort, toggle the sort
        // direction, otherwise use ascending sort by default.
        return col == GetSortIndicator() ? !IsAscendingSortIndicator() : true;
    }

protected:
    // Return pointer to the corresponding m_imagesXXX.
    const wxWithImages* GetImages(int which) const;
    wxWithImages* GetImages(int which);

    // Helper updating or creating the image list, if necessary, unlike
    // GetImageList() which just returns the previously set image list.
    wxImageList* GetUpdatedImageList(int which);

    // Real implementations methods to which our public forwards.
    virtual long DoInsertColumn(long col, const wxListItem& info) = 0;
    virtual void DoUpdateImages(int which) = 0;

    // Overridden methods of the base class.
    virtual wxSize DoGetBestClientSize() const override;

    // these functions are only used for virtual list view controls, i.e. the
    // ones with wxLC_VIRTUAL style

    // return the attribute for the item (may return nullptr if none)
    virtual wxItemAttr* OnGetItemAttr(long item) const;

    // return the text for the given column of the given item
    virtual wxString OnGetItemText(long item, long column) const;

    // return whether the given item is checked
    virtual bool OnGetItemIsChecked(long item) const;

    // return the icon for the given item. In report view, OnGetItemImage will
    // only be called for the first column. See OnGetItemColumnImage for
    // details.
    virtual int OnGetItemImage(long item) const;

    // return the icon for the given item and column.
    virtual int OnGetItemColumnImage(long item, long column) const;

    // return the attribute for the given item and column (may return nullptr if none)
    virtual wxItemAttr* OnGetItemColumnAttr(long item, long column) const;

private:
    wxWithImages m_imagesNormal,
                 m_imagesSmall,
                 m_imagesState;

    // user defined color to draw row lines, may be invalid
    wxItemAttr m_alternateRowColour;
};


//-----------------------------------------------------------------------------
// internal classes
//-----------------------------------------------------------------------------

class ibListHeaderWindow;
class ibListMainWindow;

//-----------------------------------------------------------------------------
// ibListCtrl
//-----------------------------------------------------------------------------

class DESKGUI_API ibListCtrl: public wxNavigationEnabled<ibListCtrlBase>,
                          public wxScrollHelper
{
    typedef wxNavigationEnabled<ibListCtrlBase> BaseType;

public:
    ibListCtrl() : wxScrollHelper(this)
    {
        Init();
    }

    ibListCtrl( wxWindow *parent,
                wxWindowID winid = wxID_ANY,
                const wxPoint &pos = wxDefaultPosition,
                const wxSize &size = wxDefaultSize,
                long style = wxLC_ICON,
                const wxValidator& validator = wxDefaultValidator,
                const wxString &name = wxASCII_STR(wxListCtrlNameStr))
            : wxScrollHelper(this)
    {
        Create(parent, winid, pos, size, style, validator, name);
    }

    virtual ~ibListCtrl();

    void Init();

    bool Create( wxWindow *parent,
                 wxWindowID winid = wxID_ANY,
                 const wxPoint &pos = wxDefaultPosition,
                 const wxSize &size = wxDefaultSize,
                 long style = wxLC_ICON,
                 const wxValidator& validator = wxDefaultValidator,
                 const wxString &name = wxASCII_STR(wxListCtrlNameStr));

    bool GetColumn( int col, wxListItem& item ) const override;
    bool SetColumn( int col, const wxListItem& item ) override;
    int GetColumnWidth( int col ) const override;
    bool SetColumnWidth( int col, int width) override;

    // Column ordering functions
    int GetColumnOrder(int col) const override;
    int GetColumnIndexFromOrder(int order) const override;

    wxArrayInt GetColumnsOrder() const override;
    bool SetColumnsOrder(const wxArrayInt& orders) override;

    int GetCountPerPage() const; // not the same in wxGLC as in Windows, I think
    wxRect GetViewRect() const;

    bool GetItem( wxListItem& info ) const;
    bool SetItem( wxListItem& info ) ;
    bool SetItem( long index, int col, const wxString& label, int imageId = -1 );
    int  GetItemState( long item, long stateMask ) const;
    bool SetItemState( long item, long state, long stateMask);
    bool SetItemImage( long item, int image, int selImage = -1 );
    bool SetItemColumnImage( long item, long column, int image );
    wxString GetItemText( long item, int col = 0 ) const;
    void SetItemText( long item, const wxString& str );
    wxUIntPtr GetItemData( long item ) const;
    bool SetItemPtrData(long item, wxUIntPtr data);
    bool SetItemData(long item, long data) { return SetItemPtrData(item, data); }
    bool GetItemRect( long item, wxRect& rect, int code = wxLIST_RECT_BOUNDS ) const;
    bool GetSubItemRect( long item, long subItem, wxRect& rect, int code = wxLIST_RECT_BOUNDS ) const;
    bool GetItemPosition( long item, wxPoint& pos ) const;
    bool SetItemPosition( long item, const wxPoint& pos ); // not supported in wxGLC
    int GetItemCount() const override;
    int GetColumnCount() const override;
    void SetItemSpacing( int spacing, bool isSmall = false );
    wxSize GetItemSpacing() const;
    void SetItemTextColour( long item, const wxColour& col);
    wxColour GetItemTextColour( long item ) const;
    void SetItemBackgroundColour( long item, const wxColour &col);
    wxColour GetItemBackgroundColour( long item ) const;
    void SetItemFont( long item, const wxFont &f);
    wxFont GetItemFont( long item ) const;
    int GetSelectedItemCount() const;
    wxColour GetTextColour() const;
    void SetTextColour(const wxColour& col);
    long GetTopItem() const;

    bool HasCheckBoxes() const override;
    bool EnableCheckBoxes(bool enable = true) override;
    bool IsItemChecked(long item) const override;
    void CheckItem(long item, bool check) override;

    void ShowSortIndicator(int idx, bool ascending = true) override;
    int GetSortIndicator() const override;
    bool IsAscendingSortIndicator() const override;

    void SetSingleStyle( long style, bool add = true ) ;
    void SetWindowStyleFlag( long style ) override;
    void RecreateWindow() {}
    long GetNextItem( long item, int geometry = wxLIST_NEXT_ALL, int state = wxLIST_STATE_DONTCARE ) const;
    bool Arrange( int flag = wxLIST_ALIGN_DEFAULT ); // always wxLIST_ALIGN_LEFT in wxGLC

    void ClearAll();
    bool DeleteItem( long item );
    bool DeleteAllItems();
    bool DeleteAllColumns() override;
    bool DeleteColumn( int col ) override;

    void SetItemCount(long count);

    ibTextCtrl *EditLabel(long item,
                          wxClassInfo* textControlClass = wxCLASSINFO(ibTextCtrl));

    // End label editing, optionally cancelling the edit
    bool EndEditLabel(bool cancel);

    ibTextCtrl* GetEditControl() const;
    bool IsVisible(long item) const override;
    void Edit( long item ) { EditLabel(item); }

    bool EnsureVisible( long item );
    long FindItem( long start, const wxString& str, bool partial = false );
    long FindItem( long start, wxUIntPtr data );
    long FindItem( long start, const wxPoint& pt, int direction ); // not supported in wxGLC
    long HitTest( const wxPoint& point, int& flags, long *pSubItem = nullptr ) const;
    long InsertItem(wxListItem& info);
    long InsertItem( long index, const wxString& label );
    long InsertItem( long index, int imageIndex );
    long InsertItem( long index, const wxString& label, int imageIndex );
    bool ScrollList( int dx, int dy );
    bool SortItems( wxListCtrlCompare fn, wxIntPtr data );

    // do we have a header window?
    bool HasHeader() const
        { return InReportView() && !HasFlag(wxLC_NO_HEADER); }

    // refresh items selectively (only useful for virtual list controls)
    void RefreshItem(long item);
    void RefreshItems(long itemFrom, long itemTo);

    virtual void EnableBellOnNoMatch(bool on = true) override;

    // overridden base class virtuals
    // ------------------------------

    virtual wxVisualAttributes GetDefaultAttributes() const override
    {
        return GetClassDefaultAttributes(GetWindowVariant());
    }

    static wxVisualAttributes
    GetClassDefaultAttributes(wxWindowVariant variant = wxWINDOW_VARIANT_NORMAL);

    virtual void Update() override;


    // implementation only from now on
    // -------------------------------

    // generic version extension, don't use in portable code
    bool Update( long item );

    void OnInternalIdle( ) override;

    // We have to hand down a few functions
    virtual void Refresh(bool eraseBackground = true,
                         const wxRect *rect = nullptr) override;

    virtual bool SetBackgroundColour( const wxColour &colour ) override;
    virtual bool SetForegroundColour( const wxColour &colour ) override;
    virtual wxColour GetBackgroundColour() const;
    virtual wxColour GetForegroundColour() const;
    virtual bool SetFont( const wxFont &font ) override;
    virtual bool SetCursor( const wxCursor &cursor ) override;

    virtual void ExtendRulesAndAlternateColour(bool extend = true) override;

#if wxUSE_DRAG_AND_DROP
    virtual void SetDropTarget( wxDropTarget *dropTarget ) override;
    virtual wxDropTarget *GetDropTarget() const override;
#endif

    virtual bool ShouldInheritColours() const override { return false; }

    // implementation
    // --------------

    ibListHeaderWindow  *m_headerWin;
    ibListMainWindow    *m_mainWin;

protected:
    // Implement base class pure virtual methods.
    long DoInsertColumn(long col, const wxListItem& info) override;
    void DoUpdateImages(int which) override;

    virtual wxSize DoGetBestClientSize() const override;

    // it calls our OnGetXXX() functions
    friend class ibListMainWindow;

    virtual wxBorder GetDefaultBorder() const override;

    virtual wxSize GetSizeAvailableForScrollTarget(const wxSize& size) override;

private:
    void CreateOrDestroyHeaderWindowAsNeeded();
    void OnScroll( wxScrollWinEvent& event );
    void OnSize( wxSizeEvent &event );

    WX_FORWARD_TO_SCROLL_HELPER()

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibListCtrl);
};

#endif // wxUSE_LISTCTRL

#endif // _DESKGUI_UNIV_LISTCTRL_H_
