#ifndef __TABLE_INFO_H__
#define __TABLE_INFO_H__

#include <algorithm>
#include <functional>
#include <future>

#include "backend/tableView.h"

#include "backend/system/value/valueType.h"

#include "backend/actionInfo.h"
#include "backend/srcObject.h"

// L3 Selector tree (folded from a flat snapshot) — mirrored into the RAM tree model by
// ibValueModelRamTreeBase::PopulateFromTree. Full type only needed in tableInfo.cpp.
class ibSelectorTree;

///////////////////////////////////////////////////////////////////////////////////
#define defaultCountPerPage 100
///////////////////////////////////////////////////////////////////////////////////

// ibComparisonType / ibFilterRow / ibSortOrder / ibSortData / ibSortModel
// moved to tableView.h — view-layer types used by both ibDataViewModel
// virtuals and paged Fetch SQL builders.

// =============================================================================
// Paged fetch contract — see docs/paging-design.md
// =============================================================================

enum class ibFetchDirection : int8_t {
	Reset    = 0,   // discard buffer, fetch initial window at anchor
	Forward  = 1,   // append after anchor
	Backward = -1   // prepend before anchor
};

// Stable, opaque cursor for pagination. Templated by row-key type:
//   TKey = ibGuid           — catalogs, enums, folders.
//   TKey = ibUniqueKeyPair  — registers.
template <class TKey>
struct ibFetchAnchor {
	bool                 m_empty = true;     // true = no anchor, fetch top
	TKey                 m_key{};            // last row's stable key
	std::vector<ibValue> m_sortValues;       // values of sort columns at that row
};

// Pagination request the frontend / buffer hands to the concrete model.
// Filter / sort snapshots are read directly from the model's own
// m_filterRow / m_sortOrder members at Fetch time — request carries
// only anchor + direction + batch.
template <class TKey>
struct ibFetchRequest {
	ibFetchAnchor<TKey>  m_anchor;
	ibFetchDirection     m_direction = ibFetchDirection::Reset;
	int                  m_count     = 0;
};

// Response from the concrete model's typed Fetch.
template <class TKey, class TRow>
struct ibFetchResponse {
	std::vector<TRow*>   m_rows;
	bool                 m_hasMore = false;
};

// AnchorOf builds an ibFetchAnchor (key + sort values) from a row.
// Each paged concrete model exposes one as a public helper so the
// frontend (control deque) can construct cursor parameters when it
// dispatches GetNextFetch / GetPrevFetch on its own.
template <class TKey, class TRow>
using ibAnchorOfFn = std::function<ibFetchAnchor<TKey>(const TRow*)>;

#pragma region _data_model_h_
class BACKEND_API ibDataViewModelProvider : public ibDataViewModel {
public:
	virtual ~ibDataViewModelProvider() {}
	virtual class ibValueModel* GetOwnerValueModel() const = 0;
};
#pragma endregion 

class ibVariantDataValue :
	public wxVariantData {
public:
protected:
	ibVariantDataValue() : wxVariantData() {}
};

