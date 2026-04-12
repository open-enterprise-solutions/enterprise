/////////////////////////////////////////////////////////////////////////////
// Name:        wx/dataview.h
// Purpose:     ibDataViewCtrl base classes
// Author:      Robert Roebling
// Modified by: Bo Yang
// Created:     08.01.06
// Copyright:   (c) Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_DATAVIEW_H_BASE_
#define _WX_DATAVIEW_H_BASE_

#include <wx/defs.h>

#if wxUSE_DATAVIEWCTRL

#include <wx/textctrl.h>
#include <wx/headercol.h>
#include <wx/variant.h>
#include <wx/dnd.h>            // For wxDragResult declaration only.
#include <wx/dynarray.h>
#include <wx/icon.h>
#include <wx/itemid.h>
#include <wx/weakref.h>
#include <wx/vector.h>
#include <wx/dataobj.h>
#include <wx/withimages.h>
#include <wx/systhemectrl.h>
#include <wx/vector.h>

class WXDLLIMPEXP_FWD_CORE wxImageList;
class wxItemAttr;
class ibHeaderGenericCtrl;

#include "backend/tableView.h"
#include "headerctrlg.h"
#include "frontend/frontend.h"

// ----------------------------------------------------------------------------
// ibDataViewCtrl globals
// ----------------------------------------------------------------------------

class BACKEND_API ibDataViewModel;
class FRONTEND_API ibDataViewCtrl;
class FRONTEND_API ibDataViewColumn;
class FRONTEND_API ibDataViewRenderer;
class ibDataViewModelNotifier;
#if wxUSE_ACCESSIBILITY
class FRONTEND_API ibDataViewCtrlAccessible;
#endif // wxUSE_ACCESSIBILITY

extern const char FRONTEND_API ibDataViewCtrlNameStr[];

// ----------------------------------------------------------------------------
// ibDataViewListModel: a model of a list, i.e. flat data structure without any
//      branches/containers, used as base class by ibDataViewIndexListModel and
//      ibDataViewVirtualListModel
// ----------------------------------------------------------------------------

class FRONTEND_API ibDataViewListModel : public ibDataViewModel
{
public:
	// derived classes should override these methods instead of
	// {Get,Set}Value() and GetAttr() inherited from the base class

	virtual void GetValueByRow(wxVariant& variant,
		unsigned row, unsigned col) const = 0;

	virtual bool SetValueByRow(const wxVariant& variant,
		unsigned row, unsigned col) = 0;

	virtual bool
		GetAttrByRow(unsigned WXUNUSED(row), unsigned WXUNUSED(col),
			ibDataViewItemAttr& WXUNUSED(attr)) const
	{
		return false;
	}

	virtual bool IsEnabledByRow(unsigned int WXUNUSED(row),
		unsigned int WXUNUSED(col)) const
	{
		return true;
	}


	// helper methods provided by list models only
	virtual unsigned GetRow(const ibDataViewItem& item) const = 0;

	// returns the number of rows
	virtual unsigned int GetCount() const = 0;

	// implement some base class pure virtual directly
	virtual ibDataViewItem
		GetParent(const ibDataViewItem& WXUNUSED(item)) const wxOVERRIDE
	{
		// items never have valid parent in this model
		return ibDataViewItem();
	}

	virtual bool IsContainer(const ibDataViewItem& item) const wxOVERRIDE
	{
		// only the invisible (and invalid) root item has children
		return !item.IsOk();
	}

	// and implement some others by forwarding them to our own ones
	virtual void GetValue(wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) const wxOVERRIDE
	{
		GetValueByRow(variant, GetRow(item), col);
	}

	virtual bool SetValue(const wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) wxOVERRIDE
	{
		return SetValueByRow(variant, GetRow(item), col);
	}

	virtual bool GetAttr(const ibDataViewItem& item, unsigned int col,
		ibDataViewItemAttr& attr) const wxOVERRIDE
	{
		return GetAttrByRow(GetRow(item), col, attr);
	}

	virtual bool IsEnabled(const ibDataViewItem& item, unsigned int col) const wxOVERRIDE
	{
		return IsEnabledByRow(GetRow(item), col);
	}


	virtual bool IsListModel() const wxOVERRIDE { return true; }
};

// ---------------------------------------------------------
// ibDataViewIndexListModel
// ---------------------------------------------------------

class FRONTEND_API ibDataViewIndexListModel : public ibDataViewListModel
{
public:
	ibDataViewIndexListModel(unsigned int initial_size = 0);

	void RowPrepended();
	void RowInserted(unsigned int before);
	void RowAppended();
	void RowDeleted(unsigned int row);
	void RowsDeleted(const wxArrayInt& rows);
	void RowChanged(unsigned int row);
	void RowValueChanged(unsigned int row, unsigned int col);
	void Reset(unsigned int new_size);

	// convert to/from row/ibDataViewItem

	virtual unsigned GetRow(const ibDataViewItem& item) const wxOVERRIDE;
	ibDataViewItem GetItem(unsigned int row) const;

	// implement base methods
	virtual unsigned int GetChildren(const ibDataViewItem& item, ibDataViewItemArray& children) const wxOVERRIDE;

	unsigned int GetCount() const wxOVERRIDE { return (unsigned int)m_hash.GetCount(); }

private:
	ibDataViewItemArray m_hash;
	unsigned int m_nextFreeID;
	bool m_ordered;
};

// ---------------------------------------------------------
// ibDataViewVirtualListModel
// ---------------------------------------------------------

#ifdef __WXOSX__
// better than nothing
typedef ibDataViewIndexListModel ibDataViewVirtualListModel;
#else

class FRONTEND_API ibDataViewVirtualListModel : public ibDataViewListModel
{
public:
	ibDataViewVirtualListModel(unsigned int initial_size = 0);

	void RowPrepended();
	void RowInserted(unsigned int before);
	void RowAppended();
	void RowDeleted(unsigned int row);
	void RowsDeleted(const wxArrayInt& rows);
	void RowChanged(unsigned int row);
	void RowValueChanged(unsigned int row, unsigned int col);
	void Reset(unsigned int new_size);

	// convert to/from row/ibDataViewItem

	virtual unsigned GetRow(const ibDataViewItem& item) const wxOVERRIDE;
	ibDataViewItem GetItem(unsigned int row) const;

