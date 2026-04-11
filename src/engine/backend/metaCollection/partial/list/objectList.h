#ifndef _OBJECT_LIST_H__
#define _OBJECT_LIST_H__

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/reference/reference.h"

//base list class 
class BACKEND_API ibValueListDataObject : public ibValueModelTableBase,
	public ibSourceDataObject {
	wxDECLARE_ABSTRACT_CLASS(ibValueListDataObject);
protected:
	enum Func {
		enRefresh
	};
private:

	// implementation of base class virtuals to define model
	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_recordColumnCollection; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) override {
		if (!line.IsOk())
			return nullptr;
		return ibValue::CreateAndPrepareValueRef<ibValueDataObjectListReturnLine>(this, line);
	}

public:

	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
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

	class ibValueDataObjectListColumnCollection : public ibValueModelTableBase::ibValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(ibValueDataObjectListColumnCollection);
	public:
		class ibValueDataObjectListColumnInfo : public ibValueModelTableBase::ibValueModelColumnCollection::ibValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(ibValueDataObjectListColumnInfo);
		public:

			virtual unsigned int GetColumnID() const { return m_metaAttribute->GetMetaID(); }
			virtual wxString GetColumnName() const { return m_metaAttribute->GetName(); }
			virtual wxString GetColumnCaption() const { return m_metaAttribute->GetSynonym(); }
			virtual const ibTypeDescription GetColumnType() const { return m_metaAttribute->GetTypeDesc(); }

			ibValueDataObjectListColumnInfo();
			ibValueDataObjectListColumnInfo(ibValueMetaObjectAttributeBase* attribute);
			virtual ~ibValueDataObjectListColumnInfo();

		private:
			ibValueMetaObjectAttributeBase* m_metaAttribute;
		};

	public:

		ibValueDataObjectListColumnCollection();
		ibValueDataObjectListColumnCollection(ibValueListDataObject* ownerTable, ibValueMetaObjectGenericData* metaObject);
		virtual ~ibValueDataObjectListColumnCollection();

		virtual const ibTypeDescription GetColumnType(unsigned int col) const {
			ibValueDataObjectListColumnInfo* columnInfo = m_listColumnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnType();
		}

		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_listColumnInfo.size() < idx)
				return nullptr;
			auto it = m_listColumnInfo.begin();
			std::advance(it, idx);
			return it->second;
		}

		virtual unsigned int GetColumnCount() const {
			ibValueMetaObjectGenericData* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			const auto& obj = metaTable->GetGenericAttributeArrayObject();
			return obj.size();
		}

		//array support 
		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

	protected:

		ibValueListDataObject* m_ownerTable;
		std::map<ibMetaID, ibValuePtr<ibValueDataObjectListColumnInfo>> m_listColumnInfo;
		ibValueMethodHelper* m_methodHelper;
	};

	class ibValueDataObjectListReturnLine : public ibValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(ibValueDataObjectListReturnLine);
	public:

		ibValueDataObjectListReturnLine(ibValueListDataObject* ownerTable = nullptr, const ibDataViewItem& line = ibDataViewItem(nullptr));
		virtual ~ibValueDataObjectListReturnLine();

		virtual ibValueModelTableBase* GetOwnerModel() const {
			return m_ownerTable;
		}

		virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
			//PrepareNames(); 
			return m_methodHelper;
		}

		virtual void PrepareNames() const;

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

	protected:
		ibValueListDataObject* m_ownerTable;
		ibValueMethodHelper* m_methodHelper;
	};