//Common entity for tables, list, table trees 
class BACKEND_API ibValueModel : public ibValueDynamicMembers,
	public ibActionDataObject, public ibTabularObject {
	public:

	template <typename T>
	class ibVariantDataValueImpl :
		public ibVariantDataValue {

	public:

		ibVariantDataValueImpl(T&& cValue)
			:
			m_cValue(cValue)
		{
		}

		virtual bool Eq(wxVariantData& data) const {
			ibVariantDataValueImpl* srcData = dynamic_cast<ibVariantDataValueImpl*>(&data);
			if (srcData != nullptr)
				return m_cValue == srcData->m_cValue;
			return false;
		}

#if wxUSE_STD_IOSTREAM
		virtual bool Write(wxSTD ostream& str) const {
			str << m_cValue.GetString();
			return true;
		}
#endif
		virtual bool Write(wxString& str) const {
			str = m_cValue.GetString();
			return true;
		}

		virtual wxString GetType() const {
			if (m_cValue.GetType() == ibValueTypes::TYPE_BOOLEAN)
				return wxT("bool");
			else if (m_cValue.GetType() == ibValueTypes::TYPE_NUMBER)
				return wxT("number");
			else if (m_cValue.GetType() == ibValueTypes::TYPE_DATE)
				return wxT("date");
			else if (m_cValue.GetType() == ibValueTypes::TYPE_STRING)
				return wxT("string");
			else if (m_cValue.GetType() == ibValueTypes::TYPE_VALUE)
				return wxT("value");
			else if (m_cValue.GetType() == ibValueTypes::TYPE_ENUM)
				return wxT("enum");
			else if (m_cValue.GetType() == ibValueTypes::TYPE_OLE)
				return wxT("ole");
			return wxT("string");
		}

	private:

		T m_cValue;
	};

protected:

	//def actionData
	enum Func {
		eAddValue = 1,
		eCopyValue,
		eEditValue,
		eDeleteValue,
		eFilter,
		eFilterByColumn,
		eFilterClear,
		eViewMode,
	};

#pragma region _data_model_h_

	class BACKEND_API ibDataViewModelProviderImpl :
		public ibDataViewModelProvider {
	public:

		ibDataViewModelProviderImpl(ibValueModel* owner) : ibDataViewModelProvider(), m_ownerModel(owner) {}

		virtual ibValueModel* GetOwnerValueModel() const { return m_ownerModel; }

		// get value into a wxVariant
		virtual void GetValue(wxVariant& variant,
			const ibDataViewItem& item, unsigned int col) const {
			return m_ownerModel->GetValue(variant, item, col);
		}

		// return true if the given item has a value to display in the given
		// column: this is always true except for container items which by default
		// only show their label in the first column (but see HasContainerColumns())
		virtual bool HasValue(const ibDataViewItem& item, unsigned col) const {
			return m_ownerModel->HasValue(item, col);
		}

		// usually ValueChanged() should be called after changing the value in the
		// model to update the control, ChangeValue() does it on its own while
		// SetValue() does not -- so while you will override SetValue(), you should
		// be usually calling ChangeValue()
		virtual bool SetValue(const wxVariant& variant,
			const ibDataViewItem& item,
			unsigned int col) {
			return m_ownerModel->SetValue(variant, item, col);
		}

		// Get text attribute, return false of default attributes should be used
		virtual bool GetAttr(const ibDataViewItem& item,
			unsigned int col,
			ibDataViewItemAttr& attr) const {
			return m_ownerModel->GetAttr(item, col, attr);
		}

		// Override this if you want to disable specific items
		virtual bool IsEnabled(const ibDataViewItem& item,
			unsigned int col) const {
			return m_ownerModel->IsEnabled(item, col);
		}

		// define hierarchy
		virtual ibDataViewItem GetParent(const ibDataViewItem& item) const {
			return m_ownerModel->GetParent(item);
		}

		virtual bool IsContainer(const ibDataViewItem& item) const {
			return m_ownerModel->IsContainer(item);
		}

		// Is the container just a header or an item with all columns
		virtual bool HasContainerColumns(const ibDataViewItem& item) const {
			return m_ownerModel->HasContainerColumns(item);
		}

		// GetFirstFetch delegate lives further down with its Next/Prev
		// siblings — keep all three forwarders together so the paged
		// contract reads as one block.

		// default compare function
		virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
			unsigned int column, bool ascending) const {
			return m_ownerModel->Compare(item1, item2, column, ascending);
		}

		virtual bool HasDefaultCompare() const { return false; }

		// Header-click sort change — forward to ibValueModel so paged
		// concrete models can update m_sortOrder and trigger refresh.
		virtual void OnSortColumnChanged(unsigned int col, bool ascending) override {
			m_ownerModel->OnSortColumnChanged(col, ascending);
		}

		// Universal Get*Fetch — forward to owner so concrete model's
		// override drives the paged source (DB or RAM).
		virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
			const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override {
			return m_ownerModel->GetFirstFetch(parent, anchor, count, out);
		}
		virtual unsigned int GetNextFetch(const ibDataViewItem& parent,
			const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override {
			return m_ownerModel->GetNextFetch(parent, anchor, count, out);
		}
		virtual unsigned int GetPrevFetch(const ibDataViewItem& parent,
			const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override {
			return m_ownerModel->GetPrevFetch(parent, anchor, count, out);
		}

		// All ibValueModel-derived models are paged by architecture;
		// the flag distinguishes them from non-paged wxDVC native
		// models (lists / tree-stores / predefined editor).
		virtual bool IsPagedModel() const override            { return true; }

		// Forward to ibValueModel — concrete models filter system
		// sorts in their override (see ibValueModel::IsSortable).
		virtual bool IsSortable(unsigned int col) const override {
			return m_ownerModel->IsSortable(col);
		}

		// Forward to ibValueModel which delegates to ibSession::Submit
		// (worker pool).  Future return value is discarded — fetch
		// completion is communicated via the lambda body itself
		// (typically via wxWindow::CallAfter back to the UI thread).
		virtual void SubmitFetchAsync(std::function<void()> work) override {
			m_ownerModel->SubmitFetchAsync(std::move(work));
		}

		// Capability + state forwarders — ibDataViewModel virtuals
		// resolve to the owning ibValueModel's storage.  Lets
		// datavgen.cpp ask GetModel()->GetFeatures() / GetSortOrder()
		// directly without cross-casting through this provider.
		virtual ibDataViewModel::Features GetFeatures() const override {
			return m_ownerModel->GetFeatures();
		}
		virtual ibSortOrder*       GetSortOrder()       override { return &m_ownerModel->GetSortOrder(); }
		virtual const ibSortOrder* GetSortOrder() const override { return &m_ownerModel->GetSortOrder(); }
		virtual ibFilterRow*       GetFilterRow()       override { return &m_ownerModel->GetFilterRow(); }
		virtual const ibFilterRow* GetFilterRow() const override { return &m_ownerModel->GetFilterRow(); }

		virtual void BuildAncestorBreadcrumb(const ibDataViewItem& fromRow,
		                                     ibDataViewItemArray& out) const override {
			m_ownerModel->BuildAncestorBreadcrumb(fromRow, out);
		}

		// internal
		virtual bool IsListModel() const {
			return m_ownerModel->IsListModel();
		}

		virtual bool IsVirtualListModel() const {
			return m_ownerModel->IsVirtualListModel();
		}

	private:

		ibValueModel* m_ownerModel;
	};

	ibDataViewModelProviderImpl* m_modelProvider;

#pragma endregion 

	class ibVariantDataValueModel :
		public ibVariantDataValueImpl<const ibValue&> {
	public:
		ibVariantDataValueModel(const ibValue& v) :
			ibVariantDataValueImpl(v)
		{
		}
	};

public:

	class BACKEND_API ibValueModelColumnCollection : public ibValueDynamicMembers {
	public:
		class ibValueModelColumnInfo : public ibValueDynamicMembers {
	public:

			virtual unsigned int GetColumnID() const = 0;
			virtual void SetColumnID(unsigned int col) {}

			virtual wxString GetColumnName() const = 0;
			virtual void SetColumnName(const wxString& name) {}

			virtual wxString GetColumnCaption() const = 0;
			virtual void SetColumnCaption(const wxString& caption) {}

			virtual const ibTypeDescription GetColumnType() const = 0;
			virtual void SetColumnType(const ibTypeDescription& typeData) {}

			virtual int GetColumnWidth() const { return wxDVC_DEFAULT_WIDTH; }

			virtual void SetColumnWidth(int width) {};

			ibValueModelColumnInfo();
			virtual ~ibValueModelColumnInfo();

			void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
			virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);
		};
	public:

		virtual ibValueModelColumnInfo* AddColumn(const wxString& colName,
			const ibTypeDescription& typeData,
			const wxString& caption,
			int width = wxDVC_DEFAULT_WIDTH) {
			return nullptr;
		};

		virtual void RemoveColumn(unsigned int col) {};
		virtual bool HasColumnID(unsigned int col) const {
			return GetColumnByID(col) != nullptr;
		}

		virtual ibValueModelColumnInfo* GetColumnByID(unsigned int col) const;
		virtual ibValueModelColumnInfo* GetColumnByName(const wxString& colName) const;

		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const = 0;
		virtual unsigned int GetColumnCount() const = 0;

		ibValueModelColumnCollection() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true) {}
		virtual ~ibValueModelColumnCollection() {}

		//Working with iterators
		virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override {
			class State : public ibValueIteratorState {
			public:
				explicit State(ibValueModelColumnCollection* host) : m_host(host) {}
				bool MoveNext(ibValue& current) override {
					if (m_started) ++m_pos; else m_started = true;
					if (m_pos >= m_host->GetColumnCount()) return false;
					ibValueModelColumnInfo* info = m_host->GetColumnInfo(m_pos);
					if (info == nullptr) { current = ibValue(); return true; }
					current = ibValue(static_cast<ibValue*>(info));
					return true;
				}
				void Reset() override { m_pos = 0; m_started = false; }
			private:
				ibValueModelColumnCollection* m_host;
				unsigned int m_pos = 0;
				bool m_started = false;
			};
			return std::make_shared<State>(this);
		}
	};

	class BACKEND_API ibValueModelReturnLine : public ibValueDynamicMembers {
	public:

		ibDataViewItem GetLineItem() const { return m_lineItem; };

		ibValueModelReturnLine(const ibDataViewItem& lineItem) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), m_lineItem(lineItem) {
			wxRefCounter* refCounter = static_cast<wxRefCounter*>(m_lineItem.GetID());
			if (refCounter != nullptr)
				refCounter->IncRef();
		}
		virtual ~ibValueModelReturnLine() {
			wxRefCounter* refCounter = static_cast<wxRefCounter*>(m_lineItem.GetID());
			if (refCounter != nullptr)
				refCounter->DecRef();
		}

		// True iff the cached line still refers to a row attached to
		// its owner model.  ReturnLine may outlive the row's place
		// in the model (script holds the line, model Clear/Remove
		// detaches the row) — read/write through a detached line
		// must fail safely instead of poking at a dead model link.
		bool IsLineAttached() const {
			auto* obj = m_lineItem.GetID();
			return obj != nullptr && obj->IsAttached();
		}

		virtual bool IsPropReadable(const long lPropNum) const override { return IsLineAttached(); }
		virtual bool IsPropWritable(const long lPropNum) const override { return IsLineAttached(); }

		virtual ibValueModel* GetOwnerModel() const = 0;

		//set meta/get meta
		virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) {
			if (!IsLineAttached()) return false;
			return GetOwnerModel()->SetValueByMetaID(m_lineItem, id, varMetaVal);
		}

		virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const {
			return GetOwnerModel()->GetValueByMetaID(m_lineItem, id, pvarMetaVal);
		}

		//operator '=='
		virtual bool CompareValueEQ(const ibValue& cParam) const override {
			ibValueModelReturnLine* tableReturnLine = nullptr;
			if (cParam.ConvertToValue(tableReturnLine)) {
				if (GetOwnerModel() == tableReturnLine->GetOwnerModel()
					&& m_lineItem == tableReturnLine->GetLineItem()) {
					return true;
				}
			}
			return false;
		}

		//operator '!='
		virtual bool CompareValueNE(const ibValue& cParam) const override {
			ibValueModelReturnLine* tableReturnLine = nullptr;
			if (cParam.ConvertToValue(tableReturnLine)) {
				if (GetOwnerModel() != tableReturnLine->GetOwnerModel()
					|| m_lineItem != tableReturnLine->GetLineItem()) {
					return false;
				}
				return true;
			}
			return false;
		}

	protected:
		ibDataViewItem m_lineItem;
	};

