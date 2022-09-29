#ifndef _VALUETABLE_H__
#define _VALUETABLE_H__

#include "value.h"
#include "valueArray.h"
#include "valueMap.h"
#include "common/tableInfo.h"
#include "utils/stringUtils.h"

const CLASS_ID g_valueTableCLSID = TEXT2CLSID("VL_TABL");

//Поддержка таблиц
class CValueTable : public IValueTable {
	wxDECLARE_DYNAMIC_CLASS(CValueTable);
public:
	class CValueTableColumnCollection : public IValueTable::IValueTableColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(CValueTableColumnCollection);
	public:

		class CValueTableColumnInfo : public IValueTable::IValueTableColumnCollection::IValueTableColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(CValueTableColumnInfo);
		private:

			unsigned int m_columnID;
			wxString m_columnName;
			CValueTypeDescription* m_columnTypes;
			wxString m_columnCaption;
			int m_columnWidth;

		public:

			CValueTableColumnInfo();
			CValueTableColumnInfo(unsigned int colId, const wxString& colName, CValueTypeDescription* types, const wxString& caption, int width);
			virtual ~CValueTableColumnInfo();

			virtual unsigned int GetColumnID() const { return m_columnID; }
			virtual void SetColumnID(unsigned int col_id) { m_columnID = col_id; }
			virtual wxString GetColumnName() const { return m_columnName; }
			virtual void SetColumnName(const wxString& name) { m_columnName = name; }
			virtual wxString GetColumnCaption() const { return m_columnCaption; }
			virtual void SetColumnCaption(const wxString& caption) { m_columnCaption = caption; }
			virtual CValueTypeDescription* GetColumnTypes() const { return m_columnTypes; }
			virtual void SetColumnTypes(CValueTypeDescription* td) { m_columnTypes = td; }
			virtual int GetColumnWidth() const { return m_columnWidth; }
			virtual void SetColumnWidth(int width) { m_columnWidth = width; }

			virtual wxString GetTypeString() const { 
				return wxT("tableValueColumnInfo"); 
			}
		
			virtual wxString GetString() const { 
				return wxT("tableValueColumnInfo");
			}

			friend CValueTableColumnCollection;
		};

	public:

		CValueTableColumnCollection();
		CValueTableColumnCollection(CValueTable* ownerTable);
		virtual ~CValueTableColumnCollection();

		IValueTableColumnInfo* AddColumn(const wxString& colName, CValueTypeDescription* types, const wxString& caption, int width = wxDVC_DEFAULT_WIDTH)
		{
			unsigned int max_id = 0;
			for (auto& col : m_aColumnInfo) {
				if (max_id < col->GetColumnID()) {
					max_id = col->GetColumnID();
				}
			}
			CValueTableColumnInfo* columnInfo = new CValueTableColumnInfo(max_id + 1, colName, types, caption, width);
			m_aColumnInfo.push_back(columnInfo);
			for (auto& rowValue : m_ownerTable->m_aObjectValues)
				rowValue.insert_or_assign(max_id + 1, types ? types->AdjustValue() : CValue());
			columnInfo->IncrRef();
			return columnInfo;
		}

		CValueTypeDescription* GetColumnType(unsigned int col_id) const {
			for (auto& col : m_aColumnInfo) {
				if (col_id == col->GetColumnID()) {
					return col->GetColumnTypes();
				}
			}
			return NULL;
		}

		virtual void RemoveColumn(unsigned int col_id)
		{
			for (auto& rowValue : m_ownerTable->m_aObjectValues) {
				rowValue.erase(col_id);
			}

			auto foundedIt = std::find_if(m_aColumnInfo.begin(), m_aColumnInfo.end(), [col_id](CValueTableColumnInfo* colInfo) {
				return col_id == colInfo->GetColumnID();
				});

			CValueTableColumnInfo* columnInfo = *foundedIt;
			wxASSERT(columnInfo);
			columnInfo->DecrRef();

			m_aColumnInfo.erase(foundedIt);
		}

		virtual IValueTableColumnInfo* GetColumnInfo(unsigned int idx) const
		{
			if (m_aColumnInfo.size() < idx)
				return NULL;

			auto foundedIt = m_aColumnInfo.begin();
			std::advance(foundedIt, idx);
			return *foundedIt;
		}

		virtual unsigned int GetColumnCount() const { return m_aColumnInfo.size(); }

		virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;

		virtual wxString GetTypeString() const {
			return wxT("tableValueColumn");
		}
		
		virtual wxString GetString() const { 
			return wxT("tableValueColumn"); 
		}

		//РАБОТА КАК АГРЕГАТНОГО ОБЪЕКТА
		virtual CValue Method(methodArg_t& aParams);

		//array support 
		virtual CValue GetAt(const CValue& cKey);
		virtual void SetAt(const CValue& cKey, CValue& cVal);

		//Работа с итераторами 
		virtual bool HasIterator() const { return true; }
		virtual CValue GetItAt(unsigned int idx) {
			auto itFounded = m_aColumnInfo.begin();
			std::advance(itFounded, idx);
			return *itFounded;
		}

		virtual unsigned int GetItSize() const {
			return GetColumnCount();
		}

		friend class CValueTable;

	protected:

		CValueTable* m_ownerTable;
		std::vector<CValueTableColumnInfo*> m_aColumnInfo;
		CMethods* m_methods;

	} *m_dataColumnCollection;

	class CValueTableReturnLine : public IValueTableReturnLine {
		wxDECLARE_DYNAMIC_CLASS(CValueTableReturnLine);
	public:
		CValueTable* m_ownerTable; int m_lineTable;
	public:

		virtual unsigned int GetLineTable() const { 
			return m_lineTable; 
		}
		
		virtual IValueTable* GetOwnerTable() const { 
			return m_ownerTable;
		}

		CValueTableReturnLine();
		CValueTableReturnLine(CValueTable* ownerTable, int line);
		virtual ~CValueTableReturnLine();

		//set meta/get meta
		virtual void SetValueByMetaID(const meta_identifier_t &id, const CValue& cVal);
		virtual CValue GetValueByMetaID(const meta_identifier_t &id) const;

		virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

		virtual wxString GetTypeString() const { 
			return wxT("tableValueRow");
		}
	
		virtual wxString GetString() const { 
			return wxT("tableValueRow"); 
		}

		virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal); //установка атрибута
		virtual CValue GetAttribute(attributeArg_t& aParams); //значение атрибута

	private:

		CMethods* m_methods;
	};

