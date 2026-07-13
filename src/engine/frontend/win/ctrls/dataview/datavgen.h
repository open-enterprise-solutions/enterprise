/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/dataview.h
// Purpose:     ibDataViewCtrl generic implementation header
// Author:      Robert Roebling
// Modified By: Bo Yang
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __GENERICDATAVIEWCTRLH__
#define __GENERICDATAVIEWCTRLH__

#include <wx/defs.h>
#include <wx/object.h>
#include <wx/compositewin.h>
#include <wx/control.h>
#include <wx/scrolwin.h>
#include <wx/selstore.h>
#include <wx/icon.h>
#include <wx/vector.h>
#include <functional>
#if wxUSE_ACCESSIBILITY
#include <wx/access.h>
#endif // wxUSE_ACCESSIBILITY

class ibDataViewMainWindow;
class ibDataViewHeaderWindow;
class ibDataViewFooterWindow;

#if wxUSE_ACCESSIBILITY
class FRONTEND_API ibDataViewCtrlAccessible;
#endif // wxUSE_ACCESSIBILITY

// ---------------------------------------------------------
// ibDataViewColumn
// ---------------------------------------------------------

class FRONTEND_API ibDataViewColumn : public ibDataViewColumnBase
{
	class ibDataViewFooterColumn : public wxSettableHeaderColumn
	{
	public:
		// ctors and dtor
		ibDataViewFooterColumn(ibDataViewColumn* owner)
			: m_owner(owner)
		{
			Init();
		}

		// implement base class pure virtuals
		virtual void SetTitle(const wxString& title) wxOVERRIDE
		{
			m_title = title;
			m_owner->UpdateWidth();
		}

		virtual wxString GetTitle() const wxOVERRIDE { return m_title; }

		virtual void SetBitmap(const wxBitmapBundle& bitmap) wxOVERRIDE { m_bitmap = bitmap; }
		wxBitmap GetBitmap() const wxOVERRIDE { wxFAIL_MSG("unreachable"); return wxNullBitmap; }
		wxBitmapBundle GetBitmapBundle() const wxOVERRIDE { return m_bitmap; }

		virtual void SetWidth(int width) wxOVERRIDE { m_owner->SetWidth(width); }
		virtual int GetWidth() const wxOVERRIDE { return m_owner->GetWidth(); }

		virtual void SetMinWidth(int minWidth) wxOVERRIDE { m_owner->SetMinWidth(minWidth); }
		virtual int GetMinWidth() const wxOVERRIDE { return m_owner->GetMinWidth(); }

		virtual void SetAlignment(wxAlignment align) wxOVERRIDE
		{
			m_align = align;
			m_owner->UpdateDisplay();
		}

		virtual wxAlignment GetAlignment() const wxOVERRIDE { return m_align; }

		virtual void SetFlags(int flags) wxOVERRIDE {}
		virtual int GetFlags() const wxOVERRIDE { return m_owner->GetFlags(); }

		virtual bool IsSortKey() const wxOVERRIDE { return false; }
		virtual void UnsetAsSortKey() wxOVERRIDE {}

		virtual void SetSortOrder(bool ascending) wxOVERRIDE {}
		virtual bool IsSortOrderAscending() const wxOVERRIDE { return false; }

	private:

		// common part of all ctors
		void Init()
		{
			m_align = wxAlignment::wxALIGN_LEFT;
		}

		ibDataViewColumn* m_owner;

		wxString m_title;
		wxBitmapBundle m_bitmap;
		wxAlignment m_align;
	};

public:

	ibDataViewColumn(const wxString& title,
		ibDataViewRenderer* renderer,
		unsigned int model_column,
		int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE)
		: ibDataViewColumnBase(renderer, model_column),
		m_title(title), m_footer_column(this)
	{
		Init(width, align, flags);
	}

	ibDataViewColumn(const wxBitmapBundle& bitmap,
		ibDataViewRenderer* renderer,
		unsigned int model_column,
		int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE)
		: ibDataViewColumnBase(bitmap, renderer, model_column), m_footer_column(this)
	{
		Init(width, align, flags);
	}

	// footer column data
	const wxSettableHeaderColumn& GetFooterColumn() const { return m_footer_column; }

	// implement wxHeaderColumnBase methods
	virtual void SetTitle(const wxString& title) wxOVERRIDE
	{
		m_title = title;
		UpdateWidth();
	}

	virtual wxString GetTitle() const wxOVERRIDE
	{
		return m_title;
	}

	virtual void SetWidth(int width) wxOVERRIDE
	{
		// Call the actual update method, used for both automatic and "manual"
		// width changes.
		WXUpdateWidth(width);

		// Do remember the last explicitly set width: this is used to prevent
		// UpdateColumnSizes() from resizing the last column to be smaller than
		// this size.
		m_manuallySetWidth = width;
	}
	virtual int GetWidth() const wxOVERRIDE;

	virtual void SetMinWidth(int minWidth) wxOVERRIDE
	{
		m_minWidth = minWidth;
		UpdateWidth();
	}
	virtual int GetMinWidth() const wxOVERRIDE
	{
		return m_minWidth;
	}

	virtual void SetAlignment(wxAlignment align) wxOVERRIDE
	{
		m_align = align;
		UpdateDisplay();
	}
	virtual wxAlignment GetAlignment() const wxOVERRIDE
	{
		return m_align;
	}

	virtual void SetFlags(int flags) wxOVERRIDE
	{
		m_flags = flags;
		UpdateDisplay();
	}
	virtual int GetFlags() const wxOVERRIDE
	{
		return m_flags;
	}

	virtual bool IsSortKey() const wxOVERRIDE
	{
		return m_sort;
	}

	virtual void UnsetAsSortKey() wxOVERRIDE;

	virtual void SetSortOrder(bool ascending) wxOVERRIDE;

	virtual bool IsSortOrderAscending() const wxOVERRIDE
	{
		return m_sortAscending;
	}

	virtual void SetBitmap(const wxBitmapBundle& bitmap) wxOVERRIDE
	{
		ibDataViewColumnBase::SetBitmap(bitmap);
		UpdateWidth();
	}

	// implement footer methods
	void SetFooterTitle(const wxString& title) { m_footer_column.SetTitle(title); }
	wxString GetFooterTitle() const { return m_footer_column.GetTitle(); }

	void SetFooterBitmap(const wxBitmapBundle& bitmap) { m_footer_column.SetBitmap(bitmap); }
	wxBitmap GetFooterBitmap() const { return m_footer_column.GetBitmap(); }
	wxBitmapBundle GetFooterBitmapBundle() const { return m_footer_column.GetBitmapBundle(); }

	void SetFooterAlignment(wxAlignment align) { m_footer_column.SetAlignment(align); }
	wxAlignment GetFooterAlignment() const { return m_footer_column.GetAlignment(); }

	// This method is specific to the generic implementation and is used only
	// by wxWidgets itself.
	void WXUpdateWidth(int width)
	{
		if (width == m_width)
			return;

		m_width = width;
		UpdateWidth();
	}

	// This method is also internal and called when the column is resized by
	// user interactively.
	void WXOnResize(int width);

	virtual int WXGetSpecifiedWidth() const wxOVERRIDE;

private:
	// common part of all ctors
	void Init(int width, wxAlignment align, int flags);

	// These methods forward to ibDataViewCtrl::OnColumnChange() and
	// OnColumnWidthChange() respectively, i.e. the latter is stronger than the
	// former.
	void UpdateDisplay();
	void UpdateWidth();

