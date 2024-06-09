#ifndef _TABLE_MODEL_H__
#define _TABLE_MODEL_H__

#include <wx/dataview.h>

#include "core/compiler/value.h"
#include "core/compiler/valueType.h"

#include "core/common/actionInfo.h"

class wxTableNotifier {
public:

	virtual bool NotifyDelete(const wxDataViewItem& item) = 0;
	virtual wxDataViewColumn* GetCurrentColumn() const = 0;
	virtual void StartEditing(const wxDataViewItem& item, unsigned int col) const = 0;
	virtual bool ShowFilter(struct filterRow_t& filter) = 0;

	virtual void Select(const wxDataViewItem& item) const = 0;

	virtual wxDataViewItem GetSelection() const = 0;
	virtual int GetSelections(wxDataViewItemArray& sel) const = 0;
};

enum eComparisonType {
	eComparisonType_Equal, // ==
	eComparisonType_NotEqual, // !=
};

struct filterRow_t {

	struct filterData_t {
		unsigned int m_filterModel;
		wxString m_filterName;
		wxString m_filterPresentation;
		eComparisonType m_filterComparison;
		typeDescription_t m_filterTypeDescription;
		CValue m_filterValue;
		bool m_filterUse;
	public:
		filterData_t(unsigned int filterModel, const wxString& filterName, const wxString& filterPresentation,
			eComparisonType comparisonType, const typeDescription_t& filterTypeDescription, const CValue& filterValue,
			bool filterUse = false) :
			m_filterModel(filterModel),
			m_filterName(filterName),
			m_filterPresentation(filterPresentation),
			m_filterComparison(comparisonType),
			m_filterTypeDescription(filterTypeDescription),
			m_filterValue(filterValue),
			m_filterUse(filterUse) {
		}
	};

	std::vector< filterData_t> m_filters;

public:

	void AppendFilter(unsigned int filterModel, const wxString& filterName,
		const typeDescription_t& filterTypeDescription, const CValue& filterValue) {
		m_filters.emplace_back(filterModel, filterName, filterName,
			eComparisonType::eComparisonType_Equal, filterTypeDescription, filterValue, false
		);
	}

	void AppendFilter(unsigned int filterModel, const wxString& filterName, const wxString& filterPresentation,
		const typeDescription_t& filterTypeDescription, const CValue& filterValue) {
		m_filters.emplace_back(filterModel, filterName, filterPresentation,
			eComparisonType::eComparisonType_Equal, filterTypeDescription, filterValue, false
		);
	}

	void AppendFilter(unsigned int filterModel, const wxString& filterName, const wxString& filterPresentation,
		eComparisonType comparisonType, const typeDescription_t& filterTypeDescription, const CValue& filterValue,
		bool filterUse = false) {
		m_filters.emplace_back(filterModel, filterName, filterPresentation,
			comparisonType, filterTypeDescription, filterValue, filterUse
		);
	}

	filterData_t* GetFilterByID(unsigned int filterModel) {
		auto foundedIt = std::find_if(m_filters.begin(), m_filters.end(), [filterModel](const filterData_t& data) {
			return filterModel == data.m_filterModel; }
		);
		if (foundedIt != m_filters.end())
			return &*foundedIt;
		return NULL;
	}

	filterData_t* GetFilterByName(const wxString& filterName) {
		auto foundedIt = std::find_if(m_filters.begin(), m_filters.end(), [filterName](const filterData_t& data) {
			return filterName == data.m_filterName; }
		);
		if (foundedIt != m_filters.end())
			return &*foundedIt;
		return NULL;
	}

	void SetFilterByID(unsigned int filterModel, const CValue& filterValue) {
		filterData_t* data = GetFilterByID(filterModel);
		if (data != NULL) {
			data->m_filterValue = filterValue;
			data->m_filterUse = true;
		}
	}

	void SetFilterByName(const wxString& filterName, const CValue& filterValue) {
		filterData_t* data = GetFilterByName(filterName);
		if (data != NULL) {
			data->m_filterValue = filterValue;
			data->m_filterUse = true;
		}
	}

	bool UseFilter() const {
		return m_filters.size() > 0;
	}

	void ResetFilter() {
		for (auto& filter : m_filters) {
			filter.m_filterUse = false;
		}
	}
};

struct sortOrder_t {
	struct sortData_t {
		unsigned int m_sortModel;
		wxString m_sortName;
		wxString m_sortPresentation;
		bool m_sortAscending;
		bool m_sortEnable;
		bool m_sortSystem;
	public:
		sortData_t(unsigned int sortModel, const wxString& sortName, const wxString& sortPresentation = wxEmptyString, bool sortAscending = true, bool sortEnable = true, bool sortSystem = false) :
			m_sortModel(sortModel),
			m_sortName(sortName),
			m_sortPresentation(sortPresentation),
			m_sortAscending(sortAscending),
			m_sortEnable(sortEnable),
			m_sortSystem(sortSystem)
		{
		}
	};
	std::vector< sortData_t> m_sorts;
public:

	void AppendSort(unsigned int col_id, const wxString& name, bool ascending = true, bool use = true, bool system = false) {
		AppendSort(col_id, name, wxEmptyString, ascending, use, system);
	}

	void AppendSort(unsigned int col_id, const wxString& name, const wxString& presentation, bool ascending = true, bool use = true, bool system = false) {
		if (GetSortByID(col_id) == NULL) m_sorts.emplace_back(col_id, name, presentation, ascending, use, system);
	}

	sortData_t* GetSortByID(unsigned int col_id) const {
		auto foundedIt = std::find_if(m_sorts.begin(), m_sorts.end(),
			[col_id](const sortData_t& data) {
				return col_id == data.m_sortModel; }
		);
		if (foundedIt != m_sorts.end()) return const_cast<sortData_t*>(&*foundedIt);
		return NULL;
	}

	unsigned int GetSortCount() const {
		return m_sorts.size();
	}
};

struct sortModel_t {
	unsigned int m_sortModel;
	bool m_sortAscending;
};

//Общая сущность для таблиц, списка, древа таблиц 
class IValueModel : public CValue,
	public IActionSource, public wxDataViewModel {
	wxDECLARE_ABSTRACT_CLASS(IValueModel);
protected:

	//def actions
	enum Func {
		eAddValue = 1,
		eCopyValue,
		eEditValue,
		eDeleteValue,
		eFilter,
		eFilterByColumn,
		eFilterClear,
	};

public:

	class IValueModelColumnCollection : public CValue {
		wxDECLARE_ABSTRACT_CLASS(IValueModelColumnCollection);
	public:
		class IValueModelColumnInfo : public CValue {
			wxDECLARE_ABSTRACT_CLASS(IValueModelColumnInfo);
		public:

			virtual unsigned int GetColumnID() const = 0;
			virtual void SetColumnID(unsigned int col) {};

			virtual wxString GetColumnName() const = 0;
			virtual void SetColumnName(const wxString& name) {};

			virtual wxString GetColumnCaption() const = 0;
			virtual void SetColumnCaption(const wxString& caption) {};

			virtual typeDescription_t GetColumnType() const = 0;
			virtual void SetColumnType(const typeDescription_t& typeData) {};

			virtual int GetColumnWidth() const {
				return wxDVC_DEFAULT_WIDTH;
			};

			virtual void SetColumnWidth(int width) {};

			IValueModelColumnInfo();
			virtual ~IValueModelColumnInfo();

			virtual CMethodHelper* GetPMethods() const {
				PrepareNames();
				return m_methodHelper;
			}

			virtual void PrepareNames() const;
			virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);

		protected:
			CMethodHelper* m_methodHelper;
		};
	public:

		virtual IValueModelColumnInfo* AddColumn(const wxString& colName,
			const typeDescription_t& typeData,
			const wxString& caption,
			int width = wxDVC_DEFAULT_WIDTH) {
			return NULL;
		};

		virtual void RemoveColumn(unsigned int col) {};
		virtual bool HasColumnID(unsigned int col) const {
			return GetColumnByID(col) != NULL;
		}

		virtual IValueModelColumnInfo* GetColumnByID(unsigned int col) const;
		virtual IValueModelColumnInfo* GetColumnByName(const wxString& colName) const;

		virtual IValueModelColumnInfo* GetColumnInfo(unsigned int idx) const = 0;
		virtual unsigned int GetColumnCount() const = 0;

		IValueModelColumnCollection() : CValue(eValueTypes::TYPE_VALUE, true) {}
		virtual ~IValueModelColumnCollection() {}

		//Работа с итераторами 
		virtual bool HasIterator() const {
			return true;
		}

		virtual CValue GetItAt(unsigned int idx) {
			if (idx > GetColumnCount())
				return NULL;
			return GetColumnInfo(idx);
		}

		virtual unsigned int GetItSize() const {
			return GetColumnCount();
		}
	};

	class IValueModelReturnLine : public CValue {
		wxDECLARE_ABSTRACT_CLASS(IValueModelReturnLine);
	public:

		wxDataViewItem GetLineItem() const {
			return m_lineItem;
		};

		IValueModelReturnLine(const wxDataViewItem& lineItem) : CValue(eValueTypes::TYPE_VALUE, true), m_lineItem(lineItem) {
			wxRefCounter* refCounter = static_cast<wxRefCounter*>(m_lineItem.GetID());
			if (refCounter != NULL)
				refCounter->IncRef();
		}
		virtual ~IValueModelReturnLine() {
			wxRefCounter* refCounter = static_cast<wxRefCounter*>(m_lineItem.GetID());
			if (refCounter != NULL)
				refCounter->DecRef();
		}

		virtual bool IsPropReadable(const long lPropNum) const {
			return GetOwnerModel()->ValidateReturnLine(
				const_cast<IValueModelReturnLine*>(this)
			);
		}

		virtual bool IsPropWritable(const long lPropNum) const {
			return GetOwnerModel()->ValidateReturnLine(
				const_cast<IValueModelReturnLine*>(this)
			);
		}

		virtual IValueModel* GetOwnerModel() const = 0;

		//set meta/get meta
		virtual bool SetValueByMetaID(const meta_identifier_t& id, const CValue& varMetaVal) {
			if (GetOwnerModel()->ValidateReturnLine(const_cast<IValueModelReturnLine*>(this)))
				return GetOwnerModel()->SetValueByMetaID(m_lineItem, id, varMetaVal);
			return false;
		}

		virtual bool GetValueByMetaID(const meta_identifier_t& id, CValue& pvarMetaVal) const {
			return GetOwnerModel()->GetValueByMetaID(m_lineItem, id, pvarMetaVal);
		}

		//operator '=='
		virtual inline bool CompareValueEQ(const CValue& cParam) const override {
			IValueModelReturnLine* tableReturnLine = NULL;
			if (cParam.ConvertToValue(tableReturnLine)) {
				if (GetOwnerModel() == tableReturnLine->GetOwnerModel()
					&& m_lineItem == tableReturnLine->GetLineItem()) {
					return true;
				}
			}
			return false;
		}

		//operator '!='
		virtual inline bool CompareValueNE(const CValue& cParam) const override {
			IValueModelReturnLine* tableReturnLine = NULL;
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
		wxDataViewItem m_lineItem;
	};

public:

	template <class retType>
	inline retType* GetViewData(const wxDataViewItem& item) const {
		if (!item.IsOk())
			return NULL;
		try {
			return static_cast<retType*>(item.GetID());
		}
		catch (...) {
			return NULL;
		}
	}

	void ResetSort() {
		for (auto& sort : m_sortOrder.m_sorts) {
			if (!sort.m_sortSystem)
				sort.m_sortEnable = false;
		}
	}

	sortOrder_t::sortData_t* GetSortByID(unsigned int col) const {
		return m_sortOrder.GetSortByID(col);
	}

	IValueModel();
	virtual ~IValueModel() {}

	void AppendNotifier(wxTableNotifier* notify) {
		m_srcNotifier.push_back(notify);
	}

	void RemoveNotifier(wxTableNotifier* notify) {
		auto foundedIt = std::find(m_srcNotifier.begin(), m_srcNotifier.end(), notify);
		if (foundedIt != m_srcNotifier.end())
			m_srcNotifier.erase(foundedIt);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	virtual unsigned int GetColumnCount() const override {
		IValueModelColumnCollection* colCollection = GetColumnCollection();
		if (colCollection != NULL)
			return colCollection->GetColumnCount();
		return 0;
	};

	// return type as reported by wxVariant
	virtual wxString GetColumnType(unsigned int col) const override {
		return wxT("string");
	};
	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual wxDataViewItem GetSelection() const;
	virtual void RowValueStartEdit(const wxDataViewItem& item, unsigned int col = 0);

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool ValidateReturnLine(IValueModelReturnLine* retLine) const {
		return true;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual wxDataViewItem FindRowValue(const CValue& varValue, const wxString& colName = wxEmptyString) const {
		return wxDataViewItem(NULL);
	};

	virtual wxDataViewItem FindRowValue(IValueModelReturnLine* retLine) const {
		return wxDataViewItem(NULL);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool AutoCreateColumn() const {
		return true;
	}

	virtual bool DynamicRefresh() const {
		return false;
	}

	virtual bool UseFilter() const {
		return m_filterRow.UseFilter();
	}

	virtual bool UseStandartCommand() const {
		return true;
	}

	virtual bool EditableLine(const wxDataViewItem& item, unsigned int col) const {
		return true;
	}

	virtual bool IsListModel() const { return false; }
	virtual bool IsVirtualListModel() const { return false; }

	virtual void ActivateItem(CValueForm* formOwner,
		const wxDataViewItem& item, unsigned int col) {
		IValueModel::RowValueStartEdit(item, col);
	}

	virtual bool IsSortable(unsigned int col) const {
		sortOrder_t::sortData_t* sortData = m_sortOrder.GetSortByID(col);
		if (sortData == NULL)
			return false;
		return true;
	}

	virtual void AddValue(unsigned int before = 0) {};
	virtual void CopyValue() {};
	virtual void EditValue() {};
	virtual void DeleteValue() {};

	virtual IValueModelReturnLine* GetRowAt(const wxDataViewItem& line) = 0;
	virtual IValueModelColumnCollection* GetColumnCollection() const = 0;

	//set meta/get meta
	virtual meta_identifier_t GetColumnIDByName(const wxString colName) const {
		IValueModelColumnCollection* colCollection = GetColumnCollection();
		if (colCollection == NULL)
			return wxNOT_FOUND;
		IValueModelColumnCollection::IValueModelColumnInfo* colInfo = colCollection->GetColumnByName(colName);
		if (colInfo == NULL)
			return wxNOT_FOUND;
		return colInfo->GetColumnID();
	};

	virtual bool SetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, const CValue& varMetaVal) = 0;
	virtual bool GetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, CValue& cVa) const = 0;

	//ref counter 
	virtual void DecrRef() override {
		if (CValue::GetRefCount() > 1) {
			CValue::DecrRef();
		}
		else if (wxRefCounter::GetRefCount() < 2) {
			CValue::DecrRef();
		}
	}

	//show filter 
	virtual bool ShowFilter();

	//Update model 
	virtual void RefreshModel(const wxDataViewItem& topItem = wxDataViewItem(NULL), int countPerPage = DEF_START_ITEM_PER_COUNT) {};

	/**
	* Override actions
	*/

	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, class CValueForm* srcForm);

protected:

	std::vector<wxTableNotifier*> m_srcNotifier;

	filterRow_t m_filterRow;
	sortOrder_t m_sortOrder;
};

