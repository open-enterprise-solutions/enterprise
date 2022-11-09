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
	class CValueTableColumnCollection : public IValueTable::IValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(CValueTableColumnCollection);
	public:

		class CValueTableColumnInfo : public IValueTable::IValueModelColumnCollection::IValueModelColumnInfo {
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
			virtual void SetColumnID(unsigned int col) { m_columnID = col; }
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

		CValueTableColumnCollection(CValueTable* ownerTable = NULL);
		virtual ~CValueTableColumnCollection();

		IValueModelColumnInfo* AddColumn(const wxString& colName, CValueTypeDescription* types, const wxString& caption, int width = wxDVC_DEFAULT_WIDTH) {
			unsigned int max_id = 0;
			for (auto& col : m_columnInfo) {
				if (max_id < col->GetColumnID()) {
					max_id = col->GetColumnID();
				}
			}
			CValueTableColumnInfo* columnInfo = new CValueTableColumnInfo(max_id + 1, colName, types, caption, width);
			for (long row = 0; row < m_ownerTable->GetRowCount(); row++) {
				wxValueTableRow *node = m_ownerTable->GetViewData(m_ownerTable->GetItem(row));
				wxASSERT(node);
				node->SetValue(types ? types->AdjustValue() : CValue(), max_id + 1);
			}
			columnInfo->IncrRef();
			m_columnInfo.push_back(columnInfo);
			return columnInfo;
		}

		CValueTypeDescription* GetColumnType(unsigned int col) const {
			for (auto& colInfo : m_columnInfo) {
				if (col == colInfo->GetColumnID()) {
					return colInfo->GetColumnTypes();
				}
			}
			return NULL;
		}

		virtual void RemoveColumn(unsigned int col) {
			for (long row = 0; row < m_ownerTable->GetRowCount(); row++) {
				wxValueTableRow* node = m_ownerTable->GetViewData(m_ownerTable->GetItem(row));
				wxASSERT(node);
				node->EraseValue(col);
			}
			auto foundedIt = std::find_if(m_columnInfo.begin(), m_columnInfo.end(), [col](CValueTableColumnInfo* colInfo) {
				return col == colInfo->GetColumnID();
				}
			);
			CValueTableColumnInfo* columnInfo = *foundedIt;
			wxASSERT(columnInfo);
			columnInfo->DecrRef();
			m_columnInfo.erase(foundedIt);
		}

		virtual IValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_columnInfo.size() < idx)
				return NULL;
			auto foundedIt = m_columnInfo.begin();
			std::advance(foundedIt, idx);
			return *foundedIt;
		}

		virtual unsigned int GetColumnCount() const { 
			return m_columnInfo.size(); 
		}

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

		friend class CValueTable;

	protected:

		CValueTable* m_ownerTable;
		std::vector<CValueTableColumnInfo*> m_columnInfo;
		CMethods* m_methods;

	} *m_dataColumnCollection;

	class CValueTableReturnLine : public IValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(CValueTableReturnLine);
	public:
		CValueTable* m_ownerTable; 
	public:

		CValueTableReturnLine(CValueTable* ownerTable = NULL, const wxDataViewItem& line = wxDataViewItem(NULL));
		virtual ~CValueTableReturnLine();

		virtual IValueTable* GetOwnerModel() const {
			return m_ownerTable;
		}

		//set meta/get meta
		virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
		virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

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

	static CMethods m_methods;

public:

	virtual IValueModelColumnCollection* GetColumnCollection() const {
		return m_dataColumnCollection;
	}

	virtual CValueTableReturnLine* GetRowAt(const long& line) {
		if (line < GetRowCount())
			return NULL;
		return new CValueTableReturnLine(this, GetItem(line));
	}

	virtual IValueModelReturnLine* GetRowAt(const wxDataViewItem& line) {
		if (!line.IsOk())
			return NULL;
		return GetRowAt(GetRow(line));
	}

	virtual wxDataViewItem FindRowValue(const CValue& cVal, const wxString& colName = wxEmptyString) const;
	virtual wxDataViewItem FindRowValue(IValueModelReturnLine* retLine) const;

	CValueTable();
	CValueTable(const CValueTable& val);
	virtual ~CValueTable();

	virtual void AddValue(unsigned int before = 0) {
		long row = GetRow(GetSelection());
		if (row > 0)
			AppendRow(row);
		else AppendRow();
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
		return GetRowCount() == 0;
	}

	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

	virtual CMethods* GetPMethods() const { return &m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);       //вызов метода

	// implementation of base class virtuals to define model
	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) override;

	//support def. methods (in runtime)
	long AppendRow(unsigned int before = 0);
	void CopyRow();
	void EditRow();
	void DeleteRow();

	CValueTable* Clone() {
		return new CValueTable(*this);
	}

	unsigned int Count() {
		return GetRowCount();
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
		return new CValueTableReturnLine(this, wxDataViewItem(NULL));
	}

	virtual CValue GetItAt(unsigned int idx) override {
		if (idx > (unsigned int)GetRowCount())
			return CValue();
		return new CValueTableReturnLine(this, GetItem(idx));
	}

	virtual unsigned int GetItSize() const override {
		return GetRowCount();
	}
};

#endif