	// Return the effective value corresponding to the given width, handling
	// its negative values such as wxCOL_WIDTH_DEFAULT.
	int DoGetEffectiveWidth(int width) const;

	wxString m_title;
	int m_width,
		m_manuallySetWidth,
		m_minWidth;
	wxAlignment m_align;

	int m_flags;
	bool m_sort,
		m_sortAscending;

	ibDataViewFooterColumn m_footer_column;

	friend class ibDataViewHeaderWindowBase;
	friend class ibDataViewHeaderWindow;
	friend class ibDataViewHeaderWindowMSW;
};

// NB: this used to be an anonymous namespace, but anonymous namespaces in
// HEADERS produce per-translation-unit types — each .cpp that includes
// the header sees its own WalkFlags / SortOrder, with different mangled
// names.  When `ibDataViewCtrl::GetRowByItem(item, WalkFlags)` is defined
// in one .cpp and called from another (datavgen.cpp ↔ datavgen.paged.cpp),
// the linker can't match.  Switched to a named namespace 2026-05-08 — types
// have external linkage, every TU sees the same symbol.
namespace ibDataViewInternal
{
	// Flags for Walker() function defined below.
	enum WalkFlags
	{
		Walk_All,               // Visit all items.
		Walk_ExpandedOnly       // Visit only expanded items.
	};

	// The column is either the index of the column to be used for sorting or one
	// of the special values in this enum:
	enum
	{
		// Don't sort at all.
		SortColumn_None = -2,

		// Sort using the model default sort order.
		SortColumn_Default = -1
	};

	// A class storing the definition of sort order used, as a column index and
	// sort direction by this column.
	//
	// Notice that the sort order may be invalid, meaning that items shouldn't be
	// sorted.
	class SortOrder
	{
	public:
		explicit SortOrder(int column = SortColumn_None, bool ascending = true)
			: m_column(column),
			m_ascending(ascending)
		{
		}

		// Default copy ctor, assignment operator and dtor are all OK.

		bool IsNone() const { return m_column == SortColumn_None; }

		bool UsesColumn() const { return m_column >= 0; }

		int GetColumn() const { return m_column; }
		bool IsAscending() const { return m_ascending; }

		bool operator==(const SortOrder& other) const
		{
			return m_column == other.m_column && m_ascending == other.m_ascending;
		}

		bool operator!=(const SortOrder& other) const
		{
			return !(*this == other);
		}

	private:
		int m_column;
		bool m_ascending;
	};

} // namespace ibDataViewInternal

using ibDataViewInternal::WalkFlags;
using ibDataViewInternal::Walk_All;
using ibDataViewInternal::Walk_ExpandedOnly;
using ibDataViewInternal::SortColumn_None;
using ibDataViewInternal::SortColumn_Default;
using ibDataViewInternal::SortOrder;

// ----------------------------------------------------------------------------
// ibDataViewTreeNodeViewMode
// ----------------------------------------------------------------------------

enum ibDataViewTreeNodeViewMode
{
	ibDataViewCell,
	ibDataViewHierarchy,
};

//-----------------------------------------------------------------------------
// ibDataViewTreeNode
//-----------------------------------------------------------------------------

class FRONTEND_API ibDataViewCtrl;
class ibDataViewTreeNode;

typedef wxVector<ibDataViewTreeNode*> ibDataViewTreeNodes;

// Note: this class is not used at all for virtual list models, so all code
// using it, i.e. any functions taking or returning objects of this type,
// including ibDataViewCtrl::m_root, can only be called after checking
// that we're using a non-"virtual list" model.

class ibDataViewTreeNode
{
public:

	ibDataViewTreeNode(ibDataViewTreeNode* parent, const ibDataViewItem& item)
		: m_parent(parent),
		m_item(item),
		m_branchData(NULL), m_viewMode(ibDataViewCell)
	{
	}

	ibDataViewTreeNode(ibDataViewTreeNode* parent, const ibDataViewItem& item, ibDataViewTreeNodeViewMode nodeType)
		: m_parent(parent),
		m_item(item),
		m_branchData(NULL), m_viewMode(nodeType)
	{
	}

	~ibDataViewTreeNode()
	{
		if (m_branchData)
		{
			ibDataViewTreeNodes& nodes = m_branchData->children;
			for (ibDataViewTreeNodes::iterator i = nodes.begin();
				i != nodes.end();
				++i)
			{
				delete* i;
			}

			delete m_branchData;
		}
	}

	ibDataViewTreeNodeViewMode GetViewMode() const
	{
		return m_viewMode;
	}

	static ibDataViewTreeNode* CreateRootNode()
	{
		ibDataViewTreeNode* n = new ibDataViewTreeNode(NULL, ibDataViewItem());
		n->m_branchData = new BranchNodeData;
		n->m_branchData->open = true;
		return n;
	}

	ibDataViewTreeNode* GetParent() const { return m_parent; }

	const ibDataViewTreeNodes& GetChildNodes() const
	{
		return m_branchData->children;
	}

	void InsertChild(ibDataViewCtrl* window,
		ibDataViewTreeNode* node, unsigned index);

	void RemoveChild(unsigned index)
	{
		wxCHECK_RET(m_branchData != NULL, "leaf node doesn't have children");
		m_branchData->RemoveChild(index);
	}

	// returns position of child node for given item in children list or wxNOT_FOUND
	int FindChildByItem(const ibDataViewItem& item) const
	{
		if (!m_branchData)
			return wxNOT_FOUND;

		// Pointer-identity match: operator== dispatches to
		// ibDataViewObject::IsEqualTo (value-based), which over
		// fresh / default-valued rows returns true for unrelated
		// instances.  Tree-walker and InsertChild path need actual
		// instance identity here.
		const ibDataViewTreeNodes& nodes = m_branchData->children;
		const int len = nodes.size();
		for (int i = 0; i < len; i++)
		{
			if (nodes[i]->m_item.GetID() == item.GetID())
				return i;
		}
		return wxNOT_FOUND;
	}

	const ibDataViewItem& GetItem() const { return m_item; }
	void SetItem(const ibDataViewItem& item) { m_item = item; }

	int GetIndentLevel() const
	{
		int ret = 0;
		const ibDataViewTreeNode* node = this;
		while (node->GetParent()->GetParent() != NULL)
		{
			node = node->GetParent();
			ret++;
		}
		return ret;
	}

	bool IsOpen() const
	{
		return m_branchData && m_branchData->open;
	}

	void ToggleOpen(ibDataViewCtrl* window)
	{
		// We do not allow the (invisible) root node to be collapsed because
		// there is no way to expand it again.
		if (!m_parent)
			return;

		wxCHECK_RET(m_branchData != NULL, "can't open leaf node");

		int sum = 0;

		const ibDataViewTreeNodes& nodes = m_branchData->children;
		const int len = nodes.size();
		for (int i = 0; i < len; i++)
			sum += 1 + nodes[i]->GetSubTreeCount();

		if (m_branchData->open)
		{
			ChangeSubTreeCount(-sum);
			m_branchData->open = !m_branchData->open;
		}
		else
		{
			m_branchData->open = !m_branchData->open;
			ChangeSubTreeCount(+sum);
			// Sort the children if needed
			Resort(window);
		}
	}

	// "HasChildren" property corresponds to model's IsContainer(). Note that it may be true
	// even if GetChildNodes() is empty; see below.
	bool HasChildren() const
	{
		return m_branchData != NULL;
	}

