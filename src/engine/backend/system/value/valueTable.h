#ifndef __VALUE_TABLE_H__
#define __VALUE_TABLE_H__

#include "valueArray.h"
#include "valueMap.h"

#include "backend/tableInfo.h"

const ibClassID g_valueTableCLSID = string_to_clsid("VL_TABL");

//Table support
class BACKEND_API ibValueModelTable : public ibValueModelRamTableBase {
	public:
private:
	// methods:
	enum Func {
		enAddRow = 0,
		enClone,
		enCount,
		enFind,
		enDelete,
		enClear,
		enSort,
	};
	//attributes:
	enum Prop {
		enColumns = 0,
	};
public:
	class ibValueModelTableColumnCollection : public ibValueModelTableBase::ibValueModelColumnCollection {
	public:
	private:
		enum Func {
			enAddColumn = 0,
			enRemoveColumn
		};
	public:

		class ibValueModelTableColumnInfo : public ibValueModelTableBase::ibValueModelColumnCollection::ibValueModelColumnInfo {
	public:
		private:

			unsigned int m_columnID;
			wxString m_columnName;
			ibTypeDescription m_columnType;
			wxString m_columnCaption;
			int m_columnWidth;

		public:

			ibValueModelTableColumnInfo();
			ibValueModelTableColumnInfo(unsigned int colId, const wxString& colName, const ibTypeDescription& typeDescription, const wxString& caption, int width);
			virtual ~ibValueModelTableColumnInfo();

			virtual unsigned int GetColumnID() const { return m_columnID; }
			virtual void SetColumnID(unsigned int col) { m_columnID = col; }
			virtual wxString GetColumnName() const { return m_columnName; }
			virtual void SetColumnName(const wxString& name) { m_columnName = name; }
			virtual wxString GetColumnCaption() const { return m_columnCaption; }
			virtual void SetColumnCaption(const wxString& caption) { m_columnCaption = caption; }
			virtual const ibTypeDescription GetColumnType() const { return m_columnType; }
			virtual void SetColumnType(const ibTypeDescription& typeDescription) { m_columnType = typeDescription; }
			virtual int GetColumnWidth() const { return m_columnWidth; }
			virtual void SetColumnWidth(int width) { m_columnWidth = width; }

			friend ibValueModelTableColumnCollection;
		};

	public:

		ibValueModelTableColumnCollection(ibValueModelTable* ownerTable = nullptr);
		virtual ~ibValueModelTableColumnCollection();

		ibValueModelColumnInfo* AddColumn(const wxString& colName,
			const ibTypeDescription& typeData,
			const wxString& caption,
			int width = wxDVC_DEFAULT_WIDTH) override {

			unsigned int max_id = 0;

			for (auto& col : m_listColumnInfo) {
				if (max_id < col->GetColumnID()) {
					max_id = col->GetColumnID();
				}
			}

			for (long row = 0; row < m_ownerTable->GetRowCount(); row++) {
				ibValueTableRow* node = m_ownerTable->GetViewData<ibValueTableRow>(m_ownerTable->GetItem(row));
				wxASSERT(node);
				node->SetValue(max_id + 1, ibValueTypeDescription::AdjustValue(typeData));
			}

			return m_listColumnInfo.emplace_back(
				ibValue::CreateAndPrepareValueRef<ibValueModelTableColumnInfo>(max_id + 1, colName, typeData, caption, width));
		}

		const ibTypeDescription GetColumnType(unsigned int col) const {
			for (auto& colInfo : m_listColumnInfo) {
				if (col == colInfo->GetColumnID()) {
					return colInfo->GetColumnType();
				}
			}
			return ibTypeDescription();
		}

