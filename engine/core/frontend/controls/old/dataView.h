/////////////////////////////////////////////////////////////////////////////
// Name:        dataview.h
// Purpose:     CDataViewCtrl base classes
// Author:      Robert Roebling
// Modified by: Bo Yang
// Created:     08.01.06
// Copyright:   (c) Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DATAVIEW_H_BASE_
#define _DATAVIEW_H_BASE_

#include <wx/defs.h>

#if wxUSE_DATAVIEWCTRL

#include <wx/dataview.h>

// ----------------------------------------------------------------------------
// CDataViewCtrl globals
// ----------------------------------------------------------------------------

class CDataViewModel;
class CDataViewCtrl;
class CDataViewColumn;
class CDataViewModelNotifier;
#if wxUSE_ACCESSIBILITY
class CDataViewCtrlAccessible;
#endif // wxUSE_ACCESSIBILITY

class wxDataViewRenderer;

// ---------------------------------------------------------
// CDataViewModelNotifier
// ---------------------------------------------------------

class CDataViewModelNotifier
{
public:
	CDataViewModelNotifier() { m_owner = NULL; }
	virtual ~CDataViewModelNotifier() { m_owner = NULL; }

	virtual bool ItemAdded(const wxDataViewItem &parent, const wxDataViewItem &item) = 0;
	virtual bool ItemDeleted(const wxDataViewItem &parent, const wxDataViewItem &item) = 0;
	virtual bool ItemChanged(const wxDataViewItem &item) = 0;
	virtual bool ItemsAdded(const wxDataViewItem &parent, const wxDataViewItemArray &items);
	virtual bool ItemsDeleted(const wxDataViewItem &parent, const wxDataViewItemArray &items);
	virtual bool ItemsChanged(const wxDataViewItemArray &items);
	virtual bool ValueChanged(const wxDataViewItem &item, unsigned int col) = 0;
	virtual bool Cleared() = 0;

	// some platforms, such as GTK+, may need a two step procedure for ::Reset()
	virtual bool BeforeReset() { return true; }
	virtual bool AfterReset() { return Cleared(); }

	virtual void Resort() = 0;

	void SetOwner(CDataViewModel *owner) { m_owner = owner; }
	CDataViewModel *GetOwner() const { return m_owner; }

private:
	CDataViewModel *m_owner;
};

// ---------------------------------------------------------
// CDataViewModel
// ---------------------------------------------------------

typedef wxVector<CDataViewModelNotifier*> CDataViewModelNotifiers;

class CDataViewModel : public wxDataViewModel
{
public:
	CDataViewModel();

	virtual unsigned int GetColumnCount() const = 0;

	// return type as reported by wxVariant
	virtual wxString GetColumnType(unsigned int col) const = 0;

	// get value into a wxVariant
	virtual void GetValue(wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) const = 0;

	// return true if the given item has a value to display in the given
	// column: this is always true except for container items which by default
	// only show their label in the first column (but see HasContainerColumns())
	virtual bool HasValue(const wxDataViewItem& item, unsigned col) const
	{
		return col == 0 || !IsContainer(item) || HasContainerColumns(item);
	}

	// usually ValueChanged() should be called after changing the value in the
	// model to update the control, ChangeValue() does it on its own while
	// SetValue() does not -- so while you will override SetValue(), you should
	// be usually calling ChangeValue()
	virtual bool SetValue(const wxVariant &variant,
		const wxDataViewItem &item,
		unsigned int col) = 0;

	bool ChangeValue(const wxVariant& variant,
		const wxDataViewItem& item,
		unsigned int col)
	{
		return SetValue(variant, item, col) && ValueChanged(item, col);
	}

	// Get text attribute, return false of default attributes should be used
	virtual bool GetAttr(const wxDataViewItem &WXUNUSED(item),
		unsigned int WXUNUSED(col),
		wxDataViewItemAttr &WXUNUSED(attr)) const
	{
		return false;
	}

	// Override this if you want to disable specific items
	virtual bool IsEnabled(const wxDataViewItem &WXUNUSED(item),
		unsigned int WXUNUSED(col)) const
	{
		return true;
	}

	// define hierarchy
	virtual wxDataViewItem GetParent(const wxDataViewItem &item) const = 0;
	virtual bool IsContainer(const wxDataViewItem &item) const = 0;
	// Is the container just a header or an item with all columns
	virtual bool HasContainerColumns(const wxDataViewItem& WXUNUSED(item)) const
	{
		return false;
	}
	virtual unsigned int GetChildren(const wxDataViewItem &item, wxDataViewItemArray &children) const = 0;

	// delegated notifiers
	bool ItemAdded(const wxDataViewItem &parent, const wxDataViewItem &item);
	bool ItemsAdded(const wxDataViewItem &parent, const wxDataViewItemArray &items);
	bool ItemDeleted(const wxDataViewItem &parent, const wxDataViewItem &item);
	bool ItemsDeleted(const wxDataViewItem &parent, const wxDataViewItemArray &items);
	bool ItemChanged(const wxDataViewItem &item);
	bool ItemsChanged(const wxDataViewItemArray &items);
	bool ValueChanged(const wxDataViewItem &item, unsigned int col);
	bool Cleared();

	// some platforms, such as GTK+, may need a two step procedure for ::Reset()
	bool BeforeReset();
	bool AfterReset();


	// delegated action
	virtual void Resort();

	void AddNotifier(CDataViewModelNotifier *notifier);
	void RemoveNotifier(CDataViewModelNotifier *notifier);

	// default compare function
	virtual int Compare(const wxDataViewItem &item1, const wxDataViewItem &item2,
		unsigned int column, bool ascending) const;
	virtual bool HasDefaultCompare() const { return false; }

	// internal
	virtual bool IsListModel() const { return false; }
	virtual bool IsVirtualListModel() const { return false; }

protected:
	// Dtor is protected because the objects of this class must not be deleted,
	// DecRef() must be used instead.
	virtual ~CDataViewModel();

	// Helper function used by the default Compare() implementation to compare
	// values of types it is not aware about. Can be overridden in the derived
	// classes that use columns of custom types.
	virtual int DoCompareValues(const wxVariant& WXUNUSED(value1),
		const wxVariant& WXUNUSED(value2)) const
	{
		return 0;
	}

private:
	CDataViewModelNotifiers  m_notifiers;
};