	void SetHasChildren(bool has)
	{
		// The invisible root item always has children, so ignore any attempts
		// to change this.
		if (!m_parent)
			return;

		if (!has)
		{
			wxDELETE(m_branchData);
		}
		else if (m_branchData == NULL)
		{
			m_branchData = new BranchNodeData;
		}
	}

	int GetSubTreeCount() const
	{
		return m_branchData ? m_branchData->subTreeCount : 0;
	}

	// Soft-eviction flag: hidden nodes stay in the children list but
	// are skipped by walkers and not counted in subTreeCount.  Used
	// by paged scroll prefetch to keep the loaded window compact
	// without losing already-fetched rows — backward scroll just
	// flips the flag back instead of going to the DB again.
	bool IsHidden() const { return m_hidden; }
	void SetHidden(bool h)
	{
		if (m_hidden == h) return;
		m_hidden = h;
		if (m_parent)
			m_parent->ChangeSubTreeCount(h ? -1 : +1);
	}

	void ChangeSubTreeCount(int num)
	{
		wxASSERT(m_branchData != NULL);

		if (!m_branchData->open)
			return;

		m_branchData->subTreeCount += num;
		wxASSERT(m_branchData->subTreeCount >= 0);

		if (m_parent)
			m_parent->ChangeSubTreeCount(num);
	}

	void Resort(ibDataViewCtrl* window);

	// Should be called after changing the item value to update its position in
	// the control if necessary.
	void PutInSortOrder(ibDataViewCtrl* window)
	{
		if (m_parent)
			m_parent->PutChildInSortOrder(window, this);
	}

private:

	// Called by the child after it has been updated to put it in the right
	// place among its siblings, depending on the sort order.
	//
	// The argument must be non-null, but is passed as a pointer as it's
	// inserted into m_branchData->children.
	void PutChildInSortOrder(ibDataViewCtrl* window,
		ibDataViewTreeNode* childNode);


	ibDataViewTreeNode* m_parent;

	// Set node type 
	ibDataViewTreeNodeViewMode m_viewMode;

	// Corresponding model item.
	ibDataViewItem       m_item;

	// Data specific to non-leaf (branch, inner) nodes. They are kept in a
	// separate struct in order to conserve memory.
	struct BranchNodeData
	{
		BranchNodeData()
			: open(false),
			subTreeCount(0)
		{
		}

		void InsertChild(ibDataViewTreeNode* node, unsigned index)
		{
			children.insert(children.begin() + index, node);
		}

		void RemoveChild(unsigned index)
		{
			children.erase(children.begin() + index);
		}

		// Child nodes. Note that this may be empty even if m_hasChildren in
		// case this branch of the tree wasn't expanded and realized yet.
		ibDataViewTreeNodes  children;

		// Order in which children are sorted (possibly none).
		SortOrder            sortOrder;

		// Is the branch node currently open (expanded)?
		bool                 open;

		// Total count of expanded (i.e. visible with the help of some
		// scrolling) items in the subtree, but excluding this node. I.e. it is
		// 0 for leaves and is the number of rows the subtree occupies for
		// branch nodes.
		int                  subTreeCount;
	};

	BranchNodeData* m_branchData;
	bool            m_hidden = false;
};

//-----------------------------------------------------------------------------
// ibDataViewRenameTimer
//-----------------------------------------------------------------------------

class ibDataViewRenameTimer : public wxTimer
{
public:
	ibDataViewRenameTimer(ibDataViewCtrl* owner);
	void Notify() wxOVERRIDE;
private:
	ibDataViewCtrl* m_owner;
};

// ---------------------------------------------------------
// ibDataViewCtrl
// ---------------------------------------------------------

class FRONTEND_API ibDataViewCtrl
	: 
	public wxCompositeWindow<ibDataViewCtrlBase>,
	public wxScrollHelper

