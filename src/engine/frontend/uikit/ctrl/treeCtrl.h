// FORKED from the wx GENERIC tree control (wxGenericTreeCtrl -> ibTreeCtrl,
// mechanical ib prefixes). The generic tree is already custom-drawn — the
// port is the usual uikit seam work: ibControl base, theme colours, demo.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/treectlg.h
// Purpose:     wxTreeCtrl class
// Author:      Robert Roebling
// Created:     01/02/97
// Copyright:   (c) 1997,1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _GENERIC_TREECTRL_H_
#define _GENERIC_TREECTRL_H_

#include "frontend/frontend.h"

#if wxUSE_TREECTRL

#include <wx/brush.h>
#include <wx/pen.h>
#include <wx/scrolwin.h>
#include <wx/treebase.h>
#include <wx/withimages.h>

#include "frontend/uikit/ctrl/control.h"

// -----------------------------------------------------------------------------
// forward declaration
// -----------------------------------------------------------------------------

class WXDLLIMPEXP_FWD_CORE wxTreeItemData;

class WXDLLIMPEXP_FWD_CORE wxTextCtrl;
class ibTextCtrl;

// private implementation classes
class ibTreeItem;
class ibTreeTextCtrl;

// -----------------------------------------------------------------------------
// ibTreeCtrlBase — SEAM vs wx: the lost wxTreeCtrlBase rode the native
// wxControl chain (wxSystemThemedControl<wxControl>); this is its
// re-declaration on the ib chain, interface copied from wx/treectrl.h so the
// generic implementation below keeps all its overrides valid
// -----------------------------------------------------------------------------