public:

	virtual IValueTableColumnCollection* GetColumns() const {
		return m_dataColumnCollection;
	}

	virtual IValueTableReturnLine* CreateReturnLine() {
		return AddRow();
	}

	virtual IValueTableReturnLine* GetRowAt(unsigned int line)
	{
		if (line > m_aObjectValues.size())
			return NULL;

		return new CValueTableReturnLine(this, line);
	}

	CValueTable();
	CValueTable(const CValueTable& val);
	virtual ~CValueTable();

	virtual void AddValue(unsigned int before = 0) { 
		AddRow(before);
	}
	
	virtual void CopyValue() { 
		CopyRow();
	}
	
	virtual void EditValue() { 
		EditRow(); 
	}
	
	virtual void DeleteValue() { 
		DeleteRow();
	}

	//array
	virtual CValue GetAt(const CValue& cKey);

	wxString GetTypeString() const { 
		return wxT("tableValue");
	}
	
	wxString GetString() const {
		return wxT("tableValue"); 
	}

	//check is empty
	virtual inline bool IsEmpty() const override {
		return m_aObjectValues.empty();
	}

	static CMethods m_methods;

	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

	virtual CMethods* GetPMethods() const { return &m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);       //вызов метода

	// implementation of base class virtuals to define model
	virtual unsigned int GetColumnCount() const override;
	virtual wxString GetColumnType(unsigned int col) const override;

	virtual void GetValueByRow(wxVariant& variant,
		unsigned int row, unsigned int col) const override;

	virtual bool GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr& attr) const override;

	virtual bool SetValueByRow(const wxVariant& variant,
		unsigned int row, unsigned int col) override;

	//support def. methods (in runtime)
	CValueTableReturnLine* AddRow(unsigned int before = 0);
	void CopyRow();
	void EditRow();
	void DeleteRow();

	CValueTable* Clone() {
		return new CValueTable(*this);
	}

	unsigned int Count() {
		return m_aObjectValues.size();
	}

	void Clear();

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//Работа с итераторами 
	virtual bool HasIterator() const override { 
		return true; 
	}

	virtual CValue GetItEmpty() override {
		return new CValueTableReturnLine(this, wxNOT_FOUND);
	}

	virtual CValue GetItAt(unsigned int idx) override {
		if (idx > m_aObjectValues.size())
			return CValue();
		return new CValueTableReturnLine(this, idx);
	}

	virtual unsigned int GetItSize() const override { 
		return m_aObjectValues.size();
	}

protected:

	std::vector<std::map<unsigned int, CValue>> m_aObjectValues;
};

#endif