// ----------------------------------------------------------------------------
// CDataViewListModel: a model of a list, i.e. flat data structure without any
//      branches/containers, used as base class by CDataViewIndexListModel and
//      CDataViewVirtualListModel
// ----------------------------------------------------------------------------

class CDataViewListModel : public CDataViewModel
{
public:
	// derived classes should override these methods instead of
	// {Get,Set}Value() and GetAttr() inherited from the base class

	virtual void GetValueByRow(wxVariant &variant,
		unsigned row, unsigned col) const = 0;

	virtual bool SetValueByRow(const wxVariant &variant,
		unsigned row, unsigned col) = 0;

	virtual bool
		GetAttrByRow(unsigned WXUNUSED(row), unsigned WXUNUSED(col),
			wxDataViewItemAttr &WXUNUSED(attr)) const
	{
		return false;
	}

	virtual bool IsEnabledByRow(unsigned int WXUNUSED(row),
		unsigned int WXUNUSED(col)) const
	{
		return true;
	}


	// helper methods provided by list models only
	virtual unsigned GetRow(const wxDataViewItem &item) const = 0;

	// returns the number of rows
	virtual unsigned int GetCount() const = 0;

	// implement some base class pure virtual directly
	virtual wxDataViewItem
		GetParent(const wxDataViewItem & WXUNUSED(item)) const wxOVERRIDE
	{
		// items never have valid parent in this model
		return wxDataViewItem();
	}

	virtual bool IsContainer(const wxDataViewItem &item) const wxOVERRIDE
	{
		// only the invisible (and invalid) root item has children
		return !item.IsOk();
	}

	// and implement some others by forwarding them to our own ones
	virtual void GetValue(wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) const wxOVERRIDE
	{
		GetValueByRow(variant, GetRow(item), col);
	}

	virtual bool SetValue(const wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) wxOVERRIDE
	{
		return SetValueByRow(variant, GetRow(item), col);
	}

	virtual bool GetAttr(const wxDataViewItem &item, unsigned int col,
		wxDataViewItemAttr &attr) const wxOVERRIDE
	{
		return GetAttrByRow(GetRow(item), col, attr);
	}

	virtual bool IsEnabled(const wxDataViewItem &item, unsigned int col) const wxOVERRIDE
	{
		return IsEnabledByRow(GetRow(item), col);
	}


	virtual bool IsListModel() const wxOVERRIDE { return true; }
};

// ---------------------------------------------------------
// CDataViewIndexListModel
// ---------------------------------------------------------

class CDataViewIndexListModel : public CDataViewListModel
{
public:
	CDataViewIndexListModel(unsigned int initial_size = 0);

	void RowPrepended();
	void RowInserted(unsigned int before);
	void RowAppended();
	void RowDeleted(unsigned int row);
	void RowsDeleted(const wxArrayInt &rows);
	void RowChanged(unsigned int row);
	void RowValueChanged(unsigned int row, unsigned int col);
	void Reset(unsigned int new_size);

	// convert to/from row/wxDataViewItem

	virtual unsigned GetRow(const wxDataViewItem &item) const wxOVERRIDE;
	wxDataViewItem GetItem(unsigned int row) const;

	// implement base methods
	virtual unsigned int GetChildren(const wxDataViewItem &item, wxDataViewItemArray &children) const wxOVERRIDE;

	unsigned int GetCount() const wxOVERRIDE { return (unsigned int)m_hash.GetCount(); }

private:
	wxDataViewItemArray m_hash;
	unsigned int m_nextFreeID;
	bool m_ordered;
};

// ---------------------------------------------------------
// CDataViewVirtualListModel
// ---------------------------------------------------------

#ifdef __WXMAC__
// better than nothing
typedef CDataViewIndexListModel CDataViewVirtualListModel;
#else

class CDataViewVirtualListModel : public CDataViewListModel
{
public:
	CDataViewVirtualListModel(unsigned int initial_size = 0);

	void RowPrepended();
	void RowInserted(unsigned int before);
	void RowAppended();
	void RowDeleted(unsigned int row);
	void RowsDeleted(const wxArrayInt &rows);
	void RowChanged(unsigned int row);
	void RowValueChanged(unsigned int row, unsigned int col);
	void Reset(unsigned int new_size);

	// convert to/from row/wxDataViewItem

	virtual unsigned GetRow(const wxDataViewItem &item) const wxOVERRIDE;
	wxDataViewItem GetItem(unsigned int row) const;

	// compare based on index

	virtual int Compare(const wxDataViewItem &item1, const wxDataViewItem &item2,
		unsigned int column, bool ascending) const wxOVERRIDE;
	virtual bool HasDefaultCompare() const wxOVERRIDE;

	// implement base methods
	virtual unsigned int GetChildren(const wxDataViewItem &item, wxDataViewItemArray &children) const wxOVERRIDE;

	unsigned int GetCount() const wxOVERRIDE { return m_size; }

	// internal
	virtual bool IsVirtualListModel() const wxOVERRIDE { return true; }

private:
	unsigned int m_size;
};
#endif

// ----------------------------------------------------------------------------
// wxDataViewRenderer and related classes
// ----------------------------------------------------------------------------

#include <wx/dvrenderers.h>

// ---------------------------------------------------------
// CDataViewColumnBase
// ---------------------------------------------------------

class CDataViewColumnBase : public wxSettableHeaderColumn
{
public:
	// ctor for the text columns: takes ownership of renderer
	CDataViewColumnBase(wxDataViewRenderer *renderer,
		unsigned int model_column)
	{
		Init(renderer, model_column);
	}

	// ctor for the bitmap columns
	CDataViewColumnBase(const wxBitmap& bitmap,
		wxDataViewRenderer *renderer,
		unsigned int model_column)
		: m_bitmap(bitmap)
	{
		Init(renderer, model_column);
	}

	virtual ~CDataViewColumnBase();

	// setters:
	virtual void SetOwner(CDataViewCtrl *owner)
	{
		m_owner = owner;
	}

	// getters:
	unsigned int GetModelColumn() const { return static_cast<unsigned int>(m_model_column); }
	CDataViewCtrl *GetOwner() const { return m_owner; }
	wxDataViewRenderer* GetRenderer() const { return m_renderer; }

	// implement some of base class pure virtuals (the rest is port-dependent
	// and done differently in generic and native versions)
	virtual void SetBitmap(const wxBitmap& bitmap) wxOVERRIDE { m_bitmap = bitmap; }
	virtual wxBitmap GetBitmap() const wxOVERRIDE { return m_bitmap; }

	// Special accessor for use by wxWidgets only returning the width that was
	// explicitly set, either by the application, using SetWidth(), or by the
	// user, resizing the column interactively. It is usually the same as
	// GetWidth(), but can be different for the last column.
	virtual int WXGetSpecifiedWidth() const { return GetWidth(); }

protected:
	wxDataViewRenderer      *m_renderer;
	int                      m_model_column;
	wxBitmap                 m_bitmap;
	CDataViewCtrl          *m_owner;

private:
	// common part of all ctors
	void Init(wxDataViewRenderer *renderer, unsigned int model_column);
};

// ----------------------------------------------------------------------------
// CDataViewEvent - the event class for the wxDataViewCtrl notifications
// ----------------------------------------------------------------------------

class CDataViewEvent : public wxNotifyEvent
{
public:
	// Default ctor, normally shouldn't be used and mostly exists only for
	// backwards compatibility.
	CDataViewEvent()
		: wxNotifyEvent()
	{
		Init(NULL, NULL, wxDataViewItem());
	}

	// Constructor for the events affecting columns (and possibly also items).
	CDataViewEvent(wxEventType evtType,
		CDataViewCtrlBase* dvc,
		CDataViewColumn* column,
		const wxDataViewItem& item = wxDataViewItem())
		: wxNotifyEvent(evtType, dvc->GetId())
	{
		Init(dvc, column, item);
	}

	// Constructor for the events affecting only the items.
	CDataViewEvent(wxEventType evtType,
		CDataViewCtrlBase* dvc,
		const wxDataViewItem& item)
		: wxNotifyEvent(evtType, dvc->GetId())
	{
		Init(dvc, NULL, item);
	}

	CDataViewEvent(const CDataViewEvent& event)
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
	{ }

	wxDataViewItem GetItem() const { return m_item; }
	int GetColumn() const { return m_col; }
	CDataViewModel* GetModel() const { return m_model; }

	const wxVariant &GetValue() const { return m_value; }
	void SetValue(const wxVariant &value) { m_value = value; }

	// for wxEVT_DATAVIEW_ITEM_EDITING_DONE only
	bool IsEditCancelled() const { return m_editCancelled; }

	// for wxEVT_DATAVIEW_COLUMN_HEADER_CLICKED only
	CDataViewColumn *GetDataViewColumn() const { return m_column; }

	// for wxEVT_DATAVIEW_CONTEXT_MENU only
	wxPoint GetPosition() const { return m_pos; }
	void SetPosition(int x, int y) { m_pos.x = x; m_pos.y = y; }

	// For wxEVT_DATAVIEW_CACHE_HINT
	int GetCacheFrom() const { return m_cacheFrom; }
	int GetCacheTo() const { return m_cacheTo; }
	void SetCache(int from, int to) { m_cacheFrom = from; m_cacheTo = to; }


#if wxUSE_DRAG_AND_DROP
	// For drag operations
	void SetDataObject(wxDataObject *obj) { m_dataObject = obj; }
	wxDataObject *GetDataObject() const { return m_dataObject; }

	// For drop operations
	void SetDataFormat(const wxDataFormat &format) { m_dataFormat = format; }
	wxDataFormat GetDataFormat() const { return m_dataFormat; }
	void SetDataSize(size_t size) { m_dataSize = size; }
	size_t GetDataSize() const { return m_dataSize; }
	void SetDataBuffer(void* buf) { m_dataBuffer = buf; }
	void *GetDataBuffer() const { return m_dataBuffer; }
	void SetDragFlags(int flags) { m_dragFlags = flags; }
	int GetDragFlags() const { return m_dragFlags; }
	void SetDropEffect(wxDragResult effect) { m_dropEffect = effect; }
	wxDragResult GetDropEffect() const { return m_dropEffect; }
	// For platforms (currently generic and OSX) that support Drag/Drop
	// insertion of items, this is the proposed child index for the insertion.
	void SetProposedDropIndex(int index) { m_proposedDropIndex = index; }
	int GetProposedDropIndex() const { return m_proposedDropIndex; }
#endif // wxUSE_DRAG_AND_DROP

	virtual wxEvent *Clone() const wxOVERRIDE { return new CDataViewEvent(*this); }

	// These methods shouldn't be used outside of wxWidgets and wxWidgets
	// itself doesn't use them any longer neither as it constructs the events
	// with the appropriate ctors directly.
#if WXWIN_COMPATIBILITY_3_0
	wxDEPRECATED_MSG("Pass the argument to the ctor instead")
		void SetModel(CDataViewModel *model) { m_model = model; }
	wxDEPRECATED_MSG("Pass the argument to the ctor instead")
		void SetDataViewColumn(CDataViewColumn *col) { m_column = col; }
	wxDEPRECATED_MSG("Pass the argument to the ctor instead")
		void SetItem(const wxDataViewItem &item) { m_item = item; }
#endif // WXWIN_COMPATIBILITY_3_0

	void SetColumn(int col) { m_col = col; }
	void SetEditCancelled() { m_editCancelled = true; }

protected:
	wxDataViewItem      m_item;
	int                 m_col;
	CDataViewModel    *m_model;
	wxVariant           m_value;
	CDataViewColumn   *m_column;
	wxPoint             m_pos;
	int                 m_cacheFrom;
	int                 m_cacheTo;
	bool                m_editCancelled;

#if wxUSE_DRAG_AND_DROP
	wxDataObject       *m_dataObject;

	wxDataFormat        m_dataFormat;
	void*               m_dataBuffer;
	size_t              m_dataSize;

	int                 m_dragFlags;
	wxDragResult        m_dropEffect;
	int                 m_proposedDropIndex;
#endif // wxUSE_DRAG_AND_DROP

private:
	// Common part of non-copy ctors.
	void Init(CDataViewCtrlBase* dvc,
		CDataViewColumn* column,
		const wxDataViewItem& item);

	wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN(CDataViewEvent);
};

// ---------------------------------------------------------
// CDataViewCtrlBase
// ---------------------------------------------------------

class CDataViewCtrlBase : public wxSystemThemedControl<wxControl>
{
public:
	CDataViewCtrlBase();
	virtual ~CDataViewCtrlBase();

	// model
	// -----

	virtual bool AssociateModel(CDataViewModel *model);
	CDataViewModel* GetModel();
	const CDataViewModel* GetModel() const;


	// column management
	// -----------------

	CDataViewColumn *PrependTextColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *PrependIconTextColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *PrependToggleColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_TOGGLE_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *PrependProgressColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *PrependDateColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_ACTIVATABLE, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *PrependBitmapColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *PrependTextColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *PrependIconTextColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *PrependToggleColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_TOGGLE_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *PrependProgressColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *PrependDateColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_ACTIVATABLE, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *PrependBitmapColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);

	CDataViewColumn *AppendTextColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendIconTextColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendToggleColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_TOGGLE_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendProgressColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendDateColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_ACTIVATABLE, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendBitmapColumn(const wxString &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendTextColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendIconTextColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendToggleColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_TOGGLE_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendProgressColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendDateColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_ACTIVATABLE, int width = -1,
		wxAlignment align = wxALIGN_NOT,
		int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendBitmapColumn(const wxBitmap &label, unsigned int model_column,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT, int width = -1,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE);

	virtual bool PrependColumn(CDataViewColumn *col);
	virtual bool InsertColumn(unsigned int pos, CDataViewColumn *col);
	virtual bool AppendColumn(CDataViewColumn *col);

	virtual unsigned int GetColumnCount() const = 0;
	virtual CDataViewColumn* GetColumn(unsigned int pos) const = 0;
	virtual int GetColumnPosition(const CDataViewColumn *column) const = 0;

	virtual bool DeleteColumn(CDataViewColumn *column) = 0;
	virtual bool ClearColumns() = 0;

	void SetExpanderColumn(CDataViewColumn *col)
	{
		m_expander_column = col; DoSetExpanderColumn();
	}
	CDataViewColumn *GetExpanderColumn() const
	{
		return m_expander_column;
	}

	virtual CDataViewColumn *GetSortingColumn() const = 0;
	virtual wxVector<CDataViewColumn *> GetSortingColumns() const
	{
		wxVector<CDataViewColumn *> columns;
		if (CDataViewColumn* col = GetSortingColumn())
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
	virtual void ToggleSortByColumn(int WXUNUSED(column)) { }


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
	wxDataViewItem GetCurrentItem() const;
	void SetCurrentItem(const wxDataViewItem& item);

	virtual wxDataViewItem GetTopItem() const { return wxDataViewItem(NULL); }
	virtual int GetCountPerPage() const { return wxNOT_FOUND; }

	// Currently focused column of the current item or NULL if no column has focus
	virtual CDataViewColumn *GetCurrentColumn() const = 0;

	// Selection: both GetSelection() and GetSelections() can be used for the
	// controls both with and without wxDV_MULTIPLE style. For single selection
	// controls GetSelections() is not very useful however. And for multi
	// selection controls GetSelection() returns an invalid item if more than
	// one item is selected. Use GetSelectedItemsCount() or HasSelection() to
	// check if any items are selected at all.
	virtual int GetSelectedItemsCount() const = 0;
	bool HasSelection() const { return GetSelectedItemsCount() != 0; }
	wxDataViewItem GetSelection() const;
	virtual int GetSelections(wxDataViewItemArray & sel) const = 0;
	virtual void SetSelections(const wxDataViewItemArray & sel) = 0;
	virtual void Select(const wxDataViewItem & item) = 0;
	virtual void Unselect(const wxDataViewItem & item) = 0;
	virtual bool IsSelected(const wxDataViewItem & item) const = 0;

	virtual void SelectAll() = 0;
	virtual void UnselectAll() = 0;

	void Expand(const wxDataViewItem & item);
	void ExpandChildren(const wxDataViewItem & item);
	void ExpandAncestors(const wxDataViewItem & item);
	virtual void Collapse(const wxDataViewItem & item) = 0;
	virtual bool IsExpanded(const wxDataViewItem & item) const = 0;

	virtual void EnsureVisible(const wxDataViewItem & item,
		const CDataViewColumn *column = NULL) = 0;
	virtual void HitTest(const wxPoint & point, wxDataViewItem &item, CDataViewColumn* &column) const = 0;
	virtual wxRect GetItemRect(const wxDataViewItem & item, const CDataViewColumn *column = NULL) const = 0;

	virtual bool SetRowHeight(int WXUNUSED(rowHeight)) { return false; }

	virtual void EditItem(const wxDataViewItem& item, const CDataViewColumn *column) = 0;

	// Use EditItem() instead
	wxDEPRECATED(void StartEditor(const wxDataViewItem& item, unsigned int column));

#if wxUSE_DRAG_AND_DROP
	virtual bool EnableDragSource(const wxDataFormat& WXUNUSED(format))
	{
		return false;
	}
	virtual bool EnableDropTarget(const wxDataFormat& WXUNUSED(format))
	{
		return false;
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

	// Just expand this item assuming it is already shown, i.e. its parent has
	// been already expanded using ExpandAncestors().
	//
	// If expandChildren is true, also expand all its children recursively.
	virtual void DoExpand(const wxDataViewItem & item, bool expandChildren) = 0;

private:
	// Implementation of the public Set/GetCurrentItem() methods which are only
	// called in multi selection case (for single selection controls their
	// implementation is trivial and is done in the base class itself).
	virtual wxDataViewItem DoGetCurrentItem() const = 0;
	virtual void DoSetCurrentItem(const wxDataViewItem& item) = 0;

	CDataViewModel        *m_model;
	CDataViewColumn       *m_expander_column;
	int m_indent;

protected:
	wxDECLARE_DYNAMIC_CLASS_NO_COPY(CDataViewCtrlBase);
};

//-----------------------------------------------------------------------------
// CDataViewListStore
//-----------------------------------------------------------------------------

class CDataViewListStoreLine
{
public:
	CDataViewListStoreLine(wxUIntPtr data = 0)
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


class CDataViewListStore : public CDataViewIndexListModel
{
public:
	CDataViewListStore();
	~CDataViewListStore();

	void PrependColumn(const wxString &varianttype);
	void InsertColumn(unsigned int pos, const wxString &varianttype);
	void AppendColumn(const wxString &varianttype);

	void AppendItem(const wxVector<wxVariant> &values, wxUIntPtr data = 0);
	void PrependItem(const wxVector<wxVariant> &values, wxUIntPtr data = 0);
	void InsertItem(unsigned int row, const wxVector<wxVariant> &values, wxUIntPtr data = 0);
	void DeleteItem(unsigned int pos);
	void DeleteAllItems();
	void ClearColumns();

	unsigned int GetItemCount() const;

	void SetItemData(const wxDataViewItem& item, wxUIntPtr data);
	wxUIntPtr GetItemData(const wxDataViewItem& item) const;

	// override base virtuals

	virtual unsigned int GetColumnCount() const wxOVERRIDE;

	virtual wxString GetColumnType(unsigned int col) const wxOVERRIDE;

	virtual void GetValueByRow(wxVariant &value,
		unsigned int row, unsigned int col) const wxOVERRIDE;

	virtual bool SetValueByRow(const wxVariant &value,
		unsigned int row, unsigned int col) wxOVERRIDE;


public:
	wxVector<CDataViewListStoreLine*> m_data;
	wxArrayString                      m_cols;
};

//-----------------------------------------------------------------------------

// ---------------------------------------------------------
// CDataViewColumn
// ---------------------------------------------------------

class CDataViewColumn : public CDataViewColumnBase
{
public:
	CDataViewColumn(const wxString& title,
		wxDataViewRenderer *renderer,
		unsigned int model_column,
		int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE)
		: CDataViewColumnBase(renderer, model_column),
		m_title(title)
	{
		Init(width, align, flags);
	}

	CDataViewColumn(const wxBitmap& bitmap,
		wxDataViewRenderer *renderer,
		unsigned int model_column,
		int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE)
		: CDataViewColumnBase(bitmap, renderer, model_column)
	{
		Init(width, align, flags);
	}

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

	virtual void SetBitmap(const wxBitmap& bitmap) wxOVERRIDE
	{
		CDataViewColumnBase::SetBitmap(bitmap);
		UpdateWidth();
	}

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

	// These methods forward to CDataViewCtrl::OnColumnChange() and
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

	friend class CDataViewHeaderWindowBase;
	friend class CDataViewHeaderWindow;
	friend class CDataViewHeaderWindowMSW;
};

// ---------------------------------------------------------
// CDataViewCtrl
// ---------------------------------------------------------

class CDataViewCtrl : public CDataViewCtrlBase,
	public wxScrollHelper
{
	friend class CDataViewMainWindow;
	friend class CDataViewHeaderWindowBase;
	friend class CDataViewHeaderWindow;
	friend class CDataViewHeaderWindowMSW;
	friend class CDataViewColumn;
#if wxUSE_ACCESSIBILITY
	friend class CDataViewCtrlAccessible;
#endif // wxUSE_ACCESSIBILITY

public:
	CDataViewCtrl() : wxScrollHelper(this)
	{
		Init();
	}

	CDataViewCtrl(wxWindow *parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxASCII_STR(wxDataViewCtrlNameStr))
		: wxScrollHelper(this)
	{
		Create(parent, id, pos, size, style, validator, name);
	}

	virtual ~CDataViewCtrl();

	void Init();

	bool Create(wxWindow *parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxASCII_STR(wxDataViewCtrlNameStr));

	virtual bool AssociateModel(CDataViewModel *model) wxOVERRIDE;

	virtual bool AppendColumn(CDataViewColumn *col) wxOVERRIDE;
	virtual bool PrependColumn(CDataViewColumn *col) wxOVERRIDE;
	virtual bool InsertColumn(unsigned int pos, CDataViewColumn *col) wxOVERRIDE;

	virtual void DoSetExpanderColumn() wxOVERRIDE;
	virtual void DoSetIndent() wxOVERRIDE;

	virtual unsigned int GetColumnCount() const wxOVERRIDE;
	virtual CDataViewColumn* GetColumn(unsigned int pos) const wxOVERRIDE;
	virtual bool DeleteColumn(CDataViewColumn *column) wxOVERRIDE;
	virtual bool ClearColumns() wxOVERRIDE;
	virtual int GetColumnPosition(const CDataViewColumn *column) const wxOVERRIDE;

	virtual CDataViewColumn *GetSortingColumn() const wxOVERRIDE;
	virtual wxVector<CDataViewColumn *> GetSortingColumns() const wxOVERRIDE;

	virtual wxDataViewItem GetTopItem() const wxOVERRIDE;
	virtual int GetCountPerPage() const wxOVERRIDE;

	virtual int GetSelectedItemsCount() const wxOVERRIDE;
	virtual int GetSelections(wxDataViewItemArray &sel) const wxOVERRIDE;
	virtual void SetSelections(const wxDataViewItemArray & sel) wxOVERRIDE;
	virtual void Select(const wxDataViewItem & item) wxOVERRIDE;
	virtual void Unselect(const wxDataViewItem & item) wxOVERRIDE;
	virtual bool IsSelected(const wxDataViewItem & item) const wxOVERRIDE;

	virtual void SelectAll() wxOVERRIDE;
	virtual void UnselectAll() wxOVERRIDE;

	virtual void EnsureVisible(const wxDataViewItem & item,
		const CDataViewColumn *column = NULL) wxOVERRIDE;
	virtual void HitTest(const wxPoint & point, wxDataViewItem & item,
		CDataViewColumn* &column) const wxOVERRIDE;
	virtual wxRect GetItemRect(const wxDataViewItem & item,
		const CDataViewColumn *column = NULL) const wxOVERRIDE;

	virtual bool SetRowHeight(int rowHeight) wxOVERRIDE;

	virtual void Collapse(const wxDataViewItem & item) wxOVERRIDE;
	virtual bool IsExpanded(const wxDataViewItem & item) const wxOVERRIDE;

	virtual void SetFocus() wxOVERRIDE;

	virtual bool SetFont(const wxFont & font) wxOVERRIDE;

#if wxUSE_ACCESSIBILITY
	virtual bool Show(bool show = true) wxOVERRIDE;
	virtual void SetName(const wxString &name) wxOVERRIDE;
	virtual bool Reparent(wxWindowBase *newParent) wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY
	virtual bool Enable(bool enable = true) wxOVERRIDE;

	virtual bool AllowMultiColumnSort(bool allow) wxOVERRIDE;
	virtual bool IsMultiColumnSortAllowed() const wxOVERRIDE { return m_allowMultiColumnSort; }
	virtual void ToggleSortByColumn(int column) wxOVERRIDE;

#if wxUSE_DRAG_AND_DROP
	virtual bool EnableDragSource(const wxDataFormat &format) wxOVERRIDE;
	virtual bool EnableDropTarget(const wxDataFormat &format) wxOVERRIDE;
#endif // wxUSE_DRAG_AND_DROP

	virtual wxBorder GetDefaultBorder() const wxOVERRIDE;

	virtual void EditItem(const wxDataViewItem& item, const CDataViewColumn *column) wxOVERRIDE;

	virtual bool SetHeaderAttr(const wxItemAttr& attr) wxOVERRIDE;

	virtual bool SetAlternateRowColour(const wxColour& colour) wxOVERRIDE;

	// This method is specific to generic CDataViewCtrl implementation and
	// should not be used in portable code.
	wxColour GetAlternateRowColour() const { return m_alternateRowColour; }

	// The returned pointer is null if the control has wxDV_NO_HEADER style.
	//
	// This method is only available in the generic versions.
	wxHeaderCtrl* GenericGetHeader() const;

protected:
	void EnsureVisibleRowCol(int row, int column);

	// Notice that row here may be invalid (i.e. >= GetRowCount()), this is not
	// an error and this function simply returns an invalid item in this case.
	wxDataViewItem GetItemByRow(unsigned int row) const;
	int GetRowByItem(const wxDataViewItem & item) const;

	// Mark the column as being used or not for sorting.
	void UseColumnForSorting(int idx);
	void DontUseColumnForSorting(int idx);

	// Return true if the given column is sorted
	bool IsColumnSorted(int idx) const;

	// Reset all columns currently used for sorting.
	void ResetAllSortColumns();

	virtual void DoEnableSystemTheme(bool enable, wxWindow* window) wxOVERRIDE;

	void OnDPIChanged(wxDPIChangedEvent& event);

public:     // utility functions not part of the API

	// returns the "best" width for the idx-th column
	unsigned int GetBestColumnWidth(int idx) const;

	// called by header window after reorder
	void ColumnMoved(CDataViewColumn* col, unsigned int new_pos);

	// update the display after a change to an individual column
	void OnColumnChange(unsigned int idx);

	// update after the column width changes due to interactive resizing
	void OnColumnResized();

	// update after the column width changes because of e.g. title or bitmap
	// change, invalidates the column best width and calls OnColumnChange()
	void OnColumnWidthChange(unsigned int idx);

	// update after a change to the number of columns
	void OnColumnsCountChanged();

	wxWindow *GetMainWindow() { return (wxWindow*)m_clientArea; }

	// return the index of the given column in m_cols
	int GetColumnIndex(const CDataViewColumn *column) const;

	// Return the index of the column having the given model index.
	int GetModelColumnIndex(unsigned int model_column) const;

	// return the column displayed at the given position in the control
	CDataViewColumn *GetColumnAt(unsigned int pos) const;

	virtual CDataViewColumn *GetCurrentColumn() const wxOVERRIDE;

	virtual void OnInternalIdle() wxOVERRIDE;

#if wxUSE_ACCESSIBILITY
	virtual wxAccessible* CreateAccessible() wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY

private:
	virtual wxDataViewItem DoGetCurrentItem() const wxOVERRIDE;
	virtual void DoSetCurrentItem(const wxDataViewItem& item) wxOVERRIDE;

	virtual void DoExpand(const wxDataViewItem& item, bool expandChildren) wxOVERRIDE;

	void InvalidateColBestWidths();
	void InvalidateColBestWidth(int idx);
	void UpdateColWidths();

	void DoClearColumns();

	wxVector<CDataViewColumn*> m_cols;
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

	CDataViewModelNotifier  *m_notifier;
	CDataViewMainWindow     *m_clientArea;
	CDataViewHeaderWindow   *m_headerArea;

	// user defined color to draw row lines, may be invalid
	wxColour m_alternateRowColour;

	// columns indices used for sorting, empty if nothing is sorted
	wxVector<int> m_sortingColumnIdxs;

	// if true, allow sorting by more than one column
	bool m_allowMultiColumnSort;

private:
	void OnSize(wxSizeEvent &event);
	virtual wxSize GetSizeAvailableForScrollTarget(const wxSize& size) wxOVERRIDE;

	// we need to return a special WM_GETDLGCODE value to process just the
	// arrows but let the other navigation characters through
#ifdef __WXMSW__
	virtual WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) wxOVERRIDE;
#endif // __WXMSW__

	WX_FORWARD_TO_SCROLL_HELPER()

private:
	wxDECLARE_DYNAMIC_CLASS(CDataViewCtrl);
	wxDECLARE_NO_COPY_CLASS(CDataViewCtrl);
	wxDECLARE_EVENT_TABLE();
};

#if wxUSE_ACCESSIBILITY

class CDataViewListCtrl : public CDataViewCtrl
{
public:
	CDataViewListCtrl();
	CDataViewListCtrl(wxWindow *parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxDV_ROW_LINES,
		const wxValidator& validator = wxDefaultValidator);
	~CDataViewListCtrl();

	bool Create(wxWindow *parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxDV_ROW_LINES,
		const wxValidator& validator = wxDefaultValidator);

	CDataViewListStore *GetStore()
	{
		return (CDataViewListStore*)GetModel();
	}
	const CDataViewListStore *GetStore() const
	{
		return (const CDataViewListStore*)GetModel();
	}

	int ItemToRow(const wxDataViewItem &item) const
	{
		return item.IsOk() ? (int)GetStore()->GetRow(item) : wxNOT_FOUND;
	}
	wxDataViewItem RowToItem(int row) const
	{
		return row == wxNOT_FOUND ? wxDataViewItem() : GetStore()->GetItem(row);
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

	bool AppendColumn(CDataViewColumn *column, const wxString &varianttype);
	bool PrependColumn(CDataViewColumn *column, const wxString &varianttype);
	bool InsertColumn(unsigned int pos, CDataViewColumn *column, const wxString &varianttype);

	// overridden from base class
	virtual bool PrependColumn(CDataViewColumn *col) wxOVERRIDE;
	virtual bool InsertColumn(unsigned int pos, CDataViewColumn *col) wxOVERRIDE;
	virtual bool AppendColumn(CDataViewColumn *col) wxOVERRIDE;
	virtual bool ClearColumns() wxOVERRIDE;

	CDataViewColumn *AppendTextColumn(const wxString &label,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int width = -1, wxAlignment align = wxALIGN_LEFT, int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendToggleColumn(const wxString &label,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_ACTIVATABLE,
		int width = -1, wxAlignment align = wxALIGN_LEFT, int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendProgressColumn(const wxString &label,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int width = -1, wxAlignment align = wxALIGN_LEFT, int flags = wxDATAVIEW_COL_RESIZABLE);
	CDataViewColumn *AppendIconTextColumn(const wxString &label,
		wxDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int width = -1, wxAlignment align = wxALIGN_LEFT, int flags = wxDATAVIEW_COL_RESIZABLE);

	void AppendItem(const wxVector<wxVariant> &values, wxUIntPtr data = 0)
	{
		GetStore()->AppendItem(values, data);
	}
	void PrependItem(const wxVector<wxVariant> &values, wxUIntPtr data = 0)
	{
		GetStore()->PrependItem(values, data);
	}
	void InsertItem(unsigned int row, const wxVector<wxVariant> &values, wxUIntPtr data = 0)
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

	void SetValue(const wxVariant &value, unsigned int row, unsigned int col)
	{
		GetStore()->SetValueByRow(value, row, col);
		GetStore()->RowValueChanged(row, col);
	}
	void GetValue(wxVariant &value, unsigned int row, unsigned int col)
	{
		GetStore()->GetValueByRow(value, row, col);
	}

	void SetTextValue(const wxString &value, unsigned int row, unsigned int col)
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

	void SetItemData(const wxDataViewItem& item, wxUIntPtr data)
	{
		GetStore()->SetItemData(item, data);
	}
	wxUIntPtr GetItemData(const wxDataViewItem& item) const
	{
		return GetStore()->GetItemData(item);
	}

	int GetItemCount() const
	{
		return GetStore()->GetItemCount();
	}

	void OnSize(wxSizeEvent &event);

private:
	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN(CDataViewListCtrl);
};

//-----------------------------------------------------------------------------
// CDataViewTreeStore
//-----------------------------------------------------------------------------

class CDataViewTreeStoreNode
{
public:
	CDataViewTreeStoreNode(CDataViewTreeStoreNode *parent,
		const wxString &text, const wxIcon &icon = wxNullIcon, wxClientData *data = NULL);
	virtual ~CDataViewTreeStoreNode();

	void SetText(const wxString &text)
	{
		m_text = text;
	}
	wxString GetText() const
	{
		return m_text;
	}
	void SetIcon(const wxIcon &icon)
	{
		m_icon = icon;
	}
	const wxIcon &GetIcon() const
	{
		return m_icon;
	}
	void SetData(wxClientData *data)
	{
		delete m_data; m_data = data;
	}
	wxClientData *GetData() const
	{
		return m_data;
	}

	wxDataViewItem GetItem() const
	{
		return wxDataViewItem(const_cast<void*>(static_cast<const void*>(this)));
	}

	virtual bool IsContainer()
	{
		return false;
	}

	CDataViewTreeStoreNode *GetParent()
	{
		return m_parent;
	}

private:
	CDataViewTreeStoreNode  *m_parent;
	wxString                  m_text;
	wxIcon                    m_icon;
	wxClientData             *m_data;
};

typedef wxVector<CDataViewTreeStoreNode*> CDataViewTreeStoreNodes;

class CDataViewTreeStoreContainerNode : public CDataViewTreeStoreNode
{
public:
	CDataViewTreeStoreContainerNode(CDataViewTreeStoreNode *parent,
		const wxString &text, const wxIcon &icon = wxNullIcon, const wxIcon &expanded = wxNullIcon,
		wxClientData *data = NULL);
	virtual ~CDataViewTreeStoreContainerNode();

	const CDataViewTreeStoreNodes &GetChildren() const
	{
		return m_children;
	}
	CDataViewTreeStoreNodes &GetChildren()
	{
		return m_children;
	}

	CDataViewTreeStoreNodes::iterator FindChild(CDataViewTreeStoreNode* node);

	void SetExpandedIcon(const wxIcon &icon)
	{
		m_iconExpanded = icon;
	}
	const wxIcon &GetExpandedIcon() const
	{
		return m_iconExpanded;
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
	CDataViewTreeStoreNodes     m_children;
	wxIcon                       m_iconExpanded;
	bool                         m_isExpanded;
};

//-----------------------------------------------------------------------------

class CDataViewTreeStore : public CDataViewModel
{
public:
	CDataViewTreeStore();
	~CDataViewTreeStore();

	wxDataViewItem AppendItem(const wxDataViewItem& parent,
		const wxString &text, const wxIcon &icon = wxNullIcon, wxClientData *data = NULL);
	wxDataViewItem PrependItem(const wxDataViewItem& parent,
		const wxString &text, const wxIcon &icon = wxNullIcon, wxClientData *data = NULL);
	wxDataViewItem InsertItem(const wxDataViewItem& parent, const wxDataViewItem& previous,
		const wxString &text, const wxIcon &icon = wxNullIcon, wxClientData *data = NULL);

	wxDataViewItem PrependContainer(const wxDataViewItem& parent,
		const wxString &text, const wxIcon &icon = wxNullIcon, const wxIcon &expanded = wxNullIcon,
		wxClientData *data = NULL);
	wxDataViewItem AppendContainer(const wxDataViewItem& parent,
		const wxString &text, const wxIcon &icon = wxNullIcon, const wxIcon &expanded = wxNullIcon,
		wxClientData *data = NULL);
	wxDataViewItem InsertContainer(const wxDataViewItem& parent, const wxDataViewItem& previous,
		const wxString &text, const wxIcon &icon = wxNullIcon, const wxIcon &expanded = wxNullIcon,
		wxClientData *data = NULL);

	wxDataViewItem GetNthChild(const wxDataViewItem& parent, unsigned int pos) const;
	int GetChildCount(const wxDataViewItem& parent) const;

	void SetItemText(const wxDataViewItem& item, const wxString &text);
	wxString GetItemText(const wxDataViewItem& item) const;
	void SetItemIcon(const wxDataViewItem& item, const wxIcon &icon);
	const wxIcon &GetItemIcon(const wxDataViewItem& item) const;
	void SetItemExpandedIcon(const wxDataViewItem& item, const wxIcon &icon);
	const wxIcon &GetItemExpandedIcon(const wxDataViewItem& item) const;
	void SetItemData(const wxDataViewItem& item, wxClientData *data);
	wxClientData *GetItemData(const wxDataViewItem& item) const;

	void DeleteItem(const wxDataViewItem& item);
	void DeleteChildren(const wxDataViewItem& item);
	void DeleteAllItems();

	// implement base methods

	virtual void GetValue(wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) const wxOVERRIDE;
	virtual bool SetValue(const wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) wxOVERRIDE;
	virtual wxDataViewItem GetParent(const wxDataViewItem &item) const wxOVERRIDE;
	virtual bool IsContainer(const wxDataViewItem &item) const wxOVERRIDE;
	virtual unsigned int GetChildren(const wxDataViewItem &item, wxDataViewItemArray &children) const wxOVERRIDE;

	virtual int Compare(const wxDataViewItem &item1, const wxDataViewItem &item2,
		unsigned int column, bool ascending) const wxOVERRIDE;

	virtual bool HasDefaultCompare() const wxOVERRIDE
	{
		return true;
	}
	virtual unsigned int GetColumnCount() const wxOVERRIDE
	{
		return 1;
	}
	virtual wxString GetColumnType(unsigned int WXUNUSED(col)) const wxOVERRIDE
	{
		return wxT("wxDataViewIconText");
	}

	CDataViewTreeStoreNode *FindNode(const wxDataViewItem &item) const;
	CDataViewTreeStoreContainerNode *FindContainerNode(const wxDataViewItem &item) const;
	CDataViewTreeStoreNode *GetRoot() const { return m_root; }

public:
	CDataViewTreeStoreNode *m_root;
};

//-----------------------------------------------------------------------------

class CDataViewTreeCtrl : public CDataViewCtrl,
	public wxWithImages
{
public:
	CDataViewTreeCtrl() { }
	CDataViewTreeCtrl(wxWindow *parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDV_NO_HEADER | wxDV_ROW_LINES,
		const wxValidator& validator = wxDefaultValidator)
	{
		Create(parent, id, pos, size, style, validator);
	}

	bool Create(wxWindow *parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDV_NO_HEADER | wxDV_ROW_LINES,
		const wxValidator& validator = wxDefaultValidator);

	CDataViewTreeStore *GetStore()
	{
		return (CDataViewTreeStore*)GetModel();
	}
	const CDataViewTreeStore *GetStore() const
	{
		return (const CDataViewTreeStore*)GetModel();
	}

	bool IsContainer(const wxDataViewItem& item) const
	{
		return GetStore()->IsContainer(item);
	}

	wxDataViewItem AppendItem(const wxDataViewItem& parent,
		const wxString &text, int icon = NO_IMAGE, wxClientData *data = NULL);
	wxDataViewItem PrependItem(const wxDataViewItem& parent,
		const wxString &text, int icon = NO_IMAGE, wxClientData *data = NULL);
	wxDataViewItem InsertItem(const wxDataViewItem& parent, const wxDataViewItem& previous,
		const wxString &text, int icon = NO_IMAGE, wxClientData *data = NULL);

	wxDataViewItem PrependContainer(const wxDataViewItem& parent,
		const wxString &text, int icon = NO_IMAGE, int expanded = NO_IMAGE,
		wxClientData *data = NULL);
	wxDataViewItem AppendContainer(const wxDataViewItem& parent,
		const wxString &text, int icon = NO_IMAGE, int expanded = NO_IMAGE,
		wxClientData *data = NULL);
	wxDataViewItem InsertContainer(const wxDataViewItem& parent, const wxDataViewItem& previous,
		const wxString &text, int icon = NO_IMAGE, int expanded = NO_IMAGE,
		wxClientData *data = NULL);

	wxDataViewItem GetNthChild(const wxDataViewItem& parent, unsigned int pos) const
	{
		return GetStore()->GetNthChild(parent, pos);
	}
	int GetChildCount(const wxDataViewItem& parent) const
	{
		return GetStore()->GetChildCount(parent);
	}

	void SetItemText(const wxDataViewItem& item, const wxString &text);
	wxString GetItemText(const wxDataViewItem& item) const
	{
		return GetStore()->GetItemText(item);
	}
	void SetItemIcon(const wxDataViewItem& item, const wxIcon &icon);
	const wxIcon &GetItemIcon(const wxDataViewItem& item) const
	{
		return GetStore()->GetItemIcon(item);
	}
	void SetItemExpandedIcon(const wxDataViewItem& item, const wxIcon &icon);
	const wxIcon &GetItemExpandedIcon(const wxDataViewItem& item) const
	{
		return GetStore()->GetItemExpandedIcon(item);
	}
	void SetItemData(const wxDataViewItem& item, wxClientData *data)
	{
		GetStore()->SetItemData(item, data);
	}
	wxClientData *GetItemData(const wxDataViewItem& item) const
	{
		return GetStore()->GetItemData(item);
	}

	void DeleteItem(const wxDataViewItem& item);
	void DeleteChildren(const wxDataViewItem& item);
	void DeleteAllItems();

	void OnExpanded(CDataViewEvent &event);
	void OnCollapsed(CDataViewEvent &event);
	void OnSize(wxSizeEvent &event);

private:
	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN(CDataViewTreeCtrl);
};

//-----------------------------------------------------------------------------
// CDataViewCtrlAccessible
//-----------------------------------------------------------------------------

class CDataViewCtrlAccessible : public wxWindowAccessible
{
public:
	CDataViewCtrlAccessible(CDataViewCtrl* win);
	virtual ~CDataViewCtrlAccessible() {}

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

#endif // wxUSE_DATAVIEWCTRL

#endif
	// _WX_DATAVIEW_H_BASE_
