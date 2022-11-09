#ifndef _VALUETABLEPART_H__

#include "common/tableInfo.h"
#include "common/valueInfo.h"
#include "metadata/metaObjects/table/metaTableObject.h"
#include "guid/guid.h"

class ITabularSectionDataObject : public IValueTable {
	wxDECLARE_ABSTRACT_CLASS(ITabularSectionDataObject);
public:

	virtual IValueModelColumnCollection* GetColumnCollection() const override {
		return m_dataColumnCollection;
	}

	virtual IValueModelReturnLine* GetRowAt(const wxDataViewItem& line) {
		if (!line.IsOk())
			return NULL;
		return new CTabularSectionDataObjectReturnLine(this, line);
	}

	virtual wxDataViewItem FindRowValue(const CValue& cVal, const wxString& colName = wxEmptyString) const;
	virtual wxDataViewItem FindRowValue(IValueModelReturnLine* retLine) const;

	class CTabularSectionDataObjectColumnCollection : public IValueTable::IValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(CTabularSectionDataObjectColumnCollection);
	public:
		class CValueTabularSectionColumnInfo : public IValueTable::IValueModelColumnCollection::IValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(CValueTabularSectionColumnInfo);
			IMetaAttributeObject* m_metaAttribute;
		public:

			virtual unsigned int GetColumnID() const { return m_metaAttribute->GetMetaID(); }
			virtual wxString GetColumnName() const { return m_metaAttribute->GetName(); }
			virtual wxString GetColumnCaption() const { return m_metaAttribute->GetSynonym(); }
			virtual CValueTypeDescription* GetColumnTypes() const { return m_metaAttribute->GetValueTypeDescription(); }
			virtual int GetColumnWidth() const { return 0; }

			CValueTabularSectionColumnInfo();
			CValueTabularSectionColumnInfo(IMetaAttributeObject* metaAttribute);
			virtual ~CValueTabularSectionColumnInfo();

			virtual wxString GetTypeString() const {
				return wxT("tabularSectionColumnInfo");
			}

			virtual wxString GetString() const {
				return wxT("tabularSectionColumnInfo");
			}

