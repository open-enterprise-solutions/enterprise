#ifndef __MODEL_H__
#define __MODEL_H__

#include <algorithm>
#include <functional>
#include <map>      // ibComposerNode holds the driver row's std::map<ibMetaID, ibValue>
#include <memory>   // std::shared_ptr (CreateIterator)

#include "backend/tabularModelView.h"

#include "backend/system/value/valueType.h"

#include "backend/standardCommand.h"
#include "backend/tabularDataObject.h"   // ibTabularDataObject — ibValueModel IS one (the table hop gate)

// L5-1 declarative composer — held BY VALUE (mutable ibDataDBComposer m_composer). The cycle that used to
// force a forward-decl + unique_ptr (dataComposer.h → queryLowering.h → queryable.h → tabularModel.h) is
// broken: queryable.h now takes ibComparisonType/ibMetaID from tabularModelView.h (above), NOT tabularModel.h.
#include "backend/composition/dataComposer.h"
#include "backend/composition/ramComposer.h"   // ibDataRamComposer — ibValueModelStorage holds one BY VALUE

// L3 Selector tree (folded from a flat snapshot) — mirrored into the RAM tree model by
// ibValueModelRamTreeBase::PopulateFromTree. Full type only needed in tabularModel.cpp.
class ibSelectorTree;


// L3 navigation door a DB model vends over its own rows (the base GetSourceQueryable() return type).
// Forward-declared here so tabularModel.h names the source-hook without pulling the query layer (no include
// cycle). RAM models have NO queryable — the composer reads ibRamValueStorage directly.
class ibBackendQueryable;
class ibBackendQueryColumn;

// The RAM value-storage — the RAM analog of a queryable (a flat/tree table that OWNS the live nodes). Defined
// LOWER (it holds ibValueModel::ibComposerNode); ibValueModelStorage owns one, ibDataRamComposer sources from it.
class ibRamValueStorage;

// The row identity key returned by GetItemKey (value.h → uniqueKey.h pulled in tabularModel.cpp / the model .cpp's).
class ibUniqueKey;


///////////////////////////////////////////////////////////////////////////////////
#define defaultCountPerPage 100
///////////////////////////////////////////////////////////////////////////////////

// ibComparisonType / ibFilterRow / ibSortOrder / ibSortData / ibSortModel (deleted) + ibFetchDirection (live)
// live in tabularModelView.h — view-layer types shared by both ibDataViewModel virtuals and the paged-fetch / query
// builders (ibReadPageRequest), so the query layer can see them WITHOUT including tabularModel.h (no include cycle).

// (The templated paged-fetch contract — ibFetchAnchor / ibFetchRequest / ibFetchResponse / ibAnchorOfFn — was
//  removed: the universal ibValueModel::RunComposerPage replaced every typed per-model Fetch, building the keyset
//  anchor itself from the composer + the row-key. ibFetchDirection — the live page direction — is in tabularModelView.h.)

