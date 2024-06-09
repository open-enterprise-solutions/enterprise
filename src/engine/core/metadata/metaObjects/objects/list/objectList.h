#ifndef _OBJECT_LIST_H__
#define _OBJECT_LIST_H__

#include "core/metadata/metaObjects/objects/object.h"

//base list class 
class IListDataObject : public IValueTable,
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
			return NULL;
		return new CDataObjectListReturnLine(this, line);
	}

public:

	class CDataObjectListColumnCollection : public IValueTable::IValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(CDataObjectListColumnCollection);
	public:
		class CDataObjectListColumnInfo : public IValueTable::IValueModelColumnCollection::IValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(CDataObjectListColumnInfo);
			IMetaAttributeObject* m_metaAttribute;
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
			CDataObjectListColumnInfo(IMetaAttributeObject* metaAttribute);
			virtual ~CDataObjectListColumnInfo();

			virtual wxString GetTypeString() const {
				return wxT("listColumnInfo");
			}

			virtual wxString GetString() const {
				return wxT("listSectionColumnInfo");
			}

			friend CDataObjectListColumnCollection;
		};

	public:

		CDataObjectListColumnCollection();
		CDataObjectListColumnCollection(IListDataObject* ownerTable, IMetaObjectWrapperData* metaObject);
		virtual ~CDataObjectListColumnCollection();

		virtual typeDescription_t GetColumnType(unsigned int col) const {
			CDataObjectListColumnInfo* columnInfo = m_columnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnType();
		}

		virtual IValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_columnInfo.size() < idx)
				return NULL;
			auto foundedIt = m_columnInfo.begin();
			std::advance(foundedIt, idx);
			return foundedIt->second;
		}

		virtual unsigned int GetColumnCount() const {
			IMetaObjectWrapperData* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			auto attributes =
				metaTable->GetGenericAttributes();
			return attributes.size();
		}

		virtual wxString GetTypeString() const {
			return wxT("listColumn");
		}

		virtual wxString GetString() const {
			return wxT("listColumn");
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

		CDataObjectListReturnLine(IListDataObject* ownerTable = NULL, const wxDataViewItem& line = wxDataViewItem(NULL));
		virtual ~CDataObjectListReturnLine();

		virtual IValueTable* GetOwnerModel() const {
			return m_ownerTable;
		}

		virtual CMethodHelper* GetPMethods() const {
			PrepareNames();
			return m_methodHelper;
		};

		virtual void PrepareNames() const;

		virtual wxString GetTypeString() const {
			return wxT("listValueRow");
		}

		virtual wxString GetString() const {
			return wxT("listValueRow");
		}

		virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal); //установка атрибута
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal); //значение атрибута

	protected:
		CMethodHelper* m_methodHelper;
		IListDataObject* m_ownerTable;
	};

public:

	void AppendSort(IMetaObject* metaObject, bool ascending = true, bool use = true, bool system = false) {
		if (metaObject == NULL)
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
		if (node == NULL)
			return false;
		return node->GetValue(id, pvarMetaVal);
	}

	virtual bool GetValueAttribute(IMetaAttributeObject* metaAttr, CValue& retValue, class DatabaseResultSet* resultSet, bool createData = true) {
		return IMetaAttributeObject::GetValueAttribute(metaAttr, retValue, resultSet, createData);
	}

	//ctor
	IListDataObject(IMetaObjectWrapperData* metaObject = NULL, const form_identifier_t& formType = wxNOT_FOUND);
	virtual ~IListDataObject();

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	//get metadata from object 
	virtual IMetaObjectWrapperData* GetMetaObject() const = 0;

	//get unique identifier 
	virtual CUniqueKey GetGuid() const {
		return m_objGuid;
	};

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
	virtual CLASS_ID GetTypeClass() const = 0;

	virtual wxString GetTypeString() const = 0;
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
class CListDataObjectEnumRef : public IListDataObject {
	wxDECLARE_DYNAMIC_CLASS(CListDataObjectRef);
public:
	struct wxValueTableEnumRow : public wxValueTableRow {
		Guid GetGuid() const {
			return m_objGuid;
		}
		wxValueTableEnumRow(const Guid& guid) :
			wxValueTableRow(), m_objGuid(guid) {
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
	CListDataObjectEnumRef(IMetaObjectRecordDataEnumRef* metaObject = NULL, const form_identifier_t& formType = wxNOT_FOUND, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) override;

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel(const wxDataViewItem& topItem = wxDataViewItem(NULL), int countPerPage = DEF_START_ITEM_PER_COUNT);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual CMethodHelper* GetPMethods() const {
		PrepareNames();
		return m_methodHelper;
	};

	virtual void PrepareNames() const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);       // вызов метода