	// compare based on index

	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
		unsigned int column, bool ascending) const wxOVERRIDE;
	virtual bool HasDefaultCompare() const wxOVERRIDE;

	// implement base methods
	virtual unsigned int GetChildren(const ibDataViewItem& item, ibDataViewItemArray& children) const wxOVERRIDE;

	unsigned int GetCount() const wxOVERRIDE { return m_size; }

	// internal
	virtual bool IsVirtualListModel() const wxOVERRIDE { return true; }

private:
	unsigned int m_size;
};
#endif

// ----------------------------------------------------------------------------
// ibDataViewRenderer and related classes
// ----------------------------------------------------------------------------

#include "dvrenderers.h"

// ---------------------------------------------------------
// ibDataViewColumnBase
// ---------------------------------------------------------

// for compatibility only, do not use
enum ibDataViewColumnFlags
{
	wxDATAVIEW_COL_RESIZABLE = wxCOL_RESIZABLE,
	wxDATAVIEW_COL_SORTABLE = wxCOL_SORTABLE,
	wxDATAVIEW_COL_REORDERABLE = wxCOL_REORDERABLE,
	wxDATAVIEW_COL_HIDDEN = wxCOL_HIDDEN
};

class FRONTEND_API ibDataViewColumnBase : public wxSettableHeaderColumn
{
public:
	// ctor for the text columns: takes ownership of renderer
	ibDataViewColumnBase(ibDataViewRenderer* renderer,
		unsigned int model_column)
	{
		Init(renderer, model_column);
	}

	// ctor for the bitmap columns
	ibDataViewColumnBase(const wxBitmapBundle& bitmap,
		ibDataViewRenderer* renderer,
		unsigned int model_column)
		: m_bitmap(bitmap)
	{
		Init(renderer, model_column);
	}

	virtual ~ibDataViewColumnBase();

	// setters:
	virtual void SetOwner(ibDataViewCtrl* owner)
	{
		m_owner = owner;
	}

	// getters:
	unsigned int GetModelColumn() const { return static_cast<unsigned int>(m_model_column); }
	ibDataViewCtrl* GetOwner() const { return m_owner; }
	ibDataViewRenderer* GetRenderer() const { return m_renderer; }

	// implement some of base class pure virtuals (the rest is port-dependent
	// and done differently in generic and native versions)
	virtual void SetBitmap(const wxBitmapBundle& bitmap) wxOVERRIDE { m_bitmap = bitmap; }
	virtual wxBitmap GetBitmap() const wxOVERRIDE { return m_bitmap.GetBitmap(wxDefaultSize); }
	virtual wxBitmapBundle GetBitmapBundle() const wxOVERRIDE { return m_bitmap; }

	// Special accessor for use by wxWidgets only returning the width that was
	// explicitly set, either by the application, using SetWidth(), or by the
	// user, resizing the column interactively. It is usually the same as
	// GetWidth(), but can be different for the last column.
	virtual int WXGetSpecifiedWidth() const { return GetWidth(); }

protected:
	
	ibDataViewRenderer* m_renderer;
	int                      m_model_column;
	wxBitmapBundle           m_bitmap;
	ibDataViewCtrl* m_owner;

private:

	// common part of all ctors
	void Init(ibDataViewRenderer* renderer, unsigned int model_column);
};

// ---------------------------------------------------------
// ibDataViewCtrlBase
// ---------------------------------------------------------

#define wxDV_SINGLE                  0x0000     // for convenience
#define wxDV_MULTIPLE                0x0001     // can select multiple items

#define wxDV_NO_HEADER               0x0002     // column titles not visible
#define wxDV_HORIZ_RULES             0x0004     // light horizontal rules between rows
#define wxDV_VERT_RULES              0x0008     // light vertical rules between columns

#define wxDV_ROW_LINES               0x0010     // alternating colour in rows
#define wxDV_VARIABLE_LINE_HEIGHT    0x0020     // variable line height

#define wxDV_FOOTER					 0x0040     // column footer visible

// possible selection modes
enum ibDataViewSelectionMode
{
	ibDataViewSelectCell = 0,  // allow selecting anything
	ibDataViewSelectRow = 1,  // allow selecting only entire rows
};

enum ibDataViewViewMode 
{
	ibDataViewTree, 
	ibDataViewHierarchical, 
	ibDataViewList
};

class FRONTEND_API ibDataViewCtrlBase : public wxSystemThemedControl<wxControl>
{
public:

	ibDataViewCtrlBase();
	virtual ~ibDataViewCtrlBase();

	// model
	// -----

	virtual bool AssociateModel(ibDataViewModel* model);
	ibDataViewModel* GetModel();
	const ibDataViewModel* GetModel() const;

	// column management
	// -----------------