class FRONTEND_API ibTreeCtrlBase : public ibControl,
                                    public wxWithImages
{
public:
    ibTreeCtrlBase();
    virtual ~ibTreeCtrlBase();

    // accessors
    virtual unsigned int GetCount() const = 0;

    virtual unsigned int GetIndent() const = 0;
    virtual void SetIndent(unsigned int indent) = 0;

    unsigned int GetSpacing() const { return m_spacing; }
    void SetSpacing(unsigned int spacing) { m_spacing = spacing; }

    virtual void SetStateImages(const wxVector<wxBitmapBundle>& images) = 0;

    bool HasStateImages() const { return m_imagesState.HasImages(); }
    int GetStateImageCount() const { return m_imagesState.GetImageCount(); }

    wxImageList *GetStateImageList() const
        { return m_imagesState.GetImageList(); }
    virtual void SetStateImageList(wxImageList *imageList) = 0;
    void AssignStateImageList(wxImageList *imageList)
    {
        SetStateImageList(imageList);
        m_imagesState.TakeOwnership();
    }

    virtual wxString GetItemText(const wxTreeItemId& item) const = 0;
    virtual int GetItemImage(const wxTreeItemId& item,
                     wxTreeItemIcon which = wxTreeItemIcon_Normal) const = 0;
    virtual wxTreeItemData *GetItemData(const wxTreeItemId& item) const = 0;
    virtual wxColour GetItemTextColour(const wxTreeItemId& item) const = 0;
    virtual wxColour GetItemBackgroundColour(const wxTreeItemId& item) const = 0;
    virtual wxFont GetItemFont(const wxTreeItemId& item) const = 0;

    int GetItemState(const wxTreeItemId& item) const
        { return DoGetItemState(item); }

    // modifiers
    virtual void SetItemText(const wxTreeItemId& item, const wxString& text) = 0;
    virtual void SetItemImage(const wxTreeItemId& item,
                              int image,
                              wxTreeItemIcon which = wxTreeItemIcon_Normal) = 0;
    virtual void SetItemData(const wxTreeItemId& item, wxTreeItemData *data) = 0;
    virtual void SetItemHasChildren(const wxTreeItemId& item, bool has = true) = 0;
    virtual void SetItemBold(const wxTreeItemId& item, bool bold = true) = 0;
    virtual void SetItemDropHighlight(const wxTreeItemId& item,
                                      bool highlight = true) = 0;
    virtual void SetItemTextColour(const wxTreeItemId& item,
                                   const wxColour& col) = 0;
    virtual void SetItemBackgroundColour(const wxTreeItemId& item,
                                         const wxColour& col) = 0;
    virtual void SetItemFont(const wxTreeItemId& item, const wxFont& font) = 0;

    void SetItemState(const wxTreeItemId& item, int state);

    // item status inquiries
    virtual bool IsVisible(const wxTreeItemId& item) const = 0;
    virtual bool ItemHasChildren(const wxTreeItemId& item) const = 0;
    bool HasChildren(const wxTreeItemId& item) const
        { return ItemHasChildren(item); }
    virtual bool IsExpanded(const wxTreeItemId& item) const = 0;
    virtual bool IsSelected(const wxTreeItemId& item) const = 0;
    virtual bool IsBold(const wxTreeItemId& item) const = 0;
    bool IsEmpty() const;

    virtual size_t GetChildrenCount(const wxTreeItemId& item,
                                    bool recursively = true) const = 0;

    // navigation
    virtual wxTreeItemId GetRootItem() const = 0;
    virtual wxTreeItemId GetSelection() const = 0;
    virtual size_t GetSelections(wxArrayTreeItemIds& selections) const = 0;
    virtual wxTreeItemId GetFocusedItem() const = 0;
    virtual void ClearFocusedItem() = 0;
    virtual void SetFocusedItem(const wxTreeItemId& item) = 0;
    virtual wxTreeItemId GetItemParent(const wxTreeItemId& item) const = 0;
    virtual wxTreeItemId GetFirstChild(const wxTreeItemId& item,
                                       wxTreeItemIdValue& cookie) const = 0;
    virtual wxTreeItemId GetNextChild(const wxTreeItemId& item,
                                      wxTreeItemIdValue& cookie) const = 0;
    virtual wxTreeItemId GetLastChild(const wxTreeItemId& item) const = 0;
    virtual wxTreeItemId GetNextSibling(const wxTreeItemId& item) const = 0;
    virtual wxTreeItemId GetPrevSibling(const wxTreeItemId& item) const = 0;
    virtual wxTreeItemId GetFirstVisibleItem() const = 0;
    virtual wxTreeItemId GetNextVisible(const wxTreeItemId& item) const = 0;
    virtual wxTreeItemId GetPrevVisible(const wxTreeItemId& item) const = 0;

    // operations
    virtual wxTreeItemId AddRoot(const wxString& text,
                                 int image = -1, int selImage = -1,
                                 wxTreeItemData *data = nullptr) = 0;

    wxTreeItemId PrependItem(const wxTreeItemId& parent,
                             const wxString& text,
                             int image = -1, int selImage = -1,
                             wxTreeItemData *data = nullptr)
        { return DoInsertItem(parent, 0u, text, image, selImage, data); }

    wxTreeItemId InsertItem(const wxTreeItemId& parent,
                            const wxTreeItemId& idPrevious,
                            const wxString& text,
                            int image = -1, int selImage = -1,
                            wxTreeItemData *data = nullptr)
        { return DoInsertAfter(parent, idPrevious, text, image, selImage, data); }

    wxTreeItemId InsertItem(const wxTreeItemId& parent,
                            size_t pos,
                            const wxString& text,
                            int image = -1, int selImage = -1,
                            wxTreeItemData *data = nullptr)
        { return DoInsertItem(parent, pos, text, image, selImage, data); }

    wxTreeItemId AppendItem(const wxTreeItemId& parent,
                            const wxString& text,
                            int image = -1, int selImage = -1,
                            wxTreeItemData *data = nullptr)
        { return DoInsertItem(parent, (size_t)-1, text, image, selImage, data); }

    virtual void Delete(const wxTreeItemId& item) = 0;
    virtual void DeleteChildren(const wxTreeItemId& item) = 0;
    virtual void DeleteAllItems() = 0;

    virtual void Expand(const wxTreeItemId& item) = 0;
    void ExpandAllChildren(const wxTreeItemId& item);
    void ExpandAll();
    virtual void Collapse(const wxTreeItemId& item) = 0;
    void CollapseAllChildren(const wxTreeItemId& item);
    void CollapseAll();
    virtual void CollapseAndReset(const wxTreeItemId& item) = 0;
    virtual void Toggle(const wxTreeItemId& item) = 0;

    virtual void Unselect() = 0;
    virtual void UnselectAll() = 0;
    virtual void SelectItem(const wxTreeItemId& item, bool select = true) = 0;
    virtual void SelectChildren(const wxTreeItemId& parent) = 0;
    void UnselectItem(const wxTreeItemId& item) { SelectItem(item, false); }
    void ToggleItemSelection(const wxTreeItemId& item)
        { SelectItem(item, !IsSelected(item)); }

    virtual void EnsureVisible(const wxTreeItemId& item) = 0;
    virtual void ScrollTo(const wxTreeItemId& item) = 0;

    virtual ibTextCtrl *EditLabel(const wxTreeItemId& item,
                      wxClassInfo* textCtrlClass = nullptr) = 0;
    virtual ibTextCtrl *GetEditControl() const = 0;
    virtual void EndEditLabel(const wxTreeItemId& item,
                              bool discardChanges = false) = 0;

    virtual void EnableBellOnNoMatch(bool WXUNUSED(on) = true) { }

    // sorting
    virtual int OnCompareItems(const wxTreeItemId& item1,
                               const wxTreeItemId& item2)
        { return wxStrcmp(GetItemText(item1), GetItemText(item2)); }

    virtual void SortChildren(const wxTreeItemId& item) = 0;

    // items geometry
    wxTreeItemId HitTest(const wxPoint& point) const
        { int dummy; return DoTreeHitTest(point, dummy); }
    wxTreeItemId HitTest(const wxPoint& point, int& flags) const
        { return DoTreeHitTest(point, flags); }

    virtual bool GetBoundingRect(const wxTreeItemId& item,
                                 wxRect& rect,
                                 bool textOnly = false) const = 0;

    // implementation
    virtual bool ShouldInheritColours() const override { return false; }

    void SetQuickBestSize(bool q) { m_quickBestSize = q; }
    bool GetQuickBestSize() const { return m_quickBestSize; }

protected:
    // the tree is an input surface — same thin themed frame as the fields
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_SUNKEN; }

