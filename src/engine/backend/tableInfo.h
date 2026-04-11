#ifndef __TABLE_INFO_H__
#define __TABLE_INFO_H__

#include "backend/tableView.h"

#include "backend/system/value/valueType.h"

#include "backend/actionInfo.h"
#include "backend/srcObject.h"

///////////////////////////////////////////////////////////////////////////////////
#define defaultCountPerPage 100
///////////////////////////////////////////////////////////////////////////////////

enum ibComparisonType {
	ibComparisonType_Equal, // ==
	ibComparisonType_NotEqual, // !=
};

struct ibFilterRow {

	struct ibFilterData {
		unsigned int m_filterModel;
		ibGuid m_filterGuid;
		wxString m_filterName;
		wxString m_filterPresentation;
		ibComparisonType m_filterComparison;
		ibTypeDescription m_filterTypeDescription;
		ibValue m_filterValue;
		bool m_filterUse;
	public:
		ibFilterData(unsigned int filterModel, const wxString& filterName, const wxString& filterPresentation,
			ibComparisonType comparisonType, const ibTypeDescription& filterTypeDescription, const ibValue& filterValue,
			bool filterUse = false) :
			m_filterModel(filterModel),
			m_filterGuid(ibGuid::newGuid()),
			m_filterName(filterName),
			m_filterPresentation(filterPresentation),
			m_filterComparison(comparisonType),
			m_filterTypeDescription(filterTypeDescription),
			m_filterValue(filterValue),
			m_filterUse(filterUse) {
		}
	};

	std::vector< ibFilterData> m_filters;

public:

	void AppendFilter(unsigned int filterModel, const wxString& filterName,
		const ibTypeDescription& filterTypeDescription, const ibValue& filterValue) {
		m_filters.emplace_back(filterModel, filterName, filterName,
			ibComparisonType::ibComparisonType_Equal, filterTypeDescription, filterValue, false
		);
	}

	void AppendFilter(unsigned int filterModel, const wxString& filterName, const wxString& filterPresentation,
		const ibTypeDescription& filterTypeDescription, const ibValue& filterValue) {
		m_filters.emplace_back(filterModel, filterName, filterPresentation,
			ibComparisonType::ibComparisonType_Equal, filterTypeDescription, filterValue, false
		);
	}

	void AppendFilter(unsigned int filterModel, const wxString& filterName, const wxString& filterPresentation,
		ibComparisonType comparisonType, const ibTypeDescription& filterTypeDescription, const ibValue& filterValue,
		bool filterUse = false) {
		m_filters.emplace_back(filterModel, filterName, filterPresentation,
			comparisonType, filterTypeDescription, filterValue, filterUse
		);
	}

	ibFilterData* GetFilterByID(unsigned int filterModel) {
		auto iterator = std::find_if(m_filters.begin(), m_filters.end(), [filterModel](const ibFilterData& data) {
			return filterModel == data.m_filterModel; });
		if (iterator != m_filters.end())
			return &*iterator;
		return nullptr;
	}

	ibFilterData* GetFilterByName(const wxString& filterName) {
		auto iterator = std::find_if(m_filters.begin(), m_filters.end(), [filterName](const ibFilterData& data) {
			return filterName == data.m_filterName; });
		if (iterator != m_filters.end())
			return &*iterator;
		return nullptr;
	}

	void SetFilterByID(unsigned int filterModel, const ibValue& filterValue) {
		ibFilterData* data = GetFilterByID(filterModel);
		if (data != nullptr) {
			data->m_filterValue = filterValue;
			data->m_filterUse = true;
		}
	}

