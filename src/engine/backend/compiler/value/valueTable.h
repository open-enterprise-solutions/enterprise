#ifndef _VALUETABLE_H__
#define _VALUETABLE_H__

#include "value.h"
#include "valueArray.h"
#include "valueMap.h"
#include "backend/wrapper/tableInfo.h"

const class_identifier_t g_valueTableCLSID = string_to_clsid("VL_TABL");

//Поддержка таблиц
class BACKEND_API CValueTable : public IValueTable {
	wxDECLARE_DYNAMIC_CLASS(CValueTable);
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
	class CValueTableColumnCollection : public IValueTable::IValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(CValueTableColumnCollection);
	private:
		enum Func {
			enAddColumn = 0,
			enRemoveColumn
		};
	public:

		class CValueTableColumnInfo : public IValueTable::IValueModelColumnCollection::IValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(CValueTableColumnInfo);
		private:

			unsigned int m_columnID;
			wxString m_columnName;
			typeDescription_t m_columnType;
			wxString m_columnCaption;
			int m_columnWidth;

		public:

			CValueTableColumnInfo();
			CValueTableColumnInfo(unsigned int colId, const wxString& colName, const typeDescription_t& typeDescription, const wxString& caption, int width);
			virtual ~CValueTableColumnInfo();

			virtual unsigned int GetColumnID() const {
				return m_columnID;
			}

			virtual void SetColumnID(unsigned int col) {
				m_columnID = col;
			}

			virtual wxString GetColumnName() const {
				return m_columnName;
			}

			virtual void SetColumnName(const wxString& name) {
				m_columnName = name;
			}

			virtual wxString GetColumnCaption() const {
				return m_columnCaption;
			}

			virtual void SetColumnCaption(const wxString& caption) {
				m_columnCaption = caption;
			}

			virtual typeDescription_t GetColumnType() const {
				return m_columnType;
			}

			virtual void SetColumnType(const typeDescription_t& typeDescription) {
				m_columnType = typeDescription;
			}

			virtual int GetColumnWidth() const {
				return m_columnWidth;
			}

			virtual void SetColumnWidth(int width) {
				m_columnWidth = width;
			}