	ibDataViewColumn* PrependTextColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* PrependIconTextColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* PrependToggleColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_TOGGLE_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* PrependProgressColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* PrependDateColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_ACTIVATABLE, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* PrependBitmapColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* PrependTextColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* PrependIconTextColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* PrependToggleColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_TOGGLE_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* PrependProgressColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* PrependDateColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_ACTIVATABLE, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* PrependBitmapColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);

	ibDataViewColumn* AppendTextColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendIconTextColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendToggleColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_TOGGLE_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendProgressColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendDateColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_ACTIVATABLE, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendBitmapColumn(const wxString& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendTextColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendIconTextColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendToggleColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_TOGGLE_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendProgressColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendDateColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_ACTIVATABLE, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendBitmapColumn(const wxBitmap& label, unsigned int model_column,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);

	virtual bool PrependColumn(ibDataViewColumn* col);
	virtual bool InsertColumn(unsigned int pos, ibDataViewColumn* col);
	virtual bool AppendColumn(ibDataViewColumn* col);

	virtual unsigned int GetColumnCount() const = 0;
	virtual ibDataViewColumn* GetColumn(unsigned int pos) const = 0;
	virtual int GetColumnPosition(const ibDataViewColumn* column) const = 0;

	virtual bool DeleteColumn(ibDataViewColumn* column) = 0;
	virtual bool ClearColumns() = 0;

	void SetExpanderColumn(ibDataViewColumn* col)
	{
		m_expander_column = col; DoSetExpanderColumn();
	}
	ibDataViewColumn* GetExpanderColumn() const
	{
		return m_expander_column;
	}

	virtual ibDataViewColumn* GetSortingColumn() const = 0;
	virtual wxVector<ibDataViewColumn*> GetSortingColumns() const
	{
		wxVector<ibDataViewColumn*> columns;
		if (ibDataViewColumn* col = GetSortingColumn())
			columns.push_back(col);
		return columns;
	}

	// This must be overridden to return true if the control does allow sorting
	// by more than one column, which is not the case by default.
	virtual bool AllowMultiColumnSort(bool allow)
	{
		// We can still return true when disabling multi-column sort.
		return !allow;
	}

	// Return true if multi column sort is currently allowed.
	virtual bool IsMultiColumnSortAllowed() const { return false; }

	// This should also be overridden to actually use the specified column for
	// sorting if using multiple columns is supported.
	virtual void ToggleSortByColumn(int WXUNUSED(column)) {}


	// items management
	// ----------------

	void SetIndent(int indent)
	{
		m_indent = indent; DoSetIndent();
	}
	int GetIndent() const
	{
		return m_indent;
	}

	// Current item is the one used by the keyboard navigation, it is the same
	// as the (unique) selected item in single selection mode so these
	// functions are mostly useful for controls with wxDV_MULTIPLE style.
	ibDataViewItem GetCurrentItem() const;
	void SetCurrentItem(const ibDataViewItem& item);

	virtual ibDataViewItem GetTopItem() const { return ibDataViewItem(NULL); }
	virtual int GetCountPerPage() const { return wxNOT_FOUND; }

	// Currently focused column of the current item or NULL if no column has focus
	virtual ibDataViewColumn* GetCurrentColumn() const = 0;

	// Selection: both GetSelection() and GetSelections() can be used for the
	// controls both with and without wxDV_MULTIPLE style. For single selection
	// controls GetSelections() is not very useful however. And for multi
	// selection controls GetSelection() returns an invalid item if more than
	// one item is selected. Use GetSelectedItemsCount() or HasSelection() to
	// check if any items are selected at all.
	virtual int GetSelectedItemsCount() const = 0;

	bool HasSelection() const { return GetSelectedItemsCount() != 0; }
	ibDataViewItem GetSelection() const;
	virtual int GetSelections(ibDataViewItemArray& sel) const = 0;
	virtual void SetSelections(const ibDataViewItemArray& sel) = 0;
	virtual void Select(const ibDataViewItem& item) = 0;
	virtual void Unselect(const ibDataViewItem& item) = 0;
	virtual bool IsSelected(const ibDataViewItem& item) const = 0;

	virtual void SelectAll() = 0;
	virtual void UnselectAll() = 0;

	virtual void SetSelectionMode(ibDataViewSelectionMode selmode) = 0;
	virtual ibDataViewSelectionMode GetSelectionMode() const = 0;

	virtual void SetViewMode(ibDataViewViewMode viewMode) = 0;
	virtual ibDataViewViewMode GetViewMode() const = 0;

	virtual void SetTopParent(const ibDataViewItem& item) = 0;

	void Expand(const ibDataViewItem& item);
	void ExpandChildren(const ibDataViewItem& item);
	void ExpandAncestors(const ibDataViewItem& item);
	
	virtual void Collapse(const ibDataViewItem& item) = 0;
	virtual bool IsExpanded(const ibDataViewItem& item) const = 0;

	virtual void EnsureVisible(const ibDataViewItem& item,
		const ibDataViewColumn* column = NULL) = 0;
	virtual void HitTest(const wxPoint& point, ibDataViewItem& item, ibDataViewColumn*& column) const = 0;
	virtual wxRect GetItemRect(const ibDataViewItem& item, const ibDataViewColumn* column = NULL) const = 0;

	virtual bool SetRowHeight(int WXUNUSED(rowHeight)) { return false; }
	virtual int GetRowHeight() const { return 0; }
	virtual int GetDefaultRowHeight() const { return 0; }

	virtual void EditItem(const ibDataViewItem& item, const ibDataViewColumn* column) = 0;

	// Use EditItem() instead
	wxDEPRECATED(void StartEditor(const ibDataViewItem& item, unsigned int column));

#if wxUSE_DRAG_AND_DROP
	virtual bool EnableDragSource(const wxDataFormat& WXUNUSED(format))
	{
		return false;
	}

	bool EnableDropTargets(const wxVector<wxDataFormat>& formats)
	{
		return DoEnableDropTarget(formats);
	}

	bool EnableDropTarget(const wxDataFormat& format)
	{
		wxVector<wxDataFormat> formats;
		if (format.GetType() != wxDF_INVALID)
		{
			formats.push_back(format);
		}

		return DoEnableDropTarget(formats);
	}

#endif // wxUSE_DRAG_AND_DROP

	// define control visual attributes
	// --------------------------------

	// Header attributes: only implemented in the generic version currently.
	virtual bool SetHeaderAttr(const wxItemAttr& WXUNUSED(attr))
	{
		return false;
	}

	// Set the colour used for the "alternate" rows when wxDV_ROW_LINES is on.
	// Also only supported in the generic version, which returns true to
	// indicate it.
	virtual bool SetAlternateRowColour(const wxColour& WXUNUSED(colour))
	{
		return false;
	}

	virtual wxVisualAttributes GetDefaultAttributes() const wxOVERRIDE
	{
		return GetClassDefaultAttributes(GetWindowVariant());
	}

	static wxVisualAttributes
		GetClassDefaultAttributes(wxWindowVariant variant = wxWINDOW_VARIANT_NORMAL)
	{
		return wxControl::GetCompositeControlsDefaultAttributes(variant);
	}

protected:
	virtual void DoSetExpanderColumn() = 0;
	virtual void DoSetIndent() = 0;

#if wxUSE_DRAG_AND_DROP
	// Helper function which can be used by DoEnableDropTarget() implementations
	// in the derived classes: return a composite data object supporting the
	// given formats or null if the vector is empty.
	static wxDataObjectComposite*
		CreateDataObject(const wxVector<wxDataFormat>& formats);

	virtual bool DoEnableDropTarget(const wxVector<wxDataFormat>& WXUNUSED(formats))
	{
		return false;
	}
#endif // wxUSE_DRAG_AND_DROP

	// Just expand this item assuming it is already shown, i.e. its parent has
	// been already expanded using ExpandAncestors().
	//
	// If expandChildren is true, also expand all its children recursively.
	virtual void DoExpand(const ibDataViewItem& item, bool expandChildren) = 0;

private:
	// Implementation of the public Set/GetCurrentItem() methods which are only
	// called in multi selection case (for single selection controls their
	// implementation is trivial and is done in the base class itself).
	virtual ibDataViewItem DoGetCurrentItem() const = 0;
	virtual void DoSetCurrentItem(const ibDataViewItem& item) = 0;

	ibDataViewModel* m_model;
	ibDataViewColumn* m_expander_column;
	int m_indent;

protected:
	wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibDataViewCtrlBase);
};

