#ifndef _OBJECT_LIST_H__
#define _OBJECT_LIST_H__

#include "backend/metaCollection/partial/object.h"
#include "backend/metaCollection/partial/reference/reference.h"

//base list class 
class BACKEND_API IListDataObject : public IValueTable,
	public ISourceDataObject {
	wxDECLARE_ABSTRACT_CLASS(IListDataObject);
protected:
	enum Func {
		enRefresh
	};
private:

	// implementation of base class virtuals to define model
	virtual IValueModelColumnCollection* GetColumnCollection() const override {
		return m_dataColumnCollection;
	}

	virtual IValueModelReturnLine* GetRowAt(const wxDataViewItem& line) override {
		if (!line.IsOk())
			return nullptr;
		return new CDataObjectListReturnLine(this, line);
	}

public:

	virtual int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
		unsigned int col, bool ascending) const override {
		wxASSERT(item1.IsOk() && item2.IsOk());
		const int long row1 = GetRow(item1);
		const int long row2 = GetRow(item2);
		if (row1 < row2)
			return -1;
		else if (row1 > row2)
			return 1;
		return 0;
	}

	class CDataObjectListColumnCollection : public IValueTable::IValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(CDataObjectListColumnCollection);
	public:
		class CDataObjectListColumnInfo : public IValueTable::IValueModelColumnCollection::IValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(CDataObjectListColumnInfo);
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

			CDataObjectListColumnInfo();
			CDataObjectListColumnInfo(IMetaObjectAttribute* metaAttribute);
			virtual ~CDataObjectListColumnInfo();

			friend CDataObjectListColumnCollection;
		};

	public:

		CDataObjectListColumnCollection();
		CDataObjectListColumnCollection(IListDataObject* ownerTable, IMetaObjectGenericData* metaObject);
		virtual ~CDataObjectListColumnCollection();

		virtual typeDescription_t GetColumnType(unsigned int col) const {
			CDataObjectListColumnInfo* columnInfo = m_columnInfo.at(col);
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
			IMetaObjectGenericData* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			const auto& obj = metaTable->GetGenericAttributes();
			return obj.size();
		}

		//array support 
		virtual bool SetAt(const CValue& varKeyValue, const CValue& varValue);
		virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

		friend class IListDataObject;

	protected:

		IListDataObject* m_ownerTable;
		CMethodHelper* m_methodHelper;

		std::map<meta_identifier_t, CDataObjectListColumnInfo*> m_columnInfo;
	};

	class CDataObjectListReturnLine : public IValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(CDataObjectListReturnLine);
	public:

		CDataObjectListReturnLine(IListDataObject* ownerTable = nullptr, const wxDataViewItem& line = wxDataViewItem(nullptr));
		virtual ~CDataObjectListReturnLine();

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

	protected:
		CMethodHelper* m_methodHelper;
		IListDataObject* m_ownerTable;
	};

public:

	void AppendSort(IMetaObject* metaObject, bool ascending = true, bool use = true, bool system = false) {
		if (metaObject == nullptr)
			return;
		m_sortOrder.AppendSort(
			metaObject->GetMetaID(),
			metaObject->GetName(),
			metaObject->GetSynonym(),
			ascending, use, system
		);
	}

	virtual bool AutoCreateColumn() const {
		return false;
	}

	virtual bool DynamicRefresh() const {
		return true;
	}

	virtual bool EditableLine(const wxDataViewItem& item, unsigned int col) const {
		return false;
	}

	//set meta/get meta
	virtual bool SetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, const CValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, CValue& pvarMetaVal) const {
		wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
		if (node == nullptr)
			return false;
		return node->GetValue(id, pvarMetaVal);
	}

	virtual bool GetValueAttribute(IMetaObjectAttribute* metaAttr, CValue& retValue, class IDatabaseResultSet* resultSet, bool createData = true) {
		return IMetaObjectAttribute::GetValueAttribute(metaAttr, retValue, resultSet, createData);
	}

	//ctor
	IListDataObject(IMetaObjectGenericData* metaObject = nullptr, const form_identifier_t& formType = wxNOT_FOUND);
	virtual ~IListDataObject();

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	//get metaData from object 
	virtual IMetaObjectGenericData* GetSourceMetaObject() const final {
		return GetMetaObject();
	}

	//Get ref class 
	virtual class_identifier_t GetSourceClassType() const final {
		return GetClassType();
	};

	//get unique identifier 
	virtual CUniqueKey GetGuid() const {
		return m_objGuid;
	};

	//get metaData from object 
	virtual IMetaObjectGenericData* GetMetaObject() const = 0;

	//counter
	virtual void IncrRef() {
		CValue::IncrRef();
	}

	virtual void DecrRef() {
		CValue::DecrRef();
	}

	virtual inline bool IsEmpty() const {
		return false;
	}

	//Get ref class 
	virtual class_identifier_t GetClassType() const = 0;

	virtual wxString GetClassName() const = 0;
	virtual wxString GetString() const = 0;

	//operator 
	virtual operator CValue() const {
		return this;
	};