public:

protected:
    virtual wxSize DoGetBestSize() const override;

    virtual int DoGetItemState(const wxTreeItemId& item) const = 0;
    virtual void DoSetItemState(const wxTreeItemId& item, int state) = 0;

    virtual wxTreeItemId DoInsertItem(const wxTreeItemId& parent,
                                      size_t pos,
                                      const wxString& text,
                                      int image, int selImage,
                                      wxTreeItemData *data) = 0;
    virtual wxTreeItemId DoInsertAfter(const wxTreeItemId& parent,
                                       const wxTreeItemId& idPrevious,
                                       const wxString& text,
                                       int image = -1, int selImage = -1,
                                       wxTreeItemData *data = nullptr) = 0;
    virtual wxTreeItemId DoTreeHitTest(const wxPoint& point,
                                       int& flags) const = 0;

    virtual bool HasAnyImages() const override;

    // a second set of images for the app-defined item states
    wxWithImages m_imagesState;

    // spacing between left border and the text
    unsigned int m_spacing;

    // whether full or quick calculation is done in DoGetBestSize
    bool        m_quickBestSize;

private:
    // intercept Escape and Return for the in-place edit control
    void OnCharHook(wxKeyEvent& event);

    wxDECLARE_NO_COPY_CLASS(ibTreeCtrlBase);
};

// -----------------------------------------------------------------------------
// ibTreeCtrl - the tree control
// -----------------------------------------------------------------------------