	//on activate item
	virtual void ActivateItem(CValueForm* srcForm,
		const wxDataViewItem& item, unsigned int col) {
		if (m_choiceMode)
			ChooseValue(srcForm);
	}

	//get metadata from object 
	virtual IMetaObjectRecordDataEnumRef* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//support actions
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, CValueForm* srcForm);

	//events:
	virtual void ChooseValue(CValueForm* srcForm);

private:

	bool m_choiceMode;
	IMetaObjectRecordDataEnumRef* m_metaObject;
};

// list without parent  
class CListDataObjectRef : public IListDataObject {
	wxDECLARE_DYNAMIC_CLASS(CListDataObjectRef);
public:
	struct wxValueTableListRow : public wxValueTableRow {
		Guid GetGuid() const {
			return m_objGuid;
		}
		wxValueTableListRow(const Guid& guid) :
			wxValueTableRow(), m_objGuid(guid) {
		}
	private:
		Guid m_objGuid;
	};
public:

	virtual wxDataViewItem FindRowValue(const CValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual wxDataViewItem FindRowValue(IValueModelReturnLine* retLine) const;

	//Constructor
	CListDataObjectRef(IMetaObjectRecordDataRef* metaObject = NULL, const form_identifier_t& formType = wxNOT_FOUND, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) override;

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel(const wxDataViewItem& topItem = wxDataViewItem(NULL), int countPerPage = DEF_START_ITEM_PER_COUNT);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual CMethodHelper* GetPMethods() const {
		PrepareNames();
		return m_methodHelper;
	};

	virtual void PrepareNames() const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);       // вызов метода

	//on activate item
	virtual void ActivateItem(CValueForm* srcForm,
		const wxDataViewItem& item, unsigned int col) {
		if (m_choiceMode) {
			ChooseValue(srcForm);
		}
		else {
			EditValue();
		}
	}

	//get metadata from object 
	virtual IMetaObjectRecordDataRef* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//support actions
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, CValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

	virtual void ChooseValue(CValueForm* srcForm);

private:

	bool m_choiceMode;
	IMetaObjectRecordDataRef* m_metaObject;
};

// list register
class CListRegisterObject : public IListDataObject {
	wxDECLARE_DYNAMIC_CLASS(CListRegisterObject);
public:
	struct wxValueTableKeyRow : public wxValueTableRow {
		wxValueTableKeyRow() :
			wxValueTableRow(), m_nodeKeys() {
		}

		void AppendNodeValue(const meta_identifier_t& id, const CValue& variant)
		{
			m_nodeKeys.insert_or_assign(id, variant);
		}

		CValue& AppendNodeValue(const meta_identifier_t& id)
		{
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
	CListRegisterObject(IMetaObjectRegisterData* metaObject = NULL, const form_identifier_t& formType = wxNOT_FOUND);

	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) override;

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel(const wxDataViewItem& topItem = wxDataViewItem(NULL), int countPerPage = DEF_START_ITEM_PER_COUNT);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual CMethodHelper* GetPMethods() const {
		PrepareNames();
		return m_methodHelper;
	};

	virtual void PrepareNames() const;
	virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);       // вызов метода

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	//on activate item
	virtual void ActivateItem(CValueForm* srcForm,
		const wxDataViewItem& item, unsigned int col) {
		EditValue();
	}

	//get metadata from object 
	virtual IMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//support actions
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, CValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

private:
	IMetaObjectRegisterData* m_metaObject;
};

//base tree class 
class ITreeDataObject : public IValueTree,
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
			return NULL;
		return new CDataObjectTreeReturnLine(this, line);
	}