// The `want` display positions in [0,total) around the browsed anchor position `p` (top / bottom when there is
// no anchor, p==-1), honouring the fetch direction — the ONE windowing rule every paged LEVEL uses: the RAM half's
// flat / grouped / detail rows (in-place sorted) AND the DB half's grouped level (its folded group list, which
// TOTALS return whole — the page envelope does not size a totals read, so the group level windows here). Defined
// in tabularModelRam.cpp.
std::vector<long> ibComputePageWindow(long total, long p, ibFetchDirection dir, int want);

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
	public ibStandardCommandTabular, public ibTabularDataObject {
	public:

	// The table gate (see ibValue::IsTableValue): every model IS a tabular source. Declared
	// ONCE here — all model subclasses (list / tree / table / dynamic list) inherit this
	// static through name-lookup, so the class factory reports them as tables by CLSID.
	static bool IsTableValue() { return true; }

	// NOTE on IsTransferable: deliberately NOT overridden here. Being a mutable
	// collection is not by itself a reason to refuse a session boundary — a value
	// table is a self-contained runtime value and travels with its own data, the
	// same as an array does. What cannot travel is a model that is PART of
	// something else that keeps changing: a register's record set, an object's
	// tabular section. Those say so themselves (see commonObject.h,
	// tabularSection.h), and the distinction is ownership, not mutability.

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

		// (Header-click sort is committed entirely on the FRONT now — ibValueModelTableBox::OnColumnClick pokes
		//  the concrete model's composer + RefetchAll. No SortBy / GetSortArrows dispatch on the model.)

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

		// Forward to ibValueModel — a runtime table sends the work to a background
		// job; the answer comes back through the work itself.
		virtual void SubmitFetchAsync(std::function<void()> work) override {
			m_ownerModel->SubmitFetchAsync(std::move(work));
		}

		// Capability + state forwarders — ibDataViewModel virtuals
		// resolve to the owning ibValueModel's storage.  Lets
		// datavgen.cpp ask GetTableModel()->GetFeatures() / GetSortOrder()
		// directly without cross-casting through this provider.
		virtual ibDataViewModel::Features GetFeatures() const override {
			return m_ownerModel->GetFeatures();
		}

		virtual bool HasKeyedRows() const override {
			return m_ownerModel->HasKeyedRows();
		}

		// GROUPING state — true when the model currently groups (a tree). Lets datavgen route a grouped model's
		// mutations through the re-fetch + selection-restore path (a new/edited row must re-place into its group),
		// the way a keyed DB model already does; a flat model keeps the cheap in-place tree-insert.
		virtual bool IsGroupedModel() const override {
			return m_ownerModel->IsGroupedModel();
		}

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

	// The rented run this model's last portion went out on — kept so the destructor
	// can wait it out. Nothing else keeps a model alive while a worker is reading
	// it: the control's alive token answers for the control, not for this. One slot
	// because the door already serialises reads (ibDataViewModel::GuardFetch).
	std::shared_ptr<class ibBackgroundRun> m_fetchRun;

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

	// Wrap an ibValue into a dataview wxVariant for a FRONT-side resolver (a dot-path column's
	// CheckedGetValue). Public so the front produces a cell value without reaching the protected
	// nested types; the model itself stays a plain id→value source.
	//
	// Holds a COPY, NOT a reference. A row node uses ibVariantDataValueModel (a const-ref variant)
	// because its value lives IN the node and outlives the variant. The resolver instead passes a
	// TEMPORARY (the per-row resolved value) that dies when its scope ends — a ref variant would
	// dangle and crash in GetType() on the next render (AV on 0xcccccccc). The owning instantiation
	// keeps the value alive for the variant's lifetime.
	static void ValueToVariant(wxVariant& variant, const ibValue& value) { variant = new ibVariantDataValueImpl<ibValue>(ibValue(value)); }

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

			// WHAT A VALUE IN THIS COLUMN MAY BE — the model-layer twin of GetTypeDesc / GetTypeValueDesc
			// (backend_type.h). A column that DECLARES its own type (a value-table's edited property) answers
			// both the same way, which is the default here; a column that WRAPS something already able to tell
			// them apart — a metaobject attribute, a queryable column — passes the question on instead of
			// re-deciding it. Read by whoever holds VALUES against the column: a filter's right-hand side.
			virtual const ibTypeDescription GetColumnTypeValue() const { return GetColumnType(); }

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

		// THE COLUMN'S TYPE BY ID — one body over GetColumnByID, and NOT virtual, because a column's type
		// is a property of the column and not of the collection holding it. It used to be written three
		// times: the value-table's hand loop (same test, same order, same answer), and two map lookups on
		// the section / register collections that had NO callers and would have THROWN on a miss — three
		// bodies, two miss behaviours. A miss is empty, everywhere.
		const ibTypeDescription GetColumnType(unsigned int col) const {
			ibValueModelColumnInfo* colInfo = GetColumnByID(col);
			return colInfo != nullptr ? colInfo->GetColumnType() : ibTypeDescription();
		}

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

		// The value this row hands back when the table is a PICKER (the TableBox's Command_Choose → NotifyChoice):
		// a reference / key / current row. Done ONCE here — the line REDIRECTS to its owner MODEL, which defines the
		// select value PER TABLE KIND (a list / tree → its reference, a register → its record key). A model with no
		// special one leaves it empty, so the row falls back to its OWN value (current row: value table / tabular).
		virtual ibValue GetSelectValue() const {
			const ibValue selectValue = GetOwnerModel()->GetItemSelectValue(m_lineItem);
			return selectValue.IsEmpty() ? GetValue() : selectValue;
		}

		//set meta/get meta
		virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) {
			if (!IsLineAttached()) return false;
			return GetOwnerModel()->SetValueByMetaID(m_lineItem, id, varMetaVal);
		}

		virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const {
			return GetOwnerModel()->GetValueByMetaID(m_lineItem, id, pvarMetaVal);
		}

		//set hop/get hop — the row's SCALAR hop gate. A table cell reached mid dot-walk hops THROUGH the line:
		//the line already pins the row, so it re-expresses the scalar {id,type} hop as the owner model's ROW
		//hop (m_lineItem). Twin/composite handling stays on the model side. Mirrors Get/SetValueByMetaID above.
		virtual bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value) {
			if (!IsLineAttached()) return false;
			return GetOwnerModel()->SetValueBySourceHop(m_lineItem, hop, value);
		}

		virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& pvarMetaVal) const {
			return GetOwnerModel()->GetValueBySourceHop(m_lineItem, hop, pvarMetaVal);
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

	// (GetSortModels DELETED — sort is L5 (the composer, by name). The keyset anchor + header arrows read
	//  m_composer.GetSortAt + GetColumnIDByName inline. ibSortModel is gone.)

	// Re-export the canonical Features type from ibDataViewModel so
	// concrete ibValueModel-derived classes can keep saying `Features`
	// in their override declarations (these classes inherit
	// ibValueModel, not ibDataViewModel — only the provider impl
	// bridges to the data-view side).
	using Features = ibDataViewModel::Features;

	// Concrete models advertise their USER-FACING feature set here; the provider impl forwards to
	// ibDataViewModel::GetFeatures() so the GUI sees the same value via either path. DEFAULT = no flags (a plain
	// model gets no Filter/Sort/Group tab until a list/tree subclass opts in). Fetch is uniform (RunComposerPage)
	// → no RamFetch/DbFetch here; HasKeyedRows() below carries the only real RAM-vs-keyed distinction left.
	virtual Features GetFeatures() const { return Features{}; }

	// RAM-backed models (value table / tabular section) have NO source primary key — their rows restore by index,
	// not key. Derived, source-agnostic; replaces the retired RamFetch flag. Out-of-line (needs the queryable).
	virtual bool HasKeyedRows() const;

	// True when the composer currently GROUPS (a tree): a mutated/added row must re-place into its group, so the
	// control routes the mutation through the paged refresh + selection-restore path. Out-of-line (needs the
	// composer's full type). The provider impl forwards IsGroupedModel() here.
	virtual bool IsGroupedModel() const;

	// Script-side iteration. Every ibValueModel is paged (IsPagedModel() == true) and fetches through
	// RunComposerPage, so iteration drives Get*Fetch in batches — UNLESS the composer currently GROUPS (a tree;
	// a flat walk over a grouped model would surprise users), which falls through to the base (no iteration).
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;

	// IntelliSense type-hint factory — returns an empty (skeleton) row
	// of the model's row type so the editor's static parser can offer
	// completions on `For Each x In model`. Default is empty (no hint).
	virtual ibValue GetEmptyRow() { return ibValue(); }

	// Filter + Sort both live in L5 now (ListSettings over the composer) — the per-model ibFilterRow /
	// ibSortOrder accessors are gone.

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

	// (Header-click sort is committed on the FRONT — ibValueModelTableBox::OnColumnClick pokes this model's
	//  composer directly + RefetchAll, the same shape the settings dialog uses. No sort method on the model.)

	// Hierarchical-breadcrumb hook (see ibDataViewModel virtual for
	// semantics).  Mirrored here so concrete ibValueModel-derived
	// models can override directly without going through the provider.
	virtual void BuildAncestorBreadcrumb(const ibDataViewItem& fromRow,
	                                     ibDataViewItemArray& out) const {
		(void)fromRow; (void)out;
	}

	// Universal paged-fetch API.  Mirrors the ibDataViewModel virtuals
	// so concrete models override here directly.
	//
	// The base now delegates to the universal composer-fetch core
	// (RunComposerPage below): any model with a source + composer gets a
	// composer-driven fetch DEFAULT without an intermediate override. The
	// direction is derived from (which fetch, anchor) exactly as the
	// table-of-values / dynamic-list wrappers do:
	//   GetFirstFetch -> Reset
	//   GetNextFetch  -> Reset when no anchor, else Forward
	//   GetPrevFetch  -> 0 when no anchor, else Backward
	// Models that intercept their own backing store (RAM table base's
	// BuildVisibleView slice, tree base, dynamic list) still override and
	// are unaffected by this default.
	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const {
		return RunComposerPage(parent, anchor, count, ibFetchDirection::Reset, out);
	}
	virtual unsigned int GetNextFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const {
		if (!anchor.IsOk())
			return RunComposerPage(parent, ibDataViewItem(), count, ibFetchDirection::Reset, out);
		return RunComposerPage(parent, anchor, count, ibFetchDirection::Forward, out);
	}
	virtual unsigned int GetPrevFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const {
		if (!anchor.IsOk())
			return 0;
		return RunComposerPage(parent, anchor, count, ibFetchDirection::Backward, out);
	}

	// --- the paged fetch (split BY DATA SOURCE) ----------------------------
	// The Get*Fetch triple routes here. It is PURE VIRTUAL — the fetch body is realised per source kind:
	//   * ibValueModelCursor::RunComposerPage  — renders the composer to SQL over the queryable, keyset-pages it,
	//     wraps each driver row in a COPY node; hierarchy DRILL via grouping / parent-reference tree.
	//   * ibValueModelStorage::RunComposerPage — filters + sorts the LIVE rows in place (ibDataRamComposer.ComputeOrder)
	//     and returns the LIVE storage rows windowed by the browsed anchor (the node IS the storage row).
	// (parent, anchor, count, dir) → the page; the model wraps the result into `out`.
	virtual unsigned int RunComposerPage(const ibDataViewItem& parent, const ibDataViewItem& anchor,
		int count, ibFetchDirection dir, ibDataViewItemArray& out) const = 0;

	// Page a RAM value-storage — the ONE in-memory paging primitive (the RAM sibling of RunComposerPage), shared by
	// BOTH the native RAM model (ibValueModelStorage, its own m_storage/m_composer) AND a DB model's whole-list RAM
	// SNAPSHOT (ibValueModelCursor when DynamicRead is off, its m_snapshot/m_snapshotComposer). `storage` + `composer`
	// MUST be a bound pair (composer.FromStorage(&storage)). ComputeOrder gives the display order; then window flat by
	// the anchor, or (grouping configured) emit synthetic group nodes per level and the live scoped rows at the leaf.
	// Def in tabularModelRam.cpp beside the RAM engine. (No SQL, no queryable — the rows are already in memory.)
	unsigned int RunStoragePage(ibRamValueStorage& storage, ibDataRamComposer& composer,
		const ibDataViewItem& parent, const ibDataViewItem& anchor,
		int count, ibFetchDirection dir, ibDataViewItemArray& out) const;


	// Point lookup for a FindRowValue restore STUB: given an anchor's row-key (PK value(s)), fetch that ONE row
	// from the source and return its value map, so RunComposerPage can fill the anchor's sort tuple and the
	// keyset predicate positions the page AT the row. Source-agnostic (same composer/driver path). Empty on miss.
	// This is where the old per-list FindRowValue sort-value pre-reading moved — into L5, keyed by the row-key.
	std::map<ibMetaID, ibValue> ResolveAnchorByKey(const std::vector<ibValue>& rowKey) const;

#pragma region _data_model_h_
	ibDataViewModelProviderImpl* GetDataViewModel() const { return m_modelProvider; }
#pragma endregion

	//////////////////////////////////////////////////////////////////////////////////////////////////

	// (GetSelection / GetDrillParent / RowValueStartEdit removed — the model no longer reads the current row / drill
	// from the control, nor tells it to start editing. Every command receives the front's selection as an argument;
	// inline editing is opened DIRECTLY on the control by the TableBox (OnItemActivated → EditItem).)

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Paging hooks — backend is stateless: model exposes Get*Fetch
	// only.  The GUI keeps its own deque inside ibDataViewCtrl and
	// dispatches GetNextFetch / GetPrevFetch when scroll approaches
	// an edge.  Every ibValueModel-derived model is paged by
	// architecture; the IsPagedModel() flag on the data-view base
	// only differentiates ibValueModel-derived (paged) from native
	// non-paged wxDVC models (lists, tree-stores, predefined editor).

	// A RUNTIME MODEL READS A DATABASE, so this override sends the portion to a
	// RENTED background run (ibJobTenancy::Tenant): a session of its own, because a
	// session owns one connection and the caller's is busy being the caller's, and
	// nothing else of its own — no identity, no runtime, no row in Active Users,
	// and the caller's access policy borrowed, so the read sees exactly what the
	// caller sees. It is used up, brings the data back and ends.
	//
	// WHERE the caller lives does not enter into it. A list is a list in the
	// Designer, in the thick client and in a browser tab; the run is the same in
	// all three.
	//
	// Everything that cannot be rented lands INLINE — a saturated pool, a registry
	// going down, a host with no job manager at all (tests, headless). The caller
	// is never told which happened: it hands over a unit of work and gets it done.
	// Marshalling the ANSWER back to whoever asked stays the caller's business
	// (the control posts it to the UI thread), so the backend keeps no idea that a
	// UI exists.
	// (Not an override of the view virtual — ibValueModel does not derive from
	//  ibDataViewModel; the provider bridge above forwards it here.)
	virtual void SubmitFetchAsync(std::function<void()> work);

	// Tell the read in flight to stop, and wait it out. The destructor does this too — this is for
	// the moment a WINDOW goes away while a read (a report composing, say) is still running and
	// holding references of its own, so the model itself is not dying yet. See the body.
	void CancelFetch();


	//////////////////////////////////////////////////////////////////////////////////////////////////

	// L5 selection-restore: a row's identity IS its row-key (the value). Build ONE stub composer node carrying
	// that key; the freshly-fetched page matches it by m_rowKey (IsEqualTo) and lands focus. No per-list override
	// and no sort-anchor — the restore matches within the fetched batch by KEY, never by a cursor tuple, so the
	// old catalog/register anchor-reading was dead weight. In-memory tables (script `Find(value, col)`) override
	// this with a real column search.
	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const {
		(void)colName;
		if (varValue.IsEmpty()) return ibDataViewItem();
		auto* stub = new ibComposerNode(std::vector<ibValue>{ varValue });
		ibDataViewItem item(stub);   // IncRef → 2
		stub->DecRef();              // refcount = 1, owned by item
		return item;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool AutoCreateColumn() const { return false; }


	// A CONTAINER row (a grouping header — its cell is the group's own dimension value, not a real data row) is
	// never inline-editable; only a leaf row is. Subclasses compose this (AND their own rule). The one place the
	// "you can't edit a group cell" rule lives.
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const { return !IsContainer(item); }

	// (ActivateItem removed — the double-click is fully a FRONT concern now: the TableBox DECIDES choice → select /
	// editable → edit the cell / read-only → ActivateRow. The model only EXECUTES the read-only open (per type),
	// reusing its CallAsCommand dispatch — it never decides the double-click.)

	// Sortability is no longer gated by a per-column m_sortOrder entry (retired) — in the L5 world every
	// bound data column is sortable; the data-view column carries its OWN IsSortable() gate (datavgen.cpp),
	// and the header click drives the composer ORDER BY on the front (tablebox OnColumnClick). (col unused now.)
	virtual bool IsSortable(unsigned int col) const { (void)col; return true; }

	// --- Command store (ibStandardCommandTabular) ---------------------------------------------------------------
	// The model is a STORE of commands: it lists its OWN set (GetCommandCollection) and executes one by id against the
	// FRONT-passed current row (CallAsCommand). No action composition, no widget pull — the TableBox merges the
	// set into the real action it hands the command bar and passes the current row on execute. Base = nothing;
	// concrete models ship their own (value table: Add/Copy/Edit/Delete; list: + MarkAsDelete; folder: + AddFolder).

public:
	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const override {}
	virtual void CallAsCommand(const ibActionID& lNumAction, const ibDataViewCommandContext& ctx, class ibBackendValueForm* srcForm) override {}

	// Double-click on a READ-ONLY row (a list) opens the row's OWN form — delegated here so each model opens it
	// PER TYPE (object / folder / register recorder) through its existing CallAsCommand path (the SAME the Edit
	// command runs). A public NAMED intent over the protected eEditValue id — the front says "activate this row",
	// not a raw command id. (value.ShowValue on the row value was unreliable: it needs an owned-reffer the models
	// don't uniformly return, and a register opens its recorder, not a reference.)
	virtual void ActivateItem(const ibDataViewItem& row, class ibBackendValueForm* srcForm) {}

	// The row's picker SELECT value, chosen PER MODEL KIND. Base = empty → the ReturnLine falls back to the row's
	// own value (the current row: value table / tabular). A concrete list overrides — a catalog / document / folder
	// → its reference (its metaobject's data-reference cell), a register → its record key.
	virtual ibValue GetItemSelectValue(const ibDataViewItem& item) const { return ibValue(); }

	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) = 0;
	virtual ibValueModelColumnCollection* GetColumnCollection() const = 0;

	// WHAT THIS COLUMN ACCEPTS (ibTabularDataObject's question, and the structure hop's only input) —
	// asked of the model's OWN column collection, which every model already keeps in step with its truth:
	// a tabular section's column info reads the ATTRIBUTE (GetTypeDesc) straight, a dynamic list's wraps
	// the queryable column, a value-table's holds the type property the user edits. So the answer exists
	// once, per model, where it is already synchronised — and asking it here means no model has to say it
	// a second time. (It briefly WAS said a second time: three overrides re-scanning the same three
	// collections by hand, which is the loop GetColumnByID already runs.)
	virtual ibTypeDescription GetColumnTypeById(const ibMetaID& id) const override {
		ibValueModelColumnCollection* colCollection = GetColumnCollection();
		return colCollection != nullptr ? colCollection->GetColumnType(id) : ibTypeDescription();
	}

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

	// Reverse of GetColumnIDByName: resolve a column id to its NAME via the column
	// collection. Returns an empty string if no column carries that id (or there is
	// no collection). Used to key the list-settings Filter (which is name-based) from
	// a command that only knows the column id.
	virtual wxString GetColumnNameByID(unsigned int colId) const {
		ibValueModelColumnCollection* colCollection = GetColumnCollection();
		if (colCollection == nullptr)
			return wxEmptyString;
		ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colCollection->GetColumnByID(colId);
		if (colInfo == nullptr)
			return wxEmptyString;
		return colInfo->GetColumnName();
	};

	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) = 0;
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& cVa) const = 0;


	// The hop gate comes in TWO forms — with a row and without one (the structure step) — and declaring only
	// one of them here would HIDE the other for every caller holding an ibValueModel*. It compiles today only
	// because the caller that walks structure holds an ibTabularDataObject*; the next one that does not would
	// get a compile error reading "no such method" rather than "you hid it".
	using ibTabularDataObject::GetValueBySourceHop;
	using ibTabularDataObject::SetValueBySourceHop;

	// THE table hop gate (ibTabularDataObject override) — a DIRECT translation: read the row cell by the hop's id.
	// It only GETS the value; it does NOT hop. The transition tabular-object -> source-object (and the walk on)
	// lives in GetValueByPath -> the source's own ResolvePath. Mirrors the scalar objectList gate.
	virtual bool GetValueBySourceHop(const ibDataViewItem& item, const ibSourceHop& hop, ibValue& out) const override { return GetValueByMetaID(item, hop.m_id, out); }
	virtual bool SetValueBySourceHop(const ibDataViewItem& item, const ibSourceHop& hop, const ibValue& value) override { return SetValueByMetaID(item, hop.m_id, value); }