class FRONTEND_API ibTreeCtrl : public ibTreeCtrlBase,
                                public wxScrollHelper
{
public:
    // creation
    // --------

    ibTreeCtrl() : ibTreeCtrlBase(), wxScrollHelper(this) { Init(); }

    ibTreeCtrl(wxWindow *parent, wxWindowID id = wxID_ANY,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = wxTR_DEFAULT_STYLE,
               const wxValidator &validator = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxTreeCtrlNameStr))
        : ibTreeCtrlBase(),
          wxScrollHelper(this)
    {
        Init();
        Create(parent, id, pos, size, style, validator, name);
    }

    virtual ~ibTreeCtrl();

    bool Create(wxWindow *parent, wxWindowID id = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxTR_DEFAULT_STYLE,
                const wxValidator &validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxTreeCtrlNameStr));

    // implement base class pure virtuals
    // ----------------------------------

    virtual unsigned int GetCount() const override;

    virtual unsigned int GetIndent() const override { return m_indent; }
    virtual void SetIndent(unsigned int indent) override;

    virtual void SetStateImages(const wxVector<wxBitmapBundle>& images) override;

    virtual void SetImageList(wxImageList *imageList) override;
    virtual void SetStateImageList(wxImageList *imageList) override;

    virtual wxString GetItemText(const wxTreeItemId& item) const override;
    virtual int GetItemImage(const wxTreeItemId& item,
                     wxTreeItemIcon which = wxTreeItemIcon_Normal) const override;
    virtual wxTreeItemData *GetItemData(const wxTreeItemId& item) const override;
    virtual wxColour GetItemTextColour(const wxTreeItemId& item) const override;
    virtual wxColour GetItemBackgroundColour(const wxTreeItemId& item) const override;
    virtual wxFont GetItemFont(const wxTreeItemId& item) const override;

    virtual void SetItemText(const wxTreeItemId& item, const wxString& text) override;
    virtual void SetItemImage(const wxTreeItemId& item,
                              int image,
                              wxTreeItemIcon which = wxTreeItemIcon_Normal) override;
    virtual void SetItemData(const wxTreeItemId& item, wxTreeItemData *data) override;

    virtual void SetItemHasChildren(const wxTreeItemId& item, bool has = true) override;
    virtual void SetItemBold(const wxTreeItemId& item, bool bold = true) override;
    virtual void SetItemDropHighlight(const wxTreeItemId& item, bool highlight = true) override;
    virtual void SetItemTextColour(const wxTreeItemId& item, const wxColour& col) override;
    virtual void SetItemBackgroundColour(const wxTreeItemId& item, const wxColour& col) override;
    virtual void SetItemFont(const wxTreeItemId& item, const wxFont& font) override;

    virtual bool IsVisible(const wxTreeItemId& item) const override;
    virtual bool ItemHasChildren(const wxTreeItemId& item) const override;
    virtual bool IsExpanded(const wxTreeItemId& item) const override;
    virtual bool IsSelected(const wxTreeItemId& item) const override;
    virtual bool IsBold(const wxTreeItemId& item) const override;

    virtual size_t GetChildrenCount(const wxTreeItemId& item,
                                    bool recursively = true) const override;

    // navigation
    // ----------

    virtual wxTreeItemId GetRootItem() const override { return m_anchor; }
    virtual wxTreeItemId GetSelection() const override
    {
        wxASSERT_MSG( !HasFlag(wxTR_MULTIPLE),
                       wxT("must use GetSelections() with this control") );

        return m_current;
    }
    virtual size_t GetSelections(wxArrayTreeItemIds&) const override;
    virtual wxTreeItemId GetFocusedItem() const override { return m_current; }

    virtual void ClearFocusedItem() override;
    virtual void SetFocusedItem(const wxTreeItemId& item) override;

    virtual wxTreeItemId GetItemParent(const wxTreeItemId& item) const override;
    virtual wxTreeItemId GetFirstChild(const wxTreeItemId& item,
                                       wxTreeItemIdValue& cookie) const override;
    virtual wxTreeItemId GetNextChild(const wxTreeItemId& item,
                                      wxTreeItemIdValue& cookie) const override;
    virtual wxTreeItemId GetLastChild(const wxTreeItemId& item) const override;
    virtual wxTreeItemId GetNextSibling(const wxTreeItemId& item) const override;
    virtual wxTreeItemId GetPrevSibling(const wxTreeItemId& item) const override;

    virtual wxTreeItemId GetFirstVisibleItem() const override;
    virtual wxTreeItemId GetNextVisible(const wxTreeItemId& item) const override;
    virtual wxTreeItemId GetPrevVisible(const wxTreeItemId& item) const override;


    // operations
    // ----------

    virtual wxTreeItemId AddRoot(const wxString& text,
                         int image = -1, int selectedImage = -1,
                         wxTreeItemData *data = nullptr) override;

    virtual void Delete(const wxTreeItemId& item) override;
    virtual void DeleteChildren(const wxTreeItemId& item) override;
    virtual void DeleteAllItems() override;

    virtual void Expand(const wxTreeItemId& item) override;
    virtual void Collapse(const wxTreeItemId& item) override;
    virtual void CollapseAndReset(const wxTreeItemId& item) override;
    virtual void Toggle(const wxTreeItemId& item) override;

    virtual void Unselect() override;
    virtual void UnselectAll() override;
    virtual void SelectItem(const wxTreeItemId& item, bool select = true) override;
    virtual void SelectChildren(const wxTreeItemId& parent) override;

    virtual void EnsureVisible(const wxTreeItemId& item) override;
    virtual void ScrollTo(const wxTreeItemId& item) override;

    virtual ibTextCtrl *EditLabel(const wxTreeItemId& item,
                          wxClassInfo* textCtrlClass = nullptr) override;
    virtual ibTextCtrl *GetEditControl() const override;
    virtual void EndEditLabel(const wxTreeItemId& item,
                              bool discardChanges = false) override;

    virtual void EnableBellOnNoMatch(bool on = true) override;

    virtual void SortChildren(const wxTreeItemId& item) override;

    // items geometry
    // --------------

    virtual bool GetBoundingRect(const wxTreeItemId& item,
                                 wxRect& rect,
                                 bool textOnly = false) const override;


    // this version specific methods
    // -----------------------------

    wxImageList *GetButtonsImageList() const
    {
        return m_imagesButtons.GetImageList();
    }
    void SetButtonsImageList(wxImageList *imageList);
    void AssignButtonsImageList(wxImageList *imageList);

    void SetDropEffectAboveItem( bool above = false ) { m_dropEffectAboveItem = above; }
    bool GetDropEffectAboveItem() const { return m_dropEffectAboveItem; }

    wxTreeItemId GetNext(const wxTreeItemId& item) const;

    // implementation only from now on

    // overridden base class virtuals
    virtual bool SetBackgroundColour(const wxColour& colour) override;
    virtual bool SetForegroundColour(const wxColour& colour) override;

    virtual void Refresh(bool eraseBackground = true, const wxRect *rect = nullptr) override;

    virtual bool SetFont( const wxFont &font ) override;
    virtual void SetWindowStyleFlag(long styles) override;

    // callbacks
    void OnPaint( wxPaintEvent &event );
    void OnSetFocus( wxFocusEvent &event );
    void OnKillFocus( wxFocusEvent &event );
    void OnKeyDown( wxKeyEvent &event );
    void OnChar( wxKeyEvent &event );
    void OnMouse( wxMouseEvent &event );
    void OnGetToolTip( wxTreeEvent &event );
    void OnSize( wxSizeEvent &event );
    void OnInternalIdle( ) override;

    virtual wxVisualAttributes GetDefaultAttributes() const override
    {
        return GetClassDefaultAttributes(GetWindowVariant());
    }

    static wxVisualAttributes
    GetClassDefaultAttributes(wxWindowVariant variant = wxWINDOW_VARIANT_NORMAL);

    // implementation helpers
    void AdjustMyScrollbars();

    WX_FORWARD_TO_SCROLL_HELPER()