{
	friend class ibDataViewMainWindow;
	friend class ibDataViewHeaderWindowBase;
	friend class ibDataViewHeaderWindow;
	friend class ibDataViewHeaderWindowMSW;
	friend class ibDataViewColumn;

	friend class ibDataViewMaxWidthCalculator;

#if wxUSE_ACCESSIBILITY
	friend class ibDataViewCtrlAccessible;
#endif // wxUSE_ACCESSIBILITY

	typedef wxCompositeWindow<ibDataViewCtrlBase> BaseType;

public:
	ibDataViewCtrl() : wxScrollHelper(this)
	{
		Init();
	}

	ibDataViewCtrl(wxWindow* parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxASCII_STR(ibDataViewCtrlNameStr))
		: wxScrollHelper(this)
	{
		Create(parent, id, pos, size, style, validator, name);
	}

	virtual ~ibDataViewCtrl();

	void Init();

	bool Create(wxWindow* parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxASCII_STR(ibDataViewCtrlNameStr));

	virtual bool AssociateModel(ibDataViewModel* model) wxOVERRIDE;

	virtual bool AppendColumn(ibDataViewColumn* col) wxOVERRIDE;
	virtual bool PrependColumn(ibDataViewColumn* col) wxOVERRIDE;
	virtual bool InsertColumn(unsigned int pos, ibDataViewColumn* col) wxOVERRIDE;

	virtual void DoSetExpanderColumn() wxOVERRIDE;
	virtual void DoSetIndent() wxOVERRIDE;

	virtual unsigned int GetColumnCount() const wxOVERRIDE;
	virtual ibDataViewColumn* GetColumn(unsigned int pos) const wxOVERRIDE;
	virtual bool DeleteColumn(ibDataViewColumn* column) wxOVERRIDE;
	virtual bool ClearColumns() wxOVERRIDE;
	virtual int GetColumnPosition(const ibDataViewColumn* column) const wxOVERRIDE;

	virtual ibDataViewColumn* GetSortingColumn() const wxOVERRIDE;
	virtual wxVector<ibDataViewColumn*> GetSortingColumns() const wxOVERRIDE;

	virtual ibDataViewItem GetTopItem() const wxOVERRIDE;
	virtual int GetCountPerPage() const wxOVERRIDE;

	virtual int GetSelectedItemsCount() const wxOVERRIDE;
	virtual int GetSelections(ibDataViewItemArray& sel) const wxOVERRIDE;
	virtual void SetSelections(const ibDataViewItemArray& sel) wxOVERRIDE;
	virtual void Select(const ibDataViewItem& item) wxOVERRIDE;
	virtual void Unselect(const ibDataViewItem& item) wxOVERRIDE;
	virtual bool IsSelected(const ibDataViewItem& item) const wxOVERRIDE;

	virtual void SelectAll() wxOVERRIDE;
	virtual void UnselectAll() wxOVERRIDE;

	virtual void SetSelectionMode(ibDataViewSelectionMode selmode) wxOVERRIDE;
	virtual ibDataViewSelectionMode GetSelectionMode() const wxOVERRIDE;

	virtual void SetViewMode(ibDataViewViewMode viewMode) wxOVERRIDE;
	virtual ibDataViewViewMode GetViewMode() const wxOVERRIDE;

	virtual void SetTopParent(const ibDataViewItem& item) wxOVERRIDE;

	virtual void EnsureVisible(const ibDataViewItem& item,
		const ibDataViewColumn* column = NULL) wxOVERRIDE;
	virtual void HitTest(const wxPoint& point, ibDataViewItem& item,
		ibDataViewColumn*& column) const wxOVERRIDE;
	virtual wxRect GetItemRect(const ibDataViewItem& item,
		const ibDataViewColumn* column = NULL) const wxOVERRIDE;

	virtual bool SetRowHeight(int rowHeight) wxOVERRIDE;
	virtual int GetRowHeight() const wxOVERRIDE;
	virtual int GetDefaultRowHeight() const wxOVERRIDE;

	virtual void Collapse(const ibDataViewItem& item) wxOVERRIDE;
	virtual bool IsExpanded(const ibDataViewItem& item) const wxOVERRIDE;

	virtual void SetFocus() wxOVERRIDE;

	virtual bool SetFont(const wxFont& font) wxOVERRIDE;
	virtual bool SetForegroundColour(const wxColour& colour) wxOVERRIDE;
	virtual bool SetBackgroundColour(const wxColour& colour) wxOVERRIDE;

#if wxUSE_ACCESSIBILITY
	virtual bool Show(bool show = true) wxOVERRIDE;
	virtual void SetName(const wxString& name) wxOVERRIDE;
	virtual bool Reparent(wxWindowBase* newParent) wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY
	virtual bool Enable(bool enable = true) wxOVERRIDE;

	virtual void ScrollWindow(int dx, int dy, const wxRect* rect) wxOVERRIDE;
	virtual void Refresh(bool eraseb = true, const wxRect* rect = NULL) wxOVERRIDE;

	// this function is called when the current cell highlight must be redrawn
	// and may be overridden by the user
	virtual void DrawTableContent(wxDC& dc, ibDataViewMainWindow* tableWindow);

	// Open the List-Settings window (Filter / Sort / Group) for the model. Default
	// returns false; ibTableViewCtrl overrides it to open ibDialogListSettings.
	virtual bool ShowListSettings(class ibValueModel* model) { return false; }
	virtual bool ShowViewMode() { return false; }

	virtual bool AllowMultiColumnSort(bool allow) wxOVERRIDE;
	virtual bool IsMultiColumnSortAllowed() const wxOVERRIDE { return m_allowMultiColumnSort; }
	virtual void ToggleSortByColumn(int column) wxOVERRIDE;

#if wxUSE_DRAG_AND_DROP
	virtual bool EnableDragSource(const wxDataFormat& format) wxOVERRIDE;
	virtual bool DoEnableDropTarget(const wxVector<wxDataFormat>& formats) wxOVERRIDE;
#endif // wxUSE_DRAG_AND_DROP

	virtual wxBorder GetDefaultBorder() const wxOVERRIDE;

	virtual void EditItem(const ibDataViewItem& item, const ibDataViewColumn* column) wxOVERRIDE;

	virtual bool SetHeaderAttr(const wxItemAttr& attr) wxOVERRIDE;

	virtual bool SetAlternateRowColour(const wxColour& colour) wxOVERRIDE;

	// This method is specific to generic ibDataViewCtrl implementation and
	// should not be used in portable code.
	wxColour GetAlternateRowColour() const { return m_alternateRowColour; }

	// The returned pointer is null if the control has wxDV_NO_HEADER style.
	//
	// This method is only available in the generic versions.
	ibHeaderGenericCtrl* GenericGetHeader() const;

	bool FreezeTo(int row, int col);
	bool IsFrozen() const;

	void SetHeaderHeight(int point);
	int GetHeaderHeight() const;
	void SetFooterHeight(int point);
	int GetFooterHeight() const;

	void ShowHeaderWindow(bool e);
	void ShowFooterWindow(bool e);

	ibDataViewMainWindow* CellToDataViewWindow(const ibDataViewItem& item, const ibDataViewColumn* column) const;

	void    GetDataViewWindowOffset(const ibDataViewMainWindow* tableWindow, int& x, int& y) const;
	wxPoint GetDataViewWindowOffset(const ibDataViewMainWindow* tableWindow) const;

	ibDataViewMainWindow* DevicePosToDataViewWindow(wxPoint pos) const;
	ibDataViewMainWindow* DevicePosToDataViewWindow(int x, int y) const;

	void    CalcDataViewWindowUnscrolledPosition(int x, int y, int* xx, int* yy,
		const ibDataViewMainWindow* tableWindow) const;
	wxPoint CalcDataViewWindowUnscrolledPosition(const wxPoint& pt,
		const ibDataViewMainWindow* tableWindow) const;

	void    CalcDataViewWindowScrolledPosition(int x, int y, int* xx, int* yy,
		const ibDataViewMainWindow* tableWindow) const;
	wxPoint CalcDataViewWindowScrolledPosition(const wxPoint& pt,
		const ibDataViewMainWindow* tableWindow) const;

protected:

	void CalcWindowSizes();

	void ProcessTableCharEvent(
		wxKeyEvent& event,
		ibDataViewMainWindow* tableWin);

	void ProcessTableCharHookEvent(
		wxKeyEvent& event,
		ibDataViewMainWindow* tableWin);

	void ProcessTableCharLeftKeyEvent(wxKeyEvent& event);
	void ProcessTableCharRightKeyEvent(wxKeyEvent& event);

	void ProcessTableMouseEvent(
		wxMouseEvent& event,
		ibDataViewMainWindow* tableWin);

	void EnsureVisibleRowCol(int row, int column);

	// Notice that row here may be invalid (i.e. >= GetRowCount()), this is not
	// an error and this function simply returns an invalid item in this case.
	ibDataViewItem GetItemByRow(unsigned int row) const;
	int GetRowByItem(const ibDataViewItem& item,
		WalkFlags flags = Walk_All) const;

	// Mark the column as being used or not for sorting.
	void UseColumnForSorting(int idx);
	void DontUseColumnForSorting(int idx);

	// Return true if the given column is sorted
	bool IsColumnSorted(int idx) const;

	// Reset all columns currently used for sorting. PUBLIC: the OES tablebox (ibValueModelTableBox::OnColumnClick)
	// clears the header arrows before setting the clicked column's, committing the sort straight on the composer.
public:
	void ResetAllSortColumns();
protected:

	virtual void DoEnableSystemTheme(bool enable, wxWindow* window) wxOVERRIDE;

	void OnDPIChanged(wxDPIChangedEvent& event);

	// Scroll-driven prefetch: control owns the decision (margin
	// trigger + batch size); model just gives a portion on request.
	// Backend stays a stateless fetch service.
	void OnScrollEvent(wxScrollWinEvent& event);
	// OnIdleEvent removed — body collapsed into OnInternalIdle for a
	// single idle entry point.  External callers register via SetIdleHook.

	// Paged-mode tree manipulation — called from the prefetch flow.
	// PagedBootstrap kicks the very first GetFirstFetch once layout
	// has settled.  PagedFetchForward / Backward extend the loaded
	// window when scroll approaches an edge and the side is not
	// exhausted.
	void PagedBootstrap();

	// (SyncColumnArrowsFromModel removed — header arrows are set on the front per column: the tablebox sets the
	//  clicked column's arrow in OnColumnClick and re-reads the composer's active sort on column rebuild.)
public:
	// Hierarchical drill context (control-owned, model-stateless).
	// Empty in List / Tree mode; non-empty when the user has drilled
	// into a folder via SetTopParent.
	ibDataViewItem GetDrillHierarchyItem() const {
		// Front of the chain (the deepest folder we're currently inside);
		// empty in List / Tree mode.
		return m_topParentChain.IsEmpty() ? ibDataViewItem() : m_topParentChain[0];
	}

	// Parent item passed to the model on fetch dispatch.  Three cases:
	//   * Hierarchical drill — front of m_topParentChain (the folder
	//     the user is currently inside).
	//   * List view of a hierarchical model — empty item: a List view
	//     never groups, so the composer ignores the parent and walks
	//     the whole table flat.
	//   * Otherwise (Tree, or non-hierarchical model) — empty item:
	//     model returns top-level rows.
	ibDataViewItem GetEffectiveFetchParent() const;

	// Internal entry: fires from Cleared() (BeforeReset/AfterReset notifier
	// path) and from SetTopParent on drill changes.  preferSelection is
	// stamped into m_pagedRestoreSelection so PagedBootstrap can match the
	// freshly-fetched row via ibDataViewObject::IsEqualTo and restore
	// focus.  Public-but-internal: do NOT call with a non-default
	// preferSelection from outside the ctrl until the post-Save focus path
	// teardown AV (latent in ibDocChildFrameAny m_childView/m_childDocument)
	// is closed.
	void SchedulePagedRefresh(const ibDataViewItem& preferSelection = ibDataViewItem());

	// Stamp m_pagedRestoreSelection (and m_pagedRestoreFocus) without
	// scheduling a new refresh.  Used by external selection coordinators
	// (TableBox.ApplyCurrentLine) when a restore-target is resolved
	// AFTER PagedRefresh ran but BEFORE PagedBootstrap fires — bootstrap
	// uses the value via ibDataViewObject::IsEqualTo to match against
	// the freshly-fetched batch.  Going through SchedulePagedRefresh
	// instead would start another wipe cycle which is visible flicker.
	void SetPagedRestoreSelection(const ibDataViewItem& item);

	// Idle hook — fires from OnInternalIdle (every idle pass, runs
	// always, not gated on wx idle-throttle) AFTER the control's own
	// col-widths / dirty pass.  TableBox registers its seed chain here
	// instead of competing on wxEVT_IDLE chain order: when the hook
	// fires, control's bootstrap (which runs from OnIdleEvent earlier
	// in the same idle cycle) has already settled buffer state.
	using IdleHook = std::function<void()>;
	void SetIdleHook(IdleHook hook) { m_idleHook = std::move(hook); }
private:
	// Direction discriminator for the shared paged-fetch dispatch helper.
	enum class PagedFetchDir : uint8_t { Forward, Backward };
	// Common submit-side: pre-checks + counter inc + SubmitFetchAsync +
	// worker-side model fetch + UI-side CallAfter + gen-token guard +
	// dispatch to the matching result handler.  Forward / Backward
	// differ only in which model method fires, which counter and anchor
	// they use, and which result handler runs.
	void DispatchPagedFetch(PagedFetchDir dir, int batch);
	void PagedFetchForward(int batch)  { DispatchPagedFetch(PagedFetchDir::Forward,  batch); }
	void PagedFetchBackward(int batch) { DispatchPagedFetch(PagedFetchDir::Backward, batch); }
	// UI-thread result processors invoked by CallAfter after the
	// async DB roundtrip completes on the worker.
	void OnPagedFetchForwardResult(ibDataViewItemArray& items, unsigned int n, int batch);
	void OnPagedFetchBackwardResult(ibDataViewItemArray& items, unsigned int n, int batch);
	// Wipe loaded window and queue a fresh GetFirstFetch on next idle.
	// Used as the universal "model said something changed" handler
	// (ItemInserted / ItemDeleted / DoItemChanged in paged mode).
	void PagedRefresh(const ibDataViewItem& preferSelection = ibDataViewItem());
	// 3-state thumb position derived from has-more flags + viewport
	// position inside the loaded buffer.  Computed on demand so no
	// state-machine maintenance is needed.
	enum class ibPagedThumb { Top, Middle, Bottom };
	// Lying scrollbar: thumb has 3 fixed positions driven by has-more
	// flags only.  Applied at every wxScrollHelper update path so its
	// real-virtual-size values cannot leak through to the widget.
	void UpdatePagedScrollbar();
	bool IsPagedScrollbarMode() const;
	ibPagedThumb DerivePagedThumb() const;
	void GetPagedPinRange(long& minRow, long& maxRow) const;
	// Find the deepest crumb under m_root in Hierarchical drill mode.
	// PagedBootstrap builds the tree as m_root → crumb_0 → crumb_1 →
	// ... → [data rows under deepest crumb], so new fetched rows must
	// be inserted under the deepest crumb (not under m_root) to keep
	// the breadcrumb shape consistent.  In List / Tree mode (chain
	// empty) this just returns m_root.
	ibDataViewTreeNode* GetPagedInsertParent() const;

	// wxScrollHelper / wxWindow virtual overrides for paged mode.
	virtual void AdjustScrollbars() wxOVERRIDE;
	virtual void SetScrollPos(int orient, int pos, bool refresh = true) wxOVERRIDE;
	virtual void SetScrollbar(int orient, int pos, int thumbSize, int range,
		bool refresh = true) wxOVERRIDE;

public:     // utility functions not part of the API

	// returns the "best" width for the idx-th column
	unsigned int GetBestColumnWidth(int idx) const;

	// called by header window after reorder
	void ColumnMoved(ibDataViewColumn* col, unsigned int new_pos);

	// update the display after a change to an individual column
	void OnColumnChange(unsigned int idx);

	// update after the column width changes due to interactive resizing
	void OnColumnResized();

	// update after the column width changes because of e.g. title or bitmap
	// change, invalidates the column best width and calls OnColumnChange()
	void OnColumnWidthChange(unsigned int idx);

	// update after a change to the number of columns
	void OnColumnsCountChanged();

	wxWindow* GetMainWindow() { return (wxWindow*)m_tableAreaWin; }

	// return the index of the given column in m_cols
	int GetColumnIndex(const ibDataViewColumn* column) const;

	// Return the index of the column having the given model index.
	int GetModelColumnIndex(unsigned int model_column) const;

	// return the column displayed at the given position in the control
	ibDataViewColumn* GetColumnAt(unsigned int pos) const;

	virtual ibDataViewColumn* GetCurrentColumn() const wxOVERRIDE;

	virtual void OnInternalIdle() wxOVERRIDE;

#if wxUSE_ACCESSIBILITY
	virtual wxAccessible* CreateAccessible() wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY

private:

	// Implement pure virtual method inherited from wxCompositeWindow.
	virtual wxWindowList GetCompositeWindowParts() const wxOVERRIDE;

	virtual ibDataViewItem DoGetCurrentItem() const wxOVERRIDE;
	virtual void DoSetCurrentItem(const ibDataViewItem& item) wxOVERRIDE;

	virtual void DoExpand(const ibDataViewItem& item, bool expandChildren) wxOVERRIDE;

	void InvalidateColBestWidths();
	void InvalidateColBestWidth(int idx);
	void UpdateColWidths();

	void DoClearColumns();

public:

	// A DESIGN-TIME control can carry columns with NO associated model yet (e.g. a just-dropped tablebox
	// whose columns are added before any data model is bound). Guard the null model so a column-width
	// auto-fit (header separator double-click → GetBestColumnWidth) can't NULL-deref here.
	bool IsList() const { const ibDataViewModel* model = GetModel(); return model != NULL && model->IsListModel(); }
	bool IsVirtualList() const { return m_root == NULL; }

	// notifications from ibDataViewModel
	bool ItemInserted(const ibDataViewItem& parent, const ibDataViewItem& item);
	bool ItemAppended(const ibDataViewItem& parent, const ibDataViewItem& item);
	bool ItemDeleted(const ibDataViewItem& parent, const ibDataViewItem& item);
	bool ItemChanged(const ibDataViewItem& item)
	{
		return DoItemChanged(item, wxNOT_FOUND);
	}

	bool ValueChanged(const ibDataViewItem& item, unsigned int model_column);
	bool Cleared();
	void Resort();

	// Tell the next async PagedRefresh to skip capturing top / selection
	// as restore anchors — used by header sort-column click, where the
	// saved cursor key is built for the OLD ordering and would steer
	// the new SQL into the wrong half of the table.  Cleared by
	// PagedRefresh once consumed.
	void SetPagedSkipRestoreCapture() { m_pagedSkipRestoreCapture = true; }

	void ClearRowHeightCache();

	SortOrder GetSortOrder() const
	{
		// Paged models own their order on the backend (the SQL
		// ORDER BY drives the rows we receive); telling wxDVC to
		// also sort would make ItemInserted / InsertChild route through
		// binary-search insertion and scatter freshly fetched rows
		// across the tree.  Force None so the tree just appends in
		// fetch order.
		if (GetModel() != nullptr && GetModel()->IsPagedModel())
			return SortOrder();

		ibDataViewColumn* const col = GetSortingColumn();
		if (col)
		{
			return SortOrder(col->GetModelColumn(),
				col->IsSortOrderAscending());
		}
		else
		{
			if (GetModel()->HasDefaultCompare())
				return SortOrder(SortColumn_Default);
			else
				return SortOrder();
		}
	}

#if wxUSE_DRAG_AND_DROP
	wxBitmap CreateItemBitmap(unsigned int row, int& indent);

#endif // wxUSE_DRAG_AND_DROP

	// Go to the specified row, i.e. make it current and change or extend the
	// selection extended depending on the modifier keys flags in the keyboard
	// state.
	//
	// The row must be valid.
	void GoToRow(const wxKeyboardState& state, unsigned int row);

	// Go to the item at position delta rows away (delta may be positive or
	// negative) from the current row.
	//
	// If adding delta would result in an invalid item, it's clamped to the
	// valid items range.
	void GoToRelativeRow(const wxKeyboardState& kbdState, int delta);

	void OnRenameTimer();

	void UpdateDisplay();
	void RecalculateDisplay();

	void ScrollTo(int rows, int column);

	unsigned GetCurrentRow() const { return m_currentRow; }
	bool HasCurrentRow() { return m_currentRow != (unsigned int)-1; }
	void ChangeCurrentRow(unsigned int row);
	bool TryAdvanceCurrentColumn(ibDataViewTreeNode* node, wxKeyEvent& event, bool forward);

	void ClearCurrentColumn() { m_currentCol = NULL; }

	bool IsSingleSel() const { return !HasFlag(wxDV_MULTIPLE); }
	bool IsEmpty() { return GetRowCount() == 0; }

	int GetEndOfLastCol() const;

	// Returns the position where the given column starts.
	// The column must be valid.
	int GetColumnStart(int column) const;

	unsigned int GetFirstVisibleRow() const;

	// I change this method to un const because in the tree view,
	// the displaying number of the tree are changing along with the
	// expanding/collapsing of the tree nodes
	unsigned int GetLastVisibleRow();
	unsigned int GetLastFullyVisibleRow();
	unsigned int GetRowCount() const;

	const wxSelectionStore& GetSelections() const { return m_selection; }
	void ClearSelection();
	void Select(const wxArrayInt& aSelections);

	// If a valid row is specified and it was previously selected, it is left
	// selected and the function returns false. Otherwise, i.e. if there is
	// really no selection left in the control, it returns true.
	bool UnselectAllRows(unsigned int except = (unsigned int)-1);

	void SelectRow(unsigned int row, bool on);
	void SelectRows(unsigned int from, unsigned int to);
	void ReverseRowSelection(unsigned int row);
	bool IsRowSelected(unsigned int row) const;
	void SendSelectionChangedEvent(const ibDataViewItem& item);

	void RefreshRow(unsigned int row) { RefreshRows(row, row); }
	void RefreshRows(unsigned int from, unsigned int to);
	void RefreshRowsAfter(unsigned int firstRow);

	// returns the colour to be used for drawing the rules
	wxColour GetRuleColour() const
	{
		return wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT);
	}

	wxRect GetLinesRect(unsigned int rowFrom, unsigned int rowTo) const;

	int GetLineStart(unsigned int row) const;  // row * m_lineHeight in fixed mode
	int GetLineHeight(unsigned int row) const; // m_lineHeight in fixed mode
	int GetLineAt(unsigned int y) const;       // y / m_lineHeight in fixed mode
	int QueryAndCacheLineHeight(unsigned int row, ibDataViewItem item) const;

	ibDataViewTreeNode* GetTreeNodeByRow(unsigned int row) const;

	// Methods for building the mapping tree
	void BuildTree(ibDataViewModel* model);
	void DestroyTree();
	void HitTest(const wxPoint& point, ibDataViewItem& item, ibDataViewColumn*& column);
	
	wxRect GetItemRect(const ibDataViewItem& item, const ibDataViewColumn* column);

	void ExpandRow(unsigned int row, bool expandChildren = false);
	void CollapseRow(unsigned int row);
	bool IsExpandedRow(unsigned int row) const;
	bool HasChildrenRow(unsigned int row) const;

