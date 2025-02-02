#ifndef _VALUETABLEPART_H__

#include "backend/wrapper/tableInfo.h"
#include "backend/wrapper/valueInfo.h"

#include "backend/metaCollection/table/metaTableObject.h"

class BACKEND_API ITabularSectionDataObject : public IValueTable {
	wxDECLARE_ABSTRACT_CLASS(ITabularSectionDataObject);
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

public:

	virtual IValueModelColumnCollection* GetColumnCollection() const override {
		return m_dataColumnCollection;
	}

	virtual IValueModelReturnLine* GetRowAt(const wxDataViewItem& line) {
		if (!line.IsOk())
			return nullptr;
		return new CTabularSectionDataObjectReturnLine(this, line);
	}

	virtual bool HasDefaultCompare() const override {
		return false;
	}

	virtual wxDataViewItem FindRowValue(const CValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual wxDataViewItem FindRowValue(IValueModelReturnLine* retLine) const;

	//set meta/get meta
	virtual bool SetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, const CValue& varMetaVal);
	virtual bool GetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, CValue& pvarMetaVal) const;

	virtual CValue GetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id) const {
		CValue retValue;
		if (GetValueByMetaID(item, id, retValue))
			return retValue;
		return CValue();
	}

	class CTabularSectionDataObjectColumnCollection : public IValueTable::IValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(CTabularSectionDataObjectColumnCollection);
	public:
		class CValueTabularSectionColumnInfo : public IValueTable::IValueModelColumnCollection::IValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(CValueTabularSectionColumnInfo);
			IMetaObjectAttribute* m_metaAttribute;
		public:

			virtual unsigned int GetColumnID() const {
				return m_metaAttribute->GetMetaID();
			}

			virtual wxString GetColumnName() const {
				return m_metaAttribute->GetName();
			}

			virtual wxString GetColumnCaption() const {
				return m_metaAttribute->GetSynonym();
			}

			virtual typeDescription_t GetColumnType() const {
				return m_metaAttribute->GetTypeDescription();
			}

			CValueTabularSectionColumnInfo();
			CValueTabularSectionColumnInfo(IMetaObjectAttribute* metaAttribute);
			virtual ~CValueTabularSectionColumnInfo();

			friend CTabularSectionDataObjectColumnCollection;
		};

	public:

		CTabularSectionDataObjectColumnCollection();
		CTabularSectionDataObjectColumnCollection(ITabularSectionDataObject* ownerTable);
		virtual ~CTabularSectionDataObjectColumnCollection();

		virtual typeDescription_t GetColumnType(unsigned int col) const {
			CValueTabularSectionColumnInfo* columnInfo = m_columnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnType();
		}

		virtual IValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_columnInfo.size() < idx)
				return nullptr;
			auto& it = m_columnInfo.begin();
			std::advance(it, idx);
			return it->second;
		}

		virtual unsigned int GetColumnCount() const {
			CMetaObjectTable* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			const auto& obj = metaTable->GetObjectAttributes();
			return obj.size();
		}

		//array support 
		virtual bool SetAt(const CValue& varKeyValue, const CValue& varValue);
		virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

		friend class ITabularSectionDataObject;

	protected:

		ITabularSectionDataObject* m_ownerTable;
		CMethodHelper* m_methodHelper;

		std::map<meta_identifier_t, CValueTabularSectionColumnInfo*> m_columnInfo;

	};

	class CTabularSectionDataObjectReturnLine : public IValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(CTabularSectionDataObjectReturnLine);
	public:
		ITabularSectionDataObject* m_ownerTable;
	public:

		CTabularSectionDataObjectReturnLine(ITabularSectionDataObject* ownerTable = nullptr, const wxDataViewItem& line = wxDataViewItem(nullptr));
		virtual ~CTabularSectionDataObjectReturnLine();

		virtual IValueTable* GetOwnerModel() const {
			return m_ownerTable;
		}

		virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
			//PrepareNames(); 
			return m_methodHelper;
		}
		virtual void PrepareNames() const;

		virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal); //установка атрибута
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal); //значение атрибута

		//Get ref class 
		virtual class_identifier_t GetClassType() const;

		virtual wxString GetClassName() const;
		virtual wxString GetString() const;

		friend class ITabularSectionDataObject;
	private:
		CMethodHelper* m_methodHelper;
	};

	CMetaObjectTable* GetMetaObject() const {
		return m_metaTable;
	}

	meta_identifier_t GetMetaID() const {
		return m_metaTable ? m_metaTable->GetMetaID() : wxNOT_FOUND;
	}

	ITabularSectionDataObject() :
		m_objectValue(nullptr), m_metaTable(nullptr), m_dataColumnCollection(nullptr), m_methodHelper(nullptr), m_readOnly(false) {
	}

	ITabularSectionDataObject(IObjectValueInfo* objectValue, CMetaObjectTable* tableObject, bool readOnly = false) :
		m_objectValue(objectValue), m_metaTable(tableObject), m_dataColumnCollection(nullptr), m_methodHelper(new CMethodHelper()), m_readOnly(readOnly) {
		m_dataColumnCollection = new CTabularSectionDataObjectColumnCollection(this);
		m_dataColumnCollection->IncrRef();
	}

	virtual ~ITabularSectionDataObject() {
		if (m_dataColumnCollection != nullptr) {
			m_dataColumnCollection->DecrRef();
		}
		wxDELETE(m_methodHelper);
	}

	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) override;

	virtual bool AutoCreateColumn() const { return false; }
	virtual bool EditableLine(const wxDataViewItem& item, unsigned int col) const {
		return !m_metaTable->IsNumberLine(col);
	}

	virtual void ActivateItem(IBackendValueForm* formOwner,
		const wxDataViewItem& item, unsigned int col) {
		IValueTable::RowValueStartEdit(item, col);
	}

	virtual void AddValue(unsigned int before = 0);
	virtual void CopyValue();
	virtual void EditValue();
	virtual void DeleteValue();

	//append new row
	virtual long AppendRow(unsigned int before = 0);

	virtual bool LoadData(const Guid& srcGuid, bool createData = true) { return true; }
	virtual bool SaveData() { return true; }
	virtual bool DeleteData() { return true; }

	virtual bool LoadDataFromTable(IValueTable* srcTable);
	virtual IValueTable* SaveDataToTable() const;
	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const;                             // этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);       // вызов метода

	//array
	virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

	//Get ref class 
	virtual class_identifier_t GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//Работа с итераторами 
	virtual bool HasIterator() const override {
		return true;
	}

	virtual CValue GetIteratorEmpty() override {
		return new CTabularSectionDataObjectReturnLine(this, wxDataViewItem(nullptr));
	}

	virtual CValue GetIteratorAt(unsigned int idx) override {
		if (idx > (unsigned int)GetRowCount())
			return CValue();
		return new CTabularSectionDataObjectReturnLine(this, GetItem(idx));
	}

	virtual unsigned int GetIteratorCount() const override {
		return GetRowCount();
	}

protected:
	bool m_readOnly;
	CMethodHelper* m_methodHelper;
	IObjectValueInfo* m_objectValue;
	CTabularSectionDataObjectColumnCollection* m_dataColumnCollection;
	CMetaObjectTable* m_metaTable;
};

class BACKEND_API CTabularSectionDataObject : public ITabularSectionDataObject {
	wxDECLARE_DYNAMIC_CLASS(CTabularSectionDataObject);
public:

	CTabularSectionDataObject();
	CTabularSectionDataObject(class IRecordDataObject* recordObject, CMetaObjectTable* tableObject);
	virtual ~CTabularSectionDataObject() {}
};

class BACKEND_API CTabularSectionDataObjectRef : public ITabularSectionDataObject {
	wxDECLARE_DYNAMIC_CLASS(CTabularSectionDataObjectRef);
public:

	bool IsReadAfter() const {
		return m_readAfter;
	}

	CTabularSectionDataObjectRef();
	CTabularSectionDataObjectRef(class CReferenceDataObject* reference, CMetaObjectTable* tableObject, bool readAfter = false);
	CTabularSectionDataObjectRef(class IRecordDataObjectRef* recordObject, CMetaObjectTable* tableObject);
	CTabularSectionDataObjectRef(class CSelectorDataObject* selectorObject, CMetaObjectTable* tableObject);

	virtual ~CTabularSectionDataObjectRef() {}

	virtual void CopyValue();
	virtual void DeleteValue();

	//append new row
	virtual long AppendRow(unsigned int before = 0);

	//load/save/delete data
	virtual bool LoadData(const Guid& srcGuid, bool createData = true);
	virtual bool SaveData();
	virtual bool DeleteData();

	//set meta/get meta
	virtual bool SetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, const CValue& varMetaVal);
	virtual bool GetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, CValue& pvarMetaVal) const;

protected:
	bool m_readAfter;
};

#endif // !_VALUEUUID_H__