protected:
    friend class ibTreeItem;
    friend class ibTreeRenameTimer;
    friend class ibTreeFindTimer;
    friend class ibTreeTextCtrl;

    wxFont               m_boldFont;

    ibTreeItem   *m_anchor;
    ibTreeItem   *m_current,
                        *m_key_current,
                        // A hint to select a parent item after deleting a child
                        *m_select_me;
    unsigned int         m_indent;
    int                  m_lineHeight;
    wxPen                m_dottedPen;
    wxBrush              m_hilightBrush,
                         m_hilightUnfocusedBrush;
    bool                 m_hasFocus;
    bool                 m_dirty;
    bool                 m_isDragging; // true between BEGIN/END drag events
    bool                 m_lastOnSame;  // last click on the same item as prev

    wxWithImages         m_imagesButtons;

    int                  m_dragCount;
    wxPoint              m_dragStart;
    ibTreeItem   *m_dropTarget;
    wxCursor             m_oldCursor;  // cursor is changed while dragging
    ibTreeItem   *m_oldSelection;
    ibTreeItem   *m_underMouse; // for visual effects

    enum { NoEffect, BorderEffect, AboveEffect, BelowEffect } m_dndEffect;
    ibTreeItem   *m_dndEffectItem;

    ibTreeTextCtrl      *m_textCtrl;


    wxTimer             *m_renameTimer;

    // incremental search data
    wxString             m_findPrefix;
    wxTimer             *m_findTimer;
    // This flag is set to 0 if the bell is disabled, 1 if it is enabled and -1
    // if it is globally enabled but has been temporarily disabled because we
    // had already beeped for this particular search.
    int                  m_findBell;

    bool                 m_dropEffectAboveItem;

    // the common part of all ctors
    void Init();

    // overridden wxWindow methods
    virtual void DoThaw() override;

    virtual void OnImagesChanged() override;
    void UpdateAfterImageListChange();

    // misc helpers
    void SendDeleteEvent(ibTreeItem *itemBeingDeleted);

    void DrawBorder(const wxTreeItemId& item);
    void DrawLine(const wxTreeItemId& item, bool below);
    void DrawDropEffect(ibTreeItem *item);

    void DoSelectItem(const wxTreeItemId& id,
                      bool unselect_others = true,
                      bool extended_select = false);

    virtual int DoGetItemState(const wxTreeItemId& item) const override;
    virtual void DoSetItemState(const wxTreeItemId& item, int state) override;

    virtual wxTreeItemId DoInsertItem(const wxTreeItemId& parent,
                                      size_t previous,
                                      const wxString& text,
                                      int image,
                                      int selectedImage,
                                      wxTreeItemData *data) override;
    virtual wxTreeItemId DoInsertAfter(const wxTreeItemId& parent,
                                       const wxTreeItemId& idPrevious,
                                       const wxString& text,
                                       int image = -1, int selImage = -1,
                                       wxTreeItemData *data = nullptr) override;
    virtual wxTreeItemId DoTreeHitTest(const wxPoint& point, int& flags) const override;

    // called by wxTextTreeCtrl when it marks itself for deletion
    void ResetTextControl();

    // find the first item starting with the given prefix after the given item
    wxTreeItemId FindItem(const wxTreeItemId& id, const wxString& prefix) const;

    bool HasButtons() const { return HasFlag(wxTR_HAS_BUTTONS); }

    void CalculateLineHeight();
    int  GetLineHeight(ibTreeItem *item) const;
    void PaintLevel( ibTreeItem *item, wxDC& dc, int level, int &y );
    void PaintItem( ibTreeItem *item, wxDC& dc);

    void CalculateLevel( ibTreeItem *item, wxReadOnlyDC &dc, int level, int &y );
    void CalculatePositions();

    void RefreshSubtree( ibTreeItem *item );
    void RefreshLine( ibTreeItem *item );

    // redraw all selected items
    void RefreshSelected();

    // RefreshSelected() recursive helper
    void RefreshSelectedUnder(ibTreeItem *item);

    void OnRenameTimer();
    bool OnRenameAccept(ibTreeItem *item, const wxString& value);
    void OnRenameCancelled(ibTreeItem *item);

    void FillArray(ibTreeItem*, wxArrayTreeItemIds&) const;
    void SelectItemRange( ibTreeItem *item1, ibTreeItem *item2 );
    bool TagAllChildrenUntilLast(ibTreeItem *crt_item, ibTreeItem *last_item, bool select);
    bool TagNextChildren(ibTreeItem *crt_item, ibTreeItem *last_item, bool select);
    void UnselectAllChildren( ibTreeItem *item );
    void ChildrenClosing(ibTreeItem* item);

    void DoDirtyProcessing();

    virtual wxSize DoGetBestSize() const override;

