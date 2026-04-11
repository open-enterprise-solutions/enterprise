#ifndef _VALUETABLEPART_H__

#include "backend/tableInfo.h"
#include "backend/valueInfo.h"

#include "backend/metaCollection/table/metaTableObject.h"

class BACKEND_API ibValueTabularSectionDataObjectBase : public ibValueModelTableBase {
	wxDECLARE_ABSTRACT_CLASS(ibValueTabularSectionDataObjectBase);
private:

	enum Func {
		enAddValue = 0,
		enCount,
		enFind,
		enDelete,
		enClear,
		enLoad,
		enUnload,
		enGetMetadata,
	};

	enum {
		eTabularSection,
	};

	class ibVariantDataValueNumberLine :
		public ibVariantDataValueImpl<ibValue> {
	public:
		ibVariantDataValueNumberLine(const long& cValue)
			: ibVariantDataValueImpl(static_cast<signed int>(cValue))
		{
		}
	};

public:

	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_recordColumnCollection; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) {
		if (!line.IsOk())
			return nullptr;
		return ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectReturnLine>(this, line);
	}

	virtual bool HasDefaultCompare() const override { return false; }

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual ibDataViewItem FindRowValue(ibValueModelReturnLine* retLine) const;

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal);
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const;

	virtual ibValue GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id) const {
		ibValue retValue;
		if (GetValueByMetaID(item, id, retValue))
			return retValue;
		return ibValue();
	}

	class ibValueTabularSectionDataObjectColumnCollection : public ibValueModelTableBase::ibValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(ibValueTabularSectionDataObjectColumnCollection);
	public:
		class ibValueTabularSectionColumnInfo : public ibValueModelTableBase::ibValueModelColumnCollection::ibValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(ibValueTabularSectionColumnInfo);
		public:

			virtual unsigned int GetColumnID() const { return m_metaAttribute->GetMetaID(); }
			virtual wxString GetColumnName() const { return m_metaAttribute->GetName(); }
			virtual wxString GetColumnCaption() const { return m_metaAttribute->GetSynonym(); }
			virtual const ibTypeDescription GetColumnType() const { return m_metaAttribute->GetTypeDesc(); }

			ibValueTabularSectionColumnInfo();
			ibValueTabularSectionColumnInfo(ibValueMetaObjectAttributeBase* attribute);
			virtual ~ibValueTabularSectionColumnInfo();

		private:
			ibValueMetaObjectAttributeBase* m_metaAttribute;
			friend ibValueTabularSectionDataObjectColumnCollection;
		};

	public:

		ibValueTabularSectionDataObjectColumnCollection();
		ibValueTabularSectionDataObjectColumnCollection(ibValueTabularSectionDataObjectBase* ownerTable);
		virtual ~ibValueTabularSectionDataObjectColumnCollection();

		virtual const ibTypeDescription GetColumnType(unsigned int col) const {
			return m_listColumnInfo.at(col)->GetColumnType();
		}

		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_listColumnInfo.size() < idx)
				return nullptr;
			auto it = m_listColumnInfo.begin();
			std::advance(it, idx);
			return it->second;
		}

		virtual unsigned int GetColumnCount() const { return m_listColumnInfo.size(); }

		//array support 
		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

		friend class ibValueTabularSectionDataObjectBase;

	protected:

		ibValueTabularSectionDataObjectBase* m_ownerTable;
		std::map<ibMetaID, ibValuePtr<ibValueTabularSectionColumnInfo>> m_listColumnInfo;
		ibValueMethodHelper* m_methodHelper;
	};

	class ibValueTabularSectionDataObjectReturnLine : public ibValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(ibValueTabularSectionDataObjectReturnLine);
	public:

		ibValueTabularSectionDataObjectReturnLine(ibValueTabularSectionDataObjectBase* ownerTable = nullptr, const ibDataViewItem& line = ibDataViewItem(nullptr));
		virtual ~ibValueTabularSectionDataObjectReturnLine();

		virtual ibValueModelTableBase* GetOwnerModel() const { return m_ownerTable; }
		virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
			//PrepareNames(); 
			return m_methodHelper;
		}

		virtual void PrepareNames() const;

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

		//Get ref class 
		virtual ibClassID GetClassType() const;

		virtual wxString GetClassName() const;
		virtual wxString GetString() const;

		friend class ibValueTabularSectionDataObjectBase;
	private:
		ibValueTabularSectionDataObjectBase* m_ownerTable;
		ibValueMethodHelper* m_methodHelper;
	};

	ibValueMetaObjectTableData* GetMetaObject() const { return m_metaTable; }
	ibMetaID GetMetaID() const { return m_metaTable ? m_metaTable->GetMetaID() : wxNOT_FOUND; }

#pragma region _source_data_

	//get metaData from object 
	virtual ibValueMetaObjectCompositeData* GetSourceMetaObject() const { return m_metaTable; }

	//Get ref class 
	virtual ibClassID GetSourceClassType() const { return GetClassType(); }

#pragma endregion 

	ibValueTabularSectionDataObjectBase() :
		m_objectValue(nullptr), m_metaTable(nullptr),
		m_recordColumnCollection(nullptr),
		m_methodHelper(nullptr), m_readOnly(false) {
	}

	ibValueTabularSectionDataObjectBase(ibValueDataObject* objectValue, ibValueMetaObjectTableData* tableObject, bool readOnly = false) :
		m_objectValue(objectValue), m_metaTable(tableObject),
		m_recordColumnCollection(ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectColumnCollection>(this)),
		m_methodHelper(new ibValueMethodHelper()), m_readOnly(readOnly) {
		for (const auto object : tableObject->GetGenericAttributeArrayObject()) {
			m_filterRow.AppendFilter(
				object->GetMetaID(),
				object->GetName(),
				object->GetSynonym(),
				ibComparisonType_Equal,
				object->GetTypeDesc(),
				object->CreateValue(),
				false
			);
		}
	}

	virtual ~ibValueTabularSectionDataObjectBase() { wxDELETE(m_methodHelper); }

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	virtual bool AutoCreateColumn() const { return false; }
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const {
		return !m_metaTable->IsNumberLine(col);
	}

	virtual void ActivateItem(ibBackendValueForm* formOwner,
		const ibDataViewItem& item, unsigned int col) {
		ibValueModelTableBase::RowValueStartEdit(item, col);
	}

	virtual void AddValue(unsigned int before = 0);
	virtual void CopyValue();
	virtual void EditValue();
	virtual void DeleteValue();

	//append new row
	virtual long AppendRow(unsigned int before = 0);

	virtual bool LoadData(const ibGuid& srcGuid, bool createData = true) { return true; }
	virtual bool SaveData() { return true; }
	virtual bool DeleteData() { return true; }

	virtual bool LoadDataFromTable(ibValueModelTableBase* srcTable);
	virtual ibValueModelTableBase* SaveDataToTable() const;

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}

	virtual void PrepareNames() const;                             // this method is automatically called to initialize attribute and method names
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       // method call

	//array
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//Working with iterators
	virtual bool HasIterator() const override { return true; }

	virtual ibValue GetIteratorEmpty() override {
		return ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectReturnLine>(this, ibDataViewItem(nullptr));
	}

	virtual ibValue GetIteratorAt(unsigned int idx) override {
		if (idx > (unsigned int)GetRowCount())
			return wxEmptyValue;
		return ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectReturnLine>(this, GetItem(idx));
	}

	virtual unsigned int GetIteratorCount() const override { return GetRowCount(); }

protected:

	void RefreshTabularSection();

	virtual void RefreshModel(const ibDataViewItem& topItem = ibDataViewItem(nullptr),
		const int countPerPage = defaultCountPerPage)
	{
		RefreshTabularSection();
	}

	bool m_readOnly;

	ibValueMetaObjectTableData* m_metaTable;

	ibValueDataObject* m_objectValue;
	ibValuePtr<ibValueTabularSectionDataObjectColumnCollection> m_recordColumnCollection;
	ibValueMethodHelper* m_methodHelper;
};

class BACKEND_API ibValueTabularSectionDataObject : public ibValueTabularSectionDataObjectBase {
	wxDECLARE_DYNAMIC_CLASS(ibValueTabularSectionDataObject);
public:

	ibValueTabularSectionDataObject();
	ibValueTabularSectionDataObject(class ibValueRecordDataObject* recordObject, ibValueMetaObjectTableData* tableObject);
	virtual ~ibValueTabularSectionDataObject() {}
};

class BACKEND_API ibValueTabularSectionDataObjectRef : public ibValueTabularSectionDataObjectBase {
	wxDECLARE_DYNAMIC_CLASS(ibValueTabularSectionDataObjectRef);
public:

	bool IsReadAfter() const { return m_readAfter; }

	ibValueTabularSectionDataObjectRef();
	ibValueTabularSectionDataObjectRef(class ibValueReferenceDataObject* reference, ibValueMetaObjectTableData* tableObject, bool readAfter = false);
	ibValueTabularSectionDataObjectRef(class ibValueRecordDataObjectRef* recordObject, ibValueMetaObjectTableData* tableObject);
	ibValueTabularSectionDataObjectRef(class ibValueSelectorRecordDataObject* selectorObject, ibValueMetaObjectTableData* tableObject);

	virtual ~ibValueTabularSectionDataObjectRef() {}

	virtual void CopyValue();
	virtual void DeleteValue();

	//append new row
	virtual long AppendRow(unsigned int before = 0);

	//load/save/delete data
	virtual bool LoadData(const ibGuid& srcGuid, bool createData = true);
	virtual bool SaveData();
	virtual bool DeleteData();

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal);
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const;

protected:
	bool m_readAfter;
};

#endif // !_VALUEUUID_H__