#if wxUSE_DRAG_AND_DROP
	enum DropHint
	{
		DropHint_None = 0,
		DropHint_Inside,
		DropHint_Below,
		DropHint_Above
	};

	struct DropItemInfo
	{
		unsigned int        m_row;
		DropHint            m_hint;

		ibDataViewItem      m_item;
		int                 m_proposedDropIndex;
		int                 m_indentLevel;

		DropItemInfo()
			: m_row(static_cast<unsigned int>(-1))
			, m_hint(DropHint_None)
			, m_item()
			, m_proposedDropIndex(-1)
			, m_indentLevel(-1)
		{
		}
	};

	void RefreshDropHint();
	void RemoveDropHint();
	DropItemInfo GetDropItemInfo(const wxCoord x, const wxCoord y);
	wxDragResult OnDragOver(wxDataFormat format, wxCoord x, wxCoord y, wxDragResult def);
	bool OnDrop(wxDataFormat format, wxCoord x, wxCoord y);
	wxDragResult OnData(wxDataFormat format, wxCoord x, wxCoord y, wxDragResult def);
	void OnLeave();
#endif // wxUSE_DRAG_AND_DROP

	// Adjust last column to window size
	void UpdateColumnSizes();

	// Called by ibDataViewCtrl and our own OnRenameTimer() to start edit the
	// specified item in the given column.
	void StartEditing(const ibDataViewItem& item, const ibDataViewColumn* col);
	void FinishEditing();
	bool HasEditableColumn(const ibDataViewItem& item) const
	{
		return FindColumnForEditing(item, wxDATAVIEW_CELL_EDITABLE) != NULL;
	}