// ----------------------------------------------------------------------------
// ibDataViewEvent - the event class for the ibDataViewCtrl notifications
// ----------------------------------------------------------------------------

class FRONTEND_API ibDataViewEvent : public wxNotifyEvent
{
public:
	// Default ctor, normally shouldn't be used and mostly exists only for
	// backwards compatibility.
	ibDataViewEvent()
		: wxNotifyEvent()
	{
		Init(NULL, NULL, ibDataViewItem());
	}

	// Constructor for the events affecting columns (and possibly also items).
	ibDataViewEvent(wxEventType evtType,
		ibDataViewCtrlBase* dvc,
		ibDataViewColumn* column,
		const ibDataViewItem& item = ibDataViewItem())
		: wxNotifyEvent(evtType, dvc->GetId())
	{
		Init(dvc, column, item);
	}

	// Constructor for the events affecting only the items.
	ibDataViewEvent(wxEventType evtType,
		ibDataViewCtrlBase* dvc,
		const ibDataViewItem& item)
		: wxNotifyEvent(evtType, dvc->GetId())
	{
		Init(dvc, NULL, item);
	}

	ibDataViewEvent(const ibDataViewEvent& event)
		: wxNotifyEvent(event),
		m_item(event.m_item),
		m_col(event.m_col),
		m_model(event.m_model),
		m_value(event.m_value),
		m_column(event.m_column),
		m_pos(event.m_pos),
		m_cacheFrom(event.m_cacheFrom),
		m_cacheTo(event.m_cacheTo),
		m_editCancelled(event.m_editCancelled)
#if wxUSE_DRAG_AND_DROP
		, m_dataObject(event.m_dataObject),
		m_dataFormat(event.m_dataFormat),
		m_dataBuffer(event.m_dataBuffer),
		m_dataSize(event.m_dataSize),
		m_dragFlags(event.m_dragFlags),
		m_dropEffect(event.m_dropEffect),
		m_proposedDropIndex(event.m_proposedDropIndex)
#endif
		, m_viewMode(event.m_viewMode)
	{
	}

	ibDataViewItem GetItem() const { return m_item; }
	int GetColumn() const { return m_col; }
	ibDataViewModel* GetModel() const { return m_model; }

	const wxVariant& GetValue() const { return m_value; }
	void SetValue(const wxVariant& value) { m_value = value; }

	// for wxEVT_DATAVIEW_ITEM_EDITING_DONE only
	bool IsEditCancelled() const { return m_editCancelled; }

	// for wxEVT_DATAVIEW_COLUMN_HEADER_CLICKED only
	ibDataViewColumn* GetDataViewColumn() const { return m_column; }

	// for wxEVT_DATAVIEW_CONTEXT_MENU only
	wxPoint GetPosition() const { return m_pos; }
	void SetPosition(int x, int y) { m_pos.x = x; m_pos.y = y; }

	// For wxEVT_DATAVIEW_CACHE_HINT
	int GetCacheFrom() const { return m_cacheFrom; }
	int GetCacheTo() const { return m_cacheTo; }
	void SetCache(int from, int to) { m_cacheFrom = from; m_cacheTo = to; }

	// For wxEVT_DATAVIEW_VIEW_SET
	ibDataViewViewMode GetViewMode() const { return m_viewMode; }
	void SetViewMode(ibDataViewViewMode mode) { m_viewMode = mode; }

#if wxUSE_DRAG_AND_DROP
	// For drag operations
	void SetDataObject(wxDataObject* obj) { m_dataObject = obj; }
	wxDataObject* GetDataObject() const { return m_dataObject; }

	// For drop operations
	void SetDataFormat(const wxDataFormat& format) { m_dataFormat = format; }
	wxDataFormat GetDataFormat() const { return m_dataFormat; }
	void SetDataSize(size_t size) { m_dataSize = size; }
	size_t GetDataSize() const { return m_dataSize; }
	void SetDataBuffer(void* buf) { m_dataBuffer = buf; }
	void* GetDataBuffer() const { return m_dataBuffer; }
	void SetDragFlags(int flags) { m_dragFlags = flags; }
	int GetDragFlags() const { return m_dragFlags; }
	void SetDropEffect(wxDragResult effect) { m_dropEffect = effect; }
	wxDragResult GetDropEffect() const { return m_dropEffect; }
	// For platforms (currently generic and OSX) that support Drag/Drop
	// insertion of items, this is the proposed child index for the insertion.
	void SetProposedDropIndex(int index) { m_proposedDropIndex = index; }
	int GetProposedDropIndex() const { return m_proposedDropIndex; }

	// Internal, only used by wxWidgets itself.
	void InitData(wxDataObjectComposite* obj, wxDataFormat format);
#endif // wxUSE_DRAG_AND_DROP

	virtual wxEvent* Clone() const wxOVERRIDE { return new ibDataViewEvent(*this); }

	// These methods shouldn't be used outside of wxWidgets and wxWidgets
	// itself doesn't use them any longer either as it constructs the events
	// with the appropriate ctors directly.
#if WXWIN_COMPATIBILITY_3_0
	wxDEPRECATED_MSG("Pass the argument to the ctor instead")
		void SetModel(ibDataViewModel* model) { m_model = model; }
	wxDEPRECATED_MSG("Pass the argument to the ctor instead")
		void SetDataViewColumn(ibDataViewColumn* col) { m_column = col; }
	wxDEPRECATED_MSG("Pass the argument to the ctor instead")
		void SetItem(const ibDataViewItem& item) { m_item = item; }
#endif // WXWIN_COMPATIBILITY_3_0

	void SetColumn(int col) { m_col = col; }
	void SetEditCancelled() { m_editCancelled = true; }

protected:

