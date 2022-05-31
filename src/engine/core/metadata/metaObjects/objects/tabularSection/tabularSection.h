#ifndef _VALUETABLEPART_H__

#include "common/tableInfo.h"
#include "common/valueInfo.h"
#include "metadata/metaObjects/tables/metaTableObject.h"
#include "guid/guid.h"

class ITabularSectionDataObject : public IValueTable {
	wxDECLARE_ABSTRACT_CLASS(ITabularSectionDataObject);
public:

	virtual IValueTableColumnCollection* GetColumns() const {
		return m_dataColumnCollection;
	}

	virtual IValueTableReturnLine* GetRowAt(unsigned int line)
	{
		if (line > m_aObjectValues.size())
			return NULL;
		return new CTabularSectionDataObjectReturnLine(this, line);
	}

	class CTabularSectionDataObjectColumnCollection : public IValueTable::IValueTableColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(CTabularSectionDataObjectColumnCollection);
	public:
		class CValueTabularSectionColumnInfo : public IValueTable::IValueTableColumnCollection::IValueTableColumnInfo {
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

			virtual wxString GetTypeString() const { return wxT("tabularSectionColumnInfo"); }
			virtual wxString GetString() const { return wxT("tabularSectionColumnInfo"); }

			friend CTabularSectionDataObjectColumnCollection;
		};

	public:

		CTabularSectionDataObjectColumnCollection();
		CTabularSectionDataObjectColumnCollection(ITabularSectionDataObject* ownerTable);
		virtual ~CTabularSectionDataObjectColumnCollection();

		virtual CValueTypeDescription* GetColumnTypes(unsigned int col) const
		{
			CValueTabularSectionColumnInfo* columnInfo = m_aColumnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnTypes();
		}

		virtual IValueTableColumnInfo* GetColumnInfo(unsigned int idx) const
		{
			if (m_aColumnInfo.size() < idx)
				return NULL;

			auto foundedIt = m_aColumnInfo.begin();
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

		//Работа с итераторами 
		virtual bool HasIterator() const { return true; }
		virtual CValue GetItAt(unsigned int idx)
		{
			auto itFounded = m_aColumnInfo.begin();
			std::advance(itFounded, idx);
			return itFounded->second;
		}

		virtual unsigned int GetItSize() const { return GetColumnCount(); }

		friend class ITabularSectionDataObject;

	protected:

		ITabularSectionDataObject* m_ownerTable;
		CMethods* m_methods;

		std::map<meta_identifier_t, CValueTabularSectionColumnInfo*> m_aColumnInfo;

	} *m_dataColumnCollection;

	class CTabularSectionDataObjectReturnLine : public IValueTableReturnLine {
		wxDECLARE_DYNAMIC_CLASS(CTabularSectionDataObjectReturnLine);
	public:
		ITabularSectionDataObject* m_ownerTable; int m_lineTable;
	public:

		virtual unsigned int GetLineTable() const { 
			return m_lineTable; 
		}
		
		virtual IValueTable* GetOwnerTable() const {
			return m_ownerTable; 
		}

		CTabularSectionDataObjectReturnLine();
		CTabularSectionDataObjectReturnLine(ITabularSectionDataObject* ownerTable, int line);
		virtual ~CTabularSectionDataObjectReturnLine();

		//set meta/get meta
		virtual void SetValueByMetaID(const meta_identifier_t &id, const CValue& cVal);
		virtual CValue GetValueByMetaID(const meta_identifier_t &id) const;

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

	CMetaTableObject* GetMetaObject() const {
		return m_metaTable;
	}

	meta_identifier_t GetMetaID() const {
		return m_metaTable ? m_metaTable->GetMetaID() : wxNOT_FOUND;
	}

	virtual bool AutoCreateColumns() const { return false; }
	virtual bool EditableLine(const wxDataViewItem& item, unsigned int col) const {
		auto itFounded = m_aObjectValues.begin();
		std::advance(itFounded, GetRow(item));
		auto& rowValues = *itFounded;
		auto foundedColumn = rowValues.find(col);
		if (foundedColumn != rowValues.end()) {
			return !m_metaTable->IsNumberLine(foundedColumn->first);
		}
		return true;
	}

	virtual void ActivateItem(CValueForm* formOwner,
		const wxDataViewItem& item, unsigned int col) {
		IValueTable::RowValueStartEdit(item, col);
	}

	//support def. table (in runtime)
	void Prepend(const wxString& text);
	void DeleteItem(const wxDataViewItem& item);
	void DeleteItems(const wxDataViewItemArray& items);

	// implementation of base class virtuals to define model
	virtual unsigned int GetColumnCount() const override;
	virtual wxString GetColumnType(unsigned int col) const override;

	virtual void GetValueByRow(wxVariant& variant,
		unsigned int row, unsigned int col) const override;

	virtual bool GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr& attr) const override;

	virtual bool SetValueByRow(const wxVariant& variant,
		unsigned int row, unsigned int col) override;

	virtual void AddValue(unsigned int before = 0);
	virtual void CopyValue();
	virtual void EditValue();
	virtual void DeleteValue();

	//append new row
	virtual unsigned int AppenRow(unsigned int before = 0);

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
	virtual bool HasIterator() const override { return true; }

	virtual CValue GetItEmpty() override
	{
		return new CTabularSectionDataObjectReturnLine(this, wxNOT_FOUND);
	}

	virtual CValue GetItAt(unsigned int idx) override
	{
		if (idx > m_aObjectValues.size())
			return CValue();

		return new CTabularSectionDataObjectReturnLine(this, idx);
	}

	virtual unsigned int GetItSize() const override { return m_aObjectValues.size(); }

protected:

	//set meta/get meta
	virtual void SetValueByMetaID(long line, const meta_identifier_t &id, const CValue& cVal);
	virtual CValue GetValueByMetaID(long line, const meta_identifier_t &id) const;

protected:

	IObjectValueInfo* m_dataObject;
	std::vector<std::map<meta_identifier_t, CValue>> m_aObjectValues;
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
	virtual unsigned int AppenRow(unsigned int before = 0);

	//load/save/delete data
	virtual bool LoadData(bool createData = true);
	virtual bool SaveData();
	virtual bool DeleteData();

	//set meta/get meta
	virtual void SetValueByMetaID(long line, const meta_identifier_t &id, const CValue& cVal);
	virtual CValue GetValueByMetaID(long line, const meta_identifier_t &id) const;
};

#endif // !_VALUEUUID_H__