private:

	// depending on the values of m_numFrozenRows and m_numFrozenCols, it will
	// create and initialize or delete the frozen windows
	void InitializeFrozenWindows();

	void InvalidateCount() { m_countRows = -1; }
	void UpdateCount(int count)
	{
		m_countRows = count;
		m_selection.SetItemCount(count);
	}

	int RecalculateCount() const;

	// Return false only if the event was vetoed by its handler.
	bool SendExpanderEvent(wxEventType type, const ibDataViewItem& item);

	// Shared body for ItemInserted / ItemAppended — fires `eventType`, returns
	// false if the handler vetoes; otherwise mutates the tree to materialise
	// the new row.
	bool DoItemInserted(const ibDataViewItem& parent, const ibDataViewItem& item, wxEventType eventType);

	// Walk visible nodes in the tree, counting rows.  Returns the visible
	// row index of `target` or wxNOT_FOUND if not present / not in an open
	// branch.  Uses ibDataViewItem::operator== (value-eq via IsEqualTo)
	// because fresh post-fetch nodes have new pointers that pointer-id
	// never matches against pre-refresh saved items.
	int FindVisibleRowInTree(const ibDataViewItem& target) const;

	struct FindNodeResult
	{
		ibDataViewTreeNode* m_node;
		bool                 m_subtreeRealized;
	};

	FindNodeResult FindNode(const ibDataViewItem& item);

	ibDataViewColumn* FindColumnForEditing(const ibDataViewItem& item, ibDataViewCellMode mode) const;

	bool IsCellEditableInMode(const ibDataViewItem& item, const ibDataViewColumn* col, ibDataViewCellMode mode) const;

	void DrawCellBackground(ibDataViewRenderer* cell, wxDC& dc, const wxRect& rect, ibDataViewTreeNodeViewMode mode);

	// Common part of {Item,Value}Changed(): if view_column is wxNOT_FOUND,
	// assumes that all columns were modified, otherwise just this one.
	bool DoItemChanged(const ibDataViewItem& item, int view_column);

	// Return whether the item has at most one column with a value.
	bool IsItemSingleValued(const ibDataViewItem& item) const
	{
		bool hadColumnWithValue = false;
		const unsigned int cols = GetColumnCount();
		const ibDataViewModel* const model = GetModel();
		for (unsigned int i = 0; i < cols; i++)
		{
			if (model->HasValue(item, i))
			{
				if (hadColumnWithValue)
					return false;
				hadColumnWithValue = true;
			}
		}

		return true;
	}

	// Find the first column with a value in it.
	ibDataViewColumn* FindFirstColumnWithValue(const ibDataViewItem& item) const
	{
		const unsigned int cols = GetColumnCount();
		const ibDataViewModel* const model = GetModel();
		for (unsigned int i = 0; i < cols; i++)
		{
			if (model->HasValue(item, i))
				return GetColumnAt(i);
		}

		return NULL;
	}

	// Helper of public Expand(), must be called with a valid node.
	void DoExpand(ibDataViewTreeNode* node, unsigned int row, bool expandChildren);