	void SetFilterByName(const wxString& filterName, const ibValue& filterValue) {
		ibFilterData* data = GetFilterByName(filterName);
		if (data != nullptr) {
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

struct ibSortOrder {
	
	struct ibSortData {
		unsigned int m_sortModel;
		wxString m_sortName;
		wxString m_sortPresentation;
		bool m_sortAscending;
		bool m_sortEnable;
		bool m_sortSystem;
	public:
		ibSortData(unsigned int sortModel, const wxString& sortName, const wxString& sortPresentation = wxEmptyString, bool sortAscending = true, bool sortEnable = true, bool sortSystem = false) :
			m_sortModel(sortModel),
			m_sortName(sortName),
			m_sortPresentation(sortPresentation),
			m_sortAscending(sortAscending),
			m_sortEnable(sortEnable),
			m_sortSystem(sortSystem)
		{
		}
	};
	std::vector< ibSortData> m_sorts;
public:

	void AppendSort(unsigned int col_id, const wxString& name, bool ascending = true, bool use = true, bool system = false) {
		AppendSort(col_id, name, wxEmptyString, ascending, use, system);
	}

	void AppendSort(unsigned int col_id, const wxString& name, const wxString& presentation, bool ascending = true, bool use = true, bool system = false) {
		if (GetSortByID(col_id) == nullptr) m_sorts.emplace_back(col_id, name, presentation, ascending, use, system);
	}

	ibSortData* GetSortByID(unsigned int col_id) const {
		auto iterator = std::find_if(m_sorts.begin(), m_sorts.end(),
			[col_id](const ibSortData& data) {return col_id == data.m_sortModel; });
		if (iterator != m_sorts.end()) return const_cast<ibSortData*>(&*iterator);
		return nullptr;
	}

	unsigned int GetSortCount() const {
		return m_sorts.size();
	}
};

struct ibSortModel {
	unsigned int m_sortModel;
	bool m_sortAscending;
};

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
class BACKEND_API ibValueModel : public ibValue,
	public ibActionDataObject, public ibTabularObject {
	wxDECLARE_ABSTRACT_CLASS(ibValueModel);

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

		// define current parent for hierarchical view 
		virtual bool HasParentTopItem() const {
			return m_ownerModel->HasParentTopItem();
		}

		virtual bool SetParentTopItem(const ibDataViewItem& item) {
			return m_ownerModel->SetParentTopItem(item);
		}

		virtual ibDataViewItem GetParentTopItem() const {
			return m_ownerModel->GetParentTopItem();
		}

		// Is the container just a header or an item with all columns
		virtual bool HasContainerColumns(const ibDataViewItem& item) const {
			return m_ownerModel->HasContainerColumns(item);
		}

		virtual unsigned int GetChildren(const ibDataViewItem& item, ibDataViewItemArray& children) const {
			return m_ownerModel->GetChildren(item, children);
		}

		// default compare function
		virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
			unsigned int column, bool ascending) const {
			return m_ownerModel->Compare(item1, item2, column, ascending);
		}

		virtual bool HasDefaultCompare() const { return false; }

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

	class BACKEND_API ibValueModelColumnCollection : public ibValue {
		wxDECLARE_ABSTRACT_CLASS(ibValueModelColumnCollection);
	public:
		class ibValueModelColumnInfo : public ibValue {
			wxDECLARE_ABSTRACT_CLASS(ibValueModelColumnInfo);
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

			virtual ibValueMethodHelper* GetPMethods() const {
				//PrepareNames();
				return m_methodHelper;
			}

			virtual void PrepareNames() const;
			virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

		protected:
			ibValueMethodHelper* m_methodHelper;
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

		ibValueModelColumnCollection() : ibValue(ibValueTypes::TYPE_VALUE, true) {}
		virtual ~ibValueModelColumnCollection() {}

		//Working with iterators
		virtual bool HasIterator() const {
			return true;
		}

		virtual ibValue GetIteratorAt(unsigned int idx) {
			if (idx > GetColumnCount())
				return ibValue();
			return GetColumnInfo(idx);
		}

		virtual unsigned int GetIteratorCount() const {
			return GetColumnCount();
		}
	};

	class BACKEND_API ibValueModelReturnLine : public ibValue {
		wxDECLARE_ABSTRACT_CLASS(ibValueModelReturnLine);
	public:

		ibDataViewItem GetLineItem() const { return m_lineItem; };

		ibValueModelReturnLine(const ibDataViewItem& lineItem) : ibValue(ibValueTypes::TYPE_VALUE, true), m_lineItem(lineItem) {
			wxRefCounter* refCounter = static_cast<wxRefCounter*>(m_lineItem.GetID());
			if (refCounter != nullptr)
				refCounter->IncRef();
		}
		virtual ~ibValueModelReturnLine() {
			wxRefCounter* refCounter = static_cast<wxRefCounter*>(m_lineItem.GetID());
			if (refCounter != nullptr)
				refCounter->DecRef();
		}

		virtual bool IsPropReadable(const long lPropNum) const override {
			return GetOwnerModel()->ValidateReturnLine(
				const_cast<ibValueModelReturnLine*>(this)
			);
		}

		virtual bool IsPropWritable(const long lPropNum) const override {
			return GetOwnerModel()->ValidateReturnLine(
				const_cast<ibValueModelReturnLine*>(this)
			);
		}

		virtual ibValueModel* GetOwnerModel() const = 0;

		//set meta/get meta
		virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) {
			if (GetOwnerModel()->ValidateReturnLine(const_cast<ibValueModelReturnLine*>(this)))
				return GetOwnerModel()->SetValueByMetaID(m_lineItem, id, varMetaVal);
			return false;
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

	ibValueModel();
	virtual ~ibValueModel();

	//////////////////////////////////////////////////////////////////////////////////////////////////

	bool IsCallRefreshModel() const { return m_refreshModel; }

	//Update model 
	void CallRefreshModel(const ibDataViewItem& topItem = ibDataViewItem(nullptr), const int countPerPage = defaultCountPerPage) {
		m_refreshModel = true;
		RefreshModel(topItem, countPerPage);
		m_refreshModel = false;
	};

	void CallRefreshItemModel(
		const ibDataViewItem& topItem,
		const ibDataViewItem& currentItem,
		const int countPerPage,
		const short scroll = 0
	) {
		RefreshItemModel(topItem, currentItem, countPerPage, scroll);
	};

#pragma region _data_model_h_
	ibDataViewModelProviderImpl* GetDataViewModel() const { return m_modelProvider; }
#pragma endregion 

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual ibDataViewItem GetSelection() const;
	virtual void RowValueStartEdit(const ibDataViewItem& item, unsigned int col = 0);

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool ValidateReturnLine(ibValueModelReturnLine* retLine) const { return true; }

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const { return ibDataViewItem(nullptr); }
	virtual ibDataViewItem FindRowValue(ibValueModelReturnLine* retLine) const { return ibDataViewItem(nullptr); }

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

	virtual bool IsSortable(unsigned int col) const {
		ibSortOrder::ibSortData* sortData = m_sortOrder.GetSortByID(col);
		if (sortData == nullptr)
			return false;
		return true;
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
	virtual unsigned int GetChildren(const ibDataViewItem& item, ibDataViewItemArray& children) const = 0;

	// default compare function
	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
		unsigned int column, bool ascending) const = 0;

	virtual bool HasDefaultCompare() const { return false; };

	// internal
	virtual bool IsListModel() const { return false; };
	virtual bool IsVirtualListModel() const { return false; };

#pragma endregion 

protected:

	//Update model 
	virtual void RefreshModel(const ibDataViewItem& topItem = ibDataViewItem(nullptr), const int countPerPage = defaultCountPerPage) {};
	virtual void RefreshItemModel(
		const ibDataViewItem& topItem,
		const ibDataViewItem& currentItem,
		const int countPerPage,
		const short scroll = 0
	) {
	};

protected:

	ibFilterRow m_filterRow;
	ibSortOrder m_sortOrder;

	bool m_refreshModel;
};

//Table support
class BACKEND_API ibValueModelTableBase : public ibValueModel {
	wxDECLARE_ABSTRACT_CLASS(ibValueModelTableBase);
public:

	enum
	{
		TABLE_NODE_HIDDEN = 0x0001,
	};

	struct ibValueTableRow : public wxRefCounter {

		ibValueTableRow() :
			m_valueTable(nullptr), m_nodeValues(), m_nodeFlags(0) {
		}

		ibValueTableRow(const ibValueTableRow& tableRow) :
			m_valueTable(tableRow.m_valueTable), m_nodeValues(tableRow.m_nodeValues), m_nodeFlags(0) {
		}

		/////////////////////////////////////////////////////////////////////////////

		template <class varType>
		inline void AppendTableValue(const ibMetaID& id, varType&& variant) { m_nodeValues.insert_or_assign(id, variant); }
		inline ibValue& AppendTableValue(const ibMetaID& id) { return m_nodeValues[id]; }

		/////////////////////////////////////////////////////////////////////////////

		const ibMetaValueArray& GetTableValues() const { return m_nodeValues; }

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

		// Returns true if property has given flag set.
		bool HasFlag(long flag) const
		{
			return (m_nodeFlags & flag) != 0;
		}

		// Returns true if property has all given flags set.
		bool HasFlagsExact(long flags) const
		{
			return (m_nodeFlags & flags) == flags;
		}

		void SetFlag(long flag) { m_nodeFlags |= flag; }
		void ClearFlag(long flag) { m_nodeFlags &= ~(flag); }

	private:
		friend class ibValueModelTableBase;
	protected:
		ibValueModelTableBase* m_valueTable;
		ibMetaValueArray m_nodeValues;
		long m_nodeFlags;
	};

public:

	ibValueModelTableBase() : ibValueModel() {}
	virtual ~ibValueModelTableBase() { Clear(false); }

	/////////////////////////////////////////////////////////

	virtual bool IsEmpty() const override { return GetRowCount() == 0; }

	/////////////////////////////////////////////////////////

	virtual bool ValidateReturnLine(ibValueModelReturnLine* retLine) const override {
		ibValueTableRow* node = GetViewData<ibValueTableRow>(retLine->GetLineItem());
		wxASSERT(node);
		return node ? node->m_valueTable != nullptr : false;
	}

	/////////////////////////////////////////////////////////

	void Clear(bool notify = true) {
		if (m_nodeValues.empty()) return;
		if (notify) /* wxDataViewModel:: */ m_modelProvider->BeforeReset();
		m_nodeValues.erase(std::remove_if(m_nodeValues.begin(), m_nodeValues.end(), [](const auto& node) { node->m_valueTable = nullptr; node->DecRef(); return true; }), m_nodeValues.end());
		if (notify) /* wxDataViewModel:: */ m_modelProvider->AfterReset();
	}

	/////////////////////////////////////////////////////////

	void ClearRange(const unsigned long from, const unsigned long to, bool notify = true) {
		if (from > m_nodeValues.size() || to > m_nodeValues.size()) return;
		for (auto iterator = m_nodeValues.begin() + from; iterator != m_nodeValues.begin() + to; iterator++) {
			/* wxDataViewModel:: */
			if (notify && !m_modelProvider->ItemDeleted(ibDataViewItem(nullptr), ibDataViewItem(*iterator)))
				return;
			(*iterator)->m_valueTable = nullptr;
			(*iterator)->DecRef();
		}
		m_nodeValues.erase(m_nodeValues.begin() + from, m_nodeValues.begin() + to);
	}

	/////////////////////////////////////////////////////////

	void RowChanged(ibValueTableRow* item) {
		/* wxDataViewModel:: */ m_modelProvider->ItemChanged(ibDataViewItem(item));
	}

	void RowValueChanged(ibValueTableRow* item, unsigned int col) {
		/* wxDataViewModel:: */ m_modelProvider->ValueChanged(ibDataViewItem(item), col);
	}

	/////////////////////////////////////////////////////////
	void Reserve(const long rowCount = 1) {
		m_nodeValues.reserve(m_nodeValues.size() + rowCount);
	}
	/////////////////////////////////////////////////////////

	long Append(ibValueTableRow* child, bool notify = true) {
		wxASSERT(child);

		child->m_valueTable = this;
		m_nodeValues.emplace_back(child);

		/* wxDataViewModel:: */
		if (notify && !m_modelProvider->ItemAdded(ibDataViewItem(nullptr), ibDataViewItem(child))) {
			child->m_valueTable = this;
			m_nodeValues.pop_back();
			return false;
		}

		if (!notify)
			child->SetFlag(TABLE_NODE_HIDDEN);

		return m_nodeValues.size() - 1;
	}

	long Insert(ibValueTableRow* child, unsigned int row, bool notify = true) {
		wxASSERT(child);

		child->m_valueTable = this;
		auto iterator = m_nodeValues.insert(m_nodeValues.begin() + row, child);

		/* wxDataViewModel:: */
		if (notify && !m_modelProvider->ItemAdded(ibDataViewItem(nullptr), ibDataViewItem(child))) {
			child->m_valueTable = this;
			m_nodeValues.erase(iterator);
			return false;
		}

		if (!notify)
			child->SetFlag(TABLE_NODE_HIDDEN);
		return row + 1;
	}

	bool Remove(ibValueTableRow*& child, bool notify = true) {
		wxASSERT(child);

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

	void Show(const ibDataViewItem& item, bool show = true) {

		ibValueTableRow* child = GetViewData<ibValueTableRow>(item);
		if (child != nullptr && show && child->HasFlag(TABLE_NODE_HIDDEN)) {

			/* wxDataViewModel:: */
			if (!m_modelProvider->ItemAdded(ibDataViewItem(nullptr), ibDataViewItem(child)))
				return;

			child->ClearFlag(TABLE_NODE_HIDDEN);
		}
		else if (child != nullptr && !show && !child->HasFlag(TABLE_NODE_HIDDEN)) {

			/*wxDataViewModel::*/
			if (!m_modelProvider->ItemDeleted(ibDataViewItem(nullptr), ibDataViewItem(child)))
				return;

			child->SetFlag(TABLE_NODE_HIDDEN);
		}
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

	// helper methods provided by list models only
	virtual long GetRow(const ibDataViewItem& item) const {
		ibValueTableRow* node = GetViewData<ibValueTableRow>(item);
		if (node == nullptr)
			return wxNOT_FOUND;
		auto iterator = std::find(m_nodeValues.begin(), m_nodeValues.end(), node);
		if (iterator != m_nodeValues.end())
			return std::distance(m_nodeValues.begin(), iterator);
		return wxNOT_FOUND;
	}

	virtual ibDataViewItem GetItem(long row) const {
		wxASSERT(row < (long)m_nodeValues.size());
		if (row >= 0 && row < (long)m_nodeValues.size()) {
			return ibDataViewItem(m_nodeValues[row]);
		}
		return ibDataViewItem(nullptr);
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

	// implement base methods
	virtual unsigned int GetChildren(const ibDataViewItem& parent, ibDataViewItemArray& array) const override {
		if (parent.IsOk())
			return 0;
		unsigned int count = m_nodeValues.size();
		if (count == 0)
			return 0;
		array.Alloc(count);
		for (auto& node : m_nodeValues) {
			array.Add(ibDataViewItem((void*)node));
		}
		return count;
	}

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

	virtual bool HasDefaultCompare() const override { return true; }

	// internal
	virtual bool IsListModel() const override { return true; }
	virtual bool IsVirtualListModel() const override { return false; }

#pragma endregion

protected:
	std::vector< ibValueTableRow*> m_nodeValues;
};

//Tree support
class BACKEND_API ibValueModelTreeBase : public ibValueModel {
	wxDECLARE_ABSTRACT_CLASS(ibValueModelTableBase);
public:

	struct ibValueTreeNode : public wxRefCounter {

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

		virtual bool IsContainer() const { return m_children.size() > 0; }

		/////////////////////////////////////////////////////////////////////////////

		template <class varType>
		inline void AppendTableValue(const ibMetaID& id, varType&& variant) { m_nodeValues.insert_or_assign(id, variant); }
		inline ibValue& AppendTableValue(const ibMetaID& id) { return m_nodeValues[id]; }

		/////////////////////////////////////////////////////////////////////////////

		const ibMetaValueArray& GetTableValues() const { return m_nodeValues; }

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
			if (notify && !m_valueTree->m_modelProvider->ItemAdded(ibDataViewItem(this), ibDataViewItem(child))) {
				child->m_valueTree = nullptr;
				m_children.pop_back();
				return false;
			}
			return true;
		}

		bool Insert(ibValueTreeNode* child, unsigned int n, bool notify = true) {
			child->m_valueTree = m_valueTree;
			auto iterator = m_children.insert(m_children.begin() + n, child);
			if (notify && !m_valueTree->m_modelProvider->ItemAdded(ibDataViewItem(this), ibDataViewItem(child))) {
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
	private:
		ibValueTreeNode* m_parent;
		std::vector<ibValueTreeNode*> m_children;
	protected:
		ibValueModelTreeBase* m_valueTree;
		ibMetaValueArray m_nodeValues;
	};

public:

	ibValueModelTreeBase() : ibValueModel() {
		m_root = new ibValueTreeNode(this);
	}

	virtual ~ibValueModelTreeBase() {
		wxDELETE(m_root);
	}

	/////////////////////////////////////////////////////////

	virtual bool IsEmpty() const override {
		return m_root->GetChildCount() == 0;
	}

	/////////////////////////////////////////////////////////

	virtual bool ValidateReturnLine(ibValueModelReturnLine* retLine) const override {
		ibValueTreeNode* node = GetViewData<ibValueTreeNode>(retLine->GetLineItem());
		wxASSERT(node);
		return node ? node->m_valueTree != nullptr : false;
	}


	/////////////////////////////////////////////////////////

	ibValueTreeNode* GetRoot() const { return m_root; }

	void RowChanged(ibValueTreeNode* item) {
		/* wxDataViewModel:: */ m_modelProvider->ItemChanged(ibDataViewItem(item));
	}

	void RowValueChanged(ibValueTreeNode* item, unsigned int col) {
		/* wxDataViewModel:: */ m_modelProvider->ValueChanged(ibDataViewItem(item), col);
	}

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
		// NOTE: MyMusicTreeModelNodePtrArray is only an array of _pointers_
		//       thus removing the node from it doesn't result in freeing it
		std::vector<ibValueTreeNode*>& children = node->GetParent()->GetChildren();
		std::vector<ibValueTreeNode*>::iterator children_iterator = std::find(children.begin(), children.end(), node);
		if (children_iterator != children.end())
			children.erase(children_iterator);

		// notify control
		if (notify && !m_modelProvider->ItemDeleted(parent, item))
			return false;

		// free the node
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
		if (notify) /* wxDataViewModel:: */ m_modelProvider->Cleared();
	}

	void Sort(unsigned int col, bool ascending = true, bool notify = true) {
		std::vector<ibSortModel> fixedSort = { { col, ascending } };
		Sort(fixedSort, notify);
	}

	void Sort(std::vector<ibSortModel>& paSort, bool notify = true) {
		if (notify) /* wxDataViewModel:: */ m_modelProvider->BeforeReset();
		m_root->Sort(paSort);
		if (notify) /* wxDataViewModel:: */ m_modelProvider->AfterReset();
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

	virtual ibDataViewItem GetParent(const ibDataViewItem& item) const override {
		// the invisible root node has no parent
		if (!item.IsOk())
			return ibDataViewItem(nullptr);
		ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
		// "root" also has no parent
		if (m_root == node ||
			m_root == node->GetParent())
			return ibDataViewItem(nullptr);
		return ibDataViewItem((void*)node->GetParent());
	}

	virtual bool IsContainer(const ibDataViewItem& item) const override {
		// the invisible root node can have children
		// (in our model always "root")
		if (!item.IsOk())
			return true;
		ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
		if (node == nullptr)
			return false;
		return node->IsContainer();
	}

	// define current parent for hierarchical view 
	virtual unsigned int GetChildren(const ibDataViewItem& parent,
		ibDataViewItemArray& array) const override {
		ibValueTreeNode* node = GetViewData<ibValueTreeNode>(parent);
		if (node == nullptr)
			return GetChildren(ibDataViewItem(m_root), array);
		unsigned int count = node->GetChildCount();
		if (count == 0)
			return 0;
		array.Alloc(count);
		for (unsigned int pos = 0; pos < count; pos++) {
			array.Add(ibDataViewItem((void*)node->GetChild(pos)));
		}
		return count;
	}

	// override sorting to always sort branches ascendingly
	virtual bool HasDefaultCompare() const override { return true; }

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

protected:
	ibValueTreeNode* m_root;
};

#endif 