public:

	void AppendSort(ibValueMetaObject* metaObject, bool ascending = true, bool use = true, bool system = false) {
		if (metaObject == nullptr)
			return;
		m_sortOrder.AppendSort(
			metaObject->GetMetaID(),
			metaObject->GetName(),
			metaObject->GetSynonym(),
			ascending, use, system
		);
	}

	virtual bool AutoCreateColumn() const { return false; }
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const { return false; }

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const {
		ibValueTableRow* node = GetViewData<ibValueTableRow>(item);
		if (node == nullptr)
			return false;
		return node->GetValue(id, pvarMetaVal);
	}

	virtual bool GetValueAttribute(const ibValueMetaObjectAttributeBase* metaAttr, ibValue& retValue, class ibDatabaseResultSet* resultSet, bool createData = true) {
		return ibValueMetaObjectAttributeBase::GetValueAttribute(metaAttr, retValue, resultSet, createData);
	}

	//ctor
	ibValueListDataObject(ibValueMetaObjectGenericData* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);
	virtual ~ibValueListDataObject();

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	//get metaData from object 
	virtual ibValueMetaObjectGenericData* GetSourceMetaObject() const final { return GetMetaObject(); }

	//Get ref class 
	virtual ibClassID GetSourceClassType() const final { return GetClassType(); }

	//Get presentation 
	virtual wxString GetSourceCaption() const {
		return GetMetaObject() ?
			stringUtils::GenerateSynonym(GetMetaObject()->GetClassName()) + wxT(": ") + GetMetaObject()->GetSynonym() : GetString();
	}

	//Is new object? 
	virtual bool IsNewObject() const { return false; }

	//get unique identifier 
	virtual ibUniqueKey GetGuid() const { return m_objGuid; };

	//get metaData from object 
	virtual ibValueMetaObjectGenericData* GetMetaObject() const = 0;

	//counter
	virtual void SourceIncrRef() { ibValue::IncrRef(); }
	virtual void SourceDecrRef() { ibValue::DecrRef(); }

	virtual bool IsEmpty() const { return false; }

	//Get ref class 
	virtual ibClassID GetClassType() const = 0;

	virtual wxString GetClassName() const = 0;
	virtual wxString GetString() const = 0;

protected:
	ibGuid m_objGuid;
	ibValuePtr<ibValueDataObjectListColumnCollection> m_recordColumnCollection;
	ibValueMethodHelper* m_methodHelper;
};

// list enumeration 
class BACKEND_API ibValueListDataObjectEnumRef : public ibValueListDataObject {
	wxDECLARE_DYNAMIC_CLASS(ibValueListDataObjectRef);
public:
	struct ibValueTableEnumRow : public ibValueTableRow {
		ibValueTableEnumRow(const ibGuid& guid) :
			ibValueTableRow(), m_objGuid(guid) {
		}
		ibGuid GetGuid() const {
			return m_objGuid;
		}
	private:
		ibGuid m_objGuid;
	};
public:

	virtual bool UseStandartCommand() const {
		return false;
	}

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual ibDataViewItem FindRowValue(ibValueModelReturnLine* retLine) const;

	//Constructor
	ibValueListDataObjectEnumRef(ibValueMetaObjectRecordDataEnumRef* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(ibValueModel*& tableValue, const ibMetaID& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}

	virtual void PrepareNames() const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       // method call

	//on activate item
	virtual void ActivateItem(ibBackendValueForm* srcForm,
		const ibDataViewItem& item, unsigned int col) {
		if (m_choiceMode)
			ChooseValue(srcForm);
	}

	//get metaData from object 
	virtual ibValueMetaObjectRecordDataEnumRef* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//events:
	virtual void ChooseValue(ibBackendValueForm* srcForm);

private:

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel(const ibDataViewItem& topItem = ibDataViewItem(nullptr), const int countPerPage = defaultCountPerPage);
	virtual void RefreshItemModel(
		const ibDataViewItem& topItem,
		const ibDataViewItem& currentItem,
		const int countPerPage,
		const short scroll = 0
	);

private:

	bool m_choiceMode;
	ibValueMetaObjectRecordDataEnumRef* m_metaObject;
};

// list without parent  
class BACKEND_API ibValueListDataObjectRef : public ibValueListDataObject {
	wxDECLARE_DYNAMIC_CLASS(ibValueListDataObjectRef);
public:
	struct ibValueTableListRow : public ibValueTableRow {
		ibValueTableListRow(const ibGuid& guid) :
			ibValueTableRow(), m_objGuid(guid) {
		}
		ibGuid GetGuid() const {
			return m_objGuid;
		}
	private:
		ibGuid m_objGuid;
	};
public:

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual ibDataViewItem FindRowValue(ibValueModelReturnLine* retLine) const;