protected:
	Guid m_objGuid;
	CDataObjectListColumnCollection* m_dataColumnCollection;
	CMethodHelper* m_methodHelper;
};

// list enumeration 
class BACKEND_API CListDataObjectEnumRef : public IListDataObject {
	wxDECLARE_DYNAMIC_CLASS(CListDataObjectRef);
public:
	struct wxValueTableEnumRow : public wxValueTableRow {
		wxValueTableEnumRow(const Guid& guid) :
			wxValueTableRow(), m_objGuid(guid) {
		}
		Guid GetGuid() const {
			return m_objGuid;
		}
	private:
		Guid m_objGuid;
	};
public:

	virtual bool UseStandartCommand() const {
		return false;
	}

	virtual wxDataViewItem FindRowValue(const CValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual wxDataViewItem FindRowValue(IValueModelReturnLine* retLine) const;

	//Constructor
	CListDataObjectEnumRef(IMetaObjectRecordDataEnumRef* metaObject = nullptr, const form_identifier_t& formType = wxNOT_FOUND, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) override;

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel(const int countPerPage = defaultCountPerPage);
	virtual void RefreshItemModel(
		const wxDataViewItem& topItem,
		const wxDataViewItem& currentItem,
		const int countPerPage,
		const short scroll = 0
	);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const;
	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);       // вызов метода

	//on activate item
	virtual void ActivateItem(IBackendValueForm* srcForm,
		const wxDataViewItem& item, unsigned int col) {
		if (m_choiceMode)
			ChooseValue(srcForm);
	}

	//get metaData from object 
	virtual IMetaObjectRecordDataEnumRef* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual class_identifier_t GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm);

	//events:
	virtual void ChooseValue(IBackendValueForm* srcForm);

private:

	bool m_choiceMode;
	IMetaObjectRecordDataEnumRef* m_metaObject;
};

// list without parent  
class BACKEND_API CListDataObjectRef : public IListDataObject {
	wxDECLARE_DYNAMIC_CLASS(CListDataObjectRef);
public:
	struct wxValueTableListRow : public wxValueTableRow {
		wxValueTableListRow(const Guid& guid) :
			wxValueTableRow(), m_objGuid(guid) {
		}
		Guid GetGuid() const {
			return m_objGuid;
		}
	private:
		Guid m_objGuid;
	};
public:

	virtual wxDataViewItem FindRowValue(const CValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual wxDataViewItem FindRowValue(IValueModelReturnLine* retLine) const;

	//Constructor
	CListDataObjectRef(IMetaObjectRecordDataRef* metaObject = nullptr, const form_identifier_t& formType = wxNOT_FOUND, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) override;

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel(const int countPerPage = defaultCountPerPage);
	virtual void RefreshItemModel(
		const wxDataViewItem& topItem,
		const wxDataViewItem& currentItem,
		const int countPerPage,
		const short scroll = 0
	);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const;
	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);       // вызов метода

	//on activate item
	virtual void ActivateItem(IBackendValueForm* srcForm,
		const wxDataViewItem& item, unsigned int col) {
		if (m_choiceMode) {
			ChooseValue(srcForm);
		}
		else {
			EditValue();
		}
	}

	//get metaData from object 
	virtual IMetaObjectRecordDataRef* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual class_identifier_t GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

	virtual void ChooseValue(IBackendValueForm* srcForm);

private:

	bool m_choiceMode;

	IMetaObjectRecordDataRef* m_metaObject;
};