private:

	int                         m_lineHeight;
	bool                        m_dirty;

	ibDataViewColumn* m_currentCol;
	unsigned int                m_currentRow;
	wxSelectionStore            m_selection;
	ibDataViewSelectionMode	m_selectionMode;

	ibDataViewViewMode		m_viewMode;

	ibDataViewRenameTimer* m_renameTimer;
	bool                        m_lastOnSame;

	bool                        m_hasFocus;
	bool                        m_useCellFocus;
	bool                        m_currentColSetByKeyboard;

	class HeightCache* m_rowHeightCache;

#if wxUSE_DRAG_AND_DROP
	int                         m_dragCount;
	wxPoint                     m_dragStart;

	bool                        m_dragEnabled;
	wxDataFormat                m_dragFormat;

	DropItemInfo                m_dropItemInfo;
#endif // wxUSE_DRAG_AND_DROP

	// for double click logic
	unsigned int m_lineLastClicked,
		m_lineBeforeLastClicked,
		m_lineSelectSingleOnUp;

	// the pen used to draw horiz/vertical rules
	wxPen m_penRule;

	// This is the tree structure of the model
	ibDataViewTreeNode* m_root;

	int m_countRows;

	// Number of frozen rows/columns in the beginning of the grid, 0 if none.
	int m_countFrozenRows;
	int m_countFrozenCols;

	int m_countFrozenHierarchicalRows;

	// This is the tree node under the cursor
	ibDataViewTreeNode* m_underMouse;

	// The control used for editing or NULL.
	wxWeakRef<wxWindow> m_editorCtrl;

	// Id m_editorCtrl is non-NULL, pointer to the associated renderer.
	ibDataViewRenderer* m_editorRenderer;

	wxVector<ibDataViewColumn*> m_cols;
	// cached column best widths information, values are for
	// respective columns from m_cols and the arrays have same size
	struct CachedColWidthInfo
	{
		CachedColWidthInfo() : width(0), dirty(true) {}
		int width;  // cached width or 0 if not computed
		bool dirty; // column was invalidated, header needs updating
	};
	wxVector<CachedColWidthInfo> m_colsBestWidths;
	// This indicates that at least one entry in m_colsBestWidths has 'dirty'
	// flag set. It's cheaper to check one flag in OnInternalIdle() than to
	// iterate over m_colsBestWidths to check if anything needs to be done.
	bool                      m_colsDirty;

	ibDataViewModelNotifier* m_notifier;

	ibDataViewHeaderWindow* m_headerAreaWin;
	ibDataViewMainWindow* m_tableAreaWin;
	ibDataViewMainWindow* m_tableFrozenRowAreaWin;
	ibDataViewMainWindow* m_tableFrozenColAreaWin;
	ibDataViewMainWindow* m_tableFrozenCornerAreaWin;
	ibDataViewFooterWindow* m_footerAreaWin;

	// user defined color to draw row lines, may be invalid
	wxColour m_alternateRowColour;

	// columns indices used for sorting, empty if nothing is sorted
	wxVector<int> m_sortingColumnIdxs;

	// if true, allow sorting by more than one column
	bool m_allowMultiColumnSort;

	// Paged-mode prefetch state (constant-size buffer + tree-update flow).
	// The control owns the buffer; the model just answers GetFirstFetch
	// / GetNextFetch / GetPrevFetch on request.  The loaded children
	// list is held at a constant target size (viewport + 2*kBufferSlack);
	// scroll past an edge fetches the next batch and trims the far side
	// to keep the buffer at target.
	bool m_pagedNeedsBootstrap = false;  // first fetch pending after AssociateModel
	// Refresh-anti-flicker: PagedRefresh's wipe destroys m_root
	// synchronously, then PagedBootstrap fires later from OnIdle
	// and re-fills.  Without freeze the user sees the empty tree
	// for one paint cycle.  Set in PagedRefresh, cleared by the
	// matching Thaw inside OnIdleEvent after bootstrap completes.
	bool m_pagedFrozenForBootstrap = false;
	// External idle callback — see SetIdleHook (public).  Owned by the
	// caller; cleared on AssociateModel(nullptr) is left to caller as
	// the registration is per-form, not per-model.
	IdleHook m_idleHook;
	bool m_pagedHasMoreFwd     = false;  // last forward fetch reported more rows behind it
	bool m_pagedHasMoreBwd     = false;  // ditto backward
	// Per-direction in-flight fetch counters.  Forward and backward
	// guards are independent so a scroll burst that crosses both edges
	// (rare) can dispatch to each side without serialising.
	int  m_pagedFetchingFwd    = 0;
	int  m_pagedFetchingBwd    = 0;
	// Generation token bumped on every full reset (PagedRefresh /
	// Cleared / AssociateModel).  Async fetch results captured at
	// submit time carry the token they were dispatched under and are
	// discarded if the live token has moved on while they were in
	// flight (e.g. user changed sort while a fetch was running).
	uint64_t m_pagedFetchGen   = 0;
	// Edge anchors for the next GetNextFetch / GetPrevFetch.  Tracked
	// independently of m_root.GetChildNodes() because sort-aware
	// InsertChild may scatter newly-fetched rows across the tree, so
	// .back() / .front() can't reliably serve as the cursor.
	ibDataViewItem m_pagedFwdAnchor;
	ibDataViewItem m_pagedBwdAnchor;
	// Hierarchical drill-down context: full ancestor chain captured at
	// drill time, refcount-pinned so items survive the Cleared() /
	// DestroyTree that wipes the previous m_root.  Front element is
	// the current top (the folder the user is currently inside);
	// subsequent elements are its ancestors up to the dataset root.
	// Empty in List / Tree mode.  PagedBootstrap rebuilds breadcrumb
	// from this list — walking m_parent on a tree node can't work
	// because the pointer dangles after destroy.
	ibDataViewItemArray m_topParentChain;
	// PagedRefresh saves three things so the next GetFirstFetch can
	// re-position the window correctly:
	//   m_pagedRestoreAnchor    — top-of-view item.  Default fetch
	//                             cursor for plain Refresh / sort:
	//                             items[0] = saved top → viewport
	//                             stays at the same row.
	//   m_pagedRestoreSelection — EXPLICIT preferSelection from the
	//                             caller (post-Save new-row, view-mode
	//                             switch).  When set, overrides the
	//                             anchor as fetch cursor so the new
	//                             row lands in the result and gets
	//                             centred via PagedFetchBackward.
	//   m_pagedRestoreFocus     — focus-restoration target.  Always
	//                             scanned in items[] post-fetch to
	//                             re-apply currentRow + selection.
	//                             Falls back to the pre-refresh
	//                             m_currentRow item when no explicit
	//                             prefer was passed, so plain Refresh
	//                             still re-focuses the row the user
	//                             had selected — without hijacking
	//                             the fetch cursor.
	ibDataViewItem m_pagedRestoreAnchor;
	ibDataViewItem m_pagedRestoreSelection;
	ibDataViewItem m_pagedRestoreFocus;
	// Raw row index of the focus at refresh time.  RAM-backed paged
	// models (TabularSection, ibValueTable, register record sets)
	// preserve row order across refresh — we restore focus by index
	// directly there instead of running the IsEqualTo value-eq
	// focus-scan, which produces false-positives for default-valued
	// rows.  -1 = no focus to restore.
	long m_pagedRestoreFocusRow = -1;

	// Pre-refresh selection state — guards SelectItem in post-bootstrap
	// focus-restore.  When focus exists (m_currentRow != -1) but selection
	// was empty before refresh (e.g. user clicked into the empty area
	// below the rows, OnSetFocus set m_currentRow=0 without selecting),
	// post-bootstrap restore must NOT upgrade focus-only state to
	// focus+selected — that would magically highlight row 0 after a
	// refresh that the user didn't initiate by selecting anything.
	bool m_pagedRestoreFocusWasSelected = false;

	// Set by ProcessTableMouseEvent on empty-area click; consumed by
	// ibDataViewMainWindow::OnSetFocus to skip the default
	// "ChangeCurrentRow(0) on focus-in" — without this, clicking the
	// empty space below the rows gives the control focus → wx upstream
	// auto-set currentRow=0 → focus-rect (dotted "gripper") paints on
	// row 0 and the user thinks the click selected something.
	bool m_skipFocusRowOnNextSetFocus = false;

	// Debounce coalesce for paged-mode reset signals.  Backend
	// notifier hooks (ItemInserted / Deleted / Changed / ValueChanged /
	// Cleared) all collapse to "wipe the deque + GetFirstFetch" under
	// paged semantics, so a series of mutations from script (e.g.
	// LoadData appending N rows) would otherwise trigger N synchronous
	// resets.  Dispatched through model->SubmitFetchAsync (the model's
	// worker channel — per-session FIFO worker on web, inline on
	// desktop until a GUI worker pool is wired up), so the same
	// debounce path works on both hosts.  First hook in a series
	// schedules a one-shot lambda that runs PagedRefresh; subsequent
	// hooks just update the latest selection-hint and no-op while
	// the lambda is in flight.
	bool           m_pagedRefreshScheduled = false;
	ibDataViewItem m_pagedRefreshSelection;
	// Set by SetTopParent (drill in / out) so the upcoming async
	// PagedRefresh skips capturing the OLD-folder top / selection as
	// restore anchors — they don't belong in the NEW folder context.
	// Cleared once PagedRefresh consumes it.
	bool           m_pagedSkipRestoreCapture = false;