	//Constructor
	ibValueListDataObjectRef(ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(ibValueModel*& tableValue, const ibMetaID& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}

	virtual void PrepareNames() const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       // method call

	//on activate item
	virtual void ActivateItem(ibBackendValueForm* srcForm,
		const ibDataViewItem& item, unsigned int col) {
		if (m_choiceMode) {
			ChooseValue(srcForm);
		}
		else {
			EditValue();
		}
	}

	//get metaData from object 
	virtual ibValueMetaObjectRecordDataRef* GetMetaObject() const { return m_metaObject; }

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

	virtual void MarkAsDeleteValue();
	virtual void ChooseValue(ibBackendValueForm* srcForm);

private:

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel(const ibDataViewItem& topItem = ibDataViewItem(nullptr), const int countPerPage = defaultCountPerPage);
	virtual void RefreshItemModel(
		const ibDataViewItem& topItem,
		const ibDataViewItem& currentItem,
		const int countPerPage,
		const short scroll = 0
	);

private:

	bool m_choiceMode;

	ibValueMetaObjectRecordDataMutableRef* m_metaObject;
};

// list register
class BACKEND_API ibValueListRegisterObject : public ibValueListDataObject {
	wxDECLARE_DYNAMIC_CLASS(ibValueListRegisterObject);
public:
	struct ibValueTableKeyRow : public ibValueTableRow {
		ibValueTableKeyRow() :
			ibValueTableRow(), m_nodeKeys() {
		}
		void AppendNodeValue(const ibMetaID& id, const ibValue& variant) { m_nodeKeys.insert_or_assign(id, variant); }
		ibValue& AppendNodeValue(const ibMetaID& id) { return m_nodeKeys[id]; }
		ibUniqueKeyPair GetUniquePairKey(ibValueMetaObjectRegisterData* metaObject) const { return ibUniqueKeyPair(metaObject, m_nodeValues); }

	private:
		ibMetaValueArray m_nodeKeys;
	};
public:

	virtual bool UseStandartCommand() const { return !m_metaObject->HasRecorder(); }

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual ibDataViewItem FindRowValue(ibValueModelReturnLine* retLine) const;

	//Constructor
	ibValueListRegisterObject(ibValueMetaObjectRegisterData* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND);

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(ibValueModel*& tableValue, const ibMetaID& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}

	virtual void PrepareNames() const;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       // method call

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//on activate item
	virtual void ActivateItem(ibBackendValueForm* srcForm,
		const ibDataViewItem& item, unsigned int col) {
		EditValue();
	}

	//get metaData from object 
	virtual ibValueMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

private:

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel(const ibDataViewItem& topItem = ibDataViewItem(nullptr), const int countPerPage = defaultCountPerPage);
	virtual void RefreshItemModel(
		const ibDataViewItem& topItem,
		const ibDataViewItem& currentItem,
		const int countPerPage,
		const short scroll = 0
	);

private:
	ibValueMetaObjectRegisterData* m_metaObject;
};

//base tree class 
class BACKEND_API ibValueModelTreeDataObject : public ibValueModelTreeBase,
	public ibSourceDataObject {
	wxDECLARE_ABSTRACT_CLASS(ibValueModelTreeDataObject);
protected:
	enum Func {
		enRefresh
	};
private:

	// implementation of base class virtuals to define model
	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_recordColumnCollection; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) {
		if (!line.IsOk())
			return nullptr;
		return ibValue::CreateAndPrepareValueRef<ibValueDataObjectTreeReturnLine>(this, line);
	}

public:

	class ibValueDataObjectTreeColumnCollection : public ibValueModelTreeBase::ibValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(ibValueDataObjectTreeColumnCollection);
	public:
		class ibValueDataObjectTreeColumnInfo : public ibValueModelTreeBase::ibValueModelColumnCollection::ibValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(ibValueDataObjectTreeColumnInfo);
		public:

			virtual unsigned int GetColumnID() const { return m_metaAttribute->GetMetaID(); }
			virtual wxString GetColumnName() const { return m_metaAttribute->GetName(); }
			virtual wxString GetColumnCaption() const { return m_metaAttribute->GetSynonym(); }
			virtual const ibTypeDescription GetColumnType() const { return m_metaAttribute->GetTypeDesc(); }

			ibValueDataObjectTreeColumnInfo();
			ibValueDataObjectTreeColumnInfo(ibValueMetaObjectAttributeBase* attribute);
			virtual ~ibValueDataObjectTreeColumnInfo();

		private:
			ibValueMetaObjectAttributeBase* m_metaAttribute;
		};

	public:

		ibValueDataObjectTreeColumnCollection();
		ibValueDataObjectTreeColumnCollection(ibValueModelTreeDataObject* ownerTable, ibValueMetaObjectGenericData* metaObject);
		virtual ~ibValueDataObjectTreeColumnCollection();

		virtual const ibTypeDescription GetColumnType(unsigned int col) const {
			ibValueDataObjectTreeColumnInfo* columnInfo = m_listColumnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnType();
		}

		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const {
			if (m_listColumnInfo.size() < idx)
				return nullptr;
			auto it = m_listColumnInfo.begin();
			std::advance(it, idx);
			return it->second;
		}

		virtual unsigned int GetColumnCount() const {
			ibValueMetaObjectGenericData* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			const auto& obj = metaTable->GetGenericAttributeArrayObject();
			return obj.size();
		}

		//array support 
		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

	protected:

		ibValueModelTreeDataObject* m_ownerTable;
		std::map<ibMetaID, ibValuePtr<ibValueDataObjectTreeColumnInfo>> m_listColumnInfo;
		ibValueMethodHelper* m_methodHelper;
	};

	class ibValueDataObjectTreeReturnLine : public ibValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(ibValueDataObjectTreeReturnLine);
	public:

		ibValueDataObjectTreeReturnLine(ibValueModelTreeDataObject* ownerTable = nullptr, const ibDataViewItem& line = ibDataViewItem(nullptr));
		virtual ~ibValueDataObjectTreeReturnLine();

		virtual ibValueModelTreeBase* GetOwnerModel() const {
			return m_ownerTable;
		}

		virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
			//PrepareNames(); 
			return m_methodHelper;
		}

		virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

	protected:
		ibValueMethodHelper* m_methodHelper;
		ibValueModelTreeDataObject* m_ownerTable;
	};

public:

	void AppendSort(ibValueMetaObject* metaObject, bool ascending = true, bool use = true, bool system = false) {
		if (metaObject == nullptr)
			return;
		m_sortOrder.AppendSort(
			metaObject->GetMetaID(),
			metaObject->GetName(),
			metaObject->GetSynonym(),
			ascending, use, system
		);
	}

	virtual bool AutoCreateColumn() const { return false; }
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const { return false; }

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const {
		ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
		if (node == nullptr)
			return false;
		return node->GetValue(id, pvarMetaVal);
	}

	//ctor
	ibValueModelTreeDataObject(ibValueMetaObjectGenericData* metaObject = nullptr, const ibFormID& formType = wxNOT_FOUND, bool choiceMode = false);
	virtual ~ibValueModelTreeDataObject();

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	//get metaData from object 
	virtual ibValueMetaObjectGenericData* GetSourceMetaObject() const final { return GetMetaObject(); }

	//Get ref class 
	virtual ibClassID GetSourceClassType() const final { return GetClassType(); }

	//Get presentation 
	virtual wxString GetSourceCaption() const {
		return GetMetaObject() ?
			stringUtils::GenerateSynonym(GetMetaObject()->GetClassName()) + wxT(": ") + GetMetaObject()->GetSynonym() : GetString();
	}

	//Is new object?
	virtual bool IsNewObject() const { return false; }

	//get unique identifier 
	virtual ibUniqueKey GetGuid() const { return m_objGuid; }

	//get metaData from object 
	virtual ibValueMetaObjectGenericData* GetMetaObject() const = 0;

	//counter
	virtual void SourceIncrRef() { ibValue::IncrRef(); }
	virtual void SourceDecrRef() { ibValue::DecrRef(); }

	virtual bool IsEmpty() const { return false; }

	//Get ref class 
	virtual ibClassID GetClassType() const = 0;

	virtual wxString GetClassName() const = 0;
	virtual wxString GetString() const = 0;