#pragma region _data_model_h_

	// The data ACCESSORS — GetValue/SetValue/GetAttr/IsEnabled/GetParent/IsContainer/Compare — and the L3
	// source-hook GetSourceQueryable are defined CONCRETELY further down in THIS class (the former
	// ibValueModel body, folded in): they read/write the row NODE (ibComposerNode) over the RAM
	// storage, forwarding to the GetValueByRow/SetValueByRow extension points. Only the container/parent
	// SHAPE predicates stay declared up here.

	// return true if the given item has a value to display in the given
	// column: this is always true except for container items which by default
	// only show their label in the first column (but see HasContainerColumns())
	virtual bool HasValue(const ibDataViewItem& item, unsigned col) const {
		return col == 0 || !IsContainer(item) || HasContainerColumns(item);
	}

	// (HasParentTopItem / SetParentTopItem / GetParentTopItem removed — vestigial "current parent for
	//  hierarchical view" hooks with no overrides and no callers; the drill context is GetDrillParent() +
	//  the composer group-path now.)

	// Does a container render its data columns (not just a col-0 label)? BOTH a folder AND a group do: a folder is
	// a full item (every column, like a leaf); a GROUP node carries its dimension VALUE in the grouped column
	// (RunComposerPage stamps it there), so the header must render that column to show WHAT it groups by — the
	// wx default (collapse a container to column 0) hid it, since column 0 of a value-table is the line-number column, not
	// data. Only the grouped column carries a value on a group node; the rest come back null → blank.
	virtual bool HasContainerColumns(const ibDataViewItem& item) const {
		return GetViewData<ibComposerNode>(item) != nullptr;
	}

	// GetChildren removed — concretes implement GetFirstFetch (and
	// GetNextFetch / GetPrevFetch when paged). Old non-paged sources
	// just return the whole batch from GetFirstFetch and leave Next /
	// Prev as base-default no-ops.

	virtual bool HasDefaultCompare() const { return false; };

	// internal
	virtual bool IsListModel() const { return false; };
	virtual bool IsVirtualListModel() const { return false; };

	// --- L5 composer -------------------------------------------------------
	// The model's declarative composer (L5-1). The BASE holds NONE — the composer is split BY DATA SOURCE: a
	// DB model (ibValueModelCursor) holds an ibDataDBComposer (renders SQL over a queryable), a RAM model
	// (ibValueModelStorage) holds an ibDataRamComposer (filters + sorts its LIVE rows in place — it holds the model /
	// node directly, no queryable). Each subclass OVERRIDES this to return its own concrete composer. The
	// const accessor hands out a MUTABLE ref (the composer is a scratch utility the model drives on the const
	// fetch path, NOT logical const-state — the subclass member is `mutable`). See docs/ram-composer-decoupling.md.
	//
	virtual ibDataComposer& GetModelComposer() const = 0;