	ibDataViewItem      m_item;
	int                 m_col;
	ibDataViewModel* m_model;
	wxVariant           m_value;
	ibDataViewColumn* m_column;
	wxPoint             m_pos;
	int                 m_cacheFrom;
	int                 m_cacheTo;
	bool                m_editCancelled;

#if wxUSE_DRAG_AND_DROP
	wxDataObject* m_dataObject;

	wxMemoryBuffer      m_dataBuf;
	wxDataFormat        m_dataFormat;
	void* m_dataBuffer;
	size_t              m_dataSize;

	int                 m_dragFlags;
	wxDragResult        m_dropEffect;
	int                 m_proposedDropIndex;
#endif // wxUSE_DRAG_AND_DROP

	ibDataViewViewMode m_viewMode; 

private:

	// Common part of non-copy ctors.
	void Init(ibDataViewCtrlBase* dvc,
		ibDataViewColumn* column,
		const ibDataViewItem& item);

	wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN(ibDataViewEvent);
};

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_SELECTION_CHANGED, ibDataViewEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_ACTIVATED, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_COLLAPSED, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_EXPANDED, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_COLLAPSING, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_EXPANDING, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_START_EDITING, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_EDITING_STARTED, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_EDITING_DONE, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, ibDataViewEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_START_INSERTING, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_START_DELETING, ibDataViewEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, ibDataViewEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_COLUMN_HEADER_RIGHT_CLICK, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_COLUMN_SORTED, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_COLUMN_REORDERED, ibDataViewEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_CACHE_HINT, ibDataViewEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, ibDataViewEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_ITEM_DROP, ibDataViewEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_DATAVIEW_VIEW_SET, ibDataViewEvent);

typedef void (wxEvtHandler::* ibDataViewEventFunction)(ibDataViewEvent&);

#define ibDataViewEventHandler(func) \
    wxEVENT_HANDLER_CAST(ibDataViewEventFunction, func)