protected:

	ibGuid m_objGuid;
	ibValuePtr<ibValueDataObjectTreeColumnCollection> m_recordColumnCollection;
	ibValueMethodHelper* m_methodHelper;
};

// tree with parent or only parent 
class BACKEND_API ibValueModelTreeDataObjectFolderRef : public ibValueModelTreeDataObject {
	wxDECLARE_DYNAMIC_CLASS(ibValueModelTreeDataObjectFolderRef);
public:

	enum {
		LIST_FOLDER,
		LIST_ITEM_FOLDER,
		LIST_ITEM,
	};

	struct ibValueTreeListNode : public ibValueTreeNode {
		ibGuid GetGuid() const { return m_objGuid; }
		ibValueTreeListNode(ibValueTreeNode* parent, const ibGuid& guid, ibValueModelTreeDataObject* treeValue = nullptr, bool container = false) :
			ibValueTreeNode(parent), m_objGuid(guid), m_container(container) {
			m_valueTree = treeValue;
		}
		virtual bool IsContainer() const { return m_container; }
	private:
		ibGuid m_objGuid;
		bool m_container;
	};

public:

	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const;
	virtual ibDataViewItem FindRowValue(ibValueModelReturnLine* retLine) const;

	//Constructor
	ibValueModelTreeDataObjectFolderRef(ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject = nullptr,
		const ibFormID& formType = wxNOT_FOUND, int listMode = LIST_ITEM, bool choiceMode = false);

	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) override;
	virtual bool GetAttrByRow(const ibDataViewItem& WXUNUSED(row), unsigned int WXUNUSED(col),
		ibDataViewItemAttr& WXUNUSED(attr)) const override;

	// define current parent for hierarchical view 
	virtual bool HasParentTopItem() const { return true; }
	virtual bool SetParentTopItem(const ibDataViewItem& item);
	virtual ibDataViewItem GetParentTopItem() const;

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(ibValueModel*& tableValue, const ibMetaID& id);

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************
	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);

	//on activate item
	virtual void ActivateItem(ibBackendValueForm* srcForm,
		const ibDataViewItem& item, unsigned int col) {
		if (m_choiceMode) {
			ChooseValue(srcForm);
		}
		else {
			EditValue();
		}
	}

	//get metaData from object 
	virtual ibValueMetaObjectRecordDataHierarchyMutableRef* GetMetaObject() const { return m_metaObject; }

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void AddFolderValue(unsigned int before = 0);
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

	virtual void MarkAsDeleteValue();
	virtual void ChooseValue(ibBackendValueForm* srcForm);

private:

	//****************************************************************************
	//*                               Support model                              *
	//****************************************************************************

	virtual void RefreshModel(const ibDataViewItem& topItem = ibDataViewItem(nullptr), const int countPerPage = defaultCountPerPage);

private:

	bool m_choiceMode; int m_listMode;
	ibValueMetaObjectRecordDataHierarchyMutableRef* m_metaObject;

	ibGuid m_topParentGuid; 
};

#endif 