private:

	void OnSize(wxSizeEvent& event);
	virtual wxSize GetSizeAvailableForScrollTarget(const wxSize& size) wxOVERRIDE;

	// we need to return a special WM_GETDLGCODE value to process just the
	// arrows but let the other navigation characters through
#ifdef __WXMSW__
	virtual WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) wxOVERRIDE;
#endif // __WXMSW__

	WX_FORWARD_TO_SCROLL_HELPER()

private:
	wxDECLARE_DYNAMIC_CLASS(ibDataViewCtrl);
	wxDECLARE_NO_COPY_CLASS(ibDataViewCtrl);
	wxDECLARE_EVENT_TABLE();
};

#if wxUSE_ACCESSIBILITY
//-----------------------------------------------------------------------------
// ibDataViewCtrlAccessible
//-----------------------------------------------------------------------------

class FRONTEND_API ibDataViewCtrlAccessible : public wxWindowAccessible
{
public:
	ibDataViewCtrlAccessible(ibDataViewCtrl* win);
	virtual ~ibDataViewCtrlAccessible() {}

	virtual wxAccStatus HitTest(const wxPoint& pt, int* childId,
		wxAccessible** childObject) wxOVERRIDE;

	virtual wxAccStatus GetLocation(wxRect& rect, int elementId) wxOVERRIDE;

	virtual wxAccStatus Navigate(wxNavDir navDir, int fromId,
		int* toId, wxAccessible** toObject) wxOVERRIDE;

	virtual wxAccStatus GetName(int childId, wxString* name) wxOVERRIDE;

	virtual wxAccStatus GetChildCount(int* childCount) wxOVERRIDE;

	virtual wxAccStatus GetChild(int childId, wxAccessible** child) wxOVERRIDE;

	// wxWindowAccessible::GetParent() implementation is enough.
	// virtual wxAccStatus GetParent(wxAccessible** parent) wxOVERRIDE;

	virtual wxAccStatus DoDefaultAction(int childId) wxOVERRIDE;

	virtual wxAccStatus GetDefaultAction(int childId, wxString* actionName) wxOVERRIDE;

	virtual wxAccStatus GetDescription(int childId, wxString* description) wxOVERRIDE;

	virtual wxAccStatus GetHelpText(int childId, wxString* helpText) wxOVERRIDE;

	virtual wxAccStatus GetKeyboardShortcut(int childId, wxString* shortcut) wxOVERRIDE;

	virtual wxAccStatus GetRole(int childId, wxAccRole* role) wxOVERRIDE;

	virtual wxAccStatus GetState(int childId, long* state) wxOVERRIDE;

	virtual wxAccStatus GetValue(int childId, wxString* strValue) wxOVERRIDE;

	virtual wxAccStatus Select(int childId, wxAccSelectionFlags selectFlags) wxOVERRIDE;

	virtual wxAccStatus GetFocus(int* childId, wxAccessible** child) wxOVERRIDE;

	virtual wxAccStatus GetSelections(wxVariant* selections) wxOVERRIDE;
};
#endif // wxUSE_ACCESSIBILITY

#endif // __GENERICDATAVIEWCTRLH__