public:

	template <class retType>
	inline retType* GetViewData(const ibDataViewItem& item) const {
		if (!item.IsOk())
			return nullptr;
		try {
			return static_cast<retType*>(item.GetID());
		}
		catch (...) {
			return nullptr;
		}
	}

	void ResetSort() {
		for (auto& sort : m_sortOrder.m_sorts) {
			if (!sort.m_sortSystem)
				sort.m_sortEnable = false;
		}
	}

	ibSortOrder::ibSortData* GetSortByID(unsigned int col) const {
		return m_sortOrder.GetSortByID(col);
	}

	// Re-export the canonical Features type from ibDataViewModel so
	// concrete ibValueModel-derived classes can keep saying `Features`
	// in their override declarations (these classes inherit
	// ibValueModel, not ibDataViewModel — only the provider impl
	// bridges to the data-view side).
	using Features = ibDataViewModel::Features;

	// Concrete models advertise their feature set here; the provider
	// impl forwards to ibDataViewModel::GetFeatures() so the GUI sees
	// the same value via either path.
	virtual Features GetFeatures() const { return Features{}; }

	// Script-side iteration. Any flat paged model (RamFetch or DbFetch
	// without Tree) auto-exposes a cursor that drives Get*Fetch in
	// batches. RAM-paged children inherit filter+sort consistency with
	// the GUI for free (their Get*Fetch slices BuildVisibleView). DB-
	// paged lists (Catalog / Document / Register) become iterable
	// without any per-class override. Tree models fall through to the
	// base default (no iteration) — flat-walk over a tree would
	// surprise users.
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;

	// IntelliSense type-hint factory — returns an empty (skeleton) row
	// of the model's row type so the editor's static parser can offer
	// completions on `For Each x In model`. Default is empty (no hint).
	virtual ibValue GetEmptyRow() { return ibValue(); }

	// Mutable sort accessor — the GUI uses this to insert / toggle
	// system sort entries (e.g. folder-first prefix on Tree /
	// Hierarchical view modes) without backdooring through protected
	// state.  Read-only callers go through the const overload.  Both
	// are also reachable through ibDataViewModel via the provider's
	// pointer-returning forwarders.
	ibSortOrder&       GetSortOrder()       { return m_sortOrder; }
	const ibSortOrder& GetSortOrder() const { return m_sortOrder; }
	ibFilterRow&       GetFilterRow()       { return m_filterRow; }
	const ibFilterRow& GetFilterRow() const { return m_filterRow; }

	ibValueModel();
	virtual ~ibValueModel();

	//////////////////////////////////////////////////////////////////////////////////////////////////

	// Refetch the entire current scope — wipes the control's loaded
	// buffer (BeforeReset notifier) and signals it to dispatch a fresh
	// GetFirstFetch (AfterReset notifier).  The concrete fetch shape
	// is determined by the control's view-mode + drill state at the
	// time of the next fetch dispatch (top-level, scoped to a folder,
	// or s_constIgnoreParent flat scan).  All models drive refresh
	// through this single path; legacy CallRefreshModel / RefreshModel
	// are gone.
	void RefetchAll() {
		BumpViewGeneration();   // filter change / explicit refetch -> the RAM visible-view rebuilds
		if (m_modelProvider != nullptr) {
			m_modelProvider->BeforeReset();
			m_modelProvider->AfterReset();
		}
	}

	// Column header click — rebuild the result set with new ORDER BY.
	// Updates m_sortOrder (single-column sort, disabling other user
	// sorts) and signals the control via the notifier so it wipes
	// its deque and re-dispatches GetFirstFetch.
	virtual void OnSortColumnChanged(unsigned int col, bool ascending) {
		for (auto& s : m_sortOrder.m_sorts) {
			if (!s.m_sortSystem) s.m_sortEnable = false;
		}
		if (auto* sd = m_sortOrder.GetSortByID(col)) {
			sd->m_sortAscending = ascending;
			sd->m_sortEnable    = true;
		}
		BumpViewGeneration();   // sort change -> the RAM visible-view rebuilds
		if (m_modelProvider != nullptr) {
			m_modelProvider->BeforeReset();
			m_modelProvider->AfterReset();
		}
	}

	// Hierarchical-breadcrumb hook (see ibDataViewModel virtual for
	// semantics).  Mirrored here so concrete ibValueModel-derived
	// models can override directly without going through the provider.
	virtual void BuildAncestorBreadcrumb(const ibDataViewItem& fromRow,
	                                     ibDataViewItemArray& out) const {
		(void)fromRow; (void)out;
	}

	// Universal paged-fetch API.  Mirrors the ibDataViewModel virtuals
	// so concrete models override here directly.
	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const {
		(void)parent; (void)anchor; (void)count; (void)out;
		return 0;
	}
	virtual unsigned int GetNextFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const {
		(void)parent; (void)anchor; (void)count; (void)out;
		return 0;
	}
	virtual unsigned int GetPrevFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const {
		(void)parent; (void)anchor; (void)count; (void)out;
		return 0;
	}

#pragma region _data_model_h_
	ibDataViewModelProviderImpl* GetDataViewModel() const { return m_modelProvider; }
#pragma endregion 

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual ibDataViewItem GetSelection() const;
	// Hierarchical drill-context — empty when not drilled / non-hierarchical
	// view; otherwise the deepest crumb (the folder the user is inside).
	// Used by AddValue paths to inherit parent for new items.
	virtual ibDataViewItem GetDrillParent() const;
	virtual void RowValueStartEdit(const ibDataViewItem& item, unsigned int col = 0);

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Paging hooks — backend is stateless: model exposes Get*Fetch
	// only.  The GUI keeps its own deque inside ibDataViewCtrl and
	// dispatches GetNextFetch / GetPrevFetch when scroll approaches
	// an edge.  Every ibValueModel-derived model is paged by
	// architecture; the IsPagedModel() flag on the data-view base
	// only differentiates ibValueModel-derived (paged) from native
	// non-paged wxDVC models (lists, tree-stores, predefined editor).

	// Submit a Fetch-style task to the current session's worker pool.
	// Returns the future the pool gave us; if no session is bound the
	// task runs inline and the returned future is already ready (matches
	// ibSession::Submit's no-pool fallback contract). Caller owns the
	// UI marshaling decision: future.wait() inline, wxTheApp->CallAfter
	// on completion, or fire-and-forget. Backend itself stays free of
	// frontend coupling.
	std::future<void> SubmitFetchAsync(std::function<void()> work);

	// Adopt rows returned by a typed Get*Fetch into ibDataViewItemArray.
	// Typed Fetch hands out new'd rows with refcount=1; ibDataViewItem's
	// ctor IncRefs to 2; we DecRef the initial allocation reference so
	// `out` owns exactly one reference per row. Use this at every
	// typed → universal Get*Fetch bridge to avoid leaking the initial
	// reference on long-running paged use.
	template <class TRow>
	static void AdoptRowsToItems(std::vector<TRow*>& rows,
		ibDataViewItemArray& out) {
		for (auto* r : rows) {
			out.Add(ibDataViewItem(r));
			r->DecRef();
		}
		rows.clear();
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const { return ibDataViewItem(nullptr); }

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool AutoCreateColumn() const { return false; }

	virtual bool UseStandartCommand() const { return true; }
	virtual bool UseFilter() const { return m_filterRow.UseFilter(); }
	virtual bool UseViewMode() const { return !IsListModel(); }

	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const { return true; }

	virtual void ActivateItem(ibBackendValueForm* formOwner,
		const ibDataViewItem& item, unsigned int col) {
		ibValueModel::RowValueStartEdit(item, col);
	}

	// User-toggleable iff the model carries a non-system sort entry
	// for this column.  System sorts (folders-on-top, recorder+line
	// for registers, …) are model-driven and must not be flipped by
	// header click.  Mirrored on ibDataViewModel via the provider impl
	// so the data-view fork can consult it directly (datavgen.cpp).
	virtual bool IsSortable(unsigned int col) const {
		ibSortOrder::ibSortData* sortData = m_sortOrder.GetSortByID(col);
		return sortData != nullptr && !sortData->m_sortSystem;
	}

	virtual void AddValue(unsigned int before = 0) {}
	virtual void CopyValue() {}
	virtual void EditValue() {}
	virtual void DeleteValue() {}

	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) = 0;
	virtual ibValueModelColumnCollection* GetColumnCollection() const = 0;

	//set meta/get meta
	virtual ibMetaID GetColumnIDByName(const wxString colName) const {
		ibValueModelColumnCollection* colCollection = GetColumnCollection();
		if (colCollection == nullptr)
			return wxNOT_FOUND;
		ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colCollection->GetColumnByName(colName);
		if (colInfo == nullptr)
			return wxNOT_FOUND;
		return colInfo->GetColumnID();
	};

	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) = 0;
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& cVa) const = 0;

	//show filter 
	virtual bool ShowFilter();
	virtual bool ShowViewMode();

	/**
	* Override actionData
	*/

	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, class ibBackendValueForm* srcForm);

#pragma region _data_model_h_

	// get value into a wxVariant
	virtual void GetValue(wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) const = 0;

	// return true if the given item has a value to display in the given
	// column: this is always true except for container items which by default
	// only show their label in the first column (but see HasContainerColumns())
	virtual bool HasValue(const ibDataViewItem& item, unsigned col) const {
		return col == 0 || !IsContainer(item) || HasContainerColumns(item);
	}

	// usually ValueChanged() should be called after changing the value in the
	// model to update the control, ChangeValue() does it on its own while
	// SetValue() does not -- so while you will override SetValue(), you should
	// be usually calling ChangeValue()
	virtual bool SetValue(const wxVariant& variant,
		const ibDataViewItem& item,
		unsigned int col) = 0;

	// Get text attribute, return false of default attributes should be used
	virtual bool GetAttr(const ibDataViewItem& item,
		unsigned int col,
		ibDataViewItemAttr& attr) const = 0;

	// Override this if you want to disable specific items
	virtual bool IsEnabled(const ibDataViewItem& item,
		unsigned int col) const = 0;

	// define hierarchy
	virtual ibDataViewItem GetParent(const ibDataViewItem& item) const = 0;
	virtual bool IsContainer(const ibDataViewItem& item) const = 0;

	// define current parent for hierarchical view 
	virtual bool HasParentTopItem() const { return false; };

	virtual bool SetParentTopItem(const ibDataViewItem& item) { return false; }
	virtual ibDataViewItem GetParentTopItem() const { return ibDataViewItem(nullptr); }

	// Is the container just a header or an item with all columns
	virtual bool HasContainerColumns(const ibDataViewItem& item) const { return false; }

	// GetChildren removed — concretes implement GetFirstFetch (and
	// GetNextFetch / GetPrevFetch when paged). Old non-paged sources
	// just return the whole batch from GetFirstFetch and leave Next /
	// Prev as base-default no-ops.

	// default compare function
	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
		unsigned int column, bool ascending) const = 0;

	virtual bool HasDefaultCompare() const { return false; };

	// internal
	virtual bool IsListModel() const { return false; };
	virtual bool IsVirtualListModel() const { return false; };