#pragma endregion

protected:

	// (Filter + Sort state are GONE from the model — both live in L5: ListSettings->Filter / ->Order,
	// applied to the composer per fetch. The old ibFilterRow m_filterRow / ibSortOrder m_sortOrder are removed.)

	// (The former SetUiDispatcher / DispatchToUi hop was REMOVED. It was a
	//  process-wide static, which is the wrong shape twice over: the backend had
	//  to know what a host's UI thread is, and a web server — one process, MANY
	//  sessions — has no single answer to give it. The question "run this where
	//  the view lives" is already answered, per session and polymorphically, by
	//  ibSession::Submit → ibWorkerPool: the GUI pool drains onto the wx main
	//  thread, the headless pool onto that session's FIFO worker, and a host with
	//  no pool runs inline because nothing is watching. A background read hands
	//  its result back by Submitting to the session that ASKED for it — captured
	//  when the read starts, never ibSession::Current(), which in the background
	//  is the reading session, not the form's.)

	// Monotonic change counter — bumped on any value / row mutation the model notifies the GUI of (the
	// structural-mutation reset path bumps it). Subclasses bump through BumpViewGeneration(), not the field.
	void BumpViewGeneration() const { ++m_viewGeneration; }   // m_viewGeneration is mutable (a view counter, not model state)
	uint32_t GetViewGeneration() const { return m_viewGeneration; }   // read it — the RAM snapshot re-materialises when it moves

private:

	mutable uint32_t m_viewGeneration = 0;   // view-change counter — mutable so const notify/fetch paths can bump it


	// ===== FOLDED IN: the former ibValueModel body (Max: "the base model becomes a tree; a list = a
	// tree without children; any table is simultaneously in both states"). The row NODE, the concrete data-model
	// accessors (GetValue/SetValue/GetAttr/IsEnabled/GetParent/IsContainer/Compare), the RAM storage + its
	// ops, GetSourceQueryable and the StorageIndexOf bridge all live HERE now — ibValueModel IS the single,
	// tree-by-default model. ibValueModel / RamTableBase / TreeBase are aliases of it (declared below). =====