private:
    void OnSysColourChanged(wxSysColourChangedEvent& WXUNUSED(event))
    {
        InitVisualAttributes();
    }

    // (Re)initialize colours, fonts, pens, brushes used by the control using
    // the current system colours and font.
    void InitVisualAttributes();

    // Reset the state of the last find (i.e. keyboard incremental search)
    // operation.
    void ResetFindState();

    // Find the next item, either looking inside the collapsed items or not.
    enum
    {
        Next_Any     = 0,
        Next_Visible = 1
    };
    wxTreeItemId DoGetNext(const wxTreeItemId& item, int flags = 0) const;

    // True if we're using custom colours/font, respectively, or false if we're
    // using the default colours and should update them whenever system colours
    // change.
    bool m_hasExplicitFgCol:1,
         m_hasExplicitBgCol:1,
         m_hasExplicitFont:1;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibTreeCtrl);
    wxDECLARE_NO_COPY_CLASS(ibTreeCtrl);
};

// Also define wxTreeCtrl to be ibTreeCtrl on all platforms without a
// native version, i.e. all but MSW and Qt.
#if !(defined(__WXMSW__) || defined(__WXQT__)) || defined(__WXUNIVERSAL__)
/*
 * wxTreeCtrl has to be a real class or we have problems with
 * the run-time information.
 */

class WXDLLIMPEXP_CORE wxTreeCtrl: public ibTreeCtrl
{
    wxDECLARE_DYNAMIC_CLASS(wxTreeCtrl);

public:
    wxTreeCtrl() = default;

    wxTreeCtrl(wxWindow *parent, wxWindowID id = wxID_ANY,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = wxTR_DEFAULT_STYLE,
               const wxValidator &validator = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxTreeCtrlNameStr))
    : ibTreeCtrl(parent, id, pos, size, style, validator, name)
    {
    }
};
#endif // !(__WXMSW__ || __WXQT__) || __WXUNIVERSAL__

#endif // wxUSE_TREECTRL

#endif // _GENERIC_TREECTRL_H_