#pragma endregion 

protected:

	ibFilterRow m_filterRow;
	ibSortOrder m_sortOrder;

	// Monotonic change counter — bumped on any filter / sort / value / row mutation the model
	// notifies the GUI of. The RAM visible-view cache (ibValueModelRamTableBase::BuildVisibleView)
	// stamps the generation it built at and rebuilds only on a mismatch — so the cache is NEVER
	// staler than the control (it invalidates exactly where the GUI is told the view changed).
	// (docs/paging-design.md — BuildVisibleView caching)
	uint32_t m_viewGeneration = 0;
	void     BumpViewGeneration() { ++m_viewGeneration; }
};

//Table support
class BACKEND_API ibValueModelTableBase : public ibValueModel {
	public:

	struct ibValueTableRow : public ibDataViewObject {

		// Logical equality for paged refetch: rows with matching value
		// map represent the same business row, even when behind fresh
		// pointers from a new fetch.
		virtual bool IsEqualTo(const ibDataViewObject& other) const override {
			const ibValueTableRow* o = dynamic_cast<const ibValueTableRow*>(&other);
			if (o == nullptr) return false;
			return m_nodeValues == o->m_nodeValues;
		}

		// Detached state — set by base Clear / Remove which null
		// `m_valueTable` while the row may stay alive through
		// refcount-pinning by an ibDataViewItem held elsewhere
		// (selection cache, ReturnLine).  Script reads/writes via
		// ReturnLine consult this to refuse on dead rows.
		virtual bool IsAttached() const override { return m_valueTable != nullptr; }

		ibValueTableRow() :
			m_valueTable(nullptr), m_nodeValues() {
		}

		ibValueTableRow(const ibValueTableRow& tableRow) :
			m_valueTable(tableRow.m_valueTable), m_nodeValues(tableRow.m_nodeValues) {
		}

		/////////////////////////////////////////////////////////////////////////////

		template <class varType>
		inline void AppendTableValue(const ibMetaID& id, varType&& variant) { m_nodeValues.insert_or_assign(id, variant); }
		inline ibValue& AppendTableValue(const ibMetaID& id) { return m_nodeValues[id]; }

		/////////////////////////////////////////////////////////////////////////////

		const ibRowMetaValues& GetTableValues() const { return m_nodeValues; }

		/////////////////////////////////////////////////////////////////////////////

		bool SetValue(const ibMetaID& id, const ibValue& variant, bool notify = false) {
			try {
				auto iterator = m_nodeValues.find(id);
				if (iterator != m_nodeValues.end()) {
					ibValue& cValue = m_nodeValues.at(id);
					wxASSERT(m_valueTable);
					if (notify && cValue != variant)
						m_valueTable->RowValueChanged(this, id);
					cValue = variant;
					return true;
				}
			}
			catch (std::out_of_range&) {
			}
			return false;
		}

		bool SetValue(unsigned int col, const wxVariant& variant, bool notify = false) {
			try {
				ibValue& cValue = m_nodeValues.at(col);
				std::vector<ibValue> listValue;
				if (cValue.FindValue(variant.GetString(), listValue)) {
					const ibValue& cFoundedValue = listValue.at(0);
					if (notify && cValue != cFoundedValue)
						m_valueTable->RowValueChanged(this, col);
					cValue.SetValue(cFoundedValue);
				}
				return true;
			}
			catch (std::out_of_range&) {
			}
			return false;
		}

		bool IsEmptyValue(const ibMetaID& col) const {
			auto iterator = m_nodeValues.find(col);
			if (iterator == m_nodeValues.end())
				return true;
			const ibValue& cValue = m_nodeValues.at(col);
			return cValue.IsEmpty();
		}

		bool IsEmptyValue(unsigned int col) const {
			auto iterator = m_nodeValues.find(col);
			if (iterator == m_nodeValues.end())
				return true;
			const ibValue& cValue = m_nodeValues.at(col);
			return cValue.IsEmpty();
		}

		bool HasColumnValue(const ibMetaID& id) const {
			return m_nodeValues.find(id) != m_nodeValues.end();
		}

		bool HasColumnValue(unsigned int col) const {
			return m_nodeValues.find(col) != m_nodeValues.end();
		}

		void EraseValue(const ibMetaID& id) {
			auto iterator = m_nodeValues.find(id);
			if (iterator != m_nodeValues.end())
				m_nodeValues.erase(id);
		}

		void EraseValue(unsigned int col) {
			auto iterator = m_nodeValues.find(col);
			if (iterator != m_nodeValues.end())
				m_nodeValues.erase(col);
		}

		bool CompareRow(const ibValueTableRow* tableRow, std::vector<ibSortModel>& paSort) const {
			try {
				for (unsigned long p = 0; p < paSort.size(); p++) {
					const ibValue& lhs = tableRow->m_nodeValues.at(paSort[p].m_sortModel);
					if (paSort[p].m_sortAscending) {
						if (lhs > m_nodeValues.at(paSort[p].m_sortModel))
							return true;
						else if (lhs < m_nodeValues.at(paSort[p].m_sortModel))
							return false;
					}
					else {
						if (lhs < m_nodeValues.at(paSort[p].m_sortModel))
							return true;
						else if (lhs > m_nodeValues.at(paSort[p].m_sortModel))
							return false;
					}
				}
			}
			catch (std::out_of_range&)
			{
			}
			return false;
		}

		////////////////////////////////////////////////////////////////////////

		const ibValue& GetTableValue(const ibMetaID& id) const {
			return m_nodeValues.at(id);
		}

		////////////////////////////////////////////////////////////////////////

		bool GetValue(const ibMetaID& id, ibValue& variant) const {
			try {
				variant = GetTableValue(id);
				return true;
			}
			catch (std::out_of_range&) {
				return false;
			}
			return false;
		}

		bool GetValue(unsigned int col, wxVariant& variant) const {

			try {
				variant = new ibVariantDataValueModel(GetTableValue(col));
				return true;
			}
			catch (std::out_of_range&) {
				return false;
			}

			return false;
		}

	private:
		friend class ibValueModelTableBase;
		friend class ibValueModelRamTableBase;
	protected:
		ibValueModelTableBase* m_valueTable;
		ibRowMetaValues m_nodeValues;
	};

public:

	ibValueModelTableBase() : ibValueModel() {}
	virtual ~ibValueModelTableBase() = default;

	/////////////////////////////////////////////////////////

	// Row count / row access — paged DB-backed concrete classes pull
	// rows on demand and don't keep a full-table count; RAM-backed
	// (ibValueModelRamTableBase) override.
	virtual long           GetRowCount() const         { return 0; }
	virtual long           GetRow(const ibDataViewItem& item) const { (void)item; return wxNOT_FOUND; }
	virtual ibDataViewItem GetItem(long row) const     { (void)row; return ibDataViewItem(); }

	/////////////////////////////////////////////////////////

	// Notification helpers — drive the ItemChanged / ValueChanged
	// hook from anywhere a row mutation happens (Set / write-through
	// from script).  No storage dependency, lives on the base.
	void RowChanged(ibValueTableRow* item) {
		BumpViewGeneration();   // a row's values changed -> the filtered/sorted view may differ
		m_modelProvider->ItemChanged(ibDataViewItem(item));
	}

	void RowValueChanged(ibValueTableRow* item, unsigned int col) {
		BumpViewGeneration();   // a cell changed -> filter membership / sort order may differ
		m_modelProvider->ValueChanged(ibDataViewItem(item), col);
	}

	/////////////////////////////////////////////////////////

		// derived classes should override these methods instead of
	// {Get,Set}Value() and GetAttr() inherited from the base class
	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const = 0;

	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) = 0;

	virtual bool GetAttrByRow(const ibDataViewItem& WXUNUSED(row), unsigned int WXUNUSED(col),
		ibDataViewItemAttr& WXUNUSED(attr)) const {
		return false;
	}

	virtual bool IsEnabledByRow(const ibDataViewItem& WXUNUSED(row),
		unsigned int WXUNUSED(col)) const {
		return true;
	}