public:

	struct ibComposerNode : public ibDataViewObject {

		// Logical equality for paged refetch: rows with matching value
		// map represent the same business row, even when behind fresh
		// pointers from a new fetch.
		//
		// THE node is the SINGLE row type (Max: "collapse ibComposerNode and ibComposerNode"): both a
		// RAM-storage row AND the DB-list fetch COPY row. A copy carries value COPIES + a stable identity
		// (row-key / group-path); a RAM row is a plain model-linked storage row (its stable POINTER is its
		// identity). `ibComposerNode` is the nicer ALIAS the fetch / FindRowValue construct it through.
		//
		// Identity for paged re-fetch + selection survival: a GROUP node identifies by its dimension-value path;
		// a DB-list row / FindRowValue stub by its primary-key row-key (ibValue == compares a reference key BY
		// GUID, so a guid-keyed stub matches a freshly-fetched row); a RAM row survives by its stable pointer, so
		// it rarely falls to the inherited value-map tail. Mixed kinds never match.
		virtual bool IsEqualTo(const ibDataViewObject& other) const override {
			const ibComposerNode* o = dynamic_cast<const ibComposerNode*>(&other);
			if (o == nullptr) return false;
			const bool lhsGroup = !m_groupPath.empty();
			const bool rhsGroup = !o->m_groupPath.empty();
			if (lhsGroup || rhsGroup)
				return lhsGroup == rhsGroup && m_groupPath == o->m_groupPath;
			// A RAM live row survives selection by its STABLE POINTER (same storage row across re-fetch), so the
			// value-map fallback is rarely consulted for it; a DB-list copy node identifies by its primary-key
			// row-key (a guid reference compares BY GUID, so a FindRowValue stub matches a freshly-fetched row).
			if (!m_rowKey.empty() && !o->m_rowKey.empty())
				return m_rowKey == o->m_rowKey;
			return m_nodeValues == o->m_nodeValues;
		}

		// Detached state — a model-linked storage row is attached while `m_valueTable` is set (base Clear /
		// Remove null it on a row that may outlive the model via refcount-pinning by an ibDataViewItem held
		// elsewhere — selection cache, ReturnLine). A self-contained composer copy (m_selfContained: holds value
		// copies, m_valueTable null) is ALWAYS attached. Script reads/writes via ReturnLine consult this to
		// refuse on dead rows.
		virtual bool IsAttached() const override { return m_valueTable != nullptr || m_selfContained; }

		ibComposerNode() :
			m_valueTable(nullptr), m_nodeValues() {
		}

		ibComposerNode(const ibComposerNode& tableRow) :
			m_valueTable(tableRow.m_valueTable), m_nodeValues(tableRow.m_nodeValues),
			m_groupPath(tableRow.m_groupPath), m_rowKey(tableRow.m_rowKey),
			m_container(tableRow.m_container), m_selfContained(tableRow.m_selfContained) {
		}

		// --- composer-fetch ctors (out-of-line: they need ibBackendQueryable's full type / are non-trivial) ---
		// DETAIL row (a DB-list fetch copy): copy the L5 driver row's metaID->value pairs into m_nodeValues;
		// `container` = the driver row's hasChildren (a drillable folder in a parent-ref tree — the SAME node
		// serves a flat list AND a hierarchy); `rowKey` = the source's primary-key values (DB-list identity /
		// FindRowValue match key). Self-contained copy (IsAttached -> true; the fetch is const, no const_cast).
		explicit ibComposerNode(const std::map<ibMetaID, ibValue>& values,
			bool container = false, std::vector<ibValue> rowKey = {});
		// STUB row (FindRowValue selection-restore): carries ONLY the row-key the caller navigates to, so the
		// control matches it against a freshly-fetched row by row-key.
		explicit ibComposerNode(std::vector<ibValue> rowKey);
		// GROUP-drill row (RunComposerPage grouping branch): the dimension-value path root->this (so children
		// re-fetch scoped, dim==value per drilled level) + a container flag so the view renders it drillable.
		ibComposerNode(const std::map<ibMetaID, ibValue>& values, const std::vector<ibValue>& groupPath, bool container);

		// Dimension-value path root->this for a GROUP node (empty for a detail row); RunComposerPage reads the
		// browsed parent's path to scope the next level's fetch (dim==value per already-drilled level).
		const std::vector<ibValue>& GetGroupPath() const { return m_groupPath; }
		bool IsGroup() const { return !m_groupPath.empty(); }

		// GROUP caption (self-described, model-free): the node folds by a dimension VALUE — hand back that value's
		// PRESENTATION so the front paints it as the spanning group caption. The node's OWN (deepest) dimension
		// value is m_groupPath.back(); a detail / key-only node has no group path → false (base default), so the
		// front keeps per-column rendering.
		virtual bool GetGroupCaption(wxString& caption) const override {
			if (m_groupPath.empty())
				return false;
			caption = m_groupPath.back().GetString();
			return true;
		}

		// Source primary-key value(s) — the row's stable identity. A FindRowValue restore STUB carries ONLY this
		// (no node values); RunComposerPage resolves the rest of its sort tuple by a point lookup on this key.
		const std::vector<ibValue>& GetRowKey() const { return m_rowKey; }
		bool IsKeyOnlyAnchor() const { return m_nodeValues.empty() && !m_rowKey.empty(); }

		/////////////////////////////////////////////////////////////////////////////

		template <class varType>
		inline void AppendTableValue(const ibMetaID& id, varType&& variant) { m_nodeValues.insert_or_assign(id, variant); }
		inline ibValue& AppendTableValue(const ibMetaID& id) { return m_nodeValues[id]; }

		/////////////////////////////////////////////////////////////////////////////

		const ibRowMetaValues& GetTableValues() const { return m_nodeValues; }

		/////////////////////////////////////////////////////////////////////////////

		// Mirror an edited cell into m_nodeValues + notify the owning model. A RAM list edits its LIVE storage
		// rows THROUGH this (m_valueTable set → the storage IS the displayed row), so the edit lands directly; a
		// DB-list COPY node (self-contained, m_valueTable null) just mirrors into the copy for the inline editor
		// (the grid is read-only — a real edit goes via the object form). Out-of-line (non-trivial).
		virtual bool SetValue(const ibMetaID& id, const ibValue& variant, bool notify = false);

		bool SetValue(unsigned int col, const wxVariant& variant, bool notify = false) {
			try {
				ibValue& cValue = m_nodeValues.at(col);
				std::vector<ibValue> listValue;
				if (cValue.FindValue(variant.GetString(), listValue)) {
					const ibValue& cFoundedValue = listValue.at(0);
					if (notify && m_valueTable != nullptr && cValue != cFoundedValue)
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

		// (CompareRow GONE — the in-memory RAM sort it drove is replaced by L5 composer ORDER BY.)

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
		}

		bool GetValue(unsigned int col, wxVariant& variant) const {

			try {
				variant = new ibVariantDataValueModel(GetTableValue(col));
				return true;
			}
			catch (std::out_of_range&) {
				return false;
			}
		}

		// --- STORAGE value-tree ("a list = a tree without children", Max) + DISPLAY-parent (composer-set) --------
		// The node carries STORAGE children (m_children, downward) — a flat row just never gets any — AND a
		// DISPLAY-parent the composer stamps per slice (m_parent). IsContainer is the composer's slice FLAG (below),
		// NOT has-children. The dtor releases the held display-parent (DecRef) and frees owned storage children
		// (DecRef); empty vectors make both no-ops. A COPY inherits neither (a fresh row owns nothing).
		virtual ~ibComposerNode() {
			SetDisplayParent(nullptr);   // release the held display-parent
			for (ibComposerNode* child : m_children)
				if (child != nullptr) { child->m_valueTable = nullptr; child->DecRef(); }
		}

		// Container-ness is the COMPOSER's slice call, carried as a flag (m_container): a GROUP level / a folder
		// the composer laid out hierarchically. It is NOT "has storage children" — the same storage row is a leaf
		// in a FLAT slice and an expander in a HIERARCHICAL slice; the composer decides per slice and sets the flag.
		virtual bool IsContainer() const override { return m_container; }

		// DISPLAY-parent — the node the COMPOSER fetched this one a level under (a grouped detail → its group; a
		// top-level / flat row → null). The COMPOSER sets it per slice (SetDisplayParent), so it follows the CURRENT
		// arrangement and toggles with group↔flat. Held as a MANUALLY-refcounted strong ref (IncRef/DecRef): it does
		// NOT dangle (the parent stays alive while a child points at it) and does NOT cycle (a backend group holds no
		// children — the slice tree lives on the control). GetParentItem lets the frontend walk the slice upward.
		virtual ibDataViewItem GetParentItem() const override {
			return m_parent ? ibDataViewItem(m_parent) : ibDataViewItem();
		}
		void SetDisplayParent(ibComposerNode* parent) {
			if (parent == m_parent) return;
			if (parent != nullptr)   parent->IncRef();
			if (m_parent != nullptr) m_parent->DecRef();
			m_parent = parent;
		}

		std::vector<ibComposerNode*>& GetChildren() { return m_children; }
		ibComposerNode* GetChild(unsigned int n) const { return m_children.at(n); }
		unsigned int GetChildCount() const { return static_cast<unsigned int>(m_children.size()); }

		bool Append(ibComposerNode* child, bool notify = true) {
			child->m_valueTable = m_valueTable;
			m_children.emplace_back(child);
			if (notify && m_valueTable != nullptr &&
				!m_valueTable->m_modelProvider->ItemAppended(ibDataViewItem(this), ibDataViewItem(child))) {
				child->m_valueTable = nullptr;
				m_children.pop_back();
				return false;
			}
			return true;
		}

		bool Insert(ibComposerNode* child, unsigned int n, bool notify = true) {
			child->m_valueTable = m_valueTable;
			auto it = m_children.insert(m_children.begin() + n, child);
			if (notify && m_valueTable != nullptr &&
				!m_valueTable->m_modelProvider->ItemInserted(ibDataViewItem(this), ibDataViewItem(child))) {
				child->m_valueTable = nullptr;
				m_children.erase(it);
				return false;
			}
			return true;
		}

		bool Remove(ibComposerNode* child, bool notify = true) {
			auto it = std::find(m_children.begin(), m_children.end(), child);
			if (notify && m_valueTable != nullptr &&
				!m_valueTable->m_modelProvider->ItemDeleted(ibDataViewItem(this), ibDataViewItem(child)))
				return false;
			if (it != m_children.end())
				m_children.erase(it);
			child->m_valueTable = nullptr;
			child->DecRef();
			return true;
		}

		// --- FLUENT RAM builder ------------------------------------------------------------------------------
		// Chain row construction over the RAM value-tree: node.AddRow().Set(col, v).Set(col2, v2)…. Set / AddRow
		// return a node REFERENCE so the chain flows; AddRow appends a CHILD row under this node (a STORAGE
		// value-tree level — downward links only; no per-op notify, a batch build resets the view once at the end).
		ibComposerNode& Set(const ibMetaID& col, const ibValue& val) { SetValue(col, val, false); return *this; }
		ibComposerNode& AddRow() {
			ibComposerNode* child = new ibComposerNode();
			Append(child, false);   // → m_children (Append sets the child's model back-link)
			return *child;
		}

		// (node-level recursive SortChildren removed — only the deleted RamTreeBase used it; the in-memory
		//  RAM sort it would have driven is gone too, replaced by the L5 composer ORDER BY over the RAM queryable.)

	private:
		// The model (ibValueModel) + the RAM value-storage (ibRamValueStorage, which OWNS the RAM nodes) reach
		// the node's internals to set/clear m_valueTable on attach/detach + read cells for filter/sort.
		friend class ibValueModel;
		friend class ibRamValueStorage;
	protected:
		// const — the node only READS its model link (IsAttached) and pokes the const notifier
		// (RowChanged/RowValueChanged); it NEVER mutates the model through it, so a const fetch path hands a
		// node the model with no const_cast. The model's own (non-const) RAM ops set/clear it via the friend grant.
		const ibValueModel* m_valueTable;
		ibRowMetaValues m_nodeValues;
		// DISPLAY-parent — the slice node this row was fetched a level under (the COMPOSER sets it via
		// SetDisplayParent; null = top-level). MANUALLY refcounted (IncRef/DecRef) so it neither dangles nor leaks
		// beyond the slice; released in the dtor / on re-set.
		ibComposerNode* m_parent = nullptr;
		// STORAGE value-tree children (downward). Empty for a flat row; populated for a folder / built subtree.
		// The COMPOSER reads these to lay out the flat / hierarchical display slice.
		std::vector<ibComposerNode*> m_children;
		// --- composer-fetch identity (set by the composer ctors; inert on a plain RAM storage row) ------------
		std::vector<ibValue> m_groupPath;                         // GROUP node: dimension values root->this; empty for a detail row
		std::vector<ibValue> m_rowKey;                            // DB-list / stub identity: source primary-key value(s)
		bool m_container = false;                                 // drillable group level / folder — OR'd with !m_children.empty()
		bool m_selfContained = false;                             // composer copy (value copies, m_valueTable null) — IsAttached -> true
	};

	// The universal node IS the tree node — a list is just a childless one. The historical ibValueTreeNode
	// name now aliases it so legacy tree code keeps compiling while the parallel class is gone.
	using ibValueTreeNode = ibComposerNode;

public:

	// (ctor / dtor are ibValueModel's — the dtor Clears the RAM rows then DecRefs the provider, in tabularModel.cpp.)

	/////////////////////////////////////////////////////////

	// (Row count / row access GetRowCount / GetRow / GetItem are declared LOWER as virtual DB-defaults — a DB
	// model has NO stored rows; ibValueModelStorage overrides them to read its ibRamValueStorage.)

	/////////////////////////////////////////////////////////

	// Notification helpers — drive the ItemChanged / ValueChanged
	// hook from anywhere a row mutation happens (Set / write-through
	// from script).  No storage dependency, lives on the base.
	// const: a notification POKES the view provider (ItemChanged / ValueChanged) and bumps the view
	// generation — it does NOT mutate the model's logical state, so a node holding a `const` model link
	// (the composer/dynamic-list const fetch path builds rows with a const `this`) can notify with NO
	// const_cast. BumpViewGeneration is const too (m_viewGeneration is mutable).
	void RowChanged(ibComposerNode* item) const {
		BumpViewGeneration();   // a row's values changed -> the filtered/sorted view may differ
		m_modelProvider->ItemChanged(ibDataViewItem(item));
	}

	void RowValueChanged(ibComposerNode* item, unsigned int col) const {
		BumpViewGeneration();   // a cell changed -> filter membership / sort order may differ
		m_modelProvider->ValueChanged(ibDataViewItem(item), col);
	}

	// Narrow structural notifies for a SINGLE top-level row mutation — the RAM value-storage's
	// Add / Insert / Remove drive these. Fire the SPECIFIC ItemAppended / ItemInserted / ItemDeleted event
	// (NOT the heavy NotifyReset) so the view positions AND selects the mutated row in place: the desktop
	// control's DoItemInserted ends with Select(item), and its grouped / keyed path re-fetches with the row
	// as the restore anchor — either way focus lands on the new row. NotifyReset re-fetches the page without
	// moving the selection onto the new row, so Add / Copy / Insert left the highlight on the source row.
	// The storage rows are the root's DIRECT children (top-level), so the parent item is the invisible root =
	// an empty ibDataViewItem — the same convention the wx index / virtual-list models use
	// (ItemInserted(ibDataViewItem(), item)). notify=false (silent batch build / LoadData) just bumps the
	// view generation, matching the old NotifyStructuralChange(false) branch.
	// CONST — the notify touches only the mutable view generation + the provider (through the pointer), never the
	// model's own state; so a whole-list RAM SNAPSHOT can drive them from a const fetch (silent, notify=false).
	void NotifyRowAppended(ibComposerNode* item, bool notify) const {
		BumpViewGeneration();
		if (notify && m_modelProvider != nullptr)
			m_modelProvider->ItemAppended(ibDataViewItem(), ibDataViewItem(item));
	}
	void NotifyRowInserted(ibComposerNode* item, bool notify) const {
		BumpViewGeneration();
		if (notify && m_modelProvider != nullptr)
			m_modelProvider->ItemInserted(ibDataViewItem(), ibDataViewItem(item));
	}
	void NotifyRowDeleted(ibComposerNode* item, bool notify) const {
		BumpViewGeneration();
		if (notify && m_modelProvider != nullptr)
			m_modelProvider->ItemDeleted(ibDataViewItem(), ibDataViewItem(item));
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

	// THE data accessors (the provider forwards here). They forward to the GetValueByRow/SetValueByRow
	// extension points. Declared FRESH on the single model (the former pure ibValueModel::GetValue/… are gone
	// — these concrete versions took their place), so no `override`.
	virtual void GetValue(wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) const {
		GetValueByRow(variant, item, col);
	}

	virtual bool SetValue(const wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) {
		return SetValueByRow(variant, item, col);
	}

	// The VALUE forms, which these cell accessors would otherwise hide. A model IS an ibValue, and
	// ibValue declares GetValue(bool)/SetValue(const ibValue&); a same-named member here removes them
	// from lookup on every model, so `model.GetValue()` would silently stop resolving. Different
	// parameter counts — nothing becomes ambiguous, the names simply stay reachable.
	using ibValue::GetValue;
	using ibValue::SetValue;

	virtual bool GetAttr(const ibDataViewItem& item, unsigned int col,
		ibDataViewItemAttr& attr) const {
		return GetAttrByRow(item, col, attr);
	}

	virtual bool IsEnabled(const ibDataViewItem& item, unsigned int col) const {
		return IsEnabledByRow(item, col);
	}

	// Children query: DB-backed concretes pull rows via Get*Fetch;
	// the base default returns 0 (inherited from ibValueModel), so
	// no override is needed here for the non-RAM tier.

	// The DISPLAY-parent the COMPOSER put on the node (SetDisplayParent) when it laid out the current slice — a
	// grouped detail → its group, a top-level / flat row → null. The node carries it (manually refcounted), the
	// composer owns it; the model just surfaces it.
	virtual ibDataViewItem GetParent(const ibDataViewItem& item) const {
		return item.IsOk() ? item.GetParentItem() : ibDataViewItem();
	}

	// Set a fetched node's DISPLAY-parent = the node it was fetched a level UNDER (the composer's arrangement).
	// Takes the current node + the parent to set — nothing more. DEFAULT: cast BOTH sides to our one node type; if
	// the item IS a composer node, assign the parent (SetDisplayParent, which bumps the refcount; null = top-level,
	// which also releases a stale parent from a previous slice). VIRTUAL so a model can OVERRIDE it to record
	// kind-specific info on the node (a folder marker, …). RunComposerPage calls it per fetched node at its tail.
	virtual void SetItemParent(const ibDataViewItem& item, const ibDataViewItem& parent) const {
		ibComposerNode* node = GetViewData<ibComposerNode>(item);
		if (node == nullptr)
			return;
		ibComposerNode* parentNode = (parent.IsOk() && !(parent == s_constIgnoreParent))
			? GetViewData<ibComposerNode>(parent) : nullptr;
		node->SetDisplayParent(parentNode);
	}

	// Extract this item's identity KEY — the ONE object the command layer feeds into a form / lookup (open, copy,
	// edit, choose). Each model casts the item to its node and builds the key from whatever it holds: a DB cursor
	// reads its primary-key reference (guid), a register reads its dimensions (composite), a RAM/base model has no
	// key. VIRTUAL so every list / dynamic-list / register implementation decides its own key from the item —
	// a global facade over "which value identifies this row". DEFAULT: no key (empty).
	virtual ibUniqueKey GetItemKey(const ibDataViewItem& item) const;

	virtual bool IsContainer(const ibDataViewItem& item) const {
		if (!item.IsOk())
			return true;   // the invisible root always has children
		// A row may be a CONTAINER node — a GROUP level (grouping drill) or a folder in a parent-ref
		// hierarchy: route to the node's own IsContainer() so the SAME flat-table model renders a tree
		// when the fetch returns container nodes (driver hasChildren → composer node container flag). A
		// plain detail row reports false → leaf (unchanged for flat lists; FIXES the grouping drill, where
		// the group nodes used to report non-container and so never offered an expander).
		if (const ibComposerNode* node = GetViewData<ibComposerNode>(item))
			return node->IsContainer();
		return false;
	}

	// override sorting to always sort branches ascendingly
	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
		unsigned int col, bool ascending) const {

		wxASSERT(item1.IsOk() && item2.IsOk());

		// Sorting is L5 — the composer applies the ORDER BY in the fetch, and HasDefaultCompare() is false so
		// wxDVC never drives sorting through here. This override stays only to give a STABLE total order to a
		// direct caller; the per-field value compare (the old ibSortModel / GetSortModels loop) is gone with
		// the model-side sort store.
		wxUIntPtr id1 = wxPtrToUInt(item1.GetID()),
			id2 = wxPtrToUInt(item2.GetID());
		return ascending ? id1 - id2 : id2 - id1;
	}

	// (HasDefaultCompare() — declared once up in this class: false, so wxDVC leaves ordering to the composer
	// ORDER BY; the model rebuilds on sort change via BeforeReset/AfterReset.)

	// TREE BY DEFAULT (Max): every model is a potential tree (a flat list is just one with no children), so
	// the base no longer claims to be a flat wx list model — wxDVC asks GetParent / IsContainer and renders
	// expanders whenever a row reports children. IsListModel()/IsVirtualListModel() inherit ibValueModel's
	// false; the old "list model = true" fast-path override is gone (it was the table/tree split).

	// --- RAM-backed storage (FOLDED from ibValueModelStorage) -------------------------------------
	// Concrete RAM models (tabular section / table of values / record set) own their rows in m_nodeValues; DB-backed
	// concretes (catalog / register / enum list) keep it EMPTY and page from the cursor. ONE model class now
	// (Max: "ibValueModel remains the only one") — the storage lives here, simply unused by the DB tier.

	virtual bool IsEmpty() const { return GetRowCount() == 0; }

	// The source hook. A DB model overrides it to return its metaobject / register queryable (feeds keyset
	// paging + HasKeyedRows). A RAM model does NOT override it — it has no queryable (the RAM composer reads
	// ibRamValueStorage directly), so it inherits this null default (→ HasKeyedRows false → restore-by-index).
	virtual const ibBackendQueryable* GetSourceQueryable() const { return nullptr; }

	// The FOLDER-flag display column of a hierarchical LIST: folder rows render as drillable containers even when
	// EMPTY (the folder convention). The DB level-fetch reports hasChildren=false for every row (dataComposer's flat
	// path), so this is the ONLY folder signal the tree has. Handed to the LIST at creation (ibCreateHierarchyList)
	// — a display concern of the hierarchical list, NOT a queryable accessor. Null = not a folder hierarchy.
	virtual const ibBackendQueryColumn* GetFolderDisplayColumn() const { return nullptr; }

	// Discard the rendered view and re-fetch everything (batch mutations wxDVC's incremental tracking can't follow).
	// CONST (touches only the mutable view generation + the provider) so the RAM snapshot's Clear can run in a const fetch.
	void NotifyReset() const {
		BumpViewGeneration();
		if (m_modelProvider) {
			m_modelProvider->BeforeReset();
			m_modelProvider->AfterReset();
		}
	}

	// Structural-change notify hook the RAM value-storage calls after Add/Insert/Remove/Clear on the nodes: a
	// visible batch re-fetches the page (NotifyReset — the view holds fresh nodes each fetch), a silent batch
	// (notify=false) just bumps the view generation. (Public so ibRamValueStorage, which owns the nodes, drives it.)
	void NotifyStructuralChange(bool notify) const { if (notify) NotifyReset(); else BumpViewGeneration(); }

	// --- row access (source-agnostic query; base default = DB tier: NO stored rows) ------------------------
	// A DB model reads dynamically from the cursor and has NO stored rows, so these return empty; ibValueModelStorage
	// OVERRIDES them to read its ibRamValueStorage. (Kept virtual on the base so IsEmpty / the display resolve
	// polymorphically.)
	virtual long GetRowCount() const { return 0; }
	virtual long GetRow(const ibDataViewItem& /*item*/) const { return wxNOT_FOUND; }
	virtual ibDataViewItem GetItem(long /*row*/) const { return ibDataViewItem(); }

#pragma endregion
};

// ===========================================================================================
//  MODEL SPLIT BY DATA SOURCE — abstract ibValueModel + ibValueModelCursor (queryable) / ibValueModelStorage (rows)
// ===========================================================================================
// ibValueModel is ABSTRACT (GetModelComposer + RunComposerPage are pure-virtual). A model is one of TWO KINDS,
// by where its DATA comes from — NOT by display shape (a list / tree is just a fetch that returns children or
// not, on either kind):
//   * ibValueModelCursor  — source = a DB queryable; holds an ibDataDBComposer; its fetch renders SQL, keyset-pages
//     the result, and wraps each row in a COPY node (hierarchy drill via grouping / parent-ref tree).
//   * ibValueModelStorage — source = its own LIVE in-memory rows; holds an ibDataRamComposer that holds THIS model /
//     node directly and reads it in place (no queryable); its fetch returns the LIVE storage rows.
// The SHARED machinery — the universal ibComposerNode node, the display dispatch, columns, the list-settings
// facade, ResolveAnchorByKey, Get*Fetch — stays on the base. The RAM node STORAGE is NOT on the base: a DB model
// has none (dynamic cursor reads), a RAM model owns an ibRamValueStorage. The historical aliases point each
// former subclass at its kind (RAM tables → Ram, DB lists / trees → Db), so `: public ibValueModel*Base` reparents free.

// ibRamValueStorage — the RAM analog of a queryable: a flat/tree table that OWNS the live nodes (refcounted) +
// references the model's column collection. ibValueModelStorage owns one; ibDataRamComposer sources from it (FromStorage)
// and reads the nodes IN PLACE for filter / sort / group. Mutations take the MODEL (for the view notify) — the
// storage does NOT hold the model, so the composer reading it never reaches the model. NOT an ibBackendQueryable
// (a sibling RAM source). (A node's OWN m_nodeValues is its cell map; THIS owns the node ARRAY.)
// The RAM value-storage — the RAM analog of a queryable. It does NOT keep a parallel node vector: it holds a
// single ROOT node, and the rows ARE the root's CHILDREN ("the list lives inside the root", Max). A flat list
// is a root of leaf children; a value-TREE is children that carry their own children ("children as a bonus"). The
// node (ibComposerNode) is ALREADY a tree — its own m_children / Append / Remove / GetChild ARE the storage,
// so there is exactly ONE node class and ONE tree. The root is a synthetic handle: never displayed, never a row's
// display-parent (a row's m_parent is the DISPLAY-parent the COMPOSER stamps per slice, orthogonal to storage
// membership — the root only OWNS the rows via m_children). Its dtor cascades DecRef into every child, so the
// whole tree frees itself with no per-node bookkeeping here.
class BACKEND_API ibRamValueStorage
{
public:
	using Row = ibValueModel::ibComposerNode;

	// --- read (the composer's source — the top-level rows are the root's children) ---
	long  RowCount() const { return static_cast<long>(m_root.m_children.size()); }
	Row*  GetNode(long i) const { return (i >= 0 && i < RowCount()) ? m_root.m_children[static_cast<size_t>(i)] : nullptr; }
	ibDataViewItem GetItem(long i) const { Row* n = GetNode(i); return n ? ibDataViewItem(n) : ibDataViewItem(); }
	long  IndexOf(const Row* node) const {
		const std::vector<Row*>& kids = m_root.m_children;
		for (long i = 0; i < RowCount(); ++i) if (kids[static_cast<size_t>(i)] == node) return i;
		return wxNOT_FOUND;
	}
	// A node's cell (friend of the node → reads its value map directly).
	ibValue GetCell(long i, ibMetaID col) const {
		const Row* n = GetNode(i);
		if (n == nullptr) return ibValue();
		const auto it = n->m_nodeValues.find(col);
		return it != n->m_nodeValues.end() ? it->second : ibValue();
	}
	// The ROOT node — the storage IS its children list. The model / composer walk the tree through it.
	Row&       Root()       { return m_root; }
	const Row& Root() const { return m_root; }

	// Column resolution for the composer (name → id) via the model's column collection (re-pointed each fetch).
	void     SetColumns(ibValueModel::ibValueModelColumnCollection* cols) const { m_columns = cols; }
	ibValueModel::ibValueModelColumnCollection* Columns() const { return m_columns; }
	ibMetaID ColumnIdByName(const wxString& name) const;   // out-of-line (needs the column-collection type)

	// Field-path resolution over the stored rows (shared by the RAM composer's ComputeOrder AND the model's
	// grouping): SplitField cuts a path on '.' → HEAD storage column + dotted TAIL; ResolveField reads ONE row's
	// value, walking references down the tail (a reference cell IS-A source → GetSourceExplorer name→id →
	// GetValueByMetaID). Out-of-line (ResolveField needs the source-object type). See tabularModelRam.cpp.
	bool    SplitField(const wxString& path, ibMetaID& headCol, std::vector<wxString>& tail) const;
	ibValue ResolveField(long row, ibMetaID headCol, const std::vector<wxString>& tail) const;

	// --- mutation (the RAM model's ops; take the model for the view notify) — target the root's children ---
	// The model is CONST: notify=false just bumps the mutable view generation (BumpViewGeneration is const), so a
	// whole-list RAM SNAPSHOT (ibValueModelCursor, DynamicRead off) fills these from its CONST fetch — no const_cast.
	long AddValue(const ibValueModel* model, Row* node, bool notify = true) {
		wxASSERT(node); node->m_valueTable = model; m_root.m_children.emplace_back(node);
		model->NotifyRowAppended(node, notify); return RowCount() - 1;
	}
	long InsertValue(const ibValueModel* model, Row* node, unsigned int row, bool notify = true) {
		wxASSERT(node); node->m_valueTable = model;
		m_root.m_children.insert(m_root.m_children.begin() + row, node);
		model->NotifyRowInserted(node, notify); return row + 1;
	}
	bool RemoveValue(const ibValueModel* model, Row* node, bool notify = true) {
		std::vector<Row*>& kids = m_root.m_children;
		auto it = std::find(kids.begin(), kids.end(), node);
		if (it == kids.end()) return false;
		// Notify BEFORE erase + DecRef — the view's ItemDeleted handler needs the item live to locate + drop
		// its tree node (the node is still in the storage here, refcount held by kids).
		model->NotifyRowDeleted(node, notify);
		kids.erase(it); node->m_valueTable = nullptr; node->DecRef(); return true;
	}
	void Clear(const ibValueModel* model, bool notify = true) {
		std::vector<Row*>& kids = m_root.m_children;
		if (kids.empty()) return;
		for (Row* n : kids) { n->m_valueTable = nullptr; n->DecRef(); }
		kids.clear();
		model->NotifyStructuralChange(notify);
	}
	void ClearRange(const ibValueModel* model, unsigned long from, unsigned long to, bool notify = true) {
		std::vector<Row*>& kids = m_root.m_children;
		if (from > kids.size() || to > kids.size() || from >= to) return;
		for (size_t i = from; i < to; ++i) { kids[i]->m_valueTable = nullptr; kids[i]->DecRef(); }
		kids.erase(kids.begin() + from, kids.begin() + to);
		model->NotifyStructuralChange(notify);
	}
	void Reserve(long n) { m_root.m_children.reserve(m_root.m_children.size() + static_cast<size_t>(n)); }

private:
	mutable Row                                         m_root;              // the root — its children ARE the rows
	mutable ibValueModel::ibValueModelColumnCollection* m_columns = nullptr; // the model's columns (re-pointed per fetch)
};

class BACKEND_API ibValueModelCursor : public ibValueModel
{
public:
	// The DB realisation — renders the settings to SQL over the source queryable. The concrete subclass ctor
	// binds it: GetModelComposer().FromSource(m_metaObject->GetQueryable()). COVARIANT return: a caller with the
	// DB static type gets ibDataDBComposer& directly (FromSource/FromText/RenderText, no cast); via the base it is
	// ibDataComposer&. NO node storage — a DB model reads dynamically from the cursor.
	ibDataDBComposer& GetModelComposer() const override { return m_composer; }

	// Bind the whole-list RAM SNAPSHOT's composer to its storage (used only when DynamicRead is off, see below).
	ibValueModelCursor() { m_snapshotComposer.FromStorage(&m_snapshot); }

	// DYNAMIC DATA READ — the safety toggle. TRUE (default): the list is a LIVE keyset cursor, paged from the DB one
	// batch at a time (the fast path, in-sync with concurrent writes). FALSE: the WHOLE result set is materialised
	// ONCE into an in-memory snapshot (ibRamValueStorage) and every fetch / scroll / group is served from RAM through
	// RunStoragePage — the fallback for when cursor paging misbehaves (a driver quirk, a pathological sort) or a
	// list is small and stability beats liveness. A subclass reads it off its own property (the dynamic list's
	// "DynamicRead"); the base default keeps every other cursor list live.
	virtual bool IsDynamicRead() const { return true; }

	// DB paged fetch: render → SQL → keyset page → COPY nodes; hierarchy drill. When IsDynamicRead() is false it
	// short-circuits to the whole-list RAM snapshot (EnsureSnapshot + WindowStoragePage). (Out-of-line in tabularModelDb.cpp.)
	unsigned int RunComposerPage(const ibDataViewItem& parent, const ibDataViewItem& anchor,
		int count, ibFetchDirection dir, ibDataViewItemArray& out) const override;

	// The DB row key = its primary-key REFERENCE (guid). A register overrides this to build its composite from the
	// dimensions instead. (Out-of-line in tabularModelDb.cpp, where the queryable + reference value are in scope.)
	ibUniqueKey GetItemKey(const ibDataViewItem& item) const override;

	// The ancestor chain (immediate parent → … → root) of a row, so a Hierarchical / Tree view can drill down to a
	// selection sitting inside a sub-folder after a view-mode switch. The base is a stub (returns empty), so a DB
	// catalog lost a sub-folder selection on switch; here we walk the queryable's parent-ref column upward via a
	// point lookup per level. (Out-of-line in tabularModelDb.cpp.)
	void BuildAncestorBreadcrumb(const ibDataViewItem& fromRow, ibDataViewItemArray& out) const override;

protected:
	// Materialise the WHOLE result set into m_snapshot ONCE (DynamicRead off) — render the persistent filter + sort to
	// SQL (NO grouping, NO page limit), walk every row into a COPY node, AppendSilent it. Grouping/paging then happen
	// in RAM (RunStoragePage). Cheap re-check: it re-materialises only when the view generation moved (a refresh /
	// filter / sort change bumps it; a scroll does not), so a scroll reuses the snapshot and a settings change reloads
	// it. (Out-of-line in tabularModelDb.cpp — needs the queryable + driver.)
	//
	// It runs wherever the FETCH that reached it runs — which, when the fetch was
	// dispatched in the background, is a background session. Nothing here arranges
	// that: the whole read (page or snapshot) is one unit of work, and the thread
	// it happens on is the caller's decision, made once, at the fetch.
	void EnsureSnapshot() const;

	mutable ibDataDBComposer m_composer;   // the DB composer (bound to the queryable by the concrete subclass ctor)

	// Whole-list RAM snapshot — ONLY populated when IsDynamicRead() is false. m_snapshotComposer sources from
	// m_snapshot (bound in the ctor); it re-materialises when the view generation moves past m_snapshotGen (a refresh
	// / filter / sort change bumps the generation; a scroll does not).
	mutable ibRamValueStorage m_snapshot;
	mutable ibDataRamComposer  m_snapshotComposer;
	mutable bool               m_snapshotValid = false;   // has the snapshot been materialised for m_snapshotGen?
	mutable uint32_t           m_snapshotGen   = 0;       // the view generation the snapshot was built at
};

class BACKEND_API ibValueModelStorage : public ibValueModel
{
public:
	// The composer reads THIS model's value-storage directly (bound once; NO model back-pointer, NO queryable).
	ibValueModelStorage() { m_composer.FromStorage(&m_storage); }

	// COVARIANT return (ibDataRamComposer& to a RAM caller; ibDataComposer& via the base).
	ibDataRamComposer& GetModelComposer() const override { return m_composer; }

	// A RAM READ TAKES THE PLAIN THREAD, not a rented run: there is no database on
	// the other end, so a session and a pooled connection would be minted for
	// nothing. What it wants is exactly what a plain model gets —
	// ibDataViewModel::SubmitFetchAsync, the model's own thread, under the same
	// door lock.
	//
	// Reached through the bridge and QUALIFIED: ibValueModel does not derive
	// ibDataViewModel, it owns a provider that does — and that provider's override
	// forwards straight back here, so an unqualified call would loop forever.
	void SubmitFetchAsync(std::function<void()> work) override {
		GetDataViewModel()->ibDataViewModel::SubmitFetchAsync(std::move(work));
	}

	// RAM paged fetch: ComputeOrder over the storage's nodes → window by the anchor → return the LIVE nodes.
	unsigned int RunComposerPage(const ibDataViewItem& parent, const ibDataViewItem& anchor,
		int count, ibFetchDirection dir, ibDataViewItemArray& out) const override;

	// The ancestor GROUP chain of a RAM row (a grouped RAM list drills through group levels; a plain RAM list has
	// no folder hierarchy). Mirrors the DB cursor's grouped branch so a selection inside a group survives a
	// view-mode switch — the top-level fetch returns group headers, not the row, so a plain restore-scan misses.
	// (Out-of-line in tabularModelRam.cpp.)
	void BuildAncestorBreadcrumb(const ibDataViewItem& fromRow, ibDataViewItemArray& out) const override;

	// Row access reads the storage (overrides the base DB-defaults).
	long GetRowCount() const override { return m_storage.RowCount(); }
	long GetRow(const ibDataViewItem& item) const override {
		ibComposerNode* n = GetViewData<ibComposerNode>(item);
		return n != nullptr ? m_storage.IndexOf(n) : wxNOT_FOUND;
	}
	ibDataViewItem GetItem(long row) const override { return m_storage.GetItem(row); }

	// The RAM mutation API the value-table / tabular-section / record-set call (inherited via the alias) —
	// thin delegators to the storage (which does the notify through this model).
	long Append(ibComposerNode* node, bool notify = true)                       { return m_storage.AddValue(this, node, notify); }
	long Insert(ibComposerNode* node, unsigned int row, bool notify = true)     { return m_storage.InsertValue(this, node, row, notify); }
	bool Remove(ibComposerNode*& node, bool notify = true)                      { return m_storage.RemoveValue(this, node, notify); }
	void Clear(bool notify = true)                                               { m_storage.Clear(this, notify); }
	void ClearRange(unsigned long from, unsigned long to, bool notify = true)    { m_storage.ClearRange(this, from, to, notify); }
	void Reserve(long n = 1)                                                     { m_storage.Reserve(n); }

	// Fluent top-level builder: model.AddRow().Set(col, v).Set(col2, v2)…; then row.AddRow() for a child (tree).
	ibComposerNode& AddRow() { ibComposerNode* n = new ibComposerNode(); m_storage.AddValue(this, n, false); return *n; }

	// displayed-item → storage: the item IS a live storage node, so its index is its position in the storage.
	long StorageIndexOf(const ibDataViewItem& item) const { return GetRow(item); }
	ibComposerNode* StorageRowOf(const ibDataViewItem& item) const { return m_storage.GetNode(StorageIndexOf(item)); }

	// (NO GetSourceQueryable override — a RAM model has no queryable; the composer reads m_storage directly.
	//  The base default returns null.)

	ibRamValueStorage&       Storage()       { return m_storage; }
	const ibRamValueStorage& Storage() const { return m_storage; }

	// (NO SETTINGS FIELD HERE. A model holds nothing of the kind — what is in force lives in its
	//  COMPOSER, which is the thing that reads. The RAM world's filter, sort and grouping are set on
	//  that composer while the model is alive, and nothing about them is saved as a composition:
	//  a value table, a tabular section and a record set have no query, no variants, no structure.)

protected:
	mutable ibRamValueStorage m_storage;      // OWNS the live nodes (the RAM source — the queryable analog)
	mutable ibDataRamComposer  m_composer;    // the RAM composer (sources from m_storage; NO queryable)
};

// The universal fetch row — nested in the abstract base, surfaced as a plain name (DB copies, RAM live rows,
// group nodes, tree nodes all ARE one).
using ibComposerNode = ibValueModel::ibComposerNode;

#endif