			friend CValueTableColumnCollection;
		};

	public:

		CValueTableColumnCollection(CValueTable* ownerTable = nullptr);
		virtual ~CValueTableColumnCollection();

		IValueModelColumnInfo* AddColumn(const wxString& colName,
			const typeDescription_t& typeData,
			const wxString& caption,
			int width = wxDVC_DEFAULT_WIDTH) override {
			unsigned int max_id = 0;
			for (auto& col : m_columnInfo) {
				if (max_id < col->GetColumnID()) {
					max_id = col->GetColumnID();
				}
			}
			CValueTableColumnInfo* columnInfo = new CValueTableColumnInfo(max_id + 1, colName, typeData, caption, width);
			for (long row = 0; row < m_ownerTable->GetRowCount(); row++) {
				wxValueTableRow* node = m_ownerTable->GetViewData<wxValueTableRow>(m_ownerTable->GetItem(row));
				wxASSERT(node);
				node->SetValue(max_id + 1, CValueTypeDescription::AdjustValue(typeData));
			}
			columnInfo->IncrRef();
			m_columnInfo.push_back(columnInfo);
			return columnInfo;
		}

		typeDescription_t GetColumnType(unsigned int col) const {
			for (auto& colInfo : m_columnInfo) {
				if (col == colInfo->GetColumnID()) {
					return colInfo->GetColumnType();
				}
			}
			return typeDescription_t();
		}

		virtual void RemoveColumn(unsigned int col) {
			for (long row = 0; row < m_ownerTable->GetRowCount(); row++) {
				wxValueTableRow* node = m_ownerTable->GetViewData<wxValueTableRow>(m_ownerTable->GetItem(row));
				wxASSERT(node);
				node->EraseValue(col);
			}
			auto& it = std::find_if(m_columnInfo.begin(), m_columnInfo.end(), [col](CValueTableColumnInfo* colInfo) {
				return col == colInfo->GetColumnID();
				}
			);
			CValueTableColumnInfo* columnInfo = *it;
			wxASSERT(columnInfo);
			columnInfo->DecrRef();
			m_columnInfo.erase(it);
		}

		virtual IValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_columnInfo.size() < idx)
				return nullptr;
			auto& it = m_columnInfo.begin();
			std::advance(it, idx);
			return *it;
		}

		virtual unsigned int GetColumnCount() const {
			return m_columnInfo.size();
		}

		virtual CMethodHelper* GetPMethods() const {
			//PrepareNames();
			return m_methodHelper;
		}
		
		virtual void PrepareNames() const;


		//РАБОТА КАК АГРЕГАТНОГО ОБЪЕКТА
		virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);
		virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

		//array support 
		virtual bool SetAt(const CValue& varKeyValue, const CValue& varValue);
		virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

		friend class CValueTable;

	protected:

		CValueTable* m_ownerTable;
		std::vector<CValueTableColumnInfo*> m_columnInfo;
		CMethodHelper* m_methodHelper;

	} *m_dataColumnCollection;

	class CValueTableReturnLine : public IValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(CValueTableReturnLine);
	public:
		CValueTable* m_ownerTable;
	public:

		CValueTableReturnLine(CValueTable* ownerTable = nullptr, const wxDataViewItem& line = wxDataViewItem(nullptr));
		virtual ~CValueTableReturnLine();

		virtual IValueTable* GetOwnerModel() const {
			return m_ownerTable;
		}

		virtual CMethodHelper* GetPMethods() const {
			//PrepareNames();
			return m_methodHelper;
		}
	
		virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

		virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal); //установка атрибута
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal); //значение атрибута

	private:
		CMethodHelper* m_methodHelper;
	};

	static CMethodHelper m_methodHelper;

public:

	virtual wxDataViewItem FindRowValue(const CValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual wxDataViewItem FindRowValue(IValueModelReturnLine* retLine) const;

	virtual IValueModelColumnCollection* GetColumnCollection() const {
		return m_dataColumnCollection;
	}

	virtual CValueTableReturnLine* GetRowAt(const long& line) {
		if (line > GetRowCount())
			return nullptr;
		return new CValueTableReturnLine(this, GetItem(line));
	}

	virtual IValueModelReturnLine* GetRowAt(const wxDataViewItem& line) {
		if (!line.IsOk())
			return nullptr;
		return GetRowAt(GetRow(line));
	}

	//set meta/get meta
	virtual bool SetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, const CValue& varMetaVal) {
		wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
		if (node == nullptr)
			return false;
		return node->SetValue(id, CValueTypeDescription::AdjustValue(m_dataColumnCollection->GetColumnType(id), varMetaVal), true);
	}

	virtual bool GetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, CValue& pvarMetaVal) const {
		wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
		if (node == nullptr)
			return false;
		return node->GetValue(id, pvarMetaVal);
	}

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
	virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

	//check is empty
	virtual inline bool IsEmpty() const {
		return GetRowCount() == 0;
	}

	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal); //значение атрибута

	virtual CMethodHelper* GetPMethods() const {  //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames();
		return &m_methodHelper;
	}
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);       //вызов метода

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
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//Работа с итераторами 
	virtual bool HasIterator() const override {
		return true;
	}

	virtual CValue GetIteratorEmpty() override {
		return new CValueTableReturnLine(this, wxDataViewItem(nullptr));
	}

	virtual CValue GetIteratorAt(unsigned int idx) override {
		if (idx > (unsigned int)GetRowCount())
			return CValue();
		return new CValueTableReturnLine(this, GetItem(idx));
	}

	virtual unsigned int GetIteratorCount() const override {
		return GetRowCount();
	}
};

#endif