			friend CTabularSectionDataObjectColumnCollection;
		};

	public:

		CTabularSectionDataObjectColumnCollection();
		CTabularSectionDataObjectColumnCollection(ITabularSectionDataObject* ownerTable);
		virtual ~CTabularSectionDataObjectColumnCollection();

		virtual CValueTypeDescription* GetColumnTypes(unsigned int col) const {
			CValueTabularSectionColumnInfo* columnInfo = m_columnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnTypes();
		}

		virtual IValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_columnInfo.size() < idx)
				return NULL;
			auto foundedIt = m_columnInfo.begin();
			std::advance(foundedIt, idx);
			return foundedIt->second;
		}

		virtual unsigned int GetColumnCount() const {
			CMetaTableObject* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			auto attributes =
				metaTable->GetObjectAttributes();
			return attributes.size();
		}

		virtual wxString GetTypeString() const { return wxT("tabularSectionColumn"); }
		virtual wxString GetString() const { return wxT("tabularSectionColumn"); }

		//array support 
		virtual CValue GetAt(const CValue& cKey);
		virtual void SetAt(const CValue& cKey, CValue& cVal);

		friend class ITabularSectionDataObject;

	protected:

		ITabularSectionDataObject* m_ownerTable;
		CMethods* m_methods;

		std::map<meta_identifier_t, CValueTabularSectionColumnInfo*> m_columnInfo;

	} *m_dataColumnCollection;

	class CTabularSectionDataObjectReturnLine : public IValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(CTabularSectionDataObjectReturnLine);
	public:
		ITabularSectionDataObject* m_ownerTable;
	public:

		CTabularSectionDataObjectReturnLine(ITabularSectionDataObject* ownerTable = NULL, const wxDataViewItem& line = wxDataViewItem(NULL));
		virtual ~CTabularSectionDataObjectReturnLine();

		virtual IValueTable* GetOwnerModel() const {
			return m_ownerTable;
		}

		//set meta/get meta
		virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
		virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

		virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов

		virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal); //установка атрибута
		virtual CValue GetAttribute(attributeArg_t& aParams); //значение атрибута

		virtual wxString GetTypeString() const { return wxT("tabularSectionRow"); }
		virtual wxString GetString() const { return wxT("tabularSectionRow"); }

		friend class ITabularSectionDataObject;
	private:
		CMethods* m_methods;
	};

	CMetaTableObject* GetMetaObject() const {
		return m_metaTable;
	}

	meta_identifier_t GetMetaID() const {
		return m_metaTable ? m_metaTable->GetMetaID() : wxNOT_FOUND;
	}

	ITabularSectionDataObject() : m_dataObject(NULL), m_metaTable(NULL), m_dataColumnCollection(NULL) {
	}

	ITabularSectionDataObject(IObjectValueInfo* dataObject, CMetaTableObject* tableObject) : m_dataObject(dataObject), m_metaTable(tableObject), m_dataColumnCollection(NULL) {
		m_dataColumnCollection = new CTabularSectionDataObjectColumnCollection(this);
		m_dataColumnCollection->IncrRef();
	}

	virtual ~ITabularSectionDataObject() {
		if (m_dataColumnCollection != NULL) {
			m_dataColumnCollection->DecrRef();
		}
	}

	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) override;

	virtual bool AutoCreateColumns() const { return false; }
	virtual bool EditableLine(const wxDataViewItem& item, unsigned int col) const {
		return !m_metaTable->IsNumberLine(col);
	}

	virtual void ActivateItem(CValueForm* formOwner,
		const wxDataViewItem& item, unsigned int col) {
		IValueTable::RowValueStartEdit(item, col);
	}


	virtual void AddValue(unsigned int before = 0);
	virtual void CopyValue();
	virtual void EditValue();
	virtual void DeleteValue();

	//append new row
	virtual long AppendRow(unsigned int before = 0);

	virtual bool LoadData(bool createData = true) { return true; }
	virtual bool SaveData() { return true; }
	virtual bool DeleteData() { return true; }

	virtual bool LoadDataFromTable(IValueTable* srcTable);
	virtual IValueTable* SaveDataToTable() const;
	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	static CMethods m_methods;

	virtual CMethods* GetPMethods() const;                          // получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                             // этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);       // вызов метода

	//array
	virtual CValue GetAt(const CValue& cKey);

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//Работа с итераторами 
	virtual bool HasIterator() const override {
		return true;
	}

	virtual CValue GetItEmpty() override {
		return new CTabularSectionDataObjectReturnLine(this, wxDataViewItem(NULL));
	}

	virtual CValue GetItAt(unsigned int idx) override {
		if (idx > (unsigned int)GetRowCount())
			return CValue();
		return new CTabularSectionDataObjectReturnLine(this, GetItem(idx));
	}

	virtual unsigned int GetItSize() const override { 
		return GetRowCount();
	}

protected:

	//set meta/get meta
	virtual void SetValueByMetaID(long line, const meta_identifier_t& id, const CValue& cVal);
	virtual CValue GetValueByMetaID(long line, const meta_identifier_t& id) const;

protected:

	IObjectValueInfo* m_dataObject;
	CMetaTableObject* m_metaTable;
};

class CTabularSectionDataObject : public ITabularSectionDataObject {
	wxDECLARE_DYNAMIC_CLASS(CTabularSectionDataObject);
public:

	CTabularSectionDataObject();
	CTabularSectionDataObject(IObjectValueInfo* dataObject, CMetaTableObject* tableObject);
	virtual ~CTabularSectionDataObject() {}
};

class CTabularSectionDataObjectRef : public ITabularSectionDataObject {
	wxDECLARE_DYNAMIC_CLASS(CTabularSectionDataObjectRef);
public:

	CTabularSectionDataObjectRef();
	CTabularSectionDataObjectRef(IObjectValueInfo* dataObject, CMetaTableObject* tableObject);
	virtual ~CTabularSectionDataObjectRef() {}

	//append new row
	virtual long AppendRow(unsigned int before = 0);

	//load/save/delete data
	virtual bool LoadData(bool createData = true);
	virtual bool SaveData();
	virtual bool DeleteData();

	//set meta/get meta
	virtual void SetValueByMetaID(long line, const meta_identifier_t& id, const CValue& cVal);
	virtual CValue GetValueByMetaID(long line, const meta_identifier_t& id) const;
};

#endif // !_VALUEUUID_H__