#define wx__DECLARE_DATAVIEWEVT(evt, id, fn) \
    wx__DECLARE_EVT1(wxEVT_DATAVIEW_ ## evt, id, ibDataViewEventHandler(fn))

#define EVT_DATAVIEW_SELECTION_CHANGED(id, fn) wx__DECLARE_DATAVIEWEVT(SELECTION_CHANGED, id, fn)

#define EVT_DATAVIEW_ITEM_ACTIVATED(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_ACTIVATED, id, fn)
#define EVT_DATAVIEW_ITEM_COLLAPSING(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_COLLAPSING, id, fn)
#define EVT_DATAVIEW_ITEM_COLLAPSED(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_COLLAPSED, id, fn)
#define EVT_DATAVIEW_ITEM_EXPANDING(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_EXPANDING, id, fn)
#define EVT_DATAVIEW_ITEM_EXPANDED(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_EXPANDED, id, fn)
#define EVT_DATAVIEW_ITEM_START_EDITING(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_START_EDITING, id, fn)
#define EVT_DATAVIEW_ITEM_EDITING_STARTED(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_EDITING_STARTED, id, fn)
#define EVT_DATAVIEW_ITEM_EDITING_DONE(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_EDITING_DONE, id, fn)
#define EVT_DATAVIEW_ITEM_VALUE_CHANGED(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_VALUE_CHANGED, id, fn)

#define EVT_DATAVIEW_ITEM_CONTEXT_MENU(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_CONTEXT_MENU, id, fn)

#define EVT_DATAVIEW_COLUMN_HEADER_CLICK(id, fn) wx__DECLARE_DATAVIEWEVT(COLUMN_HEADER_CLICK, id, fn)
#define EVT_DATAVIEW_COLUMN_HEADER_RIGHT_CLICK(id, fn) wx__DECLARE_DATAVIEWEVT(COLUMN_HEADER_RIGHT_CLICK, id, fn)
#define EVT_DATAVIEW_COLUMN_SORTED(id, fn) wx__DECLARE_DATAVIEWEVT(COLUMN_SORTED, id, fn)
#define EVT_DATAVIEW_COLUMN_REORDERED(id, fn) wx__DECLARE_DATAVIEWEVT(COLUMN_REORDERED, id, fn)
#define EVT_DATAVIEW_CACHE_HINT(id, fn) wx__DECLARE_DATAVIEWEVT(CACHE_HINT, id, fn)

#define EVT_DATAVIEW_ITEM_BEGIN_DRAG(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_BEGIN_DRAG, id, fn)
#define EVT_DATAVIEW_ITEM_DROP_POSSIBLE(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_DROP_POSSIBLE, id, fn)
#define EVT_DATAVIEW_ITEM_DROP(id, fn) wx__DECLARE_DATAVIEWEVT(ITEM_DROP, id, fn)

// Old and not documented synonym, don't use.
#define EVT_DATAVIEW_COLUMN_HEADER_RIGHT_CLICKED(id, fn) EVT_DATAVIEW_COLUMN_HEADER_RIGHT_CLICK(id, fn)

#include "datavgen.h"

//-----------------------------------------------------------------------------
// ibDataViewListStore
//-----------------------------------------------------------------------------

class FRONTEND_API ibDataViewListStoreLine
{
public:
	ibDataViewListStoreLine(wxUIntPtr data = 0)
	{
		m_data = data;
	}

	void SetData(wxUIntPtr data)
	{
		m_data = data;
	}
	wxUIntPtr GetData() const
	{
		return m_data;
	}

	wxVector<wxVariant>  m_values;

private:
	wxUIntPtr m_data;
};

class FRONTEND_API ibDataViewListStore : public ibDataViewIndexListModel
{
public:
	ibDataViewListStore();
	~ibDataViewListStore();

	void PrependColumn(const wxString& varianttype);
	void InsertColumn(unsigned int pos, const wxString& varianttype);
	void AppendColumn(const wxString& varianttype);

	void AppendItem(const wxVector<wxVariant>& values, wxUIntPtr data = 0);
	void PrependItem(const wxVector<wxVariant>& values, wxUIntPtr data = 0);
	void InsertItem(unsigned int row, const wxVector<wxVariant>& values, wxUIntPtr data = 0);
	void DeleteItem(unsigned int pos);
	void DeleteAllItems();
	void ClearColumns();

	unsigned int GetItemCount() const;

	void SetItemData(const ibDataViewItem& item, wxUIntPtr data);
	wxUIntPtr GetItemData(const ibDataViewItem& item) const;

	// override base virtuals

	virtual void GetValueByRow(wxVariant& value,
		unsigned int row, unsigned int col) const wxOVERRIDE;

	virtual bool SetValueByRow(const wxVariant& value,
		unsigned int row, unsigned int col) wxOVERRIDE;


public:
	wxVector<ibDataViewListStoreLine*> m_data;
	wxArrayString                      m_cols;
};

//-----------------------------------------------------------------------------

class FRONTEND_API ibDataViewListCtrl : public ibDataViewCtrl
{
public:
	ibDataViewListCtrl();
	ibDataViewListCtrl(wxWindow* parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxDV_ROW_LINES,
		const wxValidator& validator = wxDefaultValidator);
	~ibDataViewListCtrl();

	bool Create(wxWindow* parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxDV_ROW_LINES,
		const wxValidator& validator = wxDefaultValidator);

	ibDataViewListStore* GetStore()
	{
		return (ibDataViewListStore*)GetModel();
	}
	const ibDataViewListStore* GetStore() const
	{
		return (const ibDataViewListStore*)GetModel();
	}

	int ItemToRow(const ibDataViewItem& item) const
	{
		return item.IsOk() ? (int)GetStore()->GetRow(item) : wxNOT_FOUND;
	}
	ibDataViewItem RowToItem(int row) const
	{
		return row == wxNOT_FOUND ? ibDataViewItem() : GetStore()->GetItem(row);
	}

	int GetSelectedRow() const
	{
		return ItemToRow(GetSelection());
	}
	void SelectRow(unsigned row)
	{
		Select(RowToItem(row));
	}
	void UnselectRow(unsigned row)
	{
		Unselect(RowToItem(row));
	}
	bool IsRowSelected(unsigned row) const
	{
		return IsSelected(RowToItem(row));
	}

	bool AppendColumn(ibDataViewColumn* column, const wxString& varianttype);
	bool PrependColumn(ibDataViewColumn* column, const wxString& varianttype);
	bool InsertColumn(unsigned int pos, ibDataViewColumn* column, const wxString& varianttype);

	// overridden from base class
	virtual bool PrependColumn(ibDataViewColumn* col) wxOVERRIDE;
	virtual bool InsertColumn(unsigned int pos, ibDataViewColumn* col) wxOVERRIDE;
	virtual bool AppendColumn(ibDataViewColumn* col) wxOVERRIDE;
	virtual bool ClearColumns() wxOVERRIDE;

	ibDataViewColumn* AppendTextColumn(const wxString& label,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_LEFT, int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendToggleColumn(const wxString& label,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_ACTIVATABLE, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_LEFT, int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendProgressColumn(const wxString& label,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_LEFT, int flags = wxDATAVIEW_COL_RESIZABLE);
	ibDataViewColumn* AppendIconTextColumn(const wxString& label,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxCOL_WIDTH_DEFAULT,
		wxAlignment align = wxALIGN_LEFT, int flags = wxDATAVIEW_COL_RESIZABLE);

	void AppendItem(const wxVector<wxVariant>& values, wxUIntPtr data = 0)
	{
		GetStore()->AppendItem(values, data);
	}
	void PrependItem(const wxVector<wxVariant>& values, wxUIntPtr data = 0)
	{
		GetStore()->PrependItem(values, data);
	}
	void InsertItem(unsigned int row, const wxVector<wxVariant>& values, wxUIntPtr data = 0)
	{
		GetStore()->InsertItem(row, values, data);
	}
	void DeleteItem(unsigned row)
	{
		GetStore()->DeleteItem(row);
	}
	void DeleteAllItems()
	{
		GetStore()->DeleteAllItems();
	}

	void SetValue(const wxVariant& value, unsigned int row, unsigned int col)
	{
		GetStore()->SetValueByRow(value, row, col);
		GetStore()->RowValueChanged(row, col);
	}
	void GetValue(wxVariant& value, unsigned int row, unsigned int col)
	{
		GetStore()->GetValueByRow(value, row, col);
	}

	void SetTextValue(const wxString& value, unsigned int row, unsigned int col)
	{
		GetStore()->SetValueByRow(value, row, col);
		GetStore()->RowValueChanged(row, col);
	}
	wxString GetTextValue(unsigned int row, unsigned int col) const
	{
		wxVariant value; GetStore()->GetValueByRow(value, row, col); return value.GetString();
	}

	void SetToggleValue(bool value, unsigned int row, unsigned int col)
	{
		GetStore()->SetValueByRow(value, row, col);
		GetStore()->RowValueChanged(row, col);
	}
	bool GetToggleValue(unsigned int row, unsigned int col) const
	{
		wxVariant value; GetStore()->GetValueByRow(value, row, col); return value.GetBool();
	}

	void SetItemData(const ibDataViewItem& item, wxUIntPtr data)
	{
		GetStore()->SetItemData(item, data);
	}
	wxUIntPtr GetItemData(const ibDataViewItem& item) const
	{
		return GetStore()->GetItemData(item);
	}

	int GetItemCount() const
	{
		return GetStore()->GetItemCount();
	}

private:
	wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN(ibDataViewListCtrl);
};

//-----------------------------------------------------------------------------
// ibDataViewTreeStore
//-----------------------------------------------------------------------------

class FRONTEND_API ibDataViewTreeStoreNode
{
public:
	ibDataViewTreeStoreNode(ibDataViewTreeStoreNode* parent,
		const wxString& text,
		const wxBitmapBundle& icon = wxBitmapBundle(),
		wxClientData* data = NULL);
	virtual ~ibDataViewTreeStoreNode();

	void SetText(const wxString& text)
	{
		m_text = text;
	}
	wxString GetText() const
	{
		return m_text;
	}
	void SetIcon(const wxBitmapBundle& icon)
	{
		m_icon = icon;
	}
	const wxBitmapBundle& GetBitmapBundle() const
	{
		return m_icon;
	}
	wxIcon GetIcon() const
	{
		return m_icon.GetIcon(wxDefaultSize);
	}
	void SetData(wxClientData* data)
	{
		delete m_data; m_data = data;
	}
	wxClientData* GetData() const
	{
		return m_data;
	}

	ibDataViewItem GetItem() const
	{
		return ibDataViewItem(const_cast<void*>(static_cast<const void*>(this)));
	}

	virtual bool IsContainer()
	{
		return false;
	}

	ibDataViewTreeStoreNode* GetParent()
	{
		return m_parent;
	}

private:
	ibDataViewTreeStoreNode* m_parent;
	wxString                  m_text;
	wxBitmapBundle            m_icon;
	wxClientData* m_data;
};

typedef wxVector<ibDataViewTreeStoreNode*> ibDataViewTreeStoreNodes;

class FRONTEND_API ibDataViewTreeStoreContainerNode : public ibDataViewTreeStoreNode
{
public:
	ibDataViewTreeStoreContainerNode(ibDataViewTreeStoreNode* parent,
		const wxString& text,
		const wxBitmapBundle& icon = wxBitmapBundle(),
		const wxBitmapBundle& expanded = wxBitmapBundle(),
		wxClientData* data = NULL);
	virtual ~ibDataViewTreeStoreContainerNode();

	const ibDataViewTreeStoreNodes& GetChildren() const
	{
		return m_children;
	}
	ibDataViewTreeStoreNodes& GetChildren()
	{
		return m_children;
	}

	ibDataViewTreeStoreNodes::iterator FindChild(ibDataViewTreeStoreNode* node);

	void SetExpandedIcon(const wxBitmapBundle& icon)
	{
		m_iconExpanded = icon;
	}
	const wxBitmapBundle& GetExpandedBitmapBundle() const
	{
		return m_iconExpanded;
	}
	wxIcon GetExpandedIcon() const
	{
		return m_iconExpanded.GetIcon(wxDefaultSize);
	}

	void SetExpanded(bool expanded = true)
	{
		m_isExpanded = expanded;
	}
	bool IsExpanded() const
	{
		return m_isExpanded;
	}

	virtual bool IsContainer() wxOVERRIDE
	{
		return true;
	}

	void DestroyChildren();

private:
	ibDataViewTreeStoreNodes     m_children;
	wxBitmapBundle               m_iconExpanded;
	bool                         m_isExpanded;
};

//-----------------------------------------------------------------------------

class FRONTEND_API ibDataViewTreeStore : public ibDataViewModel
{
public:
	ibDataViewTreeStore();
	~ibDataViewTreeStore();

	ibDataViewItem AppendItem(const ibDataViewItem& parent,
		const wxString& text,
		const wxBitmapBundle& icon = wxBitmapBundle(),
		wxClientData* data = NULL);
	ibDataViewItem PrependItem(const ibDataViewItem& parent,
		const wxString& text,
		const wxBitmapBundle& icon = wxBitmapBundle(),
		wxClientData* data = NULL);
	ibDataViewItem InsertItem(const ibDataViewItem& parent, const ibDataViewItem& previous,
		const wxString& text,
		const wxBitmapBundle& icon = wxBitmapBundle(),
		wxClientData* data = NULL);

	ibDataViewItem PrependContainer(const ibDataViewItem& parent,
		const wxString& text,
		const wxBitmapBundle& icon = wxBitmapBundle(),
		const wxBitmapBundle& expanded = wxBitmapBundle(),
		wxClientData* data = NULL);
	ibDataViewItem AppendContainer(const ibDataViewItem& parent,
		const wxString& text,
		const wxBitmapBundle& icon = wxBitmapBundle(),
		const wxBitmapBundle& expanded = wxBitmapBundle(),
		wxClientData* data = NULL);
	ibDataViewItem InsertContainer(const ibDataViewItem& parent, const ibDataViewItem& previous,
		const wxString& text,
		const wxBitmapBundle& icon = wxBitmapBundle(),
		const wxBitmapBundle& expanded = wxBitmapBundle(),
		wxClientData* data = NULL);

	ibDataViewItem GetNthChild(const ibDataViewItem& parent, unsigned int pos) const;
	int GetChildCount(const ibDataViewItem& parent) const;

	void SetItemText(const ibDataViewItem& item, const wxString& text);
	wxString GetItemText(const ibDataViewItem& item) const;
	void SetItemIcon(const ibDataViewItem& item, const wxBitmapBundle& icon);
	wxBitmapBundle GetItemBitmapBundle(const ibDataViewItem& item) const;
	wxIcon GetItemIcon(const ibDataViewItem& item) const;
	void SetItemExpandedIcon(const ibDataViewItem& item, const wxBitmapBundle& icon);
	wxBitmapBundle GetItemExpandedBitmapBundle(const ibDataViewItem& item) const;
	wxIcon GetItemExpandedIcon(const ibDataViewItem& item) const;
	void SetItemData(const ibDataViewItem& item, wxClientData* data);
	wxClientData* GetItemData(const ibDataViewItem& item) const;

	void DeleteItem(const ibDataViewItem& item);
	void DeleteChildren(const ibDataViewItem& item);
	void DeleteAllItems();

	// implement base methods

	virtual void GetValue(wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) const wxOVERRIDE;
	virtual bool SetValue(const wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) wxOVERRIDE;
	virtual ibDataViewItem GetParent(const ibDataViewItem& item) const wxOVERRIDE;
	virtual bool IsContainer(const ibDataViewItem& item) const wxOVERRIDE;
	virtual unsigned int GetChildren(const ibDataViewItem& item, ibDataViewItemArray& children) const wxOVERRIDE;

	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
		unsigned int column, bool ascending) const wxOVERRIDE;

	virtual bool HasDefaultCompare() const wxOVERRIDE
	{
		return true;
	}

	ibDataViewTreeStoreNode* FindNode(const ibDataViewItem& item) const;
	ibDataViewTreeStoreContainerNode* FindContainerNode(const ibDataViewItem& item) const;
	ibDataViewTreeStoreNode* GetRoot() const { return m_root; }

public:
	ibDataViewTreeStoreNode* m_root;
};

//-----------------------------------------------------------------------------

class FRONTEND_API ibDataViewTreeCtrl : public ibDataViewCtrl,
	public wxWithImages
{
public:
	ibDataViewTreeCtrl() {}
	ibDataViewTreeCtrl(wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDV_NO_HEADER | wxDV_ROW_LINES,
		const wxValidator& validator = wxDefaultValidator)
	{
		Create(parent, id, pos, size, style, validator);
	}

	bool Create(wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDV_NO_HEADER | wxDV_ROW_LINES,
		const wxValidator& validator = wxDefaultValidator);

	ibDataViewTreeStore* GetStore()
	{
		return (ibDataViewTreeStore*)GetModel();
	}
	const ibDataViewTreeStore* GetStore() const
	{
		return (const ibDataViewTreeStore*)GetModel();
	}

	bool IsContainer(const ibDataViewItem& item) const
	{
		return GetStore()->IsContainer(item);
	}

	ibDataViewItem AppendItem(const ibDataViewItem& parent,
		const wxString& text, int icon = NO_IMAGE, wxClientData* data = NULL);
	ibDataViewItem PrependItem(const ibDataViewItem& parent,
		const wxString& text, int icon = NO_IMAGE, wxClientData* data = NULL);
	ibDataViewItem InsertItem(const ibDataViewItem& parent, const ibDataViewItem& previous,
		const wxString& text, int icon = NO_IMAGE, wxClientData* data = NULL);

	ibDataViewItem PrependContainer(const ibDataViewItem& parent,
		const wxString& text, int icon = NO_IMAGE, int expanded = NO_IMAGE,
		wxClientData* data = NULL);
	ibDataViewItem AppendContainer(const ibDataViewItem& parent,
		const wxString& text, int icon = NO_IMAGE, int expanded = NO_IMAGE,
		wxClientData* data = NULL);
	ibDataViewItem InsertContainer(const ibDataViewItem& parent, const ibDataViewItem& previous,
		const wxString& text, int icon = NO_IMAGE, int expanded = NO_IMAGE,
		wxClientData* data = NULL);

	ibDataViewItem GetNthChild(const ibDataViewItem& parent, unsigned int pos) const
	{
		return GetStore()->GetNthChild(parent, pos);
	}
	int GetChildCount(const ibDataViewItem& parent) const
	{
		return GetStore()->GetChildCount(parent);
	}
	ibDataViewItem GetItemParent(ibDataViewItem item) const
	{
		return GetStore()->GetParent(item);
	}

	void SetItemText(const ibDataViewItem& item, const wxString& text);
	wxString GetItemText(const ibDataViewItem& item) const
	{
		return GetStore()->GetItemText(item);
	}
	void SetItemIcon(const ibDataViewItem& item, const wxBitmapBundle& icon);
	wxIcon GetItemIcon(const ibDataViewItem& item) const
	{
		return GetStore()->GetItemIcon(item);
	}
	void SetItemExpandedIcon(const ibDataViewItem& item, const wxBitmapBundle& icon);
	wxIcon GetItemExpandedIcon(const ibDataViewItem& item) const
	{
		return GetStore()->GetItemExpandedIcon(item);
	}
	void SetItemData(const ibDataViewItem& item, wxClientData* data)
	{
		GetStore()->SetItemData(item, data);
	}
	wxClientData* GetItemData(const ibDataViewItem& item) const
	{
		return GetStore()->GetItemData(item);
	}

	void DeleteItem(const ibDataViewItem& item);
	void DeleteChildren(const ibDataViewItem& item);
	void DeleteAllItems();

	void OnExpanded(ibDataViewEvent& event);
	void OnCollapsed(ibDataViewEvent& event);
	void OnSize(wxSizeEvent& event);

protected:
	virtual void OnImagesChanged() wxOVERRIDE;

private:
	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN(ibDataViewTreeCtrl);
};

// old wxEVT_COMMAND_* constants
#define wxEVT_COMMAND_DATAVIEW_SELECTION_CHANGED           wxEVT_DATAVIEW_SELECTION_CHANGED
#define wxEVT_COMMAND_DATAVIEW_ITEM_ACTIVATED              wxEVT_DATAVIEW_ITEM_ACTIVATED
#define wxEVT_COMMAND_DATAVIEW_ITEM_COLLAPSED              wxEVT_DATAVIEW_ITEM_COLLAPSED
#define wxEVT_COMMAND_DATAVIEW_ITEM_EXPANDED               wxEVT_DATAVIEW_ITEM_EXPANDED
#define wxEVT_COMMAND_DATAVIEW_ITEM_COLLAPSING             wxEVT_DATAVIEW_ITEM_COLLAPSING
#define wxEVT_COMMAND_DATAVIEW_ITEM_EXPANDING              wxEVT_DATAVIEW_ITEM_EXPANDING
#define wxEVT_COMMAND_DATAVIEW_ITEM_START_EDITING          wxEVT_DATAVIEW_ITEM_START_EDITING
#define wxEVT_COMMAND_DATAVIEW_ITEM_EDITING_STARTED        wxEVT_DATAVIEW_ITEM_EDITING_STARTED
#define wxEVT_COMMAND_DATAVIEW_ITEM_EDITING_DONE           wxEVT_DATAVIEW_ITEM_EDITING_DONE
#define wxEVT_COMMAND_DATAVIEW_ITEM_VALUE_CHANGED          wxEVT_DATAVIEW_ITEM_VALUE_CHANGED
#define wxEVT_COMMAND_DATAVIEW_ITEM_CONTEXT_MENU           wxEVT_DATAVIEW_ITEM_CONTEXT_MENU
#define wxEVT_COMMAND_DATAVIEW_COLUMN_HEADER_CLICK         wxEVT_DATAVIEW_COLUMN_HEADER_CLICK
#define wxEVT_COMMAND_DATAVIEW_COLUMN_HEADER_RIGHT_CLICK   wxEVT_DATAVIEW_COLUMN_HEADER_RIGHT_CLICK
#define wxEVT_COMMAND_DATAVIEW_COLUMN_SORTED               wxEVT_DATAVIEW_COLUMN_SORTED
#define wxEVT_COMMAND_DATAVIEW_COLUMN_REORDERED            wxEVT_DATAVIEW_COLUMN_REORDERED
#define wxEVT_COMMAND_DATAVIEW_CACHE_HINT                  wxEVT_DATAVIEW_CACHE_HINT
#define wxEVT_COMMAND_DATAVIEW_ITEM_BEGIN_DRAG             wxEVT_DATAVIEW_ITEM_BEGIN_DRAG
#define wxEVT_COMMAND_DATAVIEW_ITEM_DROP_POSSIBLE          wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE
#define wxEVT_COMMAND_DATAVIEW_ITEM_DROP                   wxEVT_DATAVIEW_ITEM_DROP
#define wxEVT_COMMAND_DATAVIEW_VIEW_SET					   wxEVT_DATAVIEW_VIEW_SET

#endif // wxUSE_DATAVIEWCTRL

#endif
	// _WX_DATAVIEW_H_BASE_
