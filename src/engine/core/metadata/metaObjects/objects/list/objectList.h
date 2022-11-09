#ifndef _OBJECT_LIST_H__
#define _OBJECT_LIST_H__

#include "metadata/metaObjects/objects/object.h"

//base list class 
class IListDataObject : public IValueTable,
	public ISourceDataObject {
	wxDECLARE_ABSTRACT_CLASS(IListDataObject);
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

			virtual CValueTypeDescription* GetColumnTypes() const {
				return m_metaAttribute->GetValueTypeDescription();
			}

			virtual int GetColumnWidth() const {
				return 0;
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

		virtual CValueTypeDescription* GetColumnTypes(unsigned int col) const
		{
			CDataObjectListColumnInfo* columnInfo = m_columnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnTypes();
		}

		virtual IValueModelColumnInfo* GetColumnInfo(unsigned int idx) const
		{
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
		virtual CValue GetAt(const CValue& cKey);
		virtual void SetAt(const CValue& cKey, CValue& cVal);

		friend class IListDataObject;

	protected:

		IListDataObject* m_ownerTable;
		CMethods* m_methods;

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

		//set meta/get meta
		virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
		virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

		virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов

		virtual wxString GetTypeString() const {
			return wxT("listValueRow");
		}

		virtual wxString GetString() const {
			return wxT("listValueRow");
		}

		virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal); //установка атрибута
		virtual CValue GetAttribute(attributeArg_t& aParams); //значение атрибута

	protected:
		CMethods* m_methods;
		IListDataObject* m_ownerTable;
	};

public:

	virtual bool AutoCreateColumns() const {
		return false;
	}

	virtual bool EditableLine(const wxDataViewItem& item, unsigned int col) const {
		return false;
	}

	//ctor
	IListDataObject(IMetaObjectWrapperData* metaObject = NULL, const form_identifier_t& formType = wxNOT_FOUND);
	virtual ~IListDataObject();

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel() = 0;

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
	virtual CLASS_ID GetClassType() const = 0;

	virtual wxString GetTypeString() const = 0;
	virtual wxString GetString() const = 0;

	//operator 
	virtual operator CValue() const {
		return this;
	};

protected:

	Guid m_objGuid;

	CDataObjectListColumnCollection* m_dataColumnCollection;
	CMethods* m_methods;
};

// list without parent  
class CListDataObjectRef : public IListDataObject {
	wxDECLARE_DYNAMIC_CLASS(CListDataObjectRef);
public:
	struct wxValueTableListRow : public wxValueTableRow {
		Guid GetGuid() const {
			return m_objGuid;
		}
		wxValueTableListRow(const modelArray_t& nodeValues, const Guid& guid) :
			wxValueTableRow(nodeValues), m_objGuid(guid) {
		}
	private:
		Guid m_objGuid;
	};
public:

	virtual wxDataViewItem FindRowValue(const CValue& cVal, const wxString &colName = wxEmptyString) const;
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

	virtual void RefreshModel();

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual CMethods* GetPMethods() const;                          // получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                             // этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);       // вызов метода

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

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
	virtual IMetaObjectWrapperData* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//support actions
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& action, CValueForm* srcForm);

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
		wxValueTableKeyRow(const modelArray_t& nodeValues, const modelArray_t& nodeKeys) :
			wxValueTableRow(nodeValues), m_nodeKeys(nodeKeys) {
		}

		CUniquePairKey GetUniquePairKey(IMetaObjectRegisterData* metaObject) const {
			return CUniquePairKey(metaObject, m_nodeValues);
		}

	private:
		modelArray_t m_nodeKeys;
	};
public:

	virtual wxDataViewItem FindRowValue(const CValue& cVal, const wxString &colName = wxEmptyString) const;
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

	virtual void RefreshModel();

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual CMethods* GetPMethods() const;                          // получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                             // этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);       // вызов метода

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

	//on activate item
	virtual void ActivateItem(CValueForm* srcForm,
		const wxDataViewItem& item, unsigned int col) {
		EditValue();
	}

	//get metadata from object 
	virtual IMetaObjectWrapperData* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//support actions
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& action, CValueForm* srcForm);

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

			virtual CValueTypeDescription* GetColumnTypes() const {
				return m_metaAttribute->GetValueTypeDescription();
			}

			virtual int GetColumnWidth() const {
				return 0;
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

		virtual CValueTypeDescription* GetColumnTypes(unsigned int col) const {
			CDataObjectTreeColumnInfo* columnInfo = m_columnInfo.at(col);
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
		virtual CValue GetAt(const CValue& cKey);
		virtual void SetAt(const CValue& cKey, CValue& cVal);

		friend class IListDataObject;

	protected:

		ITreeDataObject* m_ownerTable;
		CMethods* m_methods;

		std::map<meta_identifier_t, CDataObjectTreeColumnInfo*> m_columnInfo;

	} *m_dataColumnCollection;

	class CDataObjectTreeReturnLine : public IValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(CDataObjectTreeReturnLine);
	public:

		CDataObjectTreeReturnLine(ITreeDataObject* ownerTable = NULL, const wxDataViewItem& line = wxDataViewItem(NULL));
		virtual ~CDataObjectTreeReturnLine();

		virtual IValueTree* GetOwnerModel() const {
			return m_ownerTable;
		}

		//set meta/get meta
		virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
		virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

		virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов

		virtual wxString GetTypeString() const {
			return wxT("treeValueRow");
		}

		virtual wxString GetString() const {
			return wxT("treeValueRow");
		}

		virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal); //установка атрибута
		virtual CValue GetAttribute(attributeArg_t& aParams); //значение атрибута

	protected:
		CMethods* m_methods;
		ITreeDataObject* m_ownerTable; 
	};

public:

	virtual bool AutoCreateColumns() const {
		return false;
	}

	virtual bool EditableLine(const wxDataViewItem& item, unsigned int col) const {
		return false;
	}

	//ctor
	ITreeDataObject(IMetaObjectWrapperData* metaObject = NULL, const form_identifier_t& formType = wxNOT_FOUND);
	virtual ~ITreeDataObject();

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel() = 0;

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
	virtual CLASS_ID GetClassType() const = 0;

	virtual wxString GetTypeString() const = 0;
	virtual wxString GetString() const = 0;

	//operator 
	virtual operator CValue() const {
		return this;
	};

protected:

	Guid m_objGuid;
	CMethods* m_methods;
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
		wxValueTreeListNode(wxValueTreeNode* parent, const modelArray_t& nodeValues, const Guid& guid, ITreeDataObject* treeValue = NULL) :
			wxValueTreeNode(parent, nodeValues), m_objGuid(guid) {
			m_valueTree = treeValue;
		}

		Guid GetGuid() const {
			return m_objGuid;
		}

	private:
		Guid m_objGuid;
	};

public:

	virtual wxDataViewItem FindRowValue(const CValue& cVal, const wxString &colName = wxEmptyString) const;
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

	virtual void RefreshModel();

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual CMethods* GetPMethods() const;                          // получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                              // этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);					// вызов метода

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

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
	virtual CLASS_ID GetClassType() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//support actions
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& action, CValueForm* srcForm);

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