#pragma region _data_model_h_

	// and implement some others by forwarding them to our own ones
	virtual void GetValue(wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) const override {
		GetValueByRow(variant, item, col);
	}

	virtual bool SetValue(const wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) override {
		return SetValueByRow(variant, item, col);
	}

	virtual bool GetAttr(const ibDataViewItem& item, unsigned int col,
		ibDataViewItemAttr& attr) const override {
		return GetAttrByRow(item, col, attr);
	}

	virtual bool IsEnabled(const ibDataViewItem& item, unsigned int col) const override {
		return IsEnabledByRow(item, col);
	}

	// Children query: DB-backed concretes pull rows via Get*Fetch;
	// the base default returns 0 (inherited from ibValueModel), so
	// no override is needed here for the non-RAM tier.

	// implement some base class pure virtual directly
	virtual ibDataViewItem GetParent(const ibDataViewItem& WXUNUSED(item)) const override {
		// items never have valid parent in this model
		return ibDataViewItem(nullptr);
	}

	virtual bool IsContainer(const ibDataViewItem& item) const override {
		// only the invisible (and invalid) root item has children
		return !item.IsOk();
	}

	// override sorting to always sort branches ascendingly
	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
		unsigned int col, bool ascending) const override {

		wxASSERT(item1.IsOk() && item2.IsOk());

		ibSortOrder::ibSortData* foundedSort = m_sortOrder.GetSortByID(col);
		if (foundedSort == nullptr && col != static_cast<unsigned int>(wxNOT_FOUND))
			return 0;

		ibValueTableRow* node1 = GetViewData<ibValueTableRow>(item1);
		if (node1 == nullptr)
			return 0;

		ibValueTableRow* node2 = GetViewData<ibValueTableRow>(item2);
		if (node2 == nullptr)
			return 0;

		for (auto sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				try {
					const ibValue& currValue1 = node1->GetTableValue(sort.m_sortModel);
					const ibValue& currValue2 = node2->GetTableValue(sort.m_sortModel);
					if (sort.m_sortAscending) {
						if (currValue1 < currValue2)
							return -1;
						else if (currValue1 > currValue2)
							return 1;
					}
					else {
						if (currValue1 > currValue2)
							return -1;
						else if (currValue1 < currValue2)
							return 1;
					}
				}
				catch (...) {
					return 0;
				}
			}
		}

		// items must be different
		wxUIntPtr id1 = wxPtrToUInt(item1.GetID()),
			id2 = wxPtrToUInt(item2.GetID());
		return ascending ? id1 - id2 : id2 - id1;
	}

	// Model rebuilds itself on sort change (column header click →
	// m_sortOrder updated → BeforeReset/AfterReset → fresh fetch
	// with new ORDER BY). wxDVC's incremental sort tracking would
	// fight us — return false so it leaves order to us.
	virtual bool HasDefaultCompare() const override { return false; }

	// internal
	virtual bool IsListModel() const override { return true; }
	virtual bool IsVirtualListModel() const override { return false; }

#pragma endregion
};

// RAM-backed table models — own the row storage and serve Get*Fetch
// by slicing it.  Concrete RAM-backed classes (TabularSection,
// ibValueModelTable, RecordSet) inherit from this; DB-backed
// (Catalog list, Enum, Register) inherit directly from
// ibValueModelTableBase and don't see the storage at all.
class BACKEND_API ibValueModelRamTableBase : public ibValueModelTableBase {
	public:

public:

	ibValueModelRamTableBase() = default;
	virtual ~ibValueModelRamTableBase() { Clear(false); }

	/////////////////////////////////////////////////////////

	virtual bool IsEmpty() const override { return GetRowCount() == 0; }