		virtual void RemoveColumn(unsigned int col) {

			for (long row = 0; row < m_ownerTable->GetRowCount(); row++) {
				ibValueTableRow* node = m_ownerTable->GetViewData<ibValueTableRow>(m_ownerTable->GetItem(row));
				wxASSERT(node);
				node->EraseValue(col);
			}

			auto it = std::find_if(m_listColumnInfo.begin(), m_listColumnInfo.end(),
				[col](ibValueModelTableColumnInfo* colInfo) {
					return col == colInfo->GetColumnID();
				}
			);

			m_listColumnInfo.erase(it);
		}

		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_listColumnInfo.size() < idx)
				return nullptr;
			auto it = m_listColumnInfo.begin();
			std::advance(it, idx);
			return *it;
		}

		virtual unsigned int GetColumnCount() const { return m_listColumnInfo.size(); }

		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

		//WORK AS AN AGGREGATE OBJECT
		virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

		//array support
		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

		friend class ibValueModelTable;

	protected:

		ibValueModelTable* m_ownerTable;
		std::vector<ibValuePtr<ibValueModelTableColumnInfo>> m_listColumnInfo;
	};

	class ibValueModelTableReturnLine : public ibValueModelReturnLine {
	public:

		ibValueModelTableReturnLine(ibValueModelTable* ownerTable = nullptr, const ibDataViewItem& line = ibDataViewItem(nullptr));
		virtual ~ibValueModelTableReturnLine();

		virtual ibValueModelTableBase* GetOwnerModel() const { return m_ownerTable; }

		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

	private:
		ibValueModelTable* m_ownerTable;
	};

public:

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;

	virtual bool AutoCreateColumn() const { return true; }

	virtual ibValueModelColumnCollection* GetColumnCollection() const { return m_tableColumnCollection; }

	virtual ibValueModelTableReturnLine* GetRowAt(const long& line) {
		if (line > GetRowCount())
			return nullptr;
		return ibValue::CreateAndPrepareValueRef<ibValueModelTableReturnLine>(this, GetItem(line));
	}

	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) {
		if (!line.IsOk())
			return nullptr;
		return GetRowAt(GetRow(line));
	}

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) {
		ibValueTableRow* node = GetViewData<ibValueTableRow>(item);
		if (node == nullptr)
			return false;
		return node->SetValue(id, ibValueTypeDescription::AdjustValue(m_tableColumnCollection->GetColumnType(id), varMetaVal), true);
	}

	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const {
		ibValueTableRow* node = GetViewData<ibValueTableRow>(item);
		if (node == nullptr)
			return false;
		return node->GetValue(id, pvarMetaVal);
	}

	ibValueModelTable();
	ibValueModelTable(const ibValueModelTable& val);
	virtual ~ibValueModelTable();

	virtual void AddValue(unsigned int before = 0) {
		long row = GetRow(GetSelection());
		if (row > 0)
			AppendRow(row);
		else AppendRow();
	}

	virtual void CopyValue() { CopyRow(); }
	virtual void EditValue() { EditRow(); }
	virtual void DeleteValue() { DeleteRow(); }

	//array
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

	//check is empty
	virtual bool IsEmpty() const { return GetRowCount() == 0; }

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); // attribute value
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       // method call

	// implementation of base class virtuals to define model
	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	//support def. methods (in runtime)
	long AppendRow(unsigned int before = 0);
	void CopyRow();
	void EditRow();
	void DeleteRow();

	ibValueModelTable* Clone() { return ibValue::CreateAndPrepareValueRef<ibValueModelTable>(*this); }
	unsigned int Count() { return GetRowCount(); }
	void Clear();

#pragma region _tabular_data_
	//get metaData from object 
	virtual const ibValueMetaObjectCompositeData* GetSourceMetaObject() const { return nullptr; }

	//Get ref class 
	virtual ibClassID GetSourceClassType() const { return g_valueTableCLSID; }
#pragma endregion 

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	// Iterator runtime path lives on ibValueModel — drives Get*Fetch
	// in batches. GetEmptyRow yields the typed skeleton for the
	// IntelliSense type hint that the iterator state surfaces.
	virtual ibValue GetEmptyRow() override {
		return ibValue::CreateAndPrepareValueRef<ibValueModelTableReturnLine>(this, ibDataViewItem(nullptr));
	}

private:

	ibValuePtr<ibValueModelTableColumnCollection> m_tableColumnCollection;
};

#endif