//Поддержка таблиц
class IValueTable : public IValueModel {
	wxDECLARE_ABSTRACT_CLASS(IValueTable);
public:

	struct wxValueTableRow : public wxRefCounter {

		wxValueTableRow() :
			m_valueTable(NULL), m_nodeValues() {
		}

		wxValueTableRow(const wxValueTableRow& tableRow) :
			m_valueTable(tableRow.m_valueTable), m_nodeValues(tableRow.m_nodeValues) {
		}
		
		/////////////////////////////////////////////////////////////////////////////

		template <class vatT> 
		inline void AppendTableValue(const meta_identifier_t& id, vatT variant)
		{
			m_nodeValues.insert_or_assign(id, variant);
		}

		inline CValue& AppendTableValue(const meta_identifier_t& id)
		{
			return m_nodeValues[id];
		}

		/////////////////////////////////////////////////////////////////////////////

		const valueArray_t& GetTableValues() const 
		{
			return m_nodeValues;
		}

		/////////////////////////////////////////////////////////////////////////////

		bool SetValue(const meta_identifier_t& id, const CValue& variant, bool notify = false) {
			try {
				auto foundedColumn = m_nodeValues.find(id);
				if (foundedColumn != m_nodeValues.end()) {
					CValue& cValue = m_nodeValues.at(id);
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
				CValue& cValue = m_nodeValues.at(col);
				std::vector<CValue> foundedObjects;
				if (cValue.FindValue(variant.GetString(), foundedObjects)) {
					const CValue& cFoundedValue = foundedObjects.at(0);
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

		bool IsEmptyValue(const meta_identifier_t& col) const {
			auto foundedColumn = m_nodeValues.find(col);
			if (foundedColumn == m_nodeValues.end())
				return true;
			const CValue& cValue = m_nodeValues.at(col);
			return cValue.IsEmpty();
		}

		bool IsEmptyValue(unsigned int col) const {
			auto foundedColumn = m_nodeValues.find(col);
			if (foundedColumn == m_nodeValues.end())
				return true;
			const CValue& cValue = m_nodeValues.at(col);
			return cValue.IsEmpty();
		}

		void EraseValue(const meta_identifier_t& id) {
			auto foundedColumn = m_nodeValues.find(id);
			if (foundedColumn != m_nodeValues.end())
				m_nodeValues.erase(id);
		}

		void EraseValue(unsigned int col) {
			auto foundedColumn = m_nodeValues.find(col);
			if (foundedColumn != m_nodeValues.end())
				m_nodeValues.erase(col);
		}

		bool CompareRow(const wxValueTableRow* tableRow, std::vector<sortModel_t>& paSort) const {
			try {
				for (unsigned long p = 0; p < paSort.size(); p++) {
					const CValue& lhs = tableRow->m_nodeValues.at(paSort[p].m_sortModel);
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

		const CValue &GetTableValue(const meta_identifier_t& id) const {
			return m_nodeValues.at(id);
		}

		////////////////////////////////////////////////////////////////////////

		bool GetValue(const meta_identifier_t& id, CValue& variant) const {
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
				variant = GetTableValue(col).GetString();
				return true;
			}
			catch (std::out_of_range&) {
				return false;
			}

			return false;
		}

	private:
		friend class IValueTable;
	protected:
		IValueTable* m_valueTable;
		valueArray_t m_nodeValues;
	};

public:

	IValueTable() : IValueModel() {}
	virtual ~IValueTable() {
		Clear(false);
	}

	/////////////////////////////////////////////////////////

	virtual bool IsEmpty() const override {
		return GetRowCount() == 0;
	}

	/////////////////////////////////////////////////////////

	virtual bool ValidateReturnLine(IValueModelReturnLine* retLine) const {
		wxValueTableRow* node = GetViewData<wxValueTableRow>(retLine->GetLineItem());
		wxASSERT(node);
		return node ? node->m_valueTable != NULL : false;
	}

	/////////////////////////////////////////////////////////

	void Clear(bool notify = true) {
		if (notify) /* wxDataViewModel:: */ BeforeReset();
		for (auto node : m_nodeValues) {
			node->m_valueTable = NULL;
			node->DecRef();
		}
		m_nodeValues.clear();
		if (notify) /* wxDataViewModel:: */ AfterReset();
	}

	/////////////////////////////////////////////////////////

	void RowChanged(wxValueTableRow* item) {
		/* wxDataViewModel:: */ ItemChanged(wxDataViewItem(item));
	}

	void RowValueChanged(wxValueTableRow* item, unsigned int col) {
		/* wxDataViewModel:: */ ValueChanged(wxDataViewItem(item), col);
	}

	/////////////////////////////////////////////////////////

	long Append(wxValueTableRow* child, bool notify = true) {
		wxASSERT(child);
		child->m_valueTable = this;
		m_nodeValues.push_back(child);
		if (notify) /* wxDataViewModel:: */ ItemAdded(
			wxDataViewItem(NULL),
			wxDataViewItem(child)
		);
		return m_nodeValues.size() - 1;
	}

	long Insert(wxValueTableRow* child, unsigned int n, bool notify = true) {
		wxASSERT(child);
		child->m_valueTable = this;
		m_nodeValues.insert(m_nodeValues.begin() + n, child);
		if (notify) /* wxDataViewModel:: */ ItemAdded(
			wxDataViewItem(NULL),
			wxDataViewItem(child)
		);
		return n + 1;
	}

	bool Remove(wxValueTableRow*& child, bool notify = true) {
		wxASSERT(child);
		auto foundedIt = std::find(
			m_nodeValues.begin(),
			m_nodeValues.end(), child
		);

		if (notify) {
			for (auto notifier : m_srcNotifier) {
				bool cancel = notifier->NotifyDelete(wxDataViewItem(child));
				if (cancel)
					return false;
			}
		}

		if (notify) /* wxDataViewModel:: */ ItemDeleted(
			wxDataViewItem(NULL),
			wxDataViewItem(child)
		);

		if (foundedIt != m_nodeValues.end()) {
			m_nodeValues.erase(foundedIt);
			child->m_valueTable = NULL;
			child->DecRef();
			return true;
		}
		return false;
	}

	void Sort(unsigned int col, bool ascending = true, bool notify = true) {
		std::vector<sortModel_t> fixedSort = { { col, ascending } };
		Sort(fixedSort, notify);
	}

	void Sort(std::vector<sortModel_t> &paSort, bool notify = true) {
		if (notify) wxDataViewModel::BeforeReset();
		std::sort(m_nodeValues.begin(), m_nodeValues.end(),
			[&paSort](const wxValueTableRow* a, const wxValueTableRow* b) {
				return a->CompareRow(b, paSort);
			}
		);
		if (notify) wxDataViewModel::AfterReset();
	}

	long GetRowCount() const {
		return m_nodeValues.size();
	}

	/////////////////////////////////////////////////////////

	virtual bool IsListModel() const { return true; }
	virtual bool IsVirtualListModel() const { return false; }

	// override sorting to always sort branches ascendingly
	virtual bool HasDefaultCompare() const override {
		return true;
	}

	virtual int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
		unsigned int col, bool ascending) const override {

		wxASSERT(item1.IsOk() && item2.IsOk());

		sortOrder_t::sortData_t* foundedSort = m_sortOrder.GetSortByID(col);
		if (foundedSort == NULL && col != unsigned int(wxNOT_FOUND))
			return 0;

		wxValueTableRow* node1 = GetViewData<wxValueTableRow>(item1);
		if (node1 == NULL)
			return 0;

		wxValueTableRow* node2 = GetViewData<wxValueTableRow>(item2);
		if (node2 == NULL)
			return 0;

		CValue currValue1, currValue2;
		for (auto sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {			
				node1->GetValue(sort.m_sortModel, currValue1);
				node2->GetValue(sort.m_sortModel, currValue2);
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
		}

		// items must be different
		wxUIntPtr id1 = wxPtrToUInt(item1.GetID()),
			id2 = wxPtrToUInt(item2.GetID());
		return ascending ? id1 - id2 : id2 - id1;
	}

	// derived classes should override these methods instead of
	// {Get,Set}Value() and GetAttr() inherited from the base class
	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const = 0;

	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) = 0;

	virtual bool GetAttrByRow(const wxDataViewItem& WXUNUSED(row), unsigned int WXUNUSED(col),
		wxDataViewItemAttr& WXUNUSED(attr)) const {
		return false;
	}

	virtual bool IsEnabledByRow(const wxDataViewItem& WXUNUSED(row),
		unsigned int WXUNUSED(col)) const {
		return true;
	}

	// helper methods provided by list models only
	virtual long GetRow(const wxDataViewItem& item) const {
		wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
		if (node == NULL)
			return wxNOT_FOUND;
		auto foundedIt = std::find(m_nodeValues.begin(), m_nodeValues.end(), node);
		if (foundedIt != m_nodeValues.end())
			return std::distance(m_nodeValues.begin(), foundedIt);
		return wxNOT_FOUND;
	}

	virtual wxDataViewItem GetItem(long row) const {
		wxASSERT(row < (long)m_nodeValues.size());
		if (row >= 0 && row < (long)m_nodeValues.size()) {
			auto foundedIt = m_nodeValues.begin();
			std::advance(foundedIt, row);
			return wxDataViewItem(*foundedIt);
		}
		return wxDataViewItem(NULL);
	}

	// implement base methods
	virtual unsigned int GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const override {
		if (parent.IsOk())
			return 0;
		unsigned int count = m_nodeValues.size();
		if (count == 0)
			return 0;
		for (unsigned int pos = 0; pos < count; pos++) {
			wxValueTableRow* child = m_nodeValues.at(pos);
			array.Add(wxDataViewItem((void*)child));
		}
		return count;
	}

	// implement some base class pure virtual directly
	virtual wxDataViewItem GetParent(const wxDataViewItem& WXUNUSED(item)) const override {
		// items never have valid parent in this model
		return wxDataViewItem(NULL);
	}

	virtual bool IsContainer(const wxDataViewItem& item) const override {
		// only the invisible (and invalid) root item has children
		return !item.IsOk();
	}

	// and implement some others by forwarding them to our own ones
	virtual void GetValue(wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) const override {
		GetValueByRow(variant, item, col);
	}

	virtual bool SetValue(const wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) override {
		return SetValueByRow(variant, item, col);
	}

	virtual bool GetAttr(const wxDataViewItem& item, unsigned int col,
		wxDataViewItemAttr& attr) const override {
		return GetAttrByRow(item, col, attr);
	}

	virtual bool IsEnabled(const wxDataViewItem& item, unsigned int col) const override {
		return IsEnabledByRow(item, col);
	}

private:
	std::vector< wxValueTableRow*> m_nodeValues;
};

//Поддержка дерева таблиц 
class IValueTree : public IValueModel {
	wxDECLARE_ABSTRACT_CLASS(IValueTable);
public:

	struct wxValueTreeNode : public wxRefCounter {

		wxValueTreeNode(IValueTree* valueTree) :
			m_valueTree(valueTree), m_parent(NULL) {
		}

		wxValueTreeNode(wxValueTreeNode* parent) :
			m_valueTree(NULL), m_parent(parent), m_nodeValues() {
			if (m_parent != NULL)
				m_parent->Append(this);
		}

		virtual ~wxValueTreeNode() {
			// free all our children nodes
			size_t count = m_children.size();
			for (size_t i = 0; i < count; i++) {
				wxValueTreeNode* child = m_children[i];
				wxASSERT(child);
				child->m_valueTree = NULL;
				child->DecRef();
			}
		}

		/////////////////////////////////////////////////////////////////////////////

		template <class vatT>
		inline void AppendTableValue(const meta_identifier_t& id, vatT variant)
		{
			m_nodeValues.insert_or_assign(id, variant);
		}

		inline CValue& AppendTableValue(const meta_identifier_t& id)
		{
			return m_nodeValues[id];
		}

		/////////////////////////////////////////////////////////////////////////////

		const valueArray_t& GetTableValues() const
		{
			return m_nodeValues;
		}

		/////////////////////////////////////////////////////////////////////////////

		bool IsContainer() const {
			return m_children.size() > 0;
		}

		void SetParent(wxValueTreeNode* parent) {
			if (m_parent)
				m_parent->Remove(this);
			if (parent != NULL)
				parent->Append(this);
			m_parent = parent;
		}

		wxValueTreeNode* GetParent() const {
			return m_parent;
		}

		std::vector<wxValueTreeNode*>& GetChildren() {
			return m_children;
		}

		wxValueTreeNode* GetChild(unsigned int n) const {
			return m_children.at(n);
		}

		void Append(wxValueTreeNode* child, bool notify = true) {
			child->m_valueTree = m_valueTree;
			m_children.push_back(child);
			if (notify) m_valueTree->ItemAdded(
				wxDataViewItem(this),
				wxDataViewItem(child)
			);
		}

		void Insert(wxValueTreeNode* child, unsigned int n, bool notify = true) {
			child->m_valueTree = m_valueTree;
			m_children.insert(m_children.begin() + n, child);
			if (notify) m_valueTree->ItemAdded(
				wxDataViewItem(this),
				wxDataViewItem(child)
			);
		}

		void Remove(wxValueTreeNode* child, bool notify = true) {
			auto foundedIt = std::find(m_children.begin(), m_children.end(), child);
			if (notify) m_valueTree->ItemDeleted(
				wxDataViewItem(this),
				wxDataViewItem(child)
			);
			if (foundedIt != m_children.end())
				m_children.erase(foundedIt);
			child->m_valueTree = NULL;
			child->DecRef();
		}

		void Sort(std::vector<sortModel_t>& paSort) {
			std::sort(m_children.begin(), m_children.end(),
				[&paSort](const wxValueTreeNode* a, const wxValueTreeNode* b) {
					return a->CompareNode(b, paSort);
				}
			);
			for (auto child : m_children) child->Sort(paSort);
		}

		unsigned int GetChildCount() const {
			return m_children.size();
		}

	public:

		bool CompareNode(const wxValueTreeNode* node, std::vector<sortModel_t>& paSort) const {
			try {
				for (unsigned long p = 0; p < paSort.size(); p++) {
					const CValue& lhs = node->m_nodeValues.at(paSort[p].m_sortModel);
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

		bool SetValue(const meta_identifier_t& id, const CValue& variant, bool notify = false) {
			try {
				CValue& cValue = m_nodeValues.at(id);
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
				CValue& cValue = m_nodeValues.at(col);
				std::vector<CValue> foundedObjects;
				if (cValue.FindValue(variant.GetString(), foundedObjects)) {
					const CValue& cFoundedValue = foundedObjects.at(0);
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

		CValue GetValue(const meta_identifier_t& id) const {
			CValue cValue; GetValue(id, cValue);
			return cValue;
		}

		wxVariant GetValue(unsigned int col) const {
			wxVariant vValue; GetValue(col, vValue);
			return vValue;
		}

		bool GetValue(const meta_identifier_t& id, CValue& variant) const {
			try {
				const CValue& cValue = m_nodeValues.at(id);
				variant = cValue; return true;
			}
			catch (std::out_of_range&) {
			}
			return false;
		}

		bool GetValue(unsigned int col, wxVariant& variant) const {
			try {
				const CValue& cValue = m_nodeValues.at(col);
				variant = cValue.GetString(); return true;
			}
			catch (std::out_of_range&) {
			}
			return false;
		}

	private:
		friend class IValueTree;
	private:
		wxValueTreeNode* m_parent;
		std::vector<wxValueTreeNode*> m_children;
	protected:
		IValueTree* m_valueTree;
		valueArray_t m_nodeValues;
	};

public:

	IValueTree() : IValueModel() {
		m_root = new wxValueTreeNode(this);
	}

	virtual ~IValueTree() {
		delete m_root;
	}

	/////////////////////////////////////////////////////////

	virtual bool IsEmpty() const override {
		return m_root->GetChildCount() == 0;
	}

	/////////////////////////////////////////////////////////

	virtual bool ValidateReturnLine(IValueModelReturnLine* retLine) const {
		wxValueTreeNode* node = GetViewData<wxValueTreeNode>(retLine->GetLineItem());
		wxASSERT(node);
		return node ? node->m_valueTree != NULL : false;
	}


	/////////////////////////////////////////////////////////

	wxValueTreeNode* GetRoot() const {
		return m_root;
	}

	void RowChanged(wxValueTreeNode* item) {
		/* wxDataViewModel:: */ ItemChanged(wxDataViewItem(item));
	}

	void RowValueChanged(wxValueTreeNode* item, unsigned int col) {
		/* wxDataViewModel:: */ ValueChanged(wxDataViewItem(item), col);
	}

	// helper methods to change the model
	bool Delete(const wxDataViewItem& item, bool notify = true) {
		wxValueTreeNode* node = (wxValueTreeNode*)item.GetID();
		if (node == NULL)
			return false;
		wxDataViewItem parent(node->GetParent());
		if (!parent.IsOk()) {
			wxASSERT(node == m_root);
			// don't make the control completely empty:
			wxLogError("Cannot remove the root item!");
			return false;
		}

		for (auto notifier : m_srcNotifier) {
			bool cancel = notifier->NotifyDelete(item);
			if (cancel)
				return false;
		}

		// first remove the node from the parent's array of children;
		// NOTE: MyMusicTreeModelNodePtrArray is only an array of _pointers_
		//       thus removing the node from it doesn't result in freeing it
		std::vector<wxValueTreeNode*>& children = node->GetParent()->GetChildren();
		std::vector<wxValueTreeNode*>::iterator children_iterator = std::find(children.begin(), children.end(), node);
		if (children_iterator != children.end())
			children.erase(children_iterator);
		// notify control
		if (notify) /* wxDataViewModel:: */ ItemDeleted(parent, item);
		// free the node
		node->m_valueTree = NULL;
		node->DecRef();
		return true;
	}

	void Clear(bool notify = true) {
		std::vector<wxValueTreeNode*>& children = m_root->GetChildren();
		while (!children.empty()) {
			wxValueTreeNode* node = m_root->GetChild(0);
			std::vector<wxValueTreeNode*>::iterator children_iterator = std::find(children.begin(), children.end(), node);
			if (children_iterator != children.end())
				children.erase(children_iterator);
			node->m_valueTree = NULL;
			node->DecRef();
		}
		if (notify) /* wxDataViewModel:: */ Cleared();
	}

	void Sort(unsigned int col, bool ascending = true, bool notify = true) {
		std::vector<sortModel_t> fixedSort = { { col, ascending } };
		Sort(fixedSort, notify);
	}

	void Sort(std::vector<sortModel_t> &paSort, bool notify = true) {
		if (notify) /* wxDataViewModel:: */ BeforeReset();
		m_root->Sort(paSort);
		if (notify) /* wxDataViewModel:: */ AfterReset();
	}

	/////////////////////////////////////////////////////////

	virtual bool IsListModel() const { return false; }
	virtual bool IsVirtualListModel() const { return false; }

	// override sorting to always sort branches ascendingly
	virtual bool HasDefaultCompare() const override {
		return true;
	}

	virtual int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
		unsigned int col, bool ascending) const override {

		wxASSERT(item1.IsOk() && item2.IsOk());

		sortOrder_t::sortData_t* foundedSort = m_sortOrder.GetSortByID(col);
		if (foundedSort == NULL && col != unsigned int(wxNOT_FOUND))
			return 0;

		wxValueTreeNode* node1 = GetViewData<wxValueTreeNode>(item1);
		if (node1 == NULL)
			return 0;
		wxValueTreeNode* node2 = GetViewData<wxValueTreeNode>(item2);
		if (node2 == NULL)
			return 0;

		CValue currValue1, currValue2;
		for (auto sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				node1->GetValue(sort.m_sortModel, currValue1);
				node2->GetValue(sort.m_sortModel, currValue2);
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
		}

		// items must be different
		wxUIntPtr id1 = wxPtrToUInt(item1.GetID()),
			id2 = wxPtrToUInt(item2.GetID());
		return ascending ? id1 - id2 : id2 - id1;
	}

	// derived classes should override these methods instead of
	// {Get,Set}Value() and GetAttr() inherited from the base class
	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& item, unsigned col) const = 0;

	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& item, unsigned col) = 0;

	virtual bool GetAttrByRow(const wxDataViewItem& WXUNUSED(item),
		unsigned WXUNUSED(col), wxDataViewItemAttr& WXUNUSED(attr)) const {
		return false;
	}

	virtual bool IsEnabledByRow(const wxDataViewItem& WXUNUSED(item),
		unsigned int WXUNUSED(col)) const {
		return true;
	}

	virtual unsigned int GetChildren(const wxDataViewItem& parent,
		wxDataViewItemArray& array) const override {
		wxValueTreeNode* node = GetViewData<wxValueTreeNode>(parent);
		if (node == NULL)
			return GetChildren(wxDataViewItem(m_root), array);
		unsigned int count = node->GetChildCount();
		if (count == 0)
			return 0;
		for (unsigned int pos = 0; pos < count; pos++) {
			wxValueTreeNode* child = node->GetChild(pos);
			array.Add(wxDataViewItem((void*)child));
		}
		return count;
	}

	virtual wxDataViewItem GetParent(const wxDataViewItem& item) const override {
		// the invisible root node has no parent
		if (!item.IsOk())
			return wxDataViewItem(NULL);
		wxValueTreeNode* node = GetViewData<wxValueTreeNode>(item);
		// "root" also has no parent
		if (m_root == node ||
			m_root == node->GetParent())
			return wxDataViewItem(NULL);
		return wxDataViewItem((void*)node->GetParent());
	}

	virtual bool IsContainer(const wxDataViewItem& item) const override {
		// the invisible root node can have children
		// (in our model always "root")
		if (!item.IsOk())
			return true;
		wxValueTreeNode* node = GetViewData<wxValueTreeNode>(item);
		if (node == NULL)
			return false;
		return node->IsContainer();
	}

	// return true if the given item has a value to display in the given
	// column: this is always true except for container items which by default
	// only show their label in the first column (but see HasContainerColumns())
	virtual bool HasValue(const wxDataViewItem& item, unsigned col) const override {
		if (HasContainerColumns(item))
			return false;
		return true;
	}

	// and implement some others by forwarding them to our own ones
	virtual void GetValue(wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) const override {
		GetValueByRow(variant, item, col);
	}

	virtual bool SetValue(const wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) override {
		return SetValueByRow(variant, item, col);
	}

	virtual bool GetAttr(const wxDataViewItem& item, unsigned int col,
		wxDataViewItemAttr& attr) const override {
		return GetAttrByRow(item, col, attr);
	}

	virtual bool IsEnabled(const wxDataViewItem& item, unsigned int col) const override {
		return IsEnabledByRow(item, col);
	}

private:
	wxValueTreeNode* m_root;
};

#endif 