// list register
class BACKEND_API CListRegisterObject : public IListDataObject {
	wxDECLARE_DYNAMIC_CLASS(CListRegisterObject);
public:
	struct wxValueTableKeyRow : public wxValueTableRow {
		wxValueTableKeyRow() :
			wxValueTableRow(), m_nodeKeys() {
		}
		void AppendNodeValue(const meta_identifier_t& id, const CValue& variant) {
			m_nodeKeys.insert_or_assign(id, variant);
		}
		CValue& AppendNodeValue(const meta_identifier_t& id) {
			return m_nodeKeys[id];
		}
		CUniquePairKey GetUniquePairKey(IMetaObjectRegisterData* metaObject) const {
			return CUniquePairKey(metaObject, m_nodeValues);
		}

	private:
		valueArray_t m_nodeKeys;
	};
public:

	virtual wxDataViewItem FindRowValue(const CValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual wxDataViewItem FindRowValue(IValueModelReturnLine* retLine) const;

	//Constructor
	CListRegisterObject(IMetaObjectRegisterData* metaObject = nullptr, const form_identifier_t& formType = wxNOT_FOUND);

	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) override;

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel(const int countPerPage = defaultCountPerPage);
	virtual void RefreshItemModel(
		const wxDataViewItem& topItem,
		const wxDataViewItem& currentItem,
		const int countPerPage,
		const short scroll = 0
	);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const;
	virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);       // вызов метода
	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	//on activate item
	virtual void ActivateItem(IBackendValueForm* srcForm,
		const wxDataViewItem& item, unsigned int col) {
		EditValue();
	}

	//get metaData from object 
	virtual IMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual class_identifier_t GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

private:
	IMetaObjectRegisterData* m_metaObject;
};

//base tree class 
class BACKEND_API ITreeDataObject : public IValueTree,
	public ISourceDataObject {
	wxDECLARE_ABSTRACT_CLASS(ITreeDataObject);
protected:
	enum Func {
		enRefresh
	};
private:

	// implementation of base class virtuals to define model
	virtual IValueModelColumnCollection* GetColumnCollection() const override {
		return m_dataColumnCollection;
	}

	virtual IValueModelReturnLine* GetRowAt(const wxDataViewItem& line) {
		if (!line.IsOk())
			return nullptr;
		return new CDataObjectTreeReturnLine(this, line);
	}

public:

	class CDataObjectTreeColumnCollection : public IValueTree::IValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(CDataObjectTreeColumnCollection);
	public:
		class CDataObjectTreeColumnInfo : public IValueTree::IValueModelColumnCollection::IValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(CDataObjectTreeColumnInfo);
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

			CDataObjectTreeColumnInfo();
			CDataObjectTreeColumnInfo(IMetaObjectAttribute* metaAttribute);
			virtual ~CDataObjectTreeColumnInfo();

			friend CDataObjectTreeColumnCollection;
		};

	public:

		CDataObjectTreeColumnCollection();
		CDataObjectTreeColumnCollection(ITreeDataObject* ownerTable, IMetaObjectGenericData* metaObject);
		virtual ~CDataObjectTreeColumnCollection();

		virtual typeDescription_t GetColumnType(unsigned int col) const {
			CDataObjectTreeColumnInfo* columnInfo = m_columnInfo.at(col);
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
			IMetaObjectGenericData* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			const auto& obj = metaTable->GetGenericAttributes();
			return obj.size();
		}

		//array support 
		virtual bool SetAt(const CValue& varKeyValue, const CValue& varValue);
		virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

		friend class IListDataObject;

	protected:

		ITreeDataObject* m_ownerTable;
		CMethodHelper* m_methodHelper;

		std::map<meta_identifier_t, CDataObjectTreeColumnInfo*> m_columnInfo;

	};

	class CDataObjectTreeReturnLine : public IValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(CDataObjectTreeReturnLine);
	public:

		CDataObjectTreeReturnLine(ITreeDataObject* ownerTable = nullptr, const wxDataViewItem& line = wxDataViewItem(nullptr));
		virtual ~CDataObjectTreeReturnLine();

		virtual IValueTree* GetOwnerModel() const {
			return m_ownerTable;
		}

		virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
			//PrepareNames(); 
			return m_methodHelper;
		}

		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов

		virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal); //установка атрибута
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal); //значение атрибута

	protected:
		CMethodHelper* m_methodHelper;
		ITreeDataObject* m_ownerTable;
	};

public:

	void AppendSort(IMetaObject* metaObject, bool ascending = true, bool use = true, bool system = false) {
		if (metaObject == nullptr)
			return;
		m_sortOrder.AppendSort(
			metaObject->GetMetaID(),
			metaObject->GetName(),
			metaObject->GetSynonym(),
			ascending, use, system
		);
	}

	virtual bool AutoCreateColumn() const {
		return false;
	}

	virtual bool EditableLine(const wxDataViewItem& item, unsigned int col) const {
		return false;
	}

	//set meta/get meta
	virtual bool SetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, const CValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, CValue& pvarMetaVal) const {
		wxValueTreeNode* node = GetViewData<wxValueTreeNode>(item);
		if (node == nullptr)
			return false;
		return node->GetValue(id, pvarMetaVal);
	}

	//ctor
	ITreeDataObject(IMetaObjectGenericData* metaObject = nullptr, const form_identifier_t& formType = wxNOT_FOUND);
	virtual ~ITreeDataObject();

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	//get metaData from object 
	virtual IMetaObjectGenericData* GetSourceMetaObject() const final {
		return GetMetaObject();
	}

	//Get ref class 
	virtual class_identifier_t GetSourceClassType() const final {
		return GetClassType();
	};

	//get unique identifier 
	virtual CUniqueKey GetGuid() const {
		return m_objGuid;
	};

	//get metaData from object 
	virtual IMetaObjectGenericData* GetMetaObject() const = 0;

	//counter
	virtual void IncrRef() { CValue::IncrRef(); }
	virtual void DecrRef() { CValue::DecrRef(); }

	virtual inline bool IsEmpty() const { return false; }

	//Get ref class 
	virtual class_identifier_t GetClassType() const = 0;

	virtual wxString GetClassName() const = 0;
	virtual wxString GetString() const = 0;

	//operator 
	virtual operator CValue() const {
		return this;
	};

protected:

	Guid m_objGuid;
	CDataObjectTreeColumnCollection* m_dataColumnCollection;
	CMethodHelper* m_methodHelper;
};

// tree with parent or only parent 
class BACKEND_API CTreeDataObjectFolderRef : public ITreeDataObject {
	wxDECLARE_DYNAMIC_CLASS(CTreeDataObjectFolderRef);
public:

	enum {
		LIST_FOLDER,
		LIST_ITEM_FOLDER,
		LIST_ITEM,
	};

	struct wxValueTreeListNode : public wxValueTreeNode {
		wxValueTreeListNode(wxValueTreeNode* parent, const Guid& guid, ITreeDataObject* treeValue = nullptr, bool container = false) :
			wxValueTreeNode(parent), m_objGuid(guid), m_container(container) {
			m_valueTree = treeValue;
		}
		Guid GetGuid() const {
			return m_objGuid;
		}
		virtual bool IsContainer() const {
			return m_container;
		};
	private:
		bool m_container;
		Guid m_objGuid;
	};

public:

	virtual wxDataViewItem FindRowValue(const CValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual wxDataViewItem FindRowValue(IValueModelReturnLine* retLine) const;

	//Constructor
	CTreeDataObjectFolderRef(IMetaObjectRecordDataFolderMutableRef* metaObject = nullptr,
		const form_identifier_t& formType = wxNOT_FOUND, int listMode = LIST_ITEM, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) override;
	virtual bool GetAttrByRow(const wxDataViewItem& WXUNUSED(row), unsigned int WXUNUSED(col),
		wxDataViewItemAttr& WXUNUSED(attr)) const override;

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel(const int countPerPage = defaultCountPerPage);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const;
	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);

	virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);

	//on activate item
	virtual void ActivateItem(IBackendValueForm* srcForm,
		const wxDataViewItem& item, unsigned int col) {
		if (m_choiceMode) {
			ChooseValue(srcForm);
		}
		else {
			EditValue();
		}
	}

	//get metaData from object 
	virtual IMetaObjectRecordDataFolderMutableRef* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual class_identifier_t GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void AddFolderValue(unsigned int before = 0);
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

	virtual void ChooseValue(IBackendValueForm* srcForm);

private:

	bool m_choiceMode; int m_listMode;
	IMetaObjectRecordDataFolderMutableRef* m_metaObject;
};

#endif 