public:

	class CDataObjectTreeColumnCollection : public IValueTree::IValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(CDataObjectTreeColumnCollection);
	public:
		class CDataObjectTreeColumnInfo : public IValueTree::IValueModelColumnCollection::IValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(CDataObjectTreeColumnInfo);
			IMetaAttributeObject* m_metaAttribute;
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
			CDataObjectTreeColumnInfo(IMetaAttributeObject* metaAttribute);
			virtual ~CDataObjectTreeColumnInfo();

			virtual wxString GetTypeString() const {
				return wxT("treeColumnInfo");
			}

			virtual wxString GetString() const {
				return wxT("treeSectionColumnInfo");
			}

			friend CDataObjectTreeColumnCollection;
		};

	public:

		CDataObjectTreeColumnCollection();
		CDataObjectTreeColumnCollection(ITreeDataObject* ownerTable, IMetaObjectWrapperData* metaObject);
		virtual ~CDataObjectTreeColumnCollection();

		virtual typeDescription_t GetColumnType(unsigned int col) const {
			CDataObjectTreeColumnInfo* columnInfo = m_columnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnType();
		}

		virtual IValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_columnInfo.size() < idx)
				return NULL;
			auto foundedIt = m_columnInfo.begin();
			std::advance(foundedIt, idx);
			return foundedIt->second;
		}

		virtual unsigned int GetColumnCount() const {
			IMetaObjectWrapperData* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			auto attributes =
				metaTable->GetGenericAttributes();
			return attributes.size();
		}

		virtual wxString GetTypeString() const {
			return wxT("treeColumn");
		}

		virtual wxString GetString() const {
			return wxT("treeColumn");
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

		CDataObjectTreeReturnLine(ITreeDataObject* ownerTable = NULL, const wxDataViewItem& line = wxDataViewItem(NULL));
		virtual ~CDataObjectTreeReturnLine();

		virtual IValueTree* GetOwnerModel() const {
			return m_ownerTable;
		}

		virtual CMethodHelper* GetPMethods() const {
			PrepareNames();
			return m_methodHelper;
		};

		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов

		virtual wxString GetTypeString() const {
			return wxT("treeValueRow");
		}

		virtual wxString GetString() const {
			return wxT("treeValueRow");
		}

		virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal); //установка атрибута
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal); //значение атрибута

	protected:
		CMethodHelper* m_methodHelper;
		ITreeDataObject* m_ownerTable;
	};

public:

	void AppendSort(IMetaObject* metaObject, bool ascending = true, bool use = true, bool system = false) {
		if (metaObject == NULL)
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
		if (node == NULL)
			return false;
		return node->GetValue(id, pvarMetaVal);
	}

	//ctor
	ITreeDataObject(IMetaObjectWrapperData* metaObject = NULL, const form_identifier_t& formType = wxNOT_FOUND);
	virtual ~ITreeDataObject();

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	//get metadata from object 
	virtual IMetaObjectWrapperData* GetMetaObject() const = 0;

	//get unique identifier 
	virtual CUniqueKey GetGuid() const {
		return m_objGuid;
	};

	//counter
	virtual void IncrRef() { CValue::IncrRef(); }
	virtual void DecrRef() { CValue::DecrRef(); }

	virtual inline bool IsEmpty() const { return false; }

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const = 0;

	virtual wxString GetTypeString() const = 0;
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
class CTreeDataObjectFolderRef : public ITreeDataObject {
	wxDECLARE_DYNAMIC_CLASS(CTreeDataObjectFolderRef);
public:

	enum {
		LIST_FOLDER,
		LIST_ITEM_FOLDER,
		LIST_ITEM,
	};

	struct wxValueTreeListNode : public wxValueTreeNode {

		Guid GetGuid() const {
			return m_objGuid;
		}

		wxValueTreeListNode(wxValueTreeNode* parent, const Guid& guid, ITreeDataObject* treeValue = NULL, bool container = false) :
			wxValueTreeNode(parent), m_objGuid(guid), m_container(container) {
			m_valueTree = treeValue;
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
	CTreeDataObjectFolderRef(IMetaObjectRecordDataFolderMutableRef* metaObject = NULL,
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

	virtual void RefreshModel(const wxDataViewItem& topItem = wxDataViewItem(NULL), int countPerPage = DEF_START_ITEM_PER_COUNT);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	CMethodHelper* GetPMethods() const {
		PrepareNames();
		return m_methodHelper;
	};

	virtual void PrepareNames() const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);

	virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);

	//on activate item
	virtual void ActivateItem(CValueForm* srcForm,
		const wxDataViewItem& item, unsigned int col) {
		if (m_choiceMode) {
			ChooseValue(srcForm);
		}
		else {
			EditValue();
		}
	}

	//get metadata from object 
	virtual IMetaObjectRecordDataFolderMutableRef* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//support actions
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, CValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void AddFolderValue(unsigned int before = 0);
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

	virtual void ChooseValue(CValueForm* srcForm);

private:

	bool m_choiceMode; int m_listMode;
	IMetaObjectRecordDataFolderMutableRef* m_metaObject;
};

#endif 