	// RAM-backed by construction — every concrete subclass slices its
	// own m_nodeValues vector through the inherited Get*Fetch.
	// Subclasses that add filter / sort UI must build on top via
	// `auto f = ibValueModelRamTableBase::GetFeatures(); f.flags |= …;`.
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::RamFetch;
		return f;
	}

	/////////////////////////////////////////////////////////

	void Clear(bool notify = true) {
		if (m_nodeValues.empty()) return;
		BumpViewGeneration();   // rows gone -> the cached view (now dangling pointers) must rebuild
		if (notify) m_modelProvider->BeforeReset();
		m_nodeValues.erase(std::remove_if(m_nodeValues.begin(), m_nodeValues.end(),
			[](const auto& node) { node->m_valueTable = nullptr; node->DecRef(); return true; }),
			m_nodeValues.end());
		if (notify) m_modelProvider->AfterReset();
	}

	// Tell control to discard its rendered view and re-fetch everything
	// from the model. Used after batch mutations that wxDVC's
	// incremental sort tracking can't follow (paged backward Insert).
	void NotifyReset() {
		BumpViewGeneration();
		if (m_modelProvider) {
			m_modelProvider->BeforeReset();
			m_modelProvider->AfterReset();
		}
	}

	/////////////////////////////////////////////////////////

	void ClearRange(const unsigned long from, const unsigned long to, bool notify = true) {
		if (from > m_nodeValues.size() || to > m_nodeValues.size()) return;
		BumpViewGeneration();
		for (auto iterator = m_nodeValues.begin() + from; iterator != m_nodeValues.begin() + to; iterator++) {
			if (notify && !m_modelProvider->ItemDeleted(ibDataViewItem(nullptr), ibDataViewItem(*iterator)))
				return;
			(*iterator)->m_valueTable = nullptr;
			(*iterator)->DecRef();
		}
		m_nodeValues.erase(m_nodeValues.begin() + from, m_nodeValues.begin() + to);
	}

	/////////////////////////////////////////////////////////

	void Reserve(const long rowCount = 1) {
		m_nodeValues.reserve(m_nodeValues.size() + rowCount);
	}

	/////////////////////////////////////////////////////////

	long Append(ibValueTableRow* child, bool notify = true) {
		wxASSERT(child);

		BumpViewGeneration();
		child->m_valueTable = this;
		m_nodeValues.emplace_back(child);

		if (notify && !m_modelProvider->ItemAppended(ibDataViewItem(nullptr), ibDataViewItem(child))) {
			child->m_valueTable = this;
			m_nodeValues.pop_back();
			return false;
		}

		return m_nodeValues.size() - 1;
	}

	long Insert(ibValueTableRow* child, unsigned int row, bool notify = true) {
		wxASSERT(child);

		BumpViewGeneration();
		child->m_valueTable = this;
		auto iterator = m_nodeValues.insert(m_nodeValues.begin() + row, child);

		if (notify && !m_modelProvider->ItemInserted(ibDataViewItem(nullptr), ibDataViewItem(child))) {
			child->m_valueTable = this;
			m_nodeValues.erase(iterator);
			return false;
		}

		return row + 1;
	}

	bool Remove(ibValueTableRow*& child, bool notify = true) {
		wxASSERT(child);

		BumpViewGeneration();
		auto iterator = std::find(
			m_nodeValues.begin(),
			m_nodeValues.end(), child
		);

		if (notify && !m_modelProvider->ItemDeleted(ibDataViewItem(nullptr), ibDataViewItem(child)))
			return false;

		if (iterator != m_nodeValues.end()) {
			m_nodeValues.erase(iterator);
			child->m_valueTable = nullptr;
			child->DecRef();
			return true;
		}
		return false;
	}

	void Sort(unsigned int col, bool ascending = true, bool notify = true) {
		std::vector<ibSortModel> fixedSort = { { col, ascending } };
		Sort(fixedSort, notify);
	}

	void Sort(std::vector<ibSortModel>& paSort, bool notify = true) {
		BumpViewGeneration();   // m_nodeValues reordered -> the cached view order is stale
		if (notify) m_modelProvider->BeforeReset();
		std::sort(m_nodeValues.begin(), m_nodeValues.end(),
			[&paSort](const ibValueTableRow* a, const ibValueTableRow* b)
			{
				return a->CompareRow(b, paSort);
			}
		);
		if (notify) m_modelProvider->AfterReset();
	}

	long GetRowCount() const { return m_nodeValues.size(); }

	/////////////////////////////////////////////////////////

	// Row index reported here is the RAW m_nodeValues position so
	// save / iteration code that uses GetRowCount() bound (raw size)
	// + GetItem(row) traverses every row including filtered-out
	// ones.  For DISPLAY-position queries (number-line column,
	// post-filter row index inside the rendered tree), use
	// BuildVisibleView directly — see TabularSection's GetValueByRow.
	virtual long GetRow(const ibDataViewItem& item) const override {
		ibValueTableRow* node = GetViewData<ibValueTableRow>(item);
		if (node == nullptr)
			return wxNOT_FOUND;
		auto iterator = std::find(m_nodeValues.begin(), m_nodeValues.end(), node);
		if (iterator != m_nodeValues.end())
			return std::distance(m_nodeValues.begin(), iterator);
		return wxNOT_FOUND;
	}

	virtual ibDataViewItem GetItem(long row) const override {
		if (row >= 0 && row < (long)m_nodeValues.size()) {
			return ibDataViewItem(m_nodeValues[row]);
		}
		return ibDataViewItem(nullptr);
	}

	// Note: the universal GetFirstFetch override is further down in
	// this class (it handles both the paged and full-batch cases by
	// inspecting `count`); the migration from the legacy GetChildren
	// API folded the old single-shot path into that one impl.

	// Build a filtered + sorted view of m_nodeValues.  Filter comes
	// from m_filterRow (eFilter / eFilterByColumn / eFilterClear UI
	// path); sort from m_sortOrder (header click).  Caller slices
	// the returned vector by anchor + count.  RAM tables are small
	// (typically << 1000 rows) so building the view per-fetch is
	// cheap; cache + invalidation isn't worth the complexity.
	std::vector<ibValueTableRow*> BuildVisibleView() const {
		// Unchanged since the last build (no filter / sort / value / row mutation) -> reuse the
		// cached view, skipping the per-row filter scan + the stable_sort. Invalidated by the
		// generation counter, bumped on every change the model notifies the GUI of.
		if (m_visibleViewGen == m_viewGeneration)
			return m_visibleViewCache;

		std::vector<ibValueTableRow*> view;
		view.reserve(m_nodeValues.size());
		ibValue scratch;
		for (auto* row : m_nodeValues) {
			if (row == nullptr) continue;
			const ibDataViewItem item(row);
			bool match = true;
			for (const auto& f : m_filterRow.m_filters) {
				if (!f.m_filterUse) continue;
				if (!GetValueByMetaID(item, f.m_filterModel, scratch)) continue;
				if (f.m_filterComparison == ibComparisonType_Equal
				    && f.m_filterValue != scratch) { match = false; break; }
				if (f.m_filterComparison == ibComparisonType_NotEqual
				    && f.m_filterValue == scratch) { match = false; break; }
			}
			if (match) view.push_back(row);
		}
		bool anySort = false;
		for (const auto& s : m_sortOrder.m_sorts) if (s.m_sortEnable) { anySort = true; break; }
		if (anySort) {
			std::stable_sort(view.begin(), view.end(),
				[this](ibValueTableRow* a, ibValueTableRow* b) {
					ibValue av, bv;
					for (const auto& s : m_sortOrder.m_sorts) {
						if (!s.m_sortEnable) continue;
						const ibDataViewItem ia(a), ib(b);
						GetValueByMetaID(ia, s.m_sortModel, av);
						GetValueByMetaID(ib, s.m_sortModel, bv);
						if (av < bv) return s.m_sortAscending;
						if (bv < av) return !s.m_sortAscending;
					}
					return false;
				});
		}
		m_visibleViewCache = view;          // stamp + cache; reused until the next change bumps m_viewGeneration
		m_visibleViewGen   = m_viewGeneration;
		return view;
	}

	// RAM-backed paged fetch — slice the filtered+sorted view by
	// anchor + count.  count <= 0 means "no batch limit" → return
	// everything in scope.
	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& array) const override {
		if (parent.IsOk()) return 0;
		auto view = BuildVisibleView();
		const size_t total = view.size();
		if (total == 0) return 0;
		size_t start = 0;
		if (anchor.IsOk()) {
			auto* row = static_cast<ibValueTableRow*>(anchor.GetID());
			auto it = std::find(view.begin(), view.end(), row);
			if (it != view.end()) start = std::distance(view.begin(), it);
		}
		if (start >= total) return 0;
		const size_t avail = total - start;
		const size_t take = (count > 0)
			? std::min<size_t>(static_cast<size_t>(count), avail) : avail;
		array.Alloc(take);
		for (size_t i = 0; i < take; ++i)
			array.Add(ibDataViewItem(view[start + i]));
		return static_cast<unsigned int>(take);
	}

	virtual unsigned int GetNextFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count,
		ibDataViewItemArray& array) const override {
		if (parent.IsOk() || !anchor.IsOk()) return 0;
		auto view = BuildVisibleView();
		const size_t total = view.size();
		if (total == 0) return 0;
		auto* anchorRow = static_cast<ibValueTableRow*>(anchor.GetID());
		auto it = std::find(view.begin(), view.end(), anchorRow);
		if (it == view.end()) return 0;
		size_t start = std::distance(view.begin(), it) + 1;
		if (start >= total) return 0;
		const size_t avail = total - start;
		const size_t take = (count > 0)
			? std::min<size_t>(static_cast<size_t>(count), avail) : avail;
		array.Alloc(take);
		for (size_t i = 0; i < take; ++i)
			array.Add(ibDataViewItem(view[start + i]));
		return static_cast<unsigned int>(take);
	}

	virtual unsigned int GetPrevFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count,
		ibDataViewItemArray& array) const override {
		if (parent.IsOk() || !anchor.IsOk()) return 0;
		auto view = BuildVisibleView();
		if (view.empty()) return 0;
		auto* anchorRow = static_cast<ibValueTableRow*>(anchor.GetID());
		auto it = std::find(view.begin(), view.end(), anchorRow);
		if (it == view.begin() || it == view.end()) return 0;
		const size_t end = std::distance(view.begin(), it);
		const size_t take = (count > 0)
			? std::min<size_t>(static_cast<size_t>(count), end) : end;
		array.Alloc(take);
		for (size_t i = 0; i < take; ++i)
			array.Add(ibDataViewItem(view[end - take + i]));
		return static_cast<unsigned int>(take);
	}

protected:
	std::vector<ibValueTableRow*> m_nodeValues;

	// BuildVisibleView cache (docs/paging-design.md): the last filtered+sorted view + the
	// generation it was built at. Mutable — BuildVisibleView is const but memoises. Initialised to
	// a generation that can never match m_viewGeneration (0), so the first call always builds.
	mutable std::vector<ibValueTableRow*> m_visibleViewCache;
	mutable uint32_t                      m_visibleViewGen = static_cast<uint32_t>(-1);
};

//Tree support
class BACKEND_API ibValueModelTreeBase : public ibValueModel {
	public:


	struct ibValueTreeNode : public ibDataViewObject {

		// Detached state — base tree's Remove / Clear nulls m_valueTree.
		// Used by script-side ReturnLine to refuse reads/writes on a
		// row whose model link has been wiped while the node itself is
		// still pinned by an ibDataViewItem.
		virtual bool IsAttached() const override { return m_valueTree != nullptr; }

		ibValueTreeNode(ibValueModelTreeBase* valueTree) :
			m_parent(nullptr), m_valueTree(valueTree) {
		}

		ibValueTreeNode(ibValueTreeNode* parent) :
			m_parent(parent), m_valueTree(nullptr) {
			if (m_parent != nullptr) m_parent->Append(this);
		}

		virtual ~ibValueTreeNode() {
			// free all our children nodes
			size_t count = m_children.size();
			for (size_t i = 0; i < count; i++) {
				ibValueTreeNode* child = m_children[i];
				wxASSERT(child);
				child->m_valueTree = nullptr;
				child->DecRef();
			}
		}

		// ibDataViewObject overrides — let ibDataViewItem queries
		// dispatch directly without going through the model.
		virtual bool IsContainer() const override { return m_children.size() > 0; }
		virtual ibDataViewItem GetParentItem() const override {
			return m_parent ? ibDataViewItem(m_parent) : ibDataViewItem();
		}

		/////////////////////////////////////////////////////////////////////////////

		template <class varType>
		inline void AppendTableValue(const ibMetaID& id, varType&& variant) { m_nodeValues.insert_or_assign(id, variant); }
		inline ibValue& AppendTableValue(const ibMetaID& id) { return m_nodeValues[id]; }

		/////////////////////////////////////////////////////////////////////////////

		const ibRowMetaValues& GetTableValues() const { return m_nodeValues; }

		/////////////////////////////////////////////////////////////////////////////

		void SetParent(ibValueTreeNode* parent) {
			if (m_parent)
				m_parent->Remove(this);
			if (parent != nullptr)
				parent->Append(this);
			m_parent = parent;
		}

		ibValueTreeNode* GetParent() const { return m_parent; }
		std::vector<ibValueTreeNode*>& GetChildren() { return m_children; }
		ibValueTreeNode* GetChild(unsigned int n) const { return m_children.at(n); }

		bool Append(ibValueTreeNode* child, bool notify = true) {
			child->m_valueTree = m_valueTree;
			m_children.emplace_back(child);
			if (notify && !m_valueTree->m_modelProvider->ItemAppended(ibDataViewItem(this), ibDataViewItem(child))) {
				child->m_valueTree = nullptr;
				m_children.pop_back();
				return false;
			}
			return true;
		}

		// Bulk-build helper: attach a fresh child node WITHOUT a per-node
		// ItemAppended notification (the caller fires ONE reset after the whole
		// tree is in place) and WITH the parent link set — plain Append leaves
		// m_parent null, which would break upward navigation. Used by
		// ibValueModelRamTreeBase::PopulateFromTree to mirror an L3
		// ibQueryRamTable Node tree in one shot. (docs/query-language-arc.md §22.1b)
		ibValueTreeNode* AddChildNode() {
			ibValueTreeNode* child = new ibValueTreeNode(m_valueTree);
			child->m_parent = this;
			m_children.emplace_back(child);
			return child;
		}

		bool Insert(ibValueTreeNode* child, unsigned int n, bool notify = true) {
			child->m_valueTree = m_valueTree;
			auto iterator = m_children.insert(m_children.begin() + n, child);
			if (notify && !m_valueTree->m_modelProvider->ItemInserted(ibDataViewItem(this), ibDataViewItem(child))) {
				child->m_valueTree = nullptr;
				m_children.erase(iterator);
				return false;
			}
			return true;
		}

		bool Remove(ibValueTreeNode* child, bool notify = true) {
			auto iterator = std::find(m_children.begin(), m_children.end(), child);
			if (notify && !m_valueTree->m_modelProvider->ItemDeleted(ibDataViewItem(this), ibDataViewItem(child)))
				return false;
			if (iterator != m_children.end())
				m_children.erase(iterator);
			child->m_valueTree = nullptr;
			child->DecRef();
			return true;
		}

		void Sort(std::vector<ibSortModel>& paSort) {
			std::sort(m_children.begin(), m_children.end(),
				[&paSort](const ibValueTreeNode* a, const ibValueTreeNode* b)
				{
					return a->CompareNode(b, paSort);
				}
			);
			for (auto child : m_children) child->Sort(paSort);
		}

		unsigned int GetChildCount() const {
			return m_children.size();
		}

	public:

		bool CompareNode(const ibValueTreeNode* node, std::vector<ibSortModel>& paSort) const {
			try {
				for (unsigned long p = 0; p < paSort.size(); p++) {
					const ibValue& lhs = node->m_nodeValues.at(paSort[p].m_sortModel);
					if (paSort[p].m_sortAscending) {
						if (lhs > m_nodeValues.at(paSort[p].m_sortModel))
							return true;
						else if (lhs < m_nodeValues.at(paSort[p].m_sortModel))
							return false;
					}
					else {
						if (lhs < m_nodeValues.at(paSort[p].m_sortModel))
							return true;
						else if (lhs > m_nodeValues.at(paSort[p].m_sortModel))
							return false;
					}
				}
			}
			catch (std::out_of_range&)
			{
			}
			return false;
		}

	public:     // public to avoid getters/setters

		bool SetValue(const ibMetaID& id, const ibValue& variant, bool notify = false) {
			try {
				ibValue& cValue = m_nodeValues.at(id);
				if (notify && cValue != variant)
					m_valueTree->RowValueChanged(this, id);
				cValue.SetValue(variant);
				return true;
			}
			catch (std::out_of_range&) {
			}
			return false;
		}

		bool SetValue(unsigned int col, const wxVariant& variant, bool notify = false) {
			try {
				ibValue& cValue = m_nodeValues.at(col);
				std::vector<ibValue> listValue;
				if (cValue.FindValue(variant.GetString(), listValue)) {
					const ibValue& cFoundedValue = listValue.at(0);
					if (notify && cValue != cFoundedValue)
						m_valueTree->RowValueChanged(this, col);
					cValue.SetValue(cFoundedValue);
				}
				return true;
			}
			catch (std::out_of_range&) {
			}
			return false;
		}

		////////////////////////////////////////////////////////////////////////

		const ibValue& GetTableValue(const ibMetaID& id) const {
			return m_nodeValues.at(id);
		}

		////////////////////////////////////////////////////////////////////////


		bool GetValue(const ibMetaID& id, ibValue& variant) const {
			try {
				variant = GetTableValue(id);
				return true;
			}
			catch (std::out_of_range&) {
			}
			return false;
		}

		bool GetValue(unsigned int col, wxVariant& variant) const {
			try {
				variant = new ibVariantDataValueModel(GetTableValue(col));
				return true;
			}
			catch (std::out_of_range&) {
			}
			return false;
		}

	private:
		friend class ibValueModelTreeBase;
		friend class ibValueModelRamTreeBase;
	private:
		ibValueTreeNode* m_parent;
		std::vector<ibValueTreeNode*> m_children;
	protected:
		ibValueModelTreeBase* m_valueTree;
		ibRowMetaValues m_nodeValues;
	};

public:

	ibValueModelTreeBase() : ibValueModel() {}

	virtual ~ibValueModelTreeBase() {}

	/////////////////////////////////////////////////////////

	void RowChanged(ibValueTreeNode* item) {
		/* wxDataViewModel:: */ m_modelProvider->ItemChanged(ibDataViewItem(item));
	}

	void RowValueChanged(ibValueTreeNode* item, unsigned int col) {
		/* wxDataViewModel:: */ m_modelProvider->ValueChanged(ibDataViewItem(item), col);
	}

	/////////////////////////////////////////////////////////

	// derived classes should override these methods instead of
	// {Get,Set}Value() and GetAttr() inherited from the base class
	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& item, unsigned col) const = 0;

	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& item, unsigned col) = 0;

	virtual bool GetAttrByRow(const ibDataViewItem& WXUNUSED(item),
		unsigned WXUNUSED(col), ibDataViewItemAttr& WXUNUSED(attr)) const {
		return false;
	}

	virtual bool IsEnabledByRow(const ibDataViewItem& WXUNUSED(item),
		unsigned int WXUNUSED(col)) const {
		return true;
	}

#pragma region _data_model_h_

	// and implement some others by forwarding them to our own ones
	virtual void GetValue(wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) const override {
		GetValueByRow(variant, item, col);
	}

	// return true if the given item has a value to display in the given
	// column: this is always true except for container items which by default
	// only show their label in the first column (but see HasContainerColumns())
	virtual bool HasValue(const ibDataViewItem& item, unsigned col) const override {
		if (HasContainerColumns(item))
			return false;
		return true;
	}

	virtual bool SetValue(const wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) override {
		return SetValueByRow(variant, item, col);
	}

	virtual bool GetAttr(const ibDataViewItem& item, unsigned int col,
		ibDataViewItemAttr& attr) const override {
		return GetAttrByRow(item, col, attr);
	}

	virtual bool IsEnabled(const ibDataViewItem& item, unsigned int col) const override {
		return IsEnabledByRow(item, col);
	}

	// Paged tree: parent is recovered from the node itself; legacy
	// invisible-root walk lives on Ram-tree-base which owns m_root.
	virtual ibDataViewItem GetParent(const ibDataViewItem& item) const override {
		if (!item.IsOk())
			return ibDataViewItem();
		ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
		if (node == nullptr || node->GetParent() == nullptr)
			return ibDataViewItem();
		return ibDataViewItem(node->GetParent());
	}

	virtual bool IsContainer(const ibDataViewItem& item) const override {
		// invisible top-level: paged path uses synthetic empty item as
		// "the whole tree", so report container
		if (!item.IsOk())
			return true;
		ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
		if (node == nullptr)
			return false;
		return node->IsContainer();
	}

	// Default returns the children stored on the node itself when
	// one is supplied; null parent has no fallback root here.
	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
		const ibDataViewItem& /*anchor*/, int /*count*/,
		ibDataViewItemArray& array) const override {
		ibValueTreeNode* node = GetViewData<ibValueTreeNode>(parent);
		if (node == nullptr)
			return 0;
		unsigned int count = node->GetChildCount();
		if (count == 0)
			return 0;
		array.Alloc(count);
		for (unsigned int pos = 0; pos < count; pos++) {
			array.Add(ibDataViewItem(node->GetChild(pos)));
		}
		return count;
	}

	// Paged tree rebuilds on sort change via BeforeReset/AfterReset —
	// no incremental wx-managed sort path needed.
	virtual bool HasDefaultCompare() const override { return false; }

	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
		unsigned int col, bool ascending) const override {

		wxASSERT(item1.IsOk() && item2.IsOk());

		ibSortOrder::ibSortData* foundedSort = m_sortOrder.GetSortByID(col);
		if (foundedSort == nullptr && col != static_cast<unsigned int>(wxNOT_FOUND))
			return 0;

		ibValueTreeNode* node1 = GetViewData<ibValueTreeNode>(item1);
		if (node1 == nullptr)
			return 0;
		ibValueTreeNode* node2 = GetViewData<ibValueTreeNode>(item2);
		if (node2 == nullptr)
			return 0;

		for (auto sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				try {
					const ibValue& currValue1 = node1->GetTableValue(sort.m_sortModel);
					const ibValue& currValue2 = node2->GetTableValue(sort.m_sortModel);
					if (sort.m_sortAscending) {
						if (currValue1 < currValue2)
							return -1;
						else if (currValue1 > currValue2)
							return 1;
					}
					else {
						if (currValue1 > currValue2)
							return -1;
						else if (currValue1 < currValue2)
							return 1;
					}
				}
				catch (...) {
					return 0;
				}
			}
		}

		// items must be different
		wxUIntPtr id1 = wxPtrToUInt(item1.GetID()),
			id2 = wxPtrToUInt(item2.GetID());
		return ascending ? id1 - id2 : id2 - id1;
	}

	virtual bool IsListModel() const override { return false; }
	virtual bool IsVirtualListModel() const override { return false; }

#pragma endregion
};

// RAM-backed tree — symmetric counterpart to ibValueModelRamTableBase.
// Owns the in-memory ibValueTreeNode root and the mutation API
// (Delete / Clear / Sort) that legacy script-side and designer-side
// consumers rely on.  Tree-base above stays a pure shape contract,
// so DB-cursor concretes (FolderRef) inherit only the Get*Fetch and
// notification primitives without paying for the m_root allocation.
class BACKEND_API ibValueModelRamTreeBase : public ibValueModelTreeBase {
	public:

public:

	ibValueModelRamTreeBase() : ibValueModelTreeBase() {
		m_root = new ibValueTreeNode(this);
	}

	virtual ~ibValueModelRamTreeBase() {
		wxDELETE(m_root);
	}

	/////////////////////////////////////////////////////////

	virtual bool IsEmpty() const override {
		return m_root->GetChildCount() == 0;
	}

	// RAM-backed tree — Tree shape + RamFetch by default.  Subclasses
	// extend via `auto f = ibValueModelRamTreeBase::GetFeatures();
	// f.flags |= Features::Folders | …; f.folderSortID = …; return f;`.
	virtual Features GetFeatures() const override {
		Features f;
		f.flags |= Features::Tree | Features::RamFetch;
		return f;
	}

	/////////////////////////////////////////////////////////

	ibValueTreeNode* GetRoot() const { return m_root; }

	// Mirror an L3 Selector tree (ibSelectorTree, folded from a flat snapshot — Execute() +
	// ibSelector::Build) into this RAM tree in ONE shot: each Node -> an ibValueTreeNode carrying
	// that Node's cell values, nested the same way. This is the EAGER backing — the Selector
	// already produced the whole shape from one snapshot, so the per-parent fetch is replaced by a
	// single tree mirror + one reset. Defined in tableInfo.cpp (needs the full ibSelectorTree).
	// (docs/query-language-arc.md §22.1b)
	void PopulateFromTree(const ibSelectorTree& tree, bool notify = true);

	// helper methods to change the model
	bool Delete(const ibDataViewItem& item, bool notify = true) {
		ibValueTreeNode* node = (ibValueTreeNode*)item.GetID();
		if (node == nullptr)
			return false;
		ibDataViewItem parent(node->GetParent());
		if (!parent.IsOk()) {
			wxASSERT(node == m_root);
			// don't make the control completely empty:
			wxLogError("Cannot remove the root item!");
			return false;
		}

		// first remove the node from the parent's array of children;
		// removing the node from the vector doesn't free it — the node
		// is refcounted via ibDataViewItem.
		std::vector<ibValueTreeNode*>& children = node->GetParent()->GetChildren();
		std::vector<ibValueTreeNode*>::iterator children_iterator = std::find(children.begin(), children.end(), node);
		if (children_iterator != children.end())
			children.erase(children_iterator);

		// notify control
		if (notify && !m_modelProvider->ItemDeleted(parent, item))
			return false;

		// detach the node and drop our ref
		node->m_valueTree = nullptr;
		node->DecRef();
		return true;
	}

	void Clear(bool notify = true) {
		std::vector<ibValueTreeNode*>& children = m_root->GetChildren();
		while (!children.empty()) {
			ibValueTreeNode* node = m_root->GetChild(0);
			std::vector<ibValueTreeNode*>::iterator children_iterator = std::find(children.begin(), children.end(), node);
			if (children_iterator != children.end())
				children.erase(children_iterator);
			node->m_valueTree = nullptr;
			node->DecRef();
		}
		if (notify) m_modelProvider->Cleared();
	}

	void Sort(unsigned int col, bool ascending = true, bool notify = true) {
		std::vector<ibSortModel> fixedSort = { { col, ascending } };
		Sort(fixedSort, notify);
	}

	void Sort(std::vector<ibSortModel>& paSort, bool notify = true) {
		if (notify) m_modelProvider->BeforeReset();
		m_root->Sort(paSort);
		if (notify) m_modelProvider->AfterReset();
	}

	/////////////////////////////////////////////////////////

	// RAM-backed paged fetch — slice the parent's child vector (or
	// m_root's children when parent is empty / invisible-root).  No
	// filter / sort UI by default; subclasses that need them override
	// this in addition to GetFeatures() to set Filters | Sorting.
	// count <= 0 means "no batch limit" → return everything in scope.
	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count,
		ibDataViewItemArray& array) const override {
		ibValueTreeNode* parentNode = GetViewData<ibValueTreeNode>(parent);
		if (parentNode == nullptr) parentNode = m_root;
		const auto& children = parentNode->m_children;
		const size_t total = children.size();
		if (total == 0) return 0;
		size_t start = 0;
		if (anchor.IsOk()) {
			auto* row = static_cast<ibValueTreeNode*>(anchor.GetID());
			auto it = std::find(children.begin(), children.end(), row);
			if (it != children.end()) start = std::distance(children.begin(), it);
		}
		if (start >= total) return 0;
		const size_t avail = total - start;
		const size_t take = (count > 0)
			? std::min<size_t>(static_cast<size_t>(count), avail) : avail;
		array.Alloc(take);
		for (size_t i = 0; i < take; ++i)
			array.Add(ibDataViewItem(children[start + i]));
		return static_cast<unsigned int>(take);
	}

	virtual unsigned int GetNextFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count,
		ibDataViewItemArray& array) const override {
		if (!anchor.IsOk()) return 0;
		ibValueTreeNode* parentNode = GetViewData<ibValueTreeNode>(parent);
		if (parentNode == nullptr) parentNode = m_root;
		const auto& children = parentNode->m_children;
		const size_t total = children.size();
		if (total == 0) return 0;
		auto* anchorRow = static_cast<ibValueTreeNode*>(anchor.GetID());
		auto it = std::find(children.begin(), children.end(), anchorRow);
		if (it == children.end()) return 0;
		size_t start = std::distance(children.begin(), it) + 1;
		if (start >= total) return 0;
		const size_t avail = total - start;
		const size_t take = (count > 0)
			? std::min<size_t>(static_cast<size_t>(count), avail) : avail;
		array.Alloc(take);
		for (size_t i = 0; i < take; ++i)
			array.Add(ibDataViewItem(children[start + i]));
		return static_cast<unsigned int>(take);
	}

	virtual unsigned int GetPrevFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count,
		ibDataViewItemArray& array) const override {
		if (!anchor.IsOk()) return 0;
		ibValueTreeNode* parentNode = GetViewData<ibValueTreeNode>(parent);
		if (parentNode == nullptr) parentNode = m_root;
		const auto& children = parentNode->m_children;
		if (children.empty()) return 0;
		auto* anchorRow = static_cast<ibValueTreeNode*>(anchor.GetID());
		auto it = std::find(children.begin(), children.end(), anchorRow);
		if (it == children.begin() || it == children.end()) return 0;
		const size_t end = std::distance(children.begin(), it);
		const size_t take = (count > 0)
			? std::min<size_t>(static_cast<size_t>(count), end) : end;
		array.Alloc(take);
		for (size_t i = 0; i < take; ++i)
			array.Add(ibDataViewItem(children[end - take + i]));
		return static_cast<unsigned int>(take);
	}

	/////////////////////////////////////////////////////////

	// Null-parent fallback is handled by the paged GetFirstFetch
	// override above — that one routes an empty parent to m_root's
	// children. No separate non-paged single-shot override here.

	// GetParent stops at the invisible root: a node whose parent is
	// m_root reports no parent (it sits at the top level).
	virtual ibDataViewItem GetParent(const ibDataViewItem& item) const override {
		if (!item.IsOk())
			return ibDataViewItem();
		ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
		if (node == nullptr)
			return ibDataViewItem();
		if (m_root == node || m_root == node->GetParent())
			return ibDataViewItem();
		return ibDataViewItem(node->GetParent());
	}

protected:
	ibValueTreeNode